#ifndef REPORTWINDOW_H
#define REPORTWINDOW_H

// ~ nouveau (v2.2.0) : fenêtre de rédaction du rapport, à trois volets :
//   gauche  : arbre des dossiers/notes du dossier exporté ;
//   centre  : éditeur Markdown (lecture) affichant la note choisie dans l'arbre ;
//   droite  : éditeur Word (DocxEdit) du fichier report.docx situé à la racine du dossier exporté.
// Ctrl+Alt+C (ou le bouton) insère dans le rapport Word la sélection du volet Markdown (ou, sans sélection,
// toute la note) à la ligne suivant le curseur du document Word.

#include <QDialog>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class MarkdownEdit;
class DocxEdit;

class ReportWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ReportWindow(const QString &folder, QWidget *parent = nullptr);

    // Ouvre <dossier>/report.docx ; s'il n'existe pas, demande un modèle .docx/.dotx et le copie à cet endroit
    // (ou propose un rapport vierge). Renvoie false si l'utilisateur renonce ou en cas d'erreur.
    bool initialize();

public slots:
    void reject() override;   // propose d'enregistrer le rapport avant de fermer

private slots:
    void onSelectionChanged();
    void insertIntoReport();
    void saveReport();

private:
    void populate(const QString &dirPath, QTreeWidgetItem *parent);
    void showStatus(const QString &text);

    QString m_folder;
    QString m_reportPath;
    QString m_currentNote;

    QTreeWidget *m_tree = nullptr;
    MarkdownEdit *m_md = nullptr;
    DocxEdit *m_docx = nullptr;
    QLabel *m_noteLabel = nullptr;
    QLabel *m_reportLabel = nullptr;
    QLabel *m_status = nullptr;
};

#endif // REPORTWINDOW_H
