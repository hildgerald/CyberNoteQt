#ifndef DOCXCONVERT_H
#define DOCXCONVERT_H

// ~ nouveau (v2.2.0) : passerelle entre le modèle de document Word (docx_model.h, sans Qt) et un
// QTextDocument affichable/éditable dans un QTextEdit.
//  - load()         : modèle -> QTextDocument (remplace le contenu)
//  - insertBlocks() : insère des blocs du modèle (ex. du Markdown converti) après le curseur
//  - extract()      : QTextDocument -> modèle (pour enregistrer en .docx)
// Les informations Word que Qt ne représente pas (style Word, propriétés de paragraphe, XML brut de
// blocs non pris en charge...) sont mémorisées dans des propriétés personnalisées des formats Qt, ce
// qui leur permet de survivre aux modifications faites dans l'éditeur.

#include <QHash>
#include <QImage>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <string>
#include <vector>

#include "docx_model.h"

class DocxImageStore
{
public:
    int add(const docx::ImageData &img);                               // renvoie l'index
    const std::vector<docx::ImageData> &all() const { return m_images; }
    QString nameOf(int index) const { return QStringLiteral("docximg:%1").arg(index); }
    int indexOf(const QString &name) const;                            // -1 si inconnue
    void registerInto(QTextDocument *doc, int index) const;            // déclare l'image comme ressource du document
    void registerAll(QTextDocument *doc) const;
    void clear() { m_images.clear(); }

private:
    std::vector<docx::ImageData> m_images;
};

class DocxConverter
{
public:
    DocxConverter(QTextDocument *doc, DocxImageStore *images);

    void setStyles(const docx::StyleSheet *styles) { m_styles = styles; }
    void setNumbering(const docx::Numbering *numbering) { m_numbering = numbering; }

    // Recalcule et affiche les numéros liés aux styles (titres numérotés "1", "1.1"...), comme Word. Le numéro est
    // du texte d'affichage : il n'est pas enregistré dans le .docx (Word le régénère). Se fond dans la dernière
    // action d'annulation. À appeler après un chargement, une insertion ou un changement de style.
    void refreshNumbering();

    void load(const docx::Document &model);

    // Insère `blocks` sur de nouvelles lignes situées après le bloc du curseur (après le tableau si le
    // curseur est dans un tableau). `blockImages` : images référencées par les blocs (Run::image).
    // Renvoie le curseur placé à la fin du contenu inséré. Une seule action d'annulation.
    QTextCursor insertBlocks(QTextCursor cursor, const std::vector<docx::Block> &blocks,
                             const std::vector<docx::ImageData> &blockImages);

    // Insère un bloc brut (ex. saut de page) après le bloc du curseur.
    QTextCursor insertRawBlock(QTextCursor cursor, const std::string &xml);

    docx::Document extract() const;

    // Mise en forme de base (police, taille, gras...) d'un style de paragraphe Word.
    QTextCharFormat baseCharFormat(const std::string &styleName) const;
    docx::StyleInfo styleFor(const std::string &styleName) const;

    // Style de paragraphe Word du bloc contenant le curseur ("" = normal).
    std::string paragraphStyleAt(const QTextCursor &cursor) const;
    // Applique un style de paragraphe Word au bloc du curseur.
    void applyParagraphStyle(QTextCursor cursor, const std::string &styleName);

private:
    friend class DocxRenderer;
    int addRaw(const std::string &xml);

    void walkNumbering(QTextFrame::iterator it, docx::NumberingCounter &counter, std::vector<std::pair<int, QString>> &updates) const;
    docx::Block extractBlock(const QTextBlock &block, QHash<QTextList *, int> &groups) const;
    docx::Block extractTable(QTextTable *table, QHash<QTextList *, int> &groups) const;
    docx::RunFormat runFormatFrom(const QTextCharFormat &cf, const QTextCharFormat &base) const;
    QTextCharFormat charFormatFor(const docx::RunFormat &f, const QTextCharFormat &base) const;

    QTextDocument *m_doc;
    DocxImageStore *m_images;
    const docx::StyleSheet *m_styles = nullptr;
    const docx::Numbering *m_numbering = nullptr;
    std::vector<std::string> m_raw;   // XML des blocs bruts (index = propriété du bloc)
};

#endif // DOCXCONVERT_H
