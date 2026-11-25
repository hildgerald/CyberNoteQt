#ifndef INSERTNOTESDIALOG_H
#define INSERTNOTESDIALOG_H

#include <QDialog>
#include <QDir>
#include <QPushButton>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QSpinBox;
class QLabel;

// ~ nouveau (v2.0.0) : choix des notes d'un dossier (et de ses sous-dossiers) à insérer dans la
// note ouverte, typiquement le gabarit d'un rapport.
//
// Convention d'exclusion : une note ou un dossier dont le NOM COMMENCE PAR "_" (ex. "_ftp-anonyme.md",
// "_fausses-pistes/") est décoché par défaut (fausse piste) ; on peut quand même le cocher à la
// main. Les notes sont listées en arbre (notes du dossier d'abord, puis sous-dossiers, tri
// "naturel" 2 < 10) ; les boutons Up/Down changent l'ordre parmi les éléments d'un même niveau.
class InsertNotesDialog : public QDialog
{
    Q_OBJECT

public:
    // rootFolder : dossier choisi dans l'arbre du projet. excludedFile : note à ne pas proposer
    // (la note actuellement ouverte, c'est-à-dire le gabarit lui-même ; chemin canonique ou vide).
    InsertNotesDialog(const QString &rootFolder, const QString &excludedFile, QWidget *parent = nullptr);

    bool hasNotes() const { return m_noteTotal > 0; }

    // Notes cochées, dans l'ordre d'affichage de l'arbre (chemins absolus).
    QStringList selectedFiles() const;

    bool addTitles() const;   // ajouter un titre (chemin relatif de la note) avant chaque note
    int titleLevel() const;   // niveau (1..5) du titre de note ; les titres des notes sont décalés en conséquence

    static bool isExcludedName(const QString &name) { return name.startsWith(QLatin1Char('_')); }

private slots:
    void onItemChanged(QTreeWidgetItem *item);
    void moveCurrent(int delta);
    void setAllChecked(bool checked);

private:
    int populate(const QDir &dir, QTreeWidgetItem *parent, bool parentExcluded);
    void collect(QTreeWidgetItem *item, QStringList &out) const;
    Qt::CheckState recomputeFolder(QTreeWidgetItem *item);   // état d'un dossier d'après ses enfants (récursif)
    void setSubtreeState(QTreeWidgetItem *item, Qt::CheckState state);
    void updateAncestors(QTreeWidgetItem *item);
    void updateCount();

    QString m_root;
    QString m_excludedFile;
    int m_noteTotal = 0;
    bool m_updating = false;   // évite la réentrance de onItemChanged pendant nos propres modifications

    QTreeWidget *m_tree = nullptr;
    QCheckBox *m_titlesCheck = nullptr;
    QSpinBox *m_levelSpin = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_okButton = nullptr;
};

#endif // INSERTNOTESDIALOG_H
