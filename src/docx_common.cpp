#include "docx_internal.h"

#include <algorithm>
#include <cctype>

namespace docx {

// ============================================================================
//  Aides
// ============================================================================
namespace {
std::string lower(std::string s) { for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

struct HlEntry { const char *name; const char *hex; };
const HlEntry kHighlights[] = {
    {"yellow", "FFFF00"}, {"green", "00FF00"}, {"cyan", "00FFFF"}, {"magenta", "FF00FF"}, {"blue", "0000FF"},
    {"red", "FF0000"}, {"darkBlue", "000080"}, {"darkCyan", "008080"}, {"darkGreen", "008000"},
    {"darkMagenta", "800080"}, {"darkRed", "800000"}, {"darkYellow", "808000"}, {"darkGray", "808080"},
    {"lightGray", "C0C0C0"}, {"black", "000000"}, {"white", "FFFFFF"},
};
} // namespace

std::string highlightColorHex(const std::string &wordName)
{
    for (const HlEntry &h : kHighlights)
        if (wordName == h.name) return h.hex;
    return std::string();
}

std::string highlightNameForHex(const std::string &hex)
{
    std::string u = hex;
    for (char &c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const HlEntry &h : kHighlights)
        if (u == h.hex) return h.name;
    return std::string();
}

int headingLevelOfStyle(const std::string &styleName)
{
    const std::string n = lower(styleName);
    if (n.size() == 9 && n.compare(0, 8, "heading ") == 0 && n[8] >= '1' && n[8] <= '6')
        return n[8] - '0';
    return 0;
}

std::string headingStyleName(int level) { return "heading " + std::to_string(std::max(1, std::min(6, level))); }

std::string describeRaw(const std::string &xml)
{
    auto has = [&](const char *s) { return xml.find(s) != std::string::npos; };
    if (has("w:type=\"page\"")) return "Saut de page";
    if (has("<w:sectPr")) return "Saut de section";
    if (has("TableOfContents") || has("Table of Contents") || has("TOC \\") || has("w:docPartGallery")) return "Table des mati\xC3\xA8res / bloc Word";
    if (has("<w:tbl>") || has("<w:tbl ")) return "Tableau complexe (conserv\xC3\xA9 tel quel)";
    if (has("<w:fldChar") || has("<w:fldSimple") || has("<w:instrText")) return "Champ Word (conserv\xC3\xA9 tel quel)";
    if (has("<w:drawing") || has("<w:pict") || has("<mc:AlternateContent")) return "Dessin / objet Word (conserv\xC3\xA9 tel quel)";
    if (has("<w:sdt")) return "Contr\xC3\xB4le de contenu Word (conserv\xC3\xA9 tel quel)";
    return "Contenu Word conserv\xC3\xA9 tel quel";
}

// ============================================================================
//  StyleSheet / Numbering
// ============================================================================
const StyleInfo *StyleSheet::byId(const std::string &id) const
{
    for (const StyleInfo &s : styles) if (s.id == id) return &s;
    return nullptr;
}

const StyleInfo *StyleSheet::byName(const std::string &name) const
{
    const std::string n = lower(name);
    for (const StyleInfo &s : styles) if (lower(s.name) == n) return &s;
    return nullptr;
}

std::string StyleSheet::idForName(const std::string &name) const
{
    const StyleInfo *s = byName(name);
    return s ? s->id : std::string();
}

StyleInfo StyleSheet::resolved(const StyleInfo &s) const
{
    StyleInfo r = s;
    std::set<std::string> seen;
    const StyleInfo *cur = &s;
    while (!cur->basedOn.empty() && seen.insert(cur->basedOn).second) {
        const StyleInfo *base = byId(cur->basedOn);
        if (!base) break;
        if (r.font.empty()) r.font = base->font;
        if (r.sizeHalfPt == 0) r.sizeHalfPt = base->sizeHalfPt;
        if (r.bold < 0) r.bold = base->bold;
        if (r.italic < 0) r.italic = base->italic;
        if (r.underline < 0) r.underline = base->underline;
        if (r.strike < 0) r.strike = base->strike;
        if (r.color.empty()) r.color = base->color;
        if (r.align == 0) r.align = base->align;
        if (r.spaceBefore < 0) r.spaceBefore = base->spaceBefore;
        if (r.spaceAfter < 0) r.spaceAfter = base->spaceAfter;
        if (r.numId < 0) { r.numId = base->numId; r.numLevel = base->numLevel; }
        cur = base;
    }
    return r;
}

int Numbering::kindFor(int numId, int level) const
{
    auto n = numToAbstract.find(numId);
    if (n == numToAbstract.end()) return 1;
    auto a = abstractFormats.find(n->second);
    if (a == abstractFormats.end() || level < 0 || level >= static_cast<int>(a->second.size())) return 1;
    const std::string &f = a->second[static_cast<size_t>(level)];
    return (f == "bullet" || f == "none" || f.empty()) ? 1 : 2;
}

// ----------------------------------------------------------------------------- numéros de paragraphes
namespace {

std::string toRoman(int n, bool upper)
{
    if (n <= 0 || n >= 4000) return std::to_string(n);
    static const struct { int v; const char *s; } t[] = {{1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},
        {50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}};
    std::string r;
    for (const auto &e : t) while (n >= e.v) { r += e.s; n -= e.v; }
    if (!upper) for (char &c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string toLetters(int n, bool upper)
{
    if (n <= 0) return std::to_string(n);
    std::string r;
    while (n > 0) { --n; r.insert(r.begin(), static_cast<char>((upper ? 'A' : 'a') + n % 26)); n /= 26; }
    return r;
}

std::string formatNumber(int n, const std::string &fmt)
{
    if (fmt == "decimalZero") return (n < 10 ? "0" : "") + std::to_string(n);
    if (fmt == "lowerLetter") return toLetters(n, false);
    if (fmt == "upperLetter") return toLetters(n, true);
    if (fmt == "lowerRoman") return toRoman(n, false);
    if (fmt == "upperRoman") return toRoman(n, true);
    if (fmt == "none") return std::string();
    return std::to_string(n);   // decimal et formats non gérés
}

} // namespace

std::string NumberingCounter::next(int numId, int level)
{
    auto na = m_n.numToAbstract.find(numId);
    if (na == m_n.numToAbstract.end()) return std::string();
    auto lv = m_n.levels.find(na->second);
    if (lv == m_n.levels.end() || level < 0 || level >= static_cast<int>(lv->second.size())) return std::string();
    const std::vector<NumLevel> &defs = lv->second;

    std::vector<int> &c = m_counters[numId];
    if (c.size() < defs.size()) c.resize(defs.size(), -1);

    // le compteur du niveau avance ; les niveaux plus profonds repartent à zéro
    c[static_cast<size_t>(level)] = (c[static_cast<size_t>(level)] < 0) ? defs[static_cast<size_t>(level)].start : c[static_cast<size_t>(level)] + 1;
    for (size_t j = static_cast<size_t>(level) + 1; j < c.size(); ++j) c[j] = -1;
    for (size_t j = 0; j < static_cast<size_t>(level); ++j) if (c[j] < 0) c[j] = defs[j].start;   // niveau parent jamais vu

    const NumLevel &d = defs[static_cast<size_t>(level)];
    if (d.fmt == "bullet") return "\xE2\x80\xA2" + std::string(d.suff == "space" ? " " : d.suff == "nothing" ? "" : "\t");

    std::string out;
    for (size_t i = 0; i < d.text.size(); ++i) {
        if (d.text[i] == '%' && i + 1 < d.text.size() && d.text[i + 1] >= '1' && d.text[i + 1] <= '9') {
            const size_t k = static_cast<size_t>(d.text[i + 1] - '1');
            out += (k < c.size() && c[k] >= 0 && k < defs.size()) ? formatNumber(c[k], defs[k].fmt) : std::string();
            ++i;
        } else {
            out += d.text[i];
        }
    }
    if (out.empty()) return out;
    return out + (d.suff == "space" ? " " : d.suff == "nothing" ? "" : "\t");
}

// ----------------------------------------------------------------------------- aperçu d'un bloc brut
bool rawHasToc(const std::string &xml)
{
    return xml.find("TOC \\") != std::string::npos || xml.find("w:docPartGallery w:val=\"Table of Contents\"") != std::string::npos
           || xml.find("Table of Contents") != std::string::npos;
}

namespace {
void collectText(const XmlNode &n, std::string &line, std::vector<std::string> &lines, int maxLines)
{
    if (static_cast<int>(lines.size()) >= maxLines) return;
    if (n.isText()) return;
    if (n.name == "w:t") { line += n.textContent(); return; }
    if (n.name == "w:tab") { line += '\t'; return; }
    if (n.name == "w:br" || n.name == "w:cr") { line += ' '; return; }
    if (n.name == "w:instrText" || n.name == "w:delText") return;
    if (n.name == "w:p") {
        std::string own;
        for (const XmlNode &c : n.children) collectText(c, own, lines, maxLines);
        lines.push_back(own);
        return;
    }
    for (const XmlNode &c : n.children) collectText(c, line, lines, maxLines);
}
} // namespace

std::string rawPreview(const std::string &xml, int maxLines)
{
    XmlNode root; std::string err;
    if (!parseXml(xml, root, err)) return std::string();
    std::vector<std::string> lines;
    std::string stray;
    collectText(root, stray, lines, maxLines);
    if (!stray.empty()) lines.push_back(stray);
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) { if (i) out += '\n'; out += lines[i]; }
    return out;
}

namespace detail {

int triState(const XmlNode &n)
{
    const std::string *v = n.attr("w:val");
    if (!v) return 1;
    return (*v == "0" || *v == "false" || *v == "off" || *v == "none") ? 0 : 1;
}

std::string fontOf(const XmlNode &rFonts)
{
    for (const char *a : {"w:ascii", "w:hAnsi", "w:cs", "w:eastAsia"})
        if (const std::string *v = rFonts.attr(a)) if (!v->empty()) return *v;
    if (const std::string *t = rFonts.attr("w:asciiTheme"))
        return lower(*t).find("major") == 0 ? "Calibri Light" : "Calibri";
    return std::string();
}

namespace {
const char *const kPprOrder[] = {"w:pStyle","w:keepNext","w:keepLines","w:pageBreakBefore","w:framePr","w:widowControl","w:numPr",
    "w:suppressLineNumbers","w:pBdr","w:shd","w:tabs","w:suppressAutoHyphens","w:kinsoku","w:wordWrap","w:overflowPunct",
    "w:topLinePunct","w:autoSpaceDE","w:autoSpaceDN","w:bidi","w:adjustRightInd","w:snapToGrid","w:spacing","w:ind",
    "w:contextualSpacing","w:mirrorIndents","w:suppressOverlap","w:jc","w:textDirection","w:textAlignment","w:textboxTightWrap",
    "w:outlineLvl","w:divId","w:cnfStyle","w:rPr","w:sectPr","w:pPrChange"};
const char *const kRprOrder[] = {"w:rStyle","w:rFonts","w:b","w:bCs","w:i","w:iCs","w:caps","w:smallCaps","w:strike","w:dstrike",
    "w:outline","w:shadow","w:emboss","w:imprint","w:noProof","w:snapToGrid","w:vanish","w:webHidden","w:color","w:spacing",
    "w:w","w:kern","w:position","w:sz","w:szCs","w:highlight","w:u","w:effect","w:bdr","w:shd","w:fitText","w:vertAlign",
    "w:rtl","w:cs","w:em","w:lang","w:eastAsianLayout","w:specVanish","w:oMath"};
template <size_t N> int indexIn(const char *const (&arr)[N], const std::string &name)
{
    for (size_t i = 0; i < N; ++i) if (name == arr[i]) return static_cast<int>(i);
    return static_cast<int>(N) + 1;   // inconnu : à la fin
}
} // namespace

int pprOrderIndex(const std::string &name) { return indexIn(kPprOrder, name); }
int rprOrderIndex(const std::string &name) { return indexIn(kRprOrder, name); }

const char *const kNs =
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"";

namespace {
std::string headingStyleXml(int level, const char *size, const char *color, bool borderBottom)
{
    std::string s = "<w:style w:type=\"paragraph\" w:styleId=\"Heading" + std::to_string(level) + "\"><w:name w:val=\"heading "
                    + std::to_string(level) + "\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:uiPriority w:val=\"9\"/><w:qFormat/>"
                    "<w:pPr><w:keepNext/><w:keepLines/>";
    if (borderBottom)
        s += "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"2\" w:color=\"8EAADB\"/></w:pBdr>";
    s += "<w:spacing w:before=\"" + std::string(level == 1 ? "360" : "240") + "\" w:after=\"120\"/><w:outlineLvl w:val=\""
         + std::to_string(level - 1) + "\"/></w:pPr><w:rPr><w:b/><w:bCs/><w:color w:val=\"" + color + "\"/><w:sz w:val=\""
         + size + "\"/><w:szCs w:val=\"" + size + "\"/></w:rPr></w:style>";
    return s;
}
} // namespace

std::string builtinStylesXml()
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    s += "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">";
    s += "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/>"
         "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"fr-FR\" w:eastAsia=\"en-US\" w:bidi=\"ar-SA\"/></w:rPr></w:rPrDefault>"
         "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>";
    s += "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>";
    s += "<w:style w:type=\"character\" w:default=\"1\" w:styleId=\"DefaultParagraphFont\"><w:name w:val=\"Default Paragraph Font\"/><w:uiPriority w:val=\"1\"/><w:semiHidden/></w:style>";
    s += "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/><w:uiPriority w:val=\"99\"/><w:semiHidden/>"
         "<w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/><w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"108\" w:type=\"dxa\"/>"
         "<w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>";
    s += headingStyleXml(1, "36", "1F3864", true);
    s += headingStyleXml(2, "30", "2F5496", false);
    s += headingStyleXml(3, "26", "2F5496", false);
    s += headingStyleXml(4, "24", "1F3864", false);
    s += headingStyleXml(5, "22", "1F3864", false);
    s += headingStyleXml(6, "22", "595959", false);
    s += "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"CodeBlock\"><w:name w:val=\"Code Block\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
         "<w:pPr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F2F2F2\"/><w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
         "<w:ind w:left=\"142\" w:right=\"142\"/></w:pPr>"
         "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/><w:sz w:val=\"18\"/><w:szCs w:val=\"18\"/></w:rPr></w:style>";
    s += "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
         "<w:pPr><w:pBdr><w:left w:val=\"single\" w:sz=\"18\" w:space=\"8\" w:color=\"A6A6A6\"/></w:pBdr><w:ind w:left=\"720\"/></w:pPr>"
         "<w:rPr><w:i/><w:iCs/><w:color w:val=\"595959\"/></w:rPr></w:style>";
    s += "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
         "<w:pPr><w:spacing w:after=\"40\"/></w:pPr></w:style>";
    s += "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"TableText\"><w:name w:val=\"Table Text\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>"
         "<w:pPr><w:spacing w:before=\"40\" w:after=\"40\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr><w:rPr><w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/></w:rPr></w:style>";
    s += "<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table Grid\"/><w:basedOn w:val=\"TableNormal\"/><w:uiPriority w:val=\"39\"/>"
         "<w:tblPr><w:tblBorders><w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/><w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/>"
         "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/><w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/>"
         "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/><w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"A6A6A6\"/></w:tblBorders></w:tblPr></w:style>";
    s += "</w:styles>";
    return s;
}

} // namespace detail
} // namespace docx
