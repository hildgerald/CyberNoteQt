#ifndef DOCX_XML_H
#define DOCX_XML_H

// ~ nouveau (v2.2.0) : petit analyseur XML (arbre DOM) pour lire les parties d'un .docx.
// Les noms gardent leur préfixe ("w:p"). Chaque élément mémorise sa position dans le texte source
// (srcBegin/srcEnd) pour pouvoir en recopier le XML tel quel (contenu Word non pris en charge).

#include <string>
#include <utility>
#include <vector>

namespace docx {

struct XmlNode {
    std::string name;      // "w:p" ; "#text" pour un nœud texte
    std::string text;      // texte (déjà décodé) pour un nœud "#text"
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XmlNode> children;

    size_t srcBegin = 0;   // offset du '<' d'ouverture
    size_t openEnd = 0;    // offset juste après le '>' de la balise ouvrante
    size_t closeBegin = 0; // offset du '<' de la balise fermante (= openEnd pour un élément vide)
    size_t srcEnd = 0;     // offset juste après le '>' final

    bool isText() const { return name == "#text"; }
    const std::string *attr(const std::string &n) const;
    std::string attrOr(const std::string &n, const std::string &def = std::string()) const;
    const XmlNode *child(const std::string &n) const;          // premier élément enfant de ce nom
    std::string textContent() const;                            // concaténation du texte des descendants
};

bool parseXml(const std::string &src, XmlNode &root, std::string &error);

// Décodage des entités (&amp; &#233; ...) / échappement pour valeur d'attribut ou texte.
std::string xmlDecode(const std::string &s);
std::string xmlEsc(const std::string &s);

} // namespace docx

#endif // DOCX_XML_H
