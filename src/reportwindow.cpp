#include "reportwindow.h"

#include "docxedit.h"
#include "markdownedit.h"

#include <QBrush>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleIsNote = Qt::UserRole + 1;
}

ReportWindow::ReportWindow(const QString &folder, QWidget *parent)
    : QDialog(parent), m_folder(QDir::cleanPath(folder))
{
    setWindowTitle(tr("Report - %1").arg(QFileInfo(m_folder).fileName()));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(1500, 860);

    auto *mainLayout = new QVBoxLayout(this);

    // --- barre supérieure ---
    auto *top = new QHBoxLayout;
    m_reportLabel = new QLabel(this);
    auto *insertBtn = new QPushButton(tr("Insert into the report (Ctrl+Alt+C)"), this);
    insertBtn->setToolTip(tr("Insert the selected text of the Markdown pane (or the whole note) after the cursor of the Word document"));
    auto *saveBtn = new QPushButton(tr("Save the report (Ctrl+S)"), this);
    top->addWidget(m_reportLabel, 1);
    top->addWidget(insertBtn);
    top->addWidget(saveBtn);
    mainLayout->addLayout(top);

    // --- trois volets ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderLabel(tr("Notes to export"));

    auto *mdPane = new QWidget(splitter);
    auto *mdLayout = new QVBoxLayout(mdPane);
    mdLayout->setContentsMargins(0, 0, 0, 0);
    m_noteLabel = new QLabel(tr("Select a note in the tree."), mdPane);
    m_md = new MarkdownEdit(mdPane);
    m_md->setReadOnly(true);
    mdLayout->addWidget(m_noteLabel);
    mdLayout->addWidget(m_md, 1);

    m_docx = new DocxEdit(splitter);

    splitter->addWidget(m_tree);
    splitter->addWidget(mdPane);
    splitter->addWidget(m_docx);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({260, 520, 720});
    mainLayout->addWidget(splitter, 1);

    m_status = new QLabel(this);
    mainLayout->addWidget(m_status);

    populate(m_folder, nullptr);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ReportWindow::onSelectionChanged);
    connect(insertBtn, &QPushButton::clicked, this, &ReportWindow::insertIntoReport);
    connect(saveBtn, &QPushButton::clicked, this, &ReportWindow::saveReport);
    connect(m_docx, &DocxEdit::statusMessage, this, &ReportWindow::showStatus);

    auto *insertShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+C")), this);
    insertShortcut->setContext(Qt::WindowShortcut);
    connect(insertShortcut, &QShortcut::activated, this, &ReportWindow::insertIntoReport);

    auto *saveShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    saveShortcut->setContext(Qt::WindowShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &ReportWindow::saveReport);
}

// ----------------------------------------------------------------------------- arbre
void ReportWindow::populate(const QString &dirPath, QTreeWidgetItem *parent)
{
    QCollator collator;
    collator.setNumericMode(true);                      // "2-web" avant "10-privesc"
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto naturalLess = [&collator](const QString &a, const QString &b) { return collator.compare(a, b) < 0; };

    const QDir dir(dirPath);
    QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(files.begin(), files.end(), naturalLess);
    std::sort(dirs.begin(), dirs.end(), naturalLess);

    auto add = [&](const QString &name, const QString &path, bool isNote) {
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, isNote ? name : name + QLatin1Char('/'));
        item->setData(0, kRolePath, path);
        item->setData(0, kRoleIsNote, isNote);
        if (name.startsWith(QLatin1Char('_'))) item->setForeground(0, QBrush(Qt::gray));   // fausse piste (préfixe _)
        return item;
    };

    for (const QString &f : files) {
        const QString suffix = QFileInfo(f).suffix().toLower();
        if (suffix != QLatin1String("md") && suffix != QLatin1String("txt")) continue;
        add(f, dir.absoluteFilePath(f), true);
    }
    for (const QString &d : dirs) {
        QTreeWidgetItem *item = add(d, dir.absoluteFilePath(d), false);
        populate(dir.absoluteFilePath(d), item);
        if (item->childCount() == 0) { delete item; continue; }   // dossier sans note : masqué
        item->setExpanded(true);
    }
}

void ReportWindow::onSelectionChanged()
{
    const QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    QTreeWidgetItem *item = sel.first();
    if (!item->data(0, kRoleIsNote).toBool()) return;

    const QString path = item->data(0, kRolePath).toString();
    m_currentNote = path;
    m_md->setReadOnly(true);
    m_md->loadFromMdFile(path);
    m_noteLabel->setText(QDir(m_folder).relativeFilePath(path));
}

// ----------------------------------------------------------------------------- initialisation du rapport
bool ReportWindow::initialize()
{
    m_reportPath = QDir(m_folder).filePath(QStringLiteral("report.docx"));
    m_reportLabel->setText(tr("Report : %1").arg(m_reportPath));
    QString err;

    if (QFileInfo::exists(m_reportPath)) {
        if (!m_docx->openFile(m_reportPath, &err)) {
            QMessageBox::critical(this, tr("Report"), tr("Impossible to open report.docx :\n%1").arg(err));
            return false;
        }
        return true;
    }

    // Pas de report.docx : on demande le modèle du rapport et on le copie à la racine du dossier exporté.
    const QString tpl = QFileDialog::getOpenFileName(this, tr("Select the Word template of the report"), m_folder,
                                                     tr("Word documents (*.docx *.dotx)"));
    if (tpl.isEmpty()) {
        if (QMessageBox::question(this, tr("Report"),
                tr("No template was chosen.\n\nCreate a blank report instead ?")) != QMessageBox::Yes)
            return false;
        m_docx->newBlank();
        m_docx->setFilePath(m_reportPath);
        if (!m_docx->save(&err)) {
            QMessageBox::critical(this, tr("Report"), tr("Impossible to create report.docx :\n%1").arg(err));
            return false;
        }
        return true;
    }

    if (!QFile::copy(tpl, m_reportPath)) {
        QMessageBox::critical(this, tr("Report"), tr("Impossible to copy the template to :\n%1").arg(m_reportPath));
        return false;
    }
    // un modèle en lecture seule resterait en lecture seule après la copie
    QFile::setPermissions(m_reportPath, QFile::permissions(m_reportPath) | QFile::ReadOwner | QFile::WriteOwner);

    if (!m_docx->openFile(m_reportPath, &err)) {
        QMessageBox::critical(this, tr("Report"), tr("Impossible to open the template :\n%1").arg(err));
        QFile::remove(m_reportPath);
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------- actions
void ReportWindow::insertIntoReport()
{
    if (m_currentNote.isEmpty()) {
        showStatus(tr("Select a note in the tree first."));
        return;
    }

    QString markdown;
    if (m_md->textCursor().hasSelection()) {
        markdown = m_md->selectedMarkdownText();
    } else {
        QFile f(m_currentNote);
        if (!f.open(QIODevice::ReadOnly)) {
            showStatus(tr("Cannot read the note."));
            return;
        }
        markdown = QString::fromUtf8(f.readAll());
    }

    const int n = m_docx->insertMarkdown(markdown, QFileInfo(m_currentNote).absolutePath());
    showStatus(n > 0 ? tr("%1 block(s) inserted into the report.").arg(n) : tr("Nothing to insert."));
}

void ReportWindow::saveReport()
{
    QString err;
    if (m_docx->save(&err)) showStatus(tr("Report saved : %1").arg(m_reportPath));
    else QMessageBox::critical(this, tr("Report"), tr("Impossible to save the report :\n%1").arg(err));
}

void ReportWindow::showStatus(const QString &text) { m_status->setText(text); }

void ReportWindow::reject()
{
    if (m_docx->isModified()) {
        const auto answer = QMessageBox::question(this, tr("Report"),
            tr("The report has unsaved changes. Save them before closing ?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save) {
            QString err;
            if (!m_docx->save(&err)) {
                QMessageBox::critical(this, tr("Report"), tr("Impossible to save the report :\n%1").arg(err));
                return;
            }
        }
    }
    QDialog::reject();
}
