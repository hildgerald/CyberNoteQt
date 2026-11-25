#include "docxedit.h"

#include <QAction>
#include <QActionGroup>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QImage>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextFragment>
#include <QMenu>
#include <QMimeData>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// QTextEdit qui accepte aussi le collage d'images (captures d'écran...).
class DocxTextEdit : public QTextEdit
{
public:
    explicit DocxTextEdit(QWidget *parent) : QTextEdit(parent) {}
    std::function<void(const QImage &)> onImage;

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override
    {
        return source->hasImage() || QTextEdit::canInsertFromMimeData(source);
    }
    void insertFromMimeData(const QMimeData *source) override
    {
        if (source->hasImage() && !source->hasHtml() && onImage) {
            const QImage img = qvariant_cast<QImage>(source->imageData());
            if (!img.isNull()) { onImage(img); return; }
        }
        QTextEdit::insertFromMimeData(source);
    }
};

namespace {

constexpr int kMaxImageWidthPx = 604;   // ~ 16 cm à 96 ppp

bool isPng(const QByteArray &b) { return b.size() > 8 && b.startsWith(QByteArrayView("\x89PNG", 4)); }
bool isJpeg(const QByteArray &b) { return b.size() > 4 && b.startsWith(QByteArrayView("\xFF\xD8", 2)); }

// Charge une image (fichier ou données) et la met au format PNG/JPEG attendu par le modèle.
bool makeImageData(const QByteArray &bytes, const QImage &image, docx::ImageData &out)
{
    if (image.isNull()) return false;
    out.wPx = image.width();
    out.hPx = image.height();
    if (isPng(bytes) || isJpeg(bytes)) {
        out.bytes.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
        out.isJpeg = isJpeg(bytes);
    } else {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        out.bytes.assign(png.constData(), static_cast<size_t>(png.size()));
        out.isJpeg = false;
    }
    return true;
}

} // namespace

// ============================================================================
DocxEdit::DocxEdit(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_bar1 = new QToolBar(this);
    m_bar2 = new QToolBar(this);
    layout->addWidget(m_bar1);
    layout->addWidget(m_bar2);

    m_edit = new DocxTextEdit(this);
    m_edit->setAcceptRichText(true);
    m_edit->setStyleSheet(QStringLiteral("QTextEdit { background-color: white; color: black; }"));
    layout->addWidget(m_edit, 1);

    m_conv.reset(new DocxConverter(m_edit->document(), &m_store));
    m_edit->onImage = [this](const QImage &img) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        insertImageBytes(png, img);
    };

    buildToolbars();

    connect(m_edit, &QTextEdit::cursorPositionChanged, this, &DocxEdit::syncToolbar);
    connect(m_edit, &QTextEdit::currentCharFormatChanged, this, [this](const QTextCharFormat &) { syncToolbar(); });
    connect(m_edit->document(), &QTextDocument::modificationChanged, this, &DocxEdit::modificationChanged);

    newBlank();
}

DocxEdit::~DocxEdit() = default;

bool DocxEdit::isModified() const { return m_edit->document()->isModified(); }

// ----------------------------------------------------------------------------- barres d'outils
void DocxEdit::buildToolbars()
{
    // Action de barre d'outils : QAction explicite + connect (valable quelle que soit la version de Qt 6).
    auto addTool = [this](QToolBar *bar, const QString &text, const QString &tip, std::function<void()> fn) {
        QAction *a = bar->addAction(text);
        a->setToolTip(tip.isEmpty() ? text : tip);
        connect(a, &QAction::triggered, this, [fn] { fn(); });
        return a;
    };

    // --- barre 1 : édition, style, police ---
    addTool(m_bar1, tr("Undo"), tr("Undo (Ctrl+Z)"), [this] { m_edit->undo(); });
    addTool(m_bar1, tr("Redo"), tr("Redo (Ctrl+Y)"), [this] { m_edit->redo(); });
    addTool(m_bar1, tr("Cut"), QString(), [this] { m_edit->cut(); });
    addTool(m_bar1, tr("Copy"), QString(), [this] { m_edit->copy(); });
    addTool(m_bar1, tr("Paste"), QString(), [this] { m_edit->paste(); });
    m_bar1->addSeparator();

    m_styleCombo = new QComboBox(this);
    m_styleCombo->setToolTip(tr("Paragraph style"));
    m_styleCombo->setMinimumWidth(130);
    m_bar1->addWidget(m_styleCombo);
    connect(m_styleCombo, QOverload<int>::of(&QComboBox::activated), this, &DocxEdit::onStyleActivated);

    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setToolTip(tr("Font"));
    m_bar1->addWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, &DocxEdit::onFontChanged);

    m_sizeCombo = new QComboBox(this);
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setToolTip(tr("Font size"));
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setMaximumWidth(70);
    m_bar1->addWidget(m_sizeCombo);
    connect(m_sizeCombo, &QComboBox::textActivated, this, &DocxEdit::onSizeChosen);

    // --- barre 2 : mise en forme, alignement, listes, insertions ---
    auto checkable = [this](const QString &text, const QKeySequence &key, const QString &tip) {
        QAction *a = new QAction(text, this);
        a->setCheckable(true);
        a->setToolTip(tip);
        if (!key.isEmpty()) { a->setShortcut(key); a->setShortcutContext(Qt::WidgetWithChildrenShortcut); }
        m_bar2->addAction(a);
        return a;
    };
    m_boldAct = checkable(tr("B"), QKeySequence(QStringLiteral("Ctrl+B")), tr("Bold (Ctrl+B)"));
    m_italicAct = checkable(tr("I"), QKeySequence(QStringLiteral("Ctrl+I")), tr("Italic (Ctrl+I)"));
    m_underAct = checkable(tr("U"), QKeySequence(QStringLiteral("Ctrl+U")), tr("Underline (Ctrl+U)"));
    m_strikeAct = checkable(tr("S"), QKeySequence(), tr("Strikethrough"));
    { QFont f = m_boldAct->font(); f.setBold(true); m_boldAct->setFont(f); }
    { QFont f = m_italicAct->font(); f.setItalic(true); m_italicAct->setFont(f); }
    { QFont f = m_underAct->font(); f.setUnderline(true); m_underAct->setFont(f); }
    { QFont f = m_strikeAct->font(); f.setStrikeOut(true); m_strikeAct->setFont(f); }

    connect(m_boldAct, &QAction::toggled, this, [this](bool on) { QTextCharFormat f; f.setFontWeight(on ? QFont::Bold : QFont::Normal); mergeFormat(f); });
    connect(m_italicAct, &QAction::toggled, this, [this](bool on) { QTextCharFormat f; f.setFontItalic(on); mergeFormat(f); });
    connect(m_underAct, &QAction::toggled, this, [this](bool on) { QTextCharFormat f; f.setFontUnderline(on); mergeFormat(f); });
    connect(m_strikeAct, &QAction::toggled, this, [this](bool on) { QTextCharFormat f; f.setFontStrikeOut(on); mergeFormat(f); });

    addTool(m_bar2, tr("Color..."), tr("Text color"), [this] { chooseTextColor(); });

    auto *hlButton = new QToolButton(this);
    hlButton->setText(tr("Highlight"));
    hlButton->setToolTip(tr("Highlight color"));
    hlButton->setPopupMode(QToolButton::InstantPopup);
    auto *hlMenu = new QMenu(hlButton);
    struct Hl { const char *label; const char *hex; };
    for (const Hl &h : {Hl{"Yellow", "#FFFF00"}, Hl{"Green", "#00FF00"}, Hl{"Cyan", "#00FFFF"}, Hl{"Magenta", "#FF00FF"}, Hl{"Light gray", "#C0C0C0"}}) {
        const QColor color(QString::fromLatin1(h.hex));
        QAction *a = hlMenu->addAction(tr(h.label));
        connect(a, &QAction::triggered, this, [this, color] { chooseHighlight(color, false); });
    }
    hlMenu->addSeparator();
    QAction *noHl = hlMenu->addAction(tr("No highlight"));
    connect(noHl, &QAction::triggered, this, [this] { chooseHighlight(QColor(), true); });
    hlButton->setMenu(hlMenu);
    m_bar2->addWidget(hlButton);
    m_bar2->addSeparator();

    auto *alignGroup = new QActionGroup(this);
    auto alignAct = [&](const QString &text, Qt::Alignment al, const QString &tip) {
        QAction *a = new QAction(text, this);
        a->setCheckable(true);
        a->setToolTip(tip);
        alignGroup->addAction(a);
        m_bar2->addAction(a);
        connect(a, &QAction::triggered, this, [this, al] { m_edit->setAlignment(al); });
        return a;
    };
    m_leftAct = alignAct(tr("Left"), Qt::AlignLeft | Qt::AlignAbsolute, tr("Align left"));
    m_centerAct = alignAct(tr("Center"), Qt::AlignHCenter, tr("Center"));
    m_rightAct = alignAct(tr("Right"), Qt::AlignRight | Qt::AlignAbsolute, tr("Align right"));
    m_justAct = alignAct(tr("Justify"), Qt::AlignJustify, tr("Justify"));
    m_bar2->addSeparator();

    m_bulletAct = addTool(m_bar2, tr("Bullets"), QString(), [this] { toggleList(QTextListFormat::ListDisc); });
    m_numberAct = addTool(m_bar2, tr("Numbering"), QString(), [this] { toggleList(QTextListFormat::ListDecimal); });
    addTool(m_bar2, tr("Indent +"), tr("Increase list level"), [this] { changeListIndent(+1); });
    addTool(m_bar2, tr("Indent -"), tr("Decrease list level"), [this] { changeListIndent(-1); });
    m_bar2->addSeparator();

    auto *tableButton = new QToolButton(this);
    tableButton->setText(tr("Table"));
    tableButton->setToolTip(tr("Tables"));
    tableButton->setPopupMode(QToolButton::InstantPopup);
    auto *tableMenu = new QMenu(tableButton);
    auto addMenu = [this, tableMenu](const QString &text, std::function<void()> fn) {
        QAction *a = tableMenu->addAction(text);
        connect(a, &QAction::triggered, this, [fn] { fn(); });
    };
    addMenu(tr("Insert a table..."), [this] { insertTable(); });
    tableMenu->addSeparator();
    addMenu(tr("Insert row above"), [this] { tableOperation(0); });
    addMenu(tr("Insert row below"), [this] { tableOperation(1); });
    addMenu(tr("Insert column left"), [this] { tableOperation(2); });
    addMenu(tr("Insert column right"), [this] { tableOperation(3); });
    tableMenu->addSeparator();
    addMenu(tr("Delete row"), [this] { tableOperation(4); });
    addMenu(tr("Delete column"), [this] { tableOperation(5); });
    tableButton->setMenu(tableMenu);
    m_bar2->addWidget(tableButton);

    addTool(m_bar2, tr("Image..."), tr("Insert an image (you can also paste one)"), [this] { insertImageFromFile(); });
    addTool(m_bar2, tr("Link..."), tr("Insert a hyperlink"), [this] { insertLink(); });
    addTool(m_bar2, tr("Page break"), tr("Insert a page break"), [this] { insertPageBreak(); });

    rebuildStyleCombo();
}

void DocxEdit::rebuildStyleCombo()
{
    const QSignalBlocker blocker(m_styleCombo);
    m_styleCombo->clear();
    m_styleCombo->addItem(tr("Normal"), QString());
    for (int i = 1; i <= 6; ++i)
        m_styleCombo->addItem(tr("Heading %1").arg(i), QString::fromStdString(docx::headingStyleName(i)));
    for (const char *name : {"Title", "Subtitle", "Quote", "Code Block"}) {
        if (m_pkg.styles.byName(name))
            m_styleCombo->addItem(tr(name), QString::fromLatin1(name));
    }
}

void DocxEdit::applyDefaults()
{
    QFont f = m_edit->document()->defaultFont();
    if (!m_pkg.styles.defFont.empty()) f.setFamilies(QStringList{QString::fromStdString(m_pkg.styles.defFont)});
    f.setPointSizeF(m_pkg.styles.defSizeHalfPt / 2.0);
    m_edit->document()->setDefaultFont(f);
}

// ----------------------------------------------------------------------------- fichier
bool DocxEdit::openFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("Cannot read %1").arg(path);
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    docx::Package pkg;
    docx::Document doc;
    std::string err;
    if (!docx::loadDocx(std::string(bytes.constData(), static_cast<size_t>(bytes.size())), pkg, doc, err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    m_pkg = std::move(pkg);
    m_conv->setStyles(&m_pkg.styles);
    m_conv->setNumbering(&m_pkg.numbering);
    applyDefaults();
    rebuildStyleCombo();
    m_conv->load(doc);
    m_path = path;
    m_edit->document()->clearUndoRedoStacks();
    return true;
}

void DocxEdit::newBlank()
{
    docx::Document doc;
    docx::blankDocx(m_pkg, doc);
    m_conv->setStyles(&m_pkg.styles);
    m_conv->setNumbering(&m_pkg.numbering);
    applyDefaults();
    rebuildStyleCombo();
    m_conv->load(doc);
    m_path.clear();
    m_edit->document()->clearUndoRedoStacks();
}

bool DocxEdit::save(QString *error)
{
    if (m_path.isEmpty()) {
        if (error) *error = tr("No file name.");
        return false;
    }
    const docx::Document model = m_conv->extract();
    std::string bytes, err;
    if (!docx::saveDocx(m_pkg, model, bytes, err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly)
        || out.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())
        || !out.commit()) {
        if (error) *error = tr("Cannot write %1").arg(m_path);
        return false;
    }
    m_edit->document()->setModified(false);
    return true;
}

// ----------------------------------------------------------------------------- Markdown
int DocxEdit::insertMarkdown(const QString &markdown, const QString &baseFolder)
{
    std::vector<docx::Block> blocks;
    std::vector<docx::ImageData> images;

    const docx::ImageLoader loader = [baseFolder](const std::string &tag, docx::ImageData &out) -> bool {
        const QString p = QString::fromStdString(tag);
        const QString path = p.startsWith(QStringLiteral("~/")) ? QDir::homePath() + QLatin1Char('/') + p.mid(2)
                             : (QDir::isAbsolutePath(p) ? p : QDir(baseFolder).filePath(p));
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray bytes = f.readAll();
        QImage img;
        if (!img.loadFromData(bytes)) return false;
        return makeImageData(bytes, img, out);
    };

    docx::markdownToBlocks(markdown.toStdString(), loader, blocks, images, m_listGroup);
    if (blocks.empty()) return 0;

    const QTextCursor end = m_conv->insertBlocks(m_edit->textCursor(), blocks, images);
    m_edit->setTextCursor(end);
    m_edit->ensureCursorVisible();
    m_edit->setFocus();
    return static_cast<int>(blocks.size());
}

// ----------------------------------------------------------------------------- mise en forme
void DocxEdit::mergeFormat(const QTextCharFormat &format)
{
    QTextCursor c = m_edit->textCursor();
    if (!c.hasSelection()) c.select(QTextCursor::WordUnderCursor);
    c.mergeCharFormat(format);
    m_edit->mergeCurrentCharFormat(format);
}

void DocxEdit::onStyleActivated(int index)
{
    const std::string name = m_styleCombo->itemData(index).toString().toStdString();
    QTextCursor c = m_edit->textCursor();
    const int start = c.selectionStart(), end = c.selectionEnd();
    c.beginEditBlock();
    for (QTextBlock b = m_edit->document()->findBlock(start); b.isValid() && (b.position() <= start || b.position() < end); b = b.next())
        m_conv->applyParagraphStyle(QTextCursor(b), name);
    c.endEditBlock();
    m_conv->refreshNumbering();   // un titre numéroté apparaît / disparaît, les suivants sont renumérotés
    m_edit->setFocus();
    syncToolbar();
}

void DocxEdit::onFontChanged(const QFont &font)
{
    QTextCharFormat f;
    f.setFontFamilies(QStringList{font.family()});
    mergeFormat(f);
    m_edit->setFocus();
}

void DocxEdit::onSizeChosen(const QString &text)
{
    bool ok = false;
    const double pt = text.toDouble(&ok);
    if (!ok || pt < 1 || pt > 400) return;
    QTextCharFormat f;
    f.setFontPointSize(pt);
    mergeFormat(f);
    m_edit->setFocus();
}

void DocxEdit::chooseTextColor()
{
    const QColor c = QColorDialog::getColor(Qt::black, this, tr("Text color"));
    if (!c.isValid()) return;
    QTextCharFormat f;
    f.setForeground(QBrush(c));
    mergeFormat(f);
}

void DocxEdit::chooseHighlight(const QColor &color, bool clear)
{
    if (!clear) {
        QTextCharFormat f;
        f.setBackground(QBrush(color));
        mergeFormat(f);
        return;
    }
    // suppression du surlignage : on retire la propriété sur chaque fragment de la sélection
    QTextCursor sel = m_edit->textCursor();
    if (!sel.hasSelection()) sel.select(QTextCursor::WordUnderCursor);
    const int start = sel.selectionStart(), end = sel.selectionEnd();
    QTextDocument *doc = m_edit->document();
    sel.beginEditBlock();
    for (QTextBlock b = doc->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const int fs = std::max(frag.position(), start), fe = std::min(frag.position() + frag.length(), end);
            if (!frag.isValid() || fs >= fe) continue;
            QTextCharFormat cf = frag.charFormat();
            cf.clearBackground();
            QTextCursor fc(doc);
            fc.setPosition(fs);
            fc.setPosition(fe, QTextCursor::KeepAnchor);
            fc.setCharFormat(cf);
        }
    }
    sel.endEditBlock();
}

// ----------------------------------------------------------------------------- listes
void DocxEdit::toggleList(int style)
{
    const QTextListFormat::Style st = static_cast<QTextListFormat::Style>(style);
    QTextCursor c = m_edit->textCursor();
    c.beginEditBlock();
    if (QTextList *list = c.currentList()) {
        if (list->format().style() == st) {
            list->remove(c.block());
            QTextBlockFormat bf;
            bf.setIndent(0);
            c.mergeBlockFormat(bf);
        } else {
            QTextListFormat lf = list->format();
            lf.setStyle(st);
            list->setFormat(lf);
        }
    } else {
        QTextListFormat lf;
        lf.setStyle(st);
        lf.setIndent(1);
        c.createList(lf);
    }
    c.endEditBlock();
    syncToolbar();
}

void DocxEdit::changeListIndent(int delta)
{
    QTextCursor c = m_edit->textCursor();
    QTextList *list = c.currentList();
    if (!list) return;
    QTextListFormat lf = list->format();
    const int indent = std::max(1, std::min(9, lf.indent() + delta));
    if (indent == lf.indent()) return;
    lf.setIndent(indent);
    lf.setStyle(lf.style() == QTextListFormat::ListDisc || lf.style() == QTextListFormat::ListCircle || lf.style() == QTextListFormat::ListSquare
                    ? (indent == 1 ? QTextListFormat::ListDisc : indent == 2 ? QTextListFormat::ListCircle : QTextListFormat::ListSquare)
                    : (indent % 3 == 1 ? QTextListFormat::ListDecimal : indent % 3 == 2 ? QTextListFormat::ListLowerAlpha : QTextListFormat::ListLowerRoman));
    c.beginEditBlock();
    c.createList(lf);   // le paragraphe passe dans une nouvelle liste au niveau demandé
    c.endEditBlock();
}

// ----------------------------------------------------------------------------- tableaux
void DocxEdit::insertTable()
{
    bool ok = false;
    const int rows = QInputDialog::getInt(this, tr("Insert a table"), tr("Rows :"), 3, 1, 200, 1, &ok);
    if (!ok) return;
    const int cols = QInputDialog::getInt(this, tr("Insert a table"), tr("Columns :"), 3, 1, 30, 1, &ok);
    if (!ok) return;

    QTextCursor c = m_edit->textCursor();
    c.beginEditBlock();
    if (!c.block().text().isEmpty()) {   // le tableau va sur une nouvelle ligne (éventuellement dans une cellule : tableau imbriqué)
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }

    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderBrush(QBrush(QColor(0xA6, 0xA6, 0xA6)));
    tf.setCellPadding(3);
    tf.setCellSpacing(0);
    tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QTextTable *table = c.insertTable(rows, cols, tf);
    if (table) {
        for (int col = 0; col < cols; ++col) {       // ligne d'en-tête : fond clair + gras
            QTextTableCell cell = table->cellAt(0, col);
            QTextTableCellFormat cf;
            cf.setBackground(QBrush(QColor(0xD9, 0xE2, 0xF3)));
            cell.setFormat(cf);
            QTextCursor cc = cell.firstCursorPosition();
            QTextCharFormat bold;
            bold.setFontWeight(QFont::Bold);
            cc.setBlockCharFormat(bold);
        }
        m_edit->setTextCursor(table->cellAt(0, 0).firstCursorPosition());
    }
    c.endEditBlock();
}

void DocxEdit::tableOperation(int op)
{
    QTextCursor c = m_edit->textCursor();
    QTextTable *t = c.currentTable();
    if (!t) {
        emit statusMessage(tr("Place the cursor in a table first."));
        return;
    }
    const QTextTableCell cell = t->cellAt(c);
    switch (op) {
    case 0: t->insertRows(cell.row(), 1); break;
    case 1: t->insertRows(cell.row() + 1, 1); break;
    case 2: t->insertColumns(cell.column(), 1); break;
    case 3: t->insertColumns(cell.column() + 1, 1); break;
    case 4: if (t->rows() > 1) t->removeRows(cell.row(), 1); else emit statusMessage(tr("A table needs at least one row.")); break;
    case 5: if (t->columns() > 1) t->removeColumns(cell.column(), 1); else emit statusMessage(tr("A table needs at least one column.")); break;
    default: break;
    }
}

// ----------------------------------------------------------------------------- images, liens, saut de page
void DocxEdit::insertImageBytes(const QByteArray &bytes, const QImage &image)
{
    docx::ImageData data;
    if (!makeImageData(bytes, image, data)) return;
    const int idx = m_store.add(data);
    m_store.registerInto(m_edit->document(), idx);

    double w = image.width(), h = image.height();
    if (w > kMaxImageWidthPx) { h = h * kMaxImageWidthPx / w; w = kMaxImageWidthPx; }
    QTextImageFormat imf;
    imf.setName(m_store.nameOf(idx));
    imf.setWidth(w);
    imf.setHeight(h);
    m_edit->textCursor().insertImage(imf);
}

void DocxEdit::insertImageFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert an image"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit statusMessage(tr("Cannot read %1").arg(path));
        return;
    }
    const QByteArray bytes = f.readAll();
    QImage img;
    if (!img.loadFromData(bytes)) {
        emit statusMessage(tr("This file is not a supported image."));
        return;
    }
    insertImageBytes(bytes, img);
}

void DocxEdit::insertLink()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Insert a link"), tr("Address (URL) :"), QLineEdit::Normal,
                                              QStringLiteral("https://"), &ok).trimmed();
    if (!ok || url.isEmpty()) return;
    QTextCharFormat f;
    f.setAnchor(true);
    f.setAnchorHref(url);
    f.setForeground(QBrush(QColor(0x05, 0x63, 0xC1)));
    f.setFontUnderline(true);
    QTextCursor c = m_edit->textCursor();
    if (c.hasSelection()) c.mergeCharFormat(f);
    else c.insertText(url, f);
}

void DocxEdit::insertPageBreak()
{
    const QTextCursor end = m_conv->insertRawBlock(m_edit->textCursor(),
        "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
    m_edit->setTextCursor(end);
}

// ----------------------------------------------------------------------------- état de la barre d'outils
void DocxEdit::syncToolbar()
{
    const QTextCharFormat cf = m_edit->currentCharFormat();
    {
        const QSignalBlocker b1(m_boldAct), b2(m_italicAct), b3(m_underAct), b4(m_strikeAct);
        m_boldAct->setChecked(cf.fontWeight() >= QFont::Bold);
        m_italicAct->setChecked(cf.fontItalic());
        m_underAct->setChecked(cf.fontUnderline());
        m_strikeAct->setChecked(cf.fontStrikeOut());
    }
    {
        const QSignalBlocker b(m_fontCombo);
        const QString fam = cf.hasProperty(QTextFormat::FontFamilies) ? cf.fontFamilies().toStringList().value(0) : QString();
        if (!fam.isEmpty()) m_fontCombo->setCurrentFont(QFont(fam));
    }
    {
        const QSignalBlocker b(m_sizeCombo);
        if (cf.fontPointSize() > 0) {
            const double pt = cf.fontPointSize();
            m_sizeCombo->setCurrentText(pt == static_cast<int>(pt) ? QString::number(static_cast<int>(pt)) : QString::number(pt, 'f', 1));
        }
    }
    {
        const Qt::Alignment a = m_edit->alignment();
        QAction *target = (a & Qt::AlignHCenter) ? m_centerAct : (a & Qt::AlignRight) ? m_rightAct : (a & Qt::AlignJustify) ? m_justAct : m_leftAct;
        const QSignalBlocker b1(m_leftAct), b2(m_centerAct), b3(m_rightAct), b4(m_justAct);
        target->setChecked(true);
    }
    {
        const QSignalBlocker b(m_styleCombo);
        const QString name = QString::fromStdString(m_conv->paragraphStyleAt(m_edit->textCursor()));
        int idx = m_styleCombo->findData(name);
        if (idx < 0) idx = 0;
        m_styleCombo->setCurrentIndex(idx);
    }
    if (QTextList *l = m_edit->textCursor().currentList()) {
        const QTextListFormat::Style s = l->format().style();
        const bool bullets = (s == QTextListFormat::ListDisc || s == QTextListFormat::ListCircle || s == QTextListFormat::ListSquare);
        m_bulletAct->setToolTip(bullets ? tr("Bullets (active)") : tr("Bullets"));
        m_numberAct->setToolTip(!bullets ? tr("Numbering (active)") : tr("Numbering"));
    }
}
