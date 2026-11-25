#include "insertnotesdialog.h"

#include <QCheckBox>
#include <QCollator>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int RolePath = Qt::UserRole;       // chemin absolu (notes uniquement)
constexpr int RoleIsNote = Qt::UserRole + 1; // true pour une note, false pour un dossier
}

InsertNotesDialog::InsertNotesDialog(const QString &rootFolder, const QString &excludedFile, QWidget *parent)
    : QDialog(parent), m_root(QDir::cleanPath(rootFolder)), m_excludedFile(excludedFile)
{
    setWindowTitle(tr("Insert the notes of a folder"));
    resize(680, 540);

    auto *mainLayout = new QVBoxLayout(this);

    auto *help = new QLabel(tr("Check the notes to insert at the line following the cursor of the open note.\n"
                               "Notes and folders whose name starts with \"_\" (dead ends) are unchecked by default."), this);
    help->setWordWrap(true);
    mainLayout->addWidget(help);

    // --- arbre + boutons d'ordre / sélection ---
    auto *middle = new QHBoxLayout;
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    middle->addWidget(m_tree, 1);

    auto *buttons = new QVBoxLayout;
    auto *upBtn = new QPushButton(tr("Up"), this);
    auto *downBtn = new QPushButton(tr("Down"), this);
    auto *allBtn = new QPushButton(tr("Check all"), this);
    auto *noneBtn = new QPushButton(tr("Uncheck all"), this);
    buttons->addWidget(upBtn);
    buttons->addWidget(downBtn);
    buttons->addSpacing(12);
    buttons->addWidget(allBtn);
    buttons->addWidget(noneBtn);
    buttons->addStretch(1);
    middle->addLayout(buttons);
    mainLayout->addLayout(middle, 1);

    // --- options de mise en forme ---
    auto *options = new QHBoxLayout;
    m_titlesCheck = new QCheckBox(tr("Add a title before each note"), this);
    m_titlesCheck->setChecked(true);
    m_levelSpin = new QSpinBox(this);
    m_levelSpin->setRange(1, 5);
    m_levelSpin->setValue(2);
    options->addWidget(m_titlesCheck);
    options->addSpacing(16);
    options->addWidget(new QLabel(tr("Title level :"), this));
    options->addWidget(m_levelSpin);
    options->addStretch(1);
    mainLayout->addLayout(options);

    m_countLabel = new QLabel(this);
    mainLayout->addWidget(m_countLabel);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = box->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Insert"));
    mainLayout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(upBtn, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { moveCurrent(+1); });
    connect(allBtn, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(noneBtn, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    connect(m_titlesCheck, &QCheckBox::toggled, m_levelSpin, &QWidget::setEnabled);

    // --- remplissage ---
    m_updating = true;
    m_noteTotal = populate(QDir(m_root), m_tree->invisibleRootItem(), false);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        recomputeFolder(m_tree->topLevelItem(i));
    m_updating = false;

    connect(m_tree, &QTreeWidget::itemChanged, this, &InsertNotesDialog::onItemChanged);
    updateCount();
}

int InsertNotesDialog::populate(const QDir &dir, QTreeWidgetItem *parent, bool parentExcluded)
{
    QCollator collator;
    collator.setNumericMode(true);                      // "2-web" avant "10-privesc"
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto naturalLess = [&collator](const QString &a, const QString &b) { return collator.compare(a, b) < 0; };

    QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(files.begin(), files.end(), naturalLess);
    std::sort(dirs.begin(), dirs.end(), naturalLess);

    int count = 0;

    for (const QString &file : files) {
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (suffix != QLatin1String("md") && suffix != QLatin1String("txt"))
            continue;
        const QString path = dir.absoluteFilePath(file);
        if (!m_excludedFile.isEmpty() && QFileInfo(path).canonicalFilePath() == m_excludedFile)
            continue; // la note ouverte (le gabarit) ne s'insère pas dans elle-même

        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, file);
        item->setData(0, RolePath, path);
        item->setData(0, RoleIsNote, true);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, (parentExcluded || isExcludedName(file)) ? Qt::Unchecked : Qt::Checked);
        ++count;
    }

    for (const QString &d : dirs) {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, d + QLatin1Char('/'));
        item->setData(0, RoleIsNote, false);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        const bool excluded = parentExcluded || isExcludedName(d);
        const int n = populate(QDir(dir.absoluteFilePath(d)), item, excluded);
        if (n == 0) {
            delete item; // dossier sans aucune note : inutile de l'afficher
            continue;
        }
        item->setExpanded(true);
        count += n;
    }
    return count;
}

Qt::CheckState InsertNotesDialog::recomputeFolder(QTreeWidgetItem *item)
{
    if (item->data(0, RoleIsNote).toBool())
        return item->checkState(0);

    int checked = 0, unchecked = 0;
    for (int i = 0; i < item->childCount(); ++i) {
        const Qt::CheckState s = recomputeFolder(item->child(i));
        if (s == Qt::Checked) ++checked;
        else if (s == Qt::Unchecked) ++unchecked;
    }
    const int total = item->childCount();
    const Qt::CheckState state = (checked == total) ? Qt::Checked
                                 : (unchecked == total) ? Qt::Unchecked : Qt::PartiallyChecked;
    item->setCheckState(0, state);
    return state;
}

void InsertNotesDialog::setSubtreeState(QTreeWidgetItem *item, Qt::CheckState state)
{
    item->setCheckState(0, state);
    for (int i = 0; i < item->childCount(); ++i)
        setSubtreeState(item->child(i), state);
}

void InsertNotesDialog::updateAncestors(QTreeWidgetItem *item)
{
    for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) {
        int checked = 0, unchecked = 0;
        for (int i = 0; i < p->childCount(); ++i) {
            const Qt::CheckState s = p->child(i)->checkState(0);
            if (s == Qt::Checked) ++checked;
            else if (s == Qt::Unchecked) ++unchecked;
        }
        const int total = p->childCount();
        p->setCheckState(0, (checked == total) ? Qt::Checked
                            : (unchecked == total) ? Qt::Unchecked : Qt::PartiallyChecked);
    }
}

void InsertNotesDialog::onItemChanged(QTreeWidgetItem *item)
{
    if (m_updating)
        return;
    m_updating = true;
    if (!item->data(0, RoleIsNote).toBool() && item->checkState(0) != Qt::PartiallyChecked)
        setSubtreeState(item, item->checkState(0)); // un dossier coché/décoché entraîne tout son contenu
    updateAncestors(item);
    m_updating = false;
    updateCount();
}

void InsertNotesDialog::moveCurrent(int delta)
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    QTreeWidgetItem *parent = item->parent() ? item->parent() : m_tree->invisibleRootItem();
    const int index = parent->indexOfChild(item);
    const int target = index + delta;
    if (target < 0 || target >= parent->childCount())
        return;

    m_updating = true;
    const bool expanded = item->isExpanded();
    parent->takeChild(index);
    parent->insertChild(target, item);
    item->setExpanded(expanded);
    m_tree->setCurrentItem(item);
    m_updating = false;
}

void InsertNotesDialog::setAllChecked(bool checked)
{
    m_updating = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        setSubtreeState(m_tree->topLevelItem(i), checked ? Qt::Checked : Qt::Unchecked);
    m_updating = false;
    updateCount();
}

void InsertNotesDialog::collect(QTreeWidgetItem *item, QStringList &out) const
{
    if (item->data(0, RoleIsNote).toBool()) {
        if (item->checkState(0) == Qt::Checked)
            out << item->data(0, RolePath).toString();
        return;
    }
    for (int i = 0; i < item->childCount(); ++i)
        collect(item->child(i), out);
}

QStringList InsertNotesDialog::selectedFiles() const
{
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        collect(m_tree->topLevelItem(i), out);
    return out;
}

void InsertNotesDialog::updateCount()
{
    const int n = selectedFiles().size();
    m_countLabel->setText(tr("%1 note(s) selected out of %2.").arg(n).arg(m_noteTotal));
    m_okButton->setEnabled(n > 0);
}

bool InsertNotesDialog::addTitles() const { return m_titlesCheck->isChecked(); }
int InsertNotesDialog::titleLevel() const { return m_levelSpin->value(); }
