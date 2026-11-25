#ifndef DOCX_MODEL_H
#define DOCX_MODEL_H

// ~ nouveau (v2.2.0) : modèle de document Word (C++17 standard, sans Qt) + lecture / écriture d'un .docx
// EN CONSERVANT LE GABARIT : toutes les parties du paquet d'origine (styles, thème, en-têtes, pieds de page,
// numérotation, polices...) sont gardées telles quelles ; seul le corps du document (word/document.xml) est
// régénéré à partir du modèle. Ce que l'éditeur ne sait pas représenter (table des matières, champs, sauts de
// page/section, formes, tableaux fusionnés...) est conservé comme blocs "bruts" (XML recopié à l'identique).

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "docx_zip.h"

namespace docx {

using XmlChild = std::pair<std::string, std::string>;   // (nom d'élément "w:spacing", XML brut de l'élément)

// ----------------------------------------------------------------------------- contenu
struct RunFormat {
    int bold = -1, italic = -1, underline = -1, strike = -1;   // -1 hérité du style, 0 désactivé, 1 activé
    std::string font;          // police explicite ("" = héritée)
    int sizeHalfPt = 0;        // taille en demi-points (0 = héritée)
    std::string color;         // "RRGGBB" ("" = héritée)
    std::string background;    // "RRGGBB" : surlignage ou ombrage ("" = aucun)
    std::string link;          // URL externe (lien hypertexte)
    std::vector<XmlChild> extra;   // autres propriétés de run (rStyle, vertAlign, caps...) conservées telles quelles

    bool operator==(const RunFormat &o) const
    {
        return bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike
            && font == o.font && sizeHalfPt == o.sizeHalfPt && color == o.color && background == o.background
            && link == o.link && extra == o.extra;
    }
};

struct Run {
    enum Kind { Text, Image } kind = Text;
    std::string text;          // UTF-8 ; '\t' = tabulation, '\n' = saut de ligne dans le paragraphe
    RunFormat fmt;
    int image = -1;            // index dans Document::images (ou dans la liste fournie avec les blocs)
    long long cx = 0, cy = 0;  // taille d'affichage de l'image en EMU
    std::string alt;
};

struct Paragraph {
    std::string style;         // NOM du style Word ("heading 1", "Quote"...) ; "" = style par défaut
    char align = 0;            // 0 hérité, 'l' 'c' 'r' 'j'
    int list = 0;              // 0 aucune, 1 puces, 2 numérotation
    int listLevel = 0;         // 0..8
    int numId = 0;             // numId d'origine (gabarit) ; 0 = à attribuer à l'enregistrement
    int listGroup = 0;         // liste logique (la numérotation repart à 1 pour chaque groupe)
    std::vector<XmlChild> pprExtra;   // autres propriétés de paragraphe (espacement, retraits, bordures...)
    std::string prefixXml;     // XML brut placé avant les runs (signets)
    std::string suffixXml;     // XML brut placé après les runs
    std::vector<Run> runs;
};

struct Block;
struct TableCell {
    std::vector<Block> blocks; // paragraphes, tableaux imbriqués, blocs bruts
    std::string tcPr;          // <w:tcPr>..</w:tcPr> d'origine ("" = généré)
    bool padding = false;      // case ajoutée pour rendre le tableau rectangulaire (la ligne d'origine était plus courte)
};
struct TableRow {
    std::string trPr;          // <w:trPr>..</w:trPr> d'origine
    std::vector<TableCell> cells;
};
struct Table {
    std::string tblPr;         // <w:tblPr>..</w:tblPr> d'origine ("" = généré : grille simple)
    std::vector<int> grid;     // largeurs de colonnes en twips (vide = réparties à l'enregistrement)
    std::vector<TableRow> rows;
};

struct Block {
    enum Kind { Para, Tbl, Raw } kind = Para;
    Paragraph para;
    Table table;
    std::string raw;           // XML brut d'un élément de corps non pris en charge
};

struct ImageData {
    std::string bytes;         // PNG ou JPEG
    bool isJpeg = false;
    int wPx = 0, hPx = 0;
    std::string origRid;       // relation d'origine dans le gabarit ("" = nouvelle image)
};

struct Document {
    std::vector<Block> blocks;
    std::vector<ImageData> images;
};

// ----------------------------------------------------------------------------- styles
struct StyleInfo {
    std::string id, name, type, basedOn;
    bool isDefault = false;
    std::string font;
    int sizeHalfPt = 0;
    int bold = -1, italic = -1, underline = -1, strike = -1;
    std::string color;
    char align = 0;
    int spaceBefore = -1, spaceAfter = -1;   // twips
    int numId = -1, numLevel = 0;            // numérotation liée au style (titres numérotés) ; -1 = aucune, 0 = désactivée
    std::string shading;                     // ~ v2.6.2 : "RRGGBB" ombrage (fond) du paragraphe défini par le style ("" = aucun) ; ex. zones de code
    bool builtin = false;      // style ajouté par CyberNoteQt (absent du gabarit)
    std::string xml;           // définition XML (styles "builtin")
};

struct StyleSheet {
    std::vector<StyleInfo> styles;
    std::string defFont;       // police par défaut du document
    int defSizeHalfPt = 22;
    std::string defParaStyleId;

    const StyleInfo *byId(const std::string &id) const;
    const StyleInfo *byName(const std::string &name) const;      // insensible à la casse
    StyleInfo resolved(const StyleInfo &s) const;                 // fusionne la chaîne basedOn
    std::string idForName(const std::string &name) const;         // "" si inconnu
    // Ajoute les styles de CyberNoteQt (titres, code, citation, liste, texte de tableau, grille) absents.
    void addBuiltins();
};

// ----------------------------------------------------------------------------- numérotation
struct NumLevel {
    std::string fmt;           // decimal, lowerLetter, upperRoman, bullet...
    std::string text;          // "%1.%2"
    std::string suff = "tab";  // tab, space, nothing
    int start = 1;
};
struct Numbering {
    bool present = false;
    std::map<int, int> numToAbstract;
    std::map<int, std::vector<std::string>> abstractFormats;      // abstractNumId -> numFmt par niveau
    std::map<int, std::vector<NumLevel>> levels;                  // abstractNumId -> niveaux
    int kindFor(int numId, int level) const;                      // 1 puces, 2 numérotation
};

// Calcule les numéros de paragraphes comme Word ("1", "1.1", "a)"...), dans l'ordre du document.
class NumberingCounter
{
public:
    explicit NumberingCounter(const Numbering &numbering) : m_n(numbering) {}
    // Fait avancer le compteur de (numId, niveau) et renvoie le numéro à afficher, suivi de son séparateur
    // (tabulation, espace ou rien). Chaîne vide si ce numéro est inconnu.
    std::string next(int numId, int level);
private:
    const Numbering &m_n;
    std::map<int, std::vector<int>> m_counters;                   // numId -> compteur par niveau (-1 = pas démarré)
};

// ----------------------------------------------------------------------------- paquet
struct Package {
    std::vector<ZipEntry> entries;     // toutes les parties d'origine
    std::string docPath = "word/document.xml";
    std::string stylesPath = "word/styles.xml";
    std::string numberingPath = "word/numbering.xml";
    std::string relsPath = "word/_rels/document.xml.rels";
    std::string docPrefix;             // document.xml jusqu'à <w:body> inclus
    std::string sectPr;                // <w:sectPr> final du corps
    std::string docSuffix;             // après les éléments du corps
    std::string originalDocXml;        // pour retrouver les identifiants déjà utilisés
    StyleSheet styles;
    Numbering numbering;
};

bool loadDocx(const std::string &bytes, Package &pkg, Document &doc, std::string &error);
void blankDocx(Package &pkg, Document &doc);        // rapport vide (styles CyberNoteQt) quand aucun gabarit n'est fourni
bool saveDocx(const Package &pkg, const Document &doc, std::string &bytes, std::string &error);

// ----------------------------------------------------------------------------- aides
int headingLevelOfStyle(const std::string &styleName);       // "heading 2" -> 2, sinon 0
std::string headingStyleName(int level);                      // 2 -> "heading 2"
std::string describeRaw(const std::string &xml);              // libellé du bloc brut ("Saut de page"...)
std::string rawPreview(const std::string &xml, int maxLines = 400);   // texte lisible d'un bloc brut (table des matières...), une ligne par paragraphe
bool rawHasToc(const std::string &xml);                       // le bloc brut contient un champ de table des matières
std::string highlightColorHex(const std::string &wordName);   // "yellow" -> "FFFF00"
std::string highlightNameForHex(const std::string &hex);      // "FFFF00" -> "yellow", "" si pas un surlignage Word

// ----------------------------------------------------------------------------- Markdown -> blocs
using ImageLoader = std::function<bool(const std::string &path, ImageData &out)>;

// Convertit du Markdown (dialecte de CyberNoteQt) en blocs du modèle. Les images sont ajoutées à `images`
// (Run::image = index dans cette liste). `listGroupCounter` donne des groupes de liste uniques.
void markdownToBlocks(const std::string &markdown, const ImageLoader &loader, std::vector<Block> &blocks,
                      std::vector<ImageData> &images, int &listGroupCounter);

} // namespace docx

#endif // DOCX_MODEL_H
