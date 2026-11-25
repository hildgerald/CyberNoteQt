#include "docxconvert.h"

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>
#include <QUrl>

#include <algorithm>
#include <map>

namespace {

// Propriétés personnalisées (>= QTextFormat::UserProperty)
constexpr int kPStyle = QTextFormat::UserProperty + 1;    // QString : nom du style Word du paragraphe
constexpr int kPExtra = QTextFormat::UserProperty + 2;    // QStringList : (nom, xml)* autres propriétés de paragraphe
constexpr int kPPrefix = QTextFormat::UserProperty + 3;   // QString : XML avant les runs (signets)
constexpr int kPSuffix = QTextFormat::UserProperty + 4;   // QString : XML après les runs
constexpr int kPRaw = QTextFormat::UserProperty + 5;      // int : index d'un bloc brut
constexpr int kPNumId = QTextFormat::UserProperty + 6;    // int : numId d'origine (gabarit)
constexpr int kPLabel = QTextFormat::UserProperty + 7;    // QString : numéro affiché au début du paragraphe (texte d'affichage)
constexpr int kCPad = QTextFormat::UserProperty + 34;     // bool : case ajoutée pour rendre le tableau rectangulaire
constexpr int kRExtra = QTextFormat::UserProperty + 20;   // QStringList : (nom, xml)* autres propriétés de run
constexpr int kTTblPr = QTextFormat::UserProperty + 30;   // QString : <w:tblPr> d'origine
constexpr int kTGrid = QTextFormat::UserProperty + 31;    // QString : "w1,w2,..." (twips)
constexpr int kCTcPr = QTextFormat::UserProperty + 32;    // QString : <w:tcPr> d'origine (cellule)
constexpr int kCTrPr = QTextFormat::UserProperty + 33;    // QString : <w:trPr> d'origine (cellule de la colonne 0)

QString toQ(const std::string &s) { return QString::fromStdString(s); }
std::string toStd(const QString &s) { return s.toStdString(); }

QColor colorFromHex(const std::string &hex) { return QColor(QStringLiteral("#") + toQ(hex)); }
std::string hexFromColor(const QColor &c) { return c.name(QColor::HexRgb).mid(1).toUpper().toStdString(); }

QStringList flatten(const std::vector<docx::XmlChild> &v)
{
    QStringList l;
    for (const docx::XmlChild &c : v) { l << toQ(c.first) << toQ(c.second); }
    return l;
}

std::vector<docx::XmlChild> unflatten(const QStringList &l)
{
    std::vector<docx::XmlChild> v;
    for (int i = 0; i + 1 < l.size(); i += 2) v.emplace_back(toStd(l.at(i)), toStd(l.at(i + 1)));
    return v;
}

// ~ v2.6.2 : fond (ombrage) d'un paragraphe : <w:shd> direct du paragraphe, sinon celui de son style. "" = aucun.
std::string paragraphFill(const std::vector<docx::XmlChild> &pprExtra, const docx::StyleInfo &st)
{
    for (const docx::XmlChild &e : pprExtra) {
        if (e.first != "w:shd") continue;
        const size_t p = e.second.find("w:fill=\"");
        if (p == std::string::npos) break;
        const size_t b = p + 8, q = e.second.find('\"', b);
        if (q == std::string::npos) break;
        const std::string fill = e.second.substr(b, q - b);
        return (fill == "auto" || fill.size() != 6) ? std::string() : fill;
    }
    return st.shading.size() == 6 ? st.shading : std::string();
}

Qt::Alignment alignFrom(char a)
{
    switch (a) {
    case 'c': return Qt::AlignHCenter;
    case 'r': return Qt::AlignRight;
    case 'j': return Qt::AlignJustify;
    default: return Qt::AlignLeft;
    }
}

char alignTo(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter) return 'c';
    if (a & Qt::AlignRight) return 'r';
    if (a & Qt::AlignJustify) return 'j';
    return 0;
}

QString fillOf(const std::string &tcPr)
{
    const size_t p = tcPr.find("w:fill=\"");
    if (p == std::string::npos) return QString();
    const size_t b = p + 8, e = tcPr.find('"', b);
    if (e == std::string::npos) return QString();
    const std::string v = tcPr.substr(b, e - b);
    return v == "auto" ? QString() : QStringLiteral("#") + toQ(v);
}

// Bordure d'affichage d'un tableau d'après le XML d'origine de sa première cellule : invisible (largeur 0) si les
// bordures sont absentes ou blanches (tableau de mise en page), sinon largeur 1 et couleur d'origine.
void tableBorder(const docx::Table &t, int &width, QColor &color)
{
    width = 1;
    color = QColor(0xA6, 0xA6, 0xA6);
    std::string tcpr;
    for (const docx::TableRow &r : t.rows)
        for (const docx::TableCell &c : r.cells)
            if (tcpr.empty() && !c.tcPr.empty()) tcpr = c.tcPr;

    auto attrOf = [](const std::string &el, const char *name) {
        const size_t p = el.find(name);
        if (p == std::string::npos) return std::string();
        const size_t b = p + std::char_traits<char>::length(name), e = el.find('"', b);
        return e == std::string::npos ? std::string() : el.substr(b, e - b);
    };

    const size_t bp = tcpr.find("<w:top ");
    if (bp != std::string::npos) {
        const size_t e = tcpr.find("/>", bp);
        const std::string top = tcpr.substr(bp, e == std::string::npos ? std::string::npos : e - bp);
        const std::string val = attrOf(top, "w:val=\""), col = attrOf(top, "w:color=\"");
        if (val.empty() || val == "nil" || val == "none" || col == "ffffff" || col == "FFFFFF") width = 0;
        else if (col.size() == 6) color = colorFromHex(col);
        return;
    }
    if (t.tblPr.find("<w:tblBorders/>") != std::string::npos) width = 0;   // bordures vides explicitement
}

QTextListFormat::Style listStyleFor(int kind, int level)
{
    static const QTextListFormat::Style bullets[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
    static const QTextListFormat::Style numbers[] = {QTextListFormat::ListDecimal, QTextListFormat::ListLowerAlpha, QTextListFormat::ListLowerRoman};
    return kind == 1 ? bullets[level % 3] : numbers[level % 3];
}

bool isBulletStyle(QTextListFormat::Style s)
{
    return s == QTextListFormat::ListDisc || s == QTextListFormat::ListCircle || s == QTextListFormat::ListSquare;
}

QString fontFamilyOf(const QTextCharFormat &cf)
{
    if (!cf.hasProperty(QTextFormat::FontFamilies)) return QString();
    return cf.fontFamilies().toStringList().value(0);
}

QString colorOf(const QTextCharFormat &cf)
{
    if (!cf.hasProperty(QTextFormat::ForegroundBrush)) return QString();
    return toQ(hexFromColor(cf.foreground().color()));
}

} // namespace

// ============================================================================
//  DocxImageStore
// ============================================================================
int DocxImageStore::add(const docx::ImageData &img)
{
    m_images.push_back(img);
    return static_cast<int>(m_images.size()) - 1;
}

int DocxImageStore::indexOf(const QString &name) const
{
    if (!name.startsWith(QLatin1String("docximg:"))) return -1;
    bool ok = false;
    const int i = name.mid(8).toInt(&ok);
    return (ok && i >= 0 && i < static_cast<int>(m_images.size())) ? i : -1;
}

void DocxImageStore::registerInto(QTextDocument *doc, int index) const
{
    const docx::ImageData &d = m_images[static_cast<size_t>(index)];
    const QImage img = QImage::fromData(QByteArray(d.bytes.data(), static_cast<int>(d.bytes.size())));
    doc->addResource(QTextDocument::ImageResource, QUrl(nameOf(index)), QVariant::fromValue(img));
}

void DocxImageStore::registerAll(QTextDocument *doc) const
{
    for (int i = 0; i < static_cast<int>(m_images.size()); ++i) registerInto(doc, i);
}

// ============================================================================
//  Rendu modèle -> QTextDocument
// ============================================================================
struct ListState {
    int key = 0;
    int lastLevel = -1;
    std::map<int, QTextList *> byLevel;
    void reset() { key = 0; lastLevel = -1; byLevel.clear(); }
};

class DocxRenderer
{
public:
    DocxRenderer(DocxConverter &conv, const std::vector<int> &imageMap) : m_conv(conv), m_imageMap(imageMap) {}

    // Insère les blocs à partir du curseur. `fresh` : le bloc courant est vide et peut être rempli.
    void blocks(QTextCursor &c, const std::vector<docx::Block> &blocks, bool &fresh)
    {
        ListState ls;
        for (const docx::Block &b : blocks) {
            if (b.kind == docx::Block::Para) paragraph(c, b.para, fresh, ls);
            else if (b.kind == docx::Block::Tbl) { ls.reset(); table(c, b.table, fresh); }
            else { ls.reset(); raw(c, b.raw, fresh); }
        }
    }

private:
    void paragraph(QTextCursor &c, const docx::Paragraph &p, bool &fresh, ListState &ls)
    {
        const QTextCharFormat base = m_conv.baseCharFormat(p.style);
        const docx::StyleInfo st = m_conv.styleFor(p.style);

        QTextBlockFormat bf;
        bf.setAlignment(alignFrom(p.align ? p.align : st.align));
        const int lvl = docx::headingLevelOfStyle(p.style);
        if (lvl > 0) bf.setHeadingLevel(lvl);
        bf.setTopMargin(st.spaceBefore >= 0 ? st.spaceBefore / 15.0 : 0.0);   // twips -> px (~)
        bf.setBottomMargin(st.spaceAfter >= 0 ? st.spaceAfter / 15.0 : 6.0);
        bf.setProperty(kPStyle, toQ(p.style));
        const std::string fill = paragraphFill(p.pprExtra, st);
        if (!fill.empty()) bf.setBackground(QBrush(colorFromHex(fill)));   // ~ v2.6.2 : fond gris des zones de code
        if (!p.pprExtra.empty()) bf.setProperty(kPExtra, flatten(p.pprExtra));
        if (!p.prefixXml.empty()) bf.setProperty(kPPrefix, toQ(p.prefixXml));
        if (!p.suffixXml.empty()) bf.setProperty(kPSuffix, toQ(p.suffixXml));
        if (p.numId > 0) bf.setProperty(kPNumId, p.numId);

        if (fresh) { c.setBlockFormat(bf); c.setBlockCharFormat(base); fresh = false; }
        else c.insertBlock(bf, base);

        if (p.list > 0) {
            const int key = p.numId > 0 ? p.numId : -std::max(1, p.listGroup);
            if (key != ls.key) { ls.byLevel.clear(); ls.key = key; }
            const int level = std::max(0, std::min(8, p.listLevel));
            for (auto it = ls.byLevel.begin(); it != ls.byLevel.end();) {
                if (it->first > level) it = ls.byLevel.erase(it); else ++it;
            }
            auto found = ls.byLevel.find(level);
            if (found != ls.byLevel.end() && found->second) {
                found->second->add(c.block());
            } else {
                QTextListFormat lf;
                lf.setStyle(listStyleFor(p.list, level));
                lf.setIndent(level + 1);
                ls.byLevel[level] = c.createList(lf);
            }
            ls.lastLevel = level;
        } else {
            ls.reset();
        }

        for (const docx::Run &r : p.runs) {
            if (r.kind == docx::Run::Image) {
                if (r.image < 0 || r.image >= static_cast<int>(m_imageMap.size())) continue;
                const int gi = m_imageMap[static_cast<size_t>(r.image)];
                QTextImageFormat imf;
                imf.setName(m_conv.m_images->nameOf(gi));
                imf.setWidth(r.cx / 9525.0);
                imf.setHeight(r.cy / 9525.0);
                c.insertImage(imf);
            } else {
                QString text = toQ(r.text);
                text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
                if (text.isEmpty()) continue;
                c.insertText(text, m_conv.charFormatFor(r.fmt, base));
            }
        }
    }

    void raw(QTextCursor &c, const std::string &xml, bool &fresh)
    {
        QTextBlockFormat bf;
        bf.setAlignment(Qt::AlignLeft);
        bf.setBackground(QBrush(QColor(0xEE, 0xEE, 0xEE)));
        bf.setProperty(kPRaw, m_conv.addRaw(xml));
        QTextCharFormat cf;
        cf.setForeground(QBrush(QColor(0x55, 0x55, 0x55)));
        cf.setFontPointSize(10);
        if (fresh) { c.setBlockFormat(bf); c.setBlockCharFormat(cf); fresh = false; }
        else c.insertBlock(bf, cf);

        // Table des matières et tableaux non modifiables : on affiche leur texte pour que le contenu reste visible.
        const bool toc = docx::rawHasToc(xml);
        QString preview;
        if (toc || xml.find("<w:tbl") != std::string::npos) preview = toQ(docx::rawPreview(xml));

        QTextCharFormat head = cf;
        head.setFontItalic(true);
        head.setFontPointSize(9);
        QString label = QStringLiteral("\u27E6 ") + toQ(docx::describeRaw(xml));
        if (toc) label += QStringLiteral(" \u2014 mise \u00E0 jour par Word \u00E0 l'ouverture");
        label += QStringLiteral(" \u27E7");
        c.insertText(label, head);

        if (!preview.isEmpty()) {
            preview.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            c.insertText(QString(QChar(QChar::LineSeparator)) + preview, cf);
        }
    }

    void table(QTextCursor &c, const docx::Table &t, bool &fresh)
    {
        if (t.rows.empty()) return;
        size_t ncols = 0;
        for (const docx::TableRow &r : t.rows) ncols = std::max(ncols, r.cells.size());
        if (ncols == 0) return;

        if (!fresh) { c.insertBlock(QTextBlockFormat(), QTextCharFormat()); fresh = true; }

        int borderWidth = 1;
        QColor borderColor;
        tableBorder(t, borderWidth, borderColor);

        QTextTableFormat tf;
        tf.setBorder(borderWidth);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderBrush(QBrush(borderColor));
        tf.setCellPadding(3);
        tf.setCellSpacing(0);
        tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        if (t.grid.size() == ncols) {
            long long total = 0;
            for (int w : t.grid) total += std::max(1, w);
            QList<QTextLength> cons;
            QStringList g;
            for (int w : t.grid) {
                cons << QTextLength(QTextLength::PercentageLength, 100.0 * std::max(1, w) / static_cast<double>(total));
                g << QString::number(w);
            }
            tf.setColumnWidthConstraints(cons);
            tf.setProperty(kTGrid, g.join(QLatin1Char(',')));
        }
        if (!t.tblPr.empty()) tf.setProperty(kTTblPr, toQ(t.tblPr));

        QTextTable *table = c.insertTable(static_cast<int>(t.rows.size()), static_cast<int>(ncols), tf);
        if (!table) return;

        for (size_t r = 0; r < t.rows.size(); ++r) {
            for (size_t col = 0; col < ncols; ++col) {
                QTextTableCell cell = table->cellAt(static_cast<int>(r), static_cast<int>(col));
                QTextTableCellFormat cf;
                const docx::TableCell *dc = col < t.rows[r].cells.size() ? &t.rows[r].cells[col] : nullptr;
                if (!dc) cf.setProperty(kCPad, true);   // la ligne d'origine était plus courte : case d'affichage seulement
                if (dc && !dc->tcPr.empty()) {
                    cf.setProperty(kCTcPr, toQ(dc->tcPr));
                    const QString fill = fillOf(dc->tcPr);
                    if (!fill.isEmpty()) cf.setBackground(QBrush(QColor(fill)));
                }
                if (col == 0 && !t.rows[r].trPr.empty()) cf.setProperty(kCTrPr, toQ(t.rows[r].trPr));
                cell.setFormat(cf);

                if (dc && !dc->blocks.empty()) {
                    QTextCursor cc = cell.firstCursorPosition();
                    bool cellFresh = true;
                    blocks(cc, dc->blocks, cellFresh);   // paragraphes, tableaux imbriqués, blocs bruts
                }
            }
        }

        // curseur : bloc situé juste après le tableau (créé vide s'il n'existe pas ou n'est pas vide)
        QTextCursor after = table->lastCursorPosition();
        if (after.movePosition(QTextCursor::NextBlock)) {
            if (!after.block().text().isEmpty() || after.block().textList()) {
                after.movePosition(QTextCursor::StartOfBlock);
                after.insertBlock(QTextBlockFormat(), QTextCharFormat());
                after.movePosition(QTextCursor::PreviousBlock);
            }
        } else {
            after.movePosition(QTextCursor::End);
            after.insertBlock(QTextBlockFormat(), QTextCharFormat());
        }
        c = after;
        fresh = true;
    }

    DocxConverter &m_conv;
    const std::vector<int> &m_imageMap;
};

// ============================================================================
//  DocxConverter
// ============================================================================
DocxConverter::DocxConverter(QTextDocument *doc, DocxImageStore *images) : m_doc(doc), m_images(images) {}

int DocxConverter::addRaw(const std::string &xml)
{
    m_raw.push_back(xml);
    return static_cast<int>(m_raw.size()) - 1;
}

docx::StyleInfo DocxConverter::styleFor(const std::string &styleName) const
{
    if (!m_styles) return docx::StyleInfo();
    const docx::StyleInfo *s = styleName.empty() ? m_styles->byId(m_styles->defParaStyleId) : m_styles->byName(styleName);
    return s ? m_styles->resolved(*s) : docx::StyleInfo();
}

QTextCharFormat DocxConverter::baseCharFormat(const std::string &styleName) const
{
    QTextCharFormat cf;
    const docx::StyleInfo st = styleFor(styleName);
    const std::string font = !st.font.empty() ? st.font : (m_styles ? m_styles->defFont : std::string());
    const int halfPt = st.sizeHalfPt > 0 ? st.sizeHalfPt : (m_styles ? m_styles->defSizeHalfPt : 22);
    if (!font.empty()) cf.setFontFamilies(QStringList{toQ(font)});
    cf.setFontPointSize(halfPt / 2.0);
    cf.setFontWeight(st.bold == 1 ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(st.italic == 1);
    cf.setFontUnderline(st.underline == 1);
    cf.setFontStrikeOut(st.strike == 1);
    if (!st.color.empty()) cf.setForeground(QBrush(colorFromHex(st.color)));
    return cf;
}

QTextCharFormat DocxConverter::charFormatFor(const docx::RunFormat &f, const QTextCharFormat &base) const
{
    QTextCharFormat cf = base;
    if (!f.font.empty()) cf.setFontFamilies(QStringList{toQ(f.font)});
    if (f.sizeHalfPt > 0) cf.setFontPointSize(f.sizeHalfPt / 2.0);
    if (f.bold >= 0) cf.setFontWeight(f.bold ? QFont::Bold : QFont::Normal);
    if (f.italic >= 0) cf.setFontItalic(f.italic != 0);
    if (f.underline >= 0) cf.setFontUnderline(f.underline != 0);
    if (f.strike >= 0) cf.setFontStrikeOut(f.strike != 0);
    if (!f.color.empty()) cf.setForeground(QBrush(colorFromHex(f.color)));
    if (!f.background.empty()) cf.setBackground(QBrush(colorFromHex(f.background)));
    if (!f.link.empty()) {
        cf.setAnchor(true);
        cf.setAnchorHref(toQ(f.link));
        if (f.color.empty()) cf.setForeground(QBrush(QColor(0x05, 0x63, 0xC1)));
        cf.setFontUnderline(true);
    }
    if (!f.extra.empty()) cf.setProperty(kRExtra, flatten(f.extra));
    return cf;
}

void DocxConverter::load(const docx::Document &model)
{
    m_raw.clear();
    m_images->clear();
    for (const docx::ImageData &img : model.images) m_images->add(img);

    m_doc->setUndoRedoEnabled(false);
    m_doc->clear();
    m_images->registerAll(m_doc);

    std::vector<int> imageMap(model.images.size());
    for (size_t i = 0; i < imageMap.size(); ++i) imageMap[i] = static_cast<int>(i);

    QTextCursor c(m_doc);
    c.beginEditBlock();
    DocxRenderer r(*this, imageMap);
    bool fresh = true;
    r.blocks(c, model.blocks, fresh);
    c.endEditBlock();
    refreshNumbering();

    m_doc->setModified(false);
    m_doc->setUndoRedoEnabled(true);
}

QTextCursor DocxConverter::insertBlocks(QTextCursor cursor, const std::vector<docx::Block> &blocks,
                                        const std::vector<docx::ImageData> &blockImages)
{
    std::vector<int> imageMap;
    for (const docx::ImageData &img : blockImages) {
        const int gi = m_images->add(img);
        m_images->registerInto(m_doc, gi);
        imageMap.push_back(gi);
    }

    // Nouvelle ligne juste après le paragraphe du curseur, y compris à l'intérieur d'une cellule de tableau
    // (les gabarits placent souvent tout leur contenu dans un tableau de mise en page).
    QTextCursor c = cursor;
    c.beginEditBlock();
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());

    DocxRenderer r(*this, imageMap);
    bool fresh = true;
    r.blocks(c, blocks, fresh);
    c.endEditBlock();
    refreshNumbering();
    return c;
}

QTextCursor DocxConverter::insertRawBlock(QTextCursor cursor, const std::string &xml)
{
    docx::Block b;
    b.kind = docx::Block::Raw;
    b.raw = xml;
    return insertBlocks(cursor, std::vector<docx::Block>{b}, std::vector<docx::ImageData>());
}

std::string DocxConverter::paragraphStyleAt(const QTextCursor &cursor) const
{
    return toStd(cursor.blockFormat().property(kPStyle).toString());
}

void DocxConverter::applyParagraphStyle(QTextCursor cursor, const std::string &styleName)
{
    if (cursor.blockFormat().hasProperty(kPRaw)) return;
    const QTextCharFormat base = baseCharFormat(styleName);
    const docx::StyleInfo st = styleFor(styleName);

    cursor.beginEditBlock();
    QTextBlockFormat bf;
    bf.setProperty(kPStyle, toQ(styleName));
    bf.setHeadingLevel(docx::headingLevelOfStyle(styleName));
    bf.setTopMargin(st.spaceBefore >= 0 ? st.spaceBefore / 15.0 : 0.0);
    bf.setBottomMargin(st.spaceAfter >= 0 ? st.spaceAfter / 15.0 : 6.0);
    {   // ~ v2.6.2 : le fond suit le style (ajouté pour « Code Block », retiré en revenant à un autre style)
        const std::string fill = paragraphFill(unflatten(cursor.blockFormat().property(kPExtra).toStringList()), st);
        bf.setBackground(fill.empty() ? QBrush(Qt::NoBrush) : QBrush(colorFromHex(fill)));
    }
    cursor.mergeBlockFormat(bf);

    QTextCursor sel(cursor.block());
    sel.movePosition(QTextCursor::StartOfBlock);
    sel.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    sel.mergeCharFormat(base);
    cursor.setBlockCharFormat(base);
    cursor.endEditBlock();
}

// ============================================================================
//  Extraction QTextDocument -> modèle
// ============================================================================
docx::RunFormat DocxConverter::runFormatFrom(const QTextCharFormat &cf, const QTextCharFormat &base) const
{
    docx::RunFormat rf;

    const QString fam = fontFamilyOf(cf);
    if (!fam.isEmpty() && fam != fontFamilyOf(base)) rf.font = toStd(fam);

    if (cf.hasProperty(QTextFormat::FontPointSize)) {
        const int half = static_cast<int>(cf.fontPointSize() * 2.0 + 0.5);
        const int baseHalf = base.hasProperty(QTextFormat::FontPointSize) ? static_cast<int>(base.fontPointSize() * 2.0 + 0.5) : 0;
        if (half > 0 && half != baseHalf) rf.sizeHalfPt = half;
    }

    const bool bold = cf.fontWeight() >= QFont::Bold, baseBold = base.fontWeight() >= QFont::Bold;
    if (bold != baseBold) rf.bold = bold ? 1 : 0;
    if (cf.fontItalic() != base.fontItalic()) rf.italic = cf.fontItalic() ? 1 : 0;
    if (cf.fontStrikeOut() != base.fontStrikeOut()) rf.strike = cf.fontStrikeOut() ? 1 : 0;
    if (cf.fontUnderline() != base.fontUnderline()) rf.underline = cf.fontUnderline() ? 1 : 0;

    const QString col = colorOf(cf);
    if (!col.isEmpty() && col != colorOf(base)) rf.color = toStd(col);

    if (cf.hasProperty(QTextFormat::BackgroundBrush) && cf.background().style() != Qt::NoBrush)
        rf.background = hexFromColor(cf.background().color());

    if (cf.isAnchor()) {
        rf.link = toStd(cf.anchorHref());
        if (rf.underline == 1) rf.underline = -1;                 // soulignement/bleu des liens : ajoutés à l'enregistrement
        if (rf.color == "0563C1") rf.color.clear();
    }

    if (cf.hasProperty(kRExtra)) rf.extra = unflatten(cf.property(kRExtra).toStringList());
    return rf;
}

docx::Block DocxConverter::extractBlock(const QTextBlock &block, QHash<QTextList *, int> &groups) const
{
    docx::Block out;
    const QTextBlockFormat bf = block.blockFormat();

    if (bf.hasProperty(kPRaw)) {
        const int idx = bf.property(kPRaw).toInt();
        if (idx >= 0 && idx < static_cast<int>(m_raw.size())) {
            out.kind = docx::Block::Raw;
            out.raw = m_raw[static_cast<size_t>(idx)];
            return out;
        }
    }

    out.kind = docx::Block::Para;
    docx::Paragraph &p = out.para;
    p.style = toStd(bf.property(kPStyle).toString());
    p.align = alignTo(bf.alignment());
    if (p.align == alignTo(alignFrom(styleFor(p.style).align))) p.align = 0;   // identique au style : non répété
    p.pprExtra = unflatten(bf.property(kPExtra).toStringList());
    p.prefixXml = toStd(bf.property(kPPrefix).toString());
    p.suffixXml = toStd(bf.property(kPSuffix).toString());

    if (QTextList *l = block.textList()) {
        const QTextListFormat lf = l->format();
        p.list = isBulletStyle(lf.style()) ? 1 : 2;
        p.listLevel = std::max(0, std::min(8, lf.indent() - 1));
        if (bf.hasProperty(kPNumId)) p.numId = bf.property(kPNumId).toInt();
        if (!groups.contains(l)) groups.insert(l, groups.size() + 1);
        p.listGroup = groups.value(l);
    }

    const QTextCharFormat base = baseCharFormat(p.style);
    // Le numéro de titre affiché au début du paragraphe n'appartient pas au contenu : il est retiré.
    QString label = bf.property(kPLabel).toString();
    int strip = (!label.isEmpty() && block.text().startsWith(label)) ? static_cast<int>(label.size()) : 0;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (!f.isValid()) continue;
        const QTextCharFormat cf = f.charFormat();

        if (cf.isImageFormat()) {
            const QTextImageFormat imf = cf.toImageFormat();
            const int idx = m_images->indexOf(imf.name());
            if (idx < 0) continue;
            docx::Run r;
            r.kind = docx::Run::Image;
            r.image = idx;
            const docx::ImageData &img = m_images->all()[static_cast<size_t>(idx)];
            const double w = imf.width() > 0 ? imf.width() : img.wPx;
            const double h = imf.height() > 0 ? imf.height() : img.hPx;
            r.cx = static_cast<long long>(w * 9525.0);
            r.cy = static_cast<long long>(h * 9525.0);
            p.runs.push_back(r);
            continue;
        }

        QString text = f.text();
        if (strip > 0) {
            const int n = std::min(strip, static_cast<int>(text.size()));
            text.remove(0, n);
            strip -= n;
        }
        text.replace(QChar(QChar::LineSeparator), QLatin1Char('\n'));
        text.remove(QChar(QChar::ObjectReplacementCharacter));
        if (text.isEmpty()) continue;

        const docx::RunFormat rf = runFormatFrom(cf, base);
        if (!p.runs.empty() && p.runs.back().kind == docx::Run::Text && p.runs.back().fmt == rf) {
            p.runs.back().text += toStd(text);   // fragments voisins de même format : un seul run
        } else {
            docx::Run r;
            r.kind = docx::Run::Text;
            r.text = toStd(text);
            r.fmt = rf;
            p.runs.push_back(r);
        }
    }
    return out;
}

docx::Block DocxConverter::extractTable(QTextTable *table, QHash<QTextList *, int> &groups) const
{
    docx::Block out;
    out.kind = docx::Block::Tbl;
    docx::Table &t = out.table;

    const QTextTableFormat tf = table->format();
    t.tblPr = toStd(tf.property(kTTblPr).toString());
    const QString grid = tf.property(kTGrid).toString();
    if (!grid.isEmpty())
        for (const QString &w : grid.split(QLatin1Char(','))) t.grid.push_back(w.toInt());

    for (int r = 0; r < table->rows(); ++r) {
        docx::TableRow row;
        for (int c = 0; c < table->columns(); ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            docx::TableCell dc;
            const QTextCharFormat cfmt = cell.format();
            dc.tcPr = toStd(cfmt.property(kCTcPr).toString());
            if (dc.tcPr.empty() && cfmt.hasProperty(QTextFormat::BackgroundBrush) && cfmt.background().style() != Qt::NoBrush)
                dc.tcPr = "<w:tcPr><w:tcW w:w=\"0\" w:type=\"auto\"/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""
                          + hexFromColor(cfmt.background().color()) + "\"/></w:tcPr>";
            if (c == 0) row.trPr = toStd(cfmt.property(kCTrPr).toString());

            if (cell.row() == r && cell.column() == c) {   // cellule fusionnée (non gérée) : cases suivantes laissées vides
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    if (QTextFrame *f = it.currentFrame()) {
                        if (QTextTable *nested = qobject_cast<QTextTable *>(f)) dc.blocks.push_back(extractTable(nested, groups));
                        continue;
                    }
                    const QTextBlock b = it.currentBlock();
                    if (b.isValid()) dc.blocks.push_back(extractBlock(b, groups));
                }
            }
            if (dc.blocks.empty()) { docx::Block e; e.kind = docx::Block::Para; dc.blocks.push_back(std::move(e)); }
            // case d'affichage restée vide : elle n'existait pas dans le fichier d'origine
            if (cfmt.property(kCPad).toBool()) {
                bool empty = true;
                for (const docx::Block &b : dc.blocks)
                    if (b.kind != docx::Block::Para || !b.para.runs.empty()) empty = false;
                dc.padding = empty;
            }
            row.cells.push_back(std::move(dc));
        }
        t.rows.push_back(std::move(row));
    }
    return out;
}

void DocxConverter::walkNumbering(QTextFrame::iterator it, docx::NumberingCounter &counter,
                                  std::vector<std::pair<int, QString>> &updates) const
{
    for (; !it.atEnd(); ++it) {
        if (QTextFrame *f = it.currentFrame()) {
            if (QTextTable *t = qobject_cast<QTextTable *>(f)) {
                for (int r = 0; r < t->rows(); ++r)
                    for (int c = 0; c < t->columns(); ++c) {
                        const QTextTableCell cell = t->cellAt(r, c);
                        if (cell.row() == r && cell.column() == c) walkNumbering(cell.begin(), counter, updates);
                    }
            }
            continue;
        }

        const QTextBlock b = it.currentBlock();
        if (!b.isValid()) continue;
        const QTextBlockFormat bf = b.blockFormat();
        if (bf.hasProperty(kPRaw)) continue;

        const docx::StyleInfo st = styleFor(toStd(bf.property(kPStyle).toString()));
        QString newLabel;
        if (QTextList *list = b.textList()) {
            // paragraphe d'une liste du gabarit : il fait avancer le compteur (les titres numérotés en dépendent)
            if (bf.hasProperty(kPNumId) && bf.property(kPNumId).toInt() > 0)
                counter.next(bf.property(kPNumId).toInt(), std::max(0, list->format().indent() - 1));
        } else if (st.numId > 0) {
            newLabel = toQ(counter.next(st.numId, st.numLevel));
        }

        const QString oldLabel = bf.property(kPLabel).toString();
        const bool oldPresent = !oldLabel.isEmpty() && b.text().startsWith(oldLabel);
        if (newLabel == oldLabel && (oldLabel.isEmpty() || oldPresent)) continue;   // déjà à jour
        updates.emplace_back(b.position(), newLabel);
    }
}

void DocxConverter::refreshNumbering()
{
    if (!m_numbering || !m_styles || !m_numbering->present) return;

    docx::NumberingCounter counter(*m_numbering);
    std::vector<std::pair<int, QString>> updates;
    walkNumbering(m_doc->rootFrame()->begin(), counter, updates);
    if (updates.empty()) return;

    // modifications appliquées de la fin vers le début : les positions déjà relevées restent valables
    std::sort(updates.begin(), updates.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    QTextCursor edit(m_doc);
    edit.joinPreviousEditBlock();   // se fond dans la dernière action d'annulation
    for (const auto &u : updates) {
        const QTextBlock b = m_doc->findBlock(u.first);
        if (!b.isValid()) continue;
        const QTextBlockFormat bf = b.blockFormat();
        const QString oldLabel = bf.property(kPLabel).toString();

        QTextCursor c(b);
        if (!oldLabel.isEmpty() && b.text().startsWith(oldLabel)) {
            c.setPosition(b.position());
            c.setPosition(b.position() + static_cast<int>(oldLabel.size()), QTextCursor::KeepAnchor);
            c.removeSelectedText();
        }
        c.setPosition(m_doc->findBlock(u.first).position());
        if (!u.second.isEmpty()) c.insertText(u.second, baseCharFormat(toStd(bf.property(kPStyle).toString())));

        QTextBlockFormat nbf = c.blockFormat();
        if (u.second.isEmpty()) nbf.clearProperty(kPLabel);
        else nbf.setProperty(kPLabel, u.second);
        c.setBlockFormat(nbf);
    }
    edit.endEditBlock();
}

docx::Document DocxConverter::extract() const
{
    docx::Document out;
    out.images = m_images->all();

    QHash<QTextList *, int> groups;
    for (QTextFrame::iterator it = m_doc->rootFrame()->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *f = it.currentFrame()) {
            if (QTextTable *t = qobject_cast<QTextTable *>(f)) out.blocks.push_back(extractTable(t, groups));
            continue;
        }
        const QTextBlock b = it.currentBlock();
        if (b.isValid()) out.blocks.push_back(extractBlock(b, groups));
    }
    return out;
}
