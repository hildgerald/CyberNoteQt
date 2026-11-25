#ifndef DOCXEDIT_H
#define DOCXEDIT_H

// ~ nouveau (v2.2.0) : éditeur de document Word intégré (QTextEdit + barre d'outils) qui lit et écrit des
// fichiers .docx en CONSERVANT le gabarit (styles, en-têtes/pieds de page, numérotation... : cf. docx_model.h).
// Fonctions : styles de paragraphe (titres 1-6, citation, code...), police/taille/couleur/surlignage,
// gras/italique/souligné/barré, alignement, listes à puces/numérotées, tableaux (création + lignes/colonnes),
// images (fichier, copier-coller), liens, saut de page, couper/copier/coller, annuler/rétablir.

#include <QWidget>

#include <memory>

#include "docx_model.h"
#include "docxconvert.h"

class QAction;
class QComboBox;
class QFontComboBox;
class QTextEdit;
class QToolBar;
class DocxTextEdit;

class DocxEdit : public QWidget
{
    Q_OBJECT

public:
    explicit DocxEdit(QWidget *parent = nullptr);
    ~DocxEdit() override;

    bool openFile(const QString &path, QString *error);   // charge un .docx (rapport ou gabarit)
    void newBlank();                                       // rapport vierge (styles CyberNoteQt)
    bool save(QString *error);                             // enregistre dans filePath()
    QString filePath() const { return m_path; }
    void setFilePath(const QString &path) { m_path = path; }
    bool isModified() const;

    // Convertit du Markdown (dialecte de CyberNoteQt) et l'insère à la ligne suivant le curseur ; les images
    // ![[x.png]] sont cherchées relativement à baseFolder. Renvoie le nombre de blocs insérés.
    int insertMarkdown(const QString &markdown, const QString &baseFolder);

signals:
    void modificationChanged(bool modified);
    void statusMessage(const QString &text);

private slots:
    void syncToolbar();
    void onStyleActivated(int index);
    void onFontChanged(const QFont &font);
    void onSizeChosen(const QString &text);
    void chooseTextColor();
    void chooseHighlight(const QColor &color, bool clear);
    void toggleList(int style);
    void changeListIndent(int delta);
    void insertTable();
    void tableOperation(int op);
    void insertImageFromFile();
    void insertLink();
    void insertPageBreak();

private:
    void buildToolbars();
    void rebuildStyleCombo();
    void applyDefaults();
    void mergeFormat(const QTextCharFormat &format);
    void insertImageBytes(const QByteArray &bytes, const QImage &image);

    DocxTextEdit *m_edit = nullptr;
    QToolBar *m_bar1 = nullptr, *m_bar2 = nullptr;
    QComboBox *m_styleCombo = nullptr, *m_sizeCombo = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QAction *m_boldAct = nullptr, *m_italicAct = nullptr, *m_underAct = nullptr, *m_strikeAct = nullptr;
    QAction *m_leftAct = nullptr, *m_centerAct = nullptr, *m_rightAct = nullptr, *m_justAct = nullptr;
    QAction *m_bulletAct = nullptr, *m_numberAct = nullptr;

    docx::Package m_pkg;
    DocxImageStore m_store;
    std::unique_ptr<DocxConverter> m_conv;
    QString m_path;
    int m_listGroup = 1000000;
};

#endif // DOCXEDIT_H
