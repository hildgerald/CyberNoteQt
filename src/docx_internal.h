#ifndef DOCX_INTERNAL_H
#define DOCX_INTERNAL_H

#include <string>
#include "docx_model.h"
#include "docx_xml.h"

namespace docx {
namespace detail {

inline std::string slice(const std::string &src, const XmlNode &n) { return src.substr(n.srcBegin, n.srcEnd - n.srcBegin); }

// <w:b/> -> 1 ; val="0"/"false"/"off"/"none" -> 0
int triState(const XmlNode &n);
// police explicite d'un <w:rFonts> ("" si définie par un thème)
std::string fontOf(const XmlNode &rFonts);

std::string builtinStylesXml();     // <w:styles> complet de CyberNoteQt (docDefaults, Normal, titres, code, citation...)
extern const char *const kNs;       // espaces de noms de la racine <w:document> d'un rapport vide

// Ordre des enfants de <w:pPr> / <w:rPr> imposé par le schéma OOXML (Word y est sensible).
int pprOrderIndex(const std::string &name);
int rprOrderIndex(const std::string &name);

} // namespace detail
} // namespace docx

#endif // DOCX_INTERNAL_H
