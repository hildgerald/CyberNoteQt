#include "docx_internal.h"

#include <algorithm>
#include <cstdint>

namespace docx {
namespace {

const char *const kRelNs = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
const char *const kWpNs = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
const char *const kANs = "http://schemas.openxmlformats.org/drawingml/2006/main";
const char *const kPicNs = "http://schemas.openxmlformats.org/drawingml/2006/picture";

const char *const kUtf8Replacement = "\xEF\xBF\xBD";

// Texte sûr pour du XML 1.0 : UTF-8 invalide -> U+FFFD, caractères de contrôle supprimés, & < > " échappés.
std::string xmlText(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: if (c >= 0x20 && c != 0x7F) out += static_cast<char>(c);
            }
            continue;
        }
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { out += kUtf8Replacement; ++i; continue; }
        bool ok = (i + static_cast<size_t>(len) <= n);
        for (int k = 1; ok && k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) ok = false; else cp = (cp << 6) | (cc & 0x3F);
        }
        if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
        if (!ok) { out += kUtf8Replacement; ++i; continue; }
        if (cp == 0xFFFE || cp == 0xFFFF) { i += static_cast<size_t>(len); continue; }
        out.append(s, i, static_cast<size_t>(len));
        i += static_cast<size_t>(len);
    }
    return out;
}

std::string xmlAttr(const std::string &s) { return xmlText(s); }

ZipEntry *findEntryMut(std::vector<ZipEntry> &entries, const std::string &name)
{
    for (ZipEntry &e : entries) if (e.name == name) return &e;
    return nullptr;
}

int maxNumericAfter(const std::string &xml, const std::string &needle)
{
    int best = 0;
    size_t p = 0;
    while ((p = xml.find(needle, p)) != std::string::npos) {
        p += needle.size();
        int v = 0;
        bool any = false;
        while (p < xml.size() && xml[p] >= '0' && xml[p] <= '9') { v = v * 10 + (xml[p] - '0'); ++p; any = true; if (v > 100000000) break; }
        if (any) best = std::max(best, v);
    }
    return best;
}

class Emitter
{
public:
    Emitter(const Package &pkg, const Document &doc) : m_pkg(pkg), m_doc(doc)
    {
        const ZipEntry *rels = nullptr;
        for (const ZipEntry &e : pkg.entries) if (e.name == pkg.relsPath) rels = &e;
        const std::string relsXml = rels ? rels->data : std::string();
        m_nextRid = std::max(10, maxNumericAfter(relsXml, "Id=\"rId") + 1);
        m_nextDocPr = maxNumericAfter(pkg.originalDocXml, "wp:docPr id=\"") + 1;
        if (m_nextDocPr < 1) m_nextDocPr = 1;

        int maxMedia = 0;
        for (const ZipEntry &e : pkg.entries) maxMedia = std::max(maxMedia, maxNumericAfter(e.name, "word/media/cn_image"));
        m_mediaCounter = maxMedia;

        const ZipEntry *num = nullptr;
        for (const ZipEntry &e : pkg.entries) if (e.name == pkg.numberingPath) num = &e;
        if (num) {
            m_numberingXml = num->data;
            m_nextAbstract = maxNumericAfter(num->data, "w:abstractNumId=\"") + 1;
            m_nextNum = maxNumericAfter(num->data, "w:numId=\"") + 1;
        }
    }

    std::string body() { return blocks(m_doc.blocks); }

    // Émet une suite de blocs ; une cellule ou le corps ne doit pas se terminer par un tableau.
    std::string blocks(const std::vector<Block> &list)
    {
        std::string out;
        bool prevTable = false;
        for (const Block &b : list) {
            if (b.kind == Block::Para) { out += paragraph(b.para); prevTable = false; }
            else if (b.kind == Block::Raw) { out += b.raw; prevTable = false; }
            else {
                if (prevTable) out += "<w:p/>";   // deux tableaux ne doivent pas se toucher
                out += table(b.table);
                prevTable = true;
            }
        }
        if (prevTable) out += "<w:p/>";
        return out;
    }

    // --- parties modifiées ---
    std::vector<std::string> newRelationships;           // XML <Relationship .../>
    std::vector<ZipEntry> newMedia;
    bool needPng = false, needJpeg = false;
    std::set<std::string> usedStyleIds;

    bool numberingChanged() const { return m_bulletNum != 0 || !m_newAbstracts.empty() || !m_newNums.empty(); }
    std::string finalNumbering(bool &created) const
    {
        created = m_numberingXml.empty();
        std::string xml = m_numberingXml;
        if (created)
            xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"></w:numbering>";
        std::string abs, nums;
        for (const std::string &a : m_newAbstracts) abs += a;
        for (const std::string &n : m_newNums) nums += n;

        size_t posAbs = xml.find("<w:num ");
        if (posAbs == std::string::npos) posAbs = xml.find("<w:numIdMacAtCleanup");
        if (posAbs == std::string::npos) posAbs = xml.rfind("</w:numbering>");
        if (posAbs == std::string::npos) return xml;
        xml.insert(posAbs, abs);

        size_t posNum = xml.find("<w:numIdMacAtCleanup");
        if (posNum == std::string::npos) posNum = xml.rfind("</w:numbering>");
        xml.insert(posNum, nums);
        return xml;
    }

private:
    std::string styleId(const std::string &name)
    {
        if (name.empty()) return std::string();
        const StyleInfo *s = m_pkg.styles.byName(name);
        if (!s) return std::string();
        if (s->builtin) usedStyleIds.insert(s->id);
        return s->id;
    }

    std::string nextRid() { return "rId" + std::to_string(m_nextRid++); }

    // --- numérotation ---
    static std::string abstractXml(int absId, bool bullets)
    {
        std::string s = "<w:abstractNum w:abstractNumId=\"" + std::to_string(absId) + "\"><w:multiLevelType w:val=\"hybridMultilevel\"/>";
        static const char *const glyphs[] = {"\xE2\x80\xA2", "\xE2\x80\x93", "\xE2\x96\xAA"};
        static const char *const fmts[] = {"decimal", "lowerLetter", "lowerRoman"};
        for (int l = 0; l < 9; ++l) {
            s += "<w:lvl w:ilvl=\"" + std::to_string(l) + "\"><w:start w:val=\"1\"/><w:numFmt w:val=\"" + (bullets ? "bullet" : fmts[l % 3]) + "\"/>";
            s += "<w:lvlText w:val=\"" + std::string(bullets ? glyphs[l % 3] : ("%" + std::to_string(l + 1) + ".").c_str()) + "\"/>";
            s += "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"" + std::to_string(720 * (l + 1)) + "\" w:hanging=\"360\"/></w:pPr>";
            if (bullets) s += "<w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:hint=\"default\"/></w:rPr>";
            s += "</w:lvl>";
        }
        return s + "</w:abstractNum>";
    }

    int bulletNumId()
    {
        if (!m_bulletNum) {
            const int abs = m_nextAbstract++;
            m_newAbstracts.push_back(abstractXml(abs, true));
            m_bulletNum = m_nextNum++;
            m_newNums.push_back("<w:num w:numId=\"" + std::to_string(m_bulletNum) + "\"><w:abstractNumId w:val=\"" + std::to_string(abs) + "\"/></w:num>");
        }
        return m_bulletNum;
    }

    int orderedNumId(int group)
    {
        if (!m_orderedAbstract) {
            m_orderedAbstract = m_nextAbstract++;
            m_newAbstracts.push_back(abstractXml(m_orderedAbstract, false));
        }
        auto it = m_orderedByGroup.find(group);
        if (it != m_orderedByGroup.end()) return it->second;
        const int id = m_nextNum++;
        std::string n = "<w:num w:numId=\"" + std::to_string(id) + "\"><w:abstractNumId w:val=\"" + std::to_string(m_orderedAbstract) + "\"/>";
        for (int l = 0; l < 9; ++l)
            n += "<w:lvlOverride w:ilvl=\"" + std::to_string(l) + "\"><w:startOverride w:val=\"1\"/></w:lvlOverride>";
        n += "</w:num>";
        m_newNums.push_back(n);
        m_orderedByGroup[group] = id;
        return id;
    }

    // --- assemblage ordonné ---
    static std::string assemble(std::vector<XmlChild> items, int (*orderFn)(const std::string &))
    {
        std::stable_sort(items.begin(), items.end(), [orderFn](const XmlChild &a, const XmlChild &b) {
            return orderFn(a.first) < orderFn(b.first);
        });
        std::string s;
        for (const XmlChild &c : items) s += c.second;
        return s;
    }

    // --- images ---
    std::string imageRid(int index)
    {
        auto it = m_imageRid.find(index);
        if (it != m_imageRid.end()) return it->second;
        const ImageData &img = m_doc.images[static_cast<size_t>(index)];
        std::string rid;
        if (!img.origRid.empty()) {
            rid = img.origRid;
        } else {
            ++m_mediaCounter;
            const std::string name = "cn_image" + std::to_string(m_mediaCounter) + (img.isJpeg ? ".jpeg" : ".png");
            ZipEntry e;
            e.name = (m_pkg.docPath.rfind('/') == std::string::npos ? std::string() : m_pkg.docPath.substr(0, m_pkg.docPath.rfind('/') + 1)) + "media/" + name;
            e.data = img.bytes;
            newMedia.push_back(std::move(e));
            rid = nextRid();
            newRelationships.push_back("<Relationship Id=\"" + rid + "\" Type=\"" + kRelNs + "/image\" Target=\"media/" + name + "\"/>");
            (img.isJpeg ? needJpeg : needPng) = true;
        }
        m_imageRid[index] = rid;
        return rid;
    }

    std::string image(const Run &r)
    {
        if (r.image < 0 || r.image >= static_cast<int>(m_doc.images.size()))
            return "<w:r><w:rPr><w:i/><w:color w:val=\"C00000\"/></w:rPr><w:t>[image]</w:t></w:r>";
        const ImageData &img = m_doc.images[static_cast<size_t>(r.image)];
        long long cx = r.cx, cy = r.cy;
        if (cx <= 0 || cy <= 0) { cx = static_cast<long long>(img.wPx) * 9525; cy = static_cast<long long>(img.hPx) * 9525; }
        const std::string rid = imageRid(r.image);
        const int id = m_nextDocPr++;
        const std::string sid = std::to_string(id), scx = std::to_string(cx), scy = std::to_string(cy);
        std::string x = "<w:r><w:drawing xmlns:wp=\"" + std::string(kWpNs) + "\" xmlns:a=\"" + kANs + "\" xmlns:pic=\"" + kPicNs
                        + "\" xmlns:r=\"" + kRelNs + "\"><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">";
        x += "<wp:extent cx=\"" + scx + "\" cy=\"" + scy + "\"/>";
        x += "<wp:docPr id=\"" + sid + "\" name=\"Image " + sid + "\" descr=\"" + xmlAttr(r.alt) + "\"/>";
        x += "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>";
        x += "<a:graphic><a:graphicData uri=\"" + std::string(kPicNs) + "\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"" + sid + "\" name=\"Image " + sid + "\"/><pic:cNvPicPr/></pic:nvPicPr>";
        x += "<pic:blipFill><a:blip r:embed=\"" + rid + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>";
        x += "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + scx + "\" cy=\"" + scy + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>";
        x += "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>";
        return x;
    }

    // --- runs ---
    std::string run(const Run &r)
    {
        if (r.kind == Run::Image) return image(r);

        const RunFormat &f = r.fmt;
        std::vector<XmlChild> rp;
        // rFonts d'origine conservé tant que la police n'a pas changé ; sinon remplacé par une police explicite
        bool keepFontsExtra = true;
        if (!f.font.empty()) {
            std::string orig;
            for (const XmlChild &e : f.extra) {
                if (e.first != "w:rFonts") continue;
                const size_t p = e.second.find("w:ascii=\"");
                if (p != std::string::npos) { const size_t b = p + 9, q = e.second.find('"', b); if (q != std::string::npos) orig = e.second.substr(b, q - b); }
                else {
                    const size_t h = e.second.find("w:hAnsi=\"");
                    if (h != std::string::npos) { const size_t b = h + 10, q = e.second.find('"', b); if (q != std::string::npos) orig = e.second.substr(b, q - b); }
                }
            }
            if (orig != xmlAttr(f.font) && orig != f.font) {
                keepFontsExtra = false;
                const std::string e = xmlAttr(f.font);
                rp.emplace_back("w:rFonts", "<w:rFonts w:ascii=\"" + e + "\" w:hAnsi=\"" + e + "\" w:eastAsia=\"" + e + "\" w:cs=\"" + e + "\"/>");
            }
        }
        if (f.bold >= 0) rp.emplace_back("w:b", f.bold ? "<w:b/>" : "<w:b w:val=\"0\"/>");
        if (f.italic >= 0) rp.emplace_back("w:i", f.italic ? "<w:i/>" : "<w:i w:val=\"0\"/>");
        if (f.strike >= 0) rp.emplace_back("w:strike", f.strike ? "<w:strike/>" : "<w:strike w:val=\"0\"/>");
        std::string color = f.color;
        int underline = f.underline;
        if (!f.link.empty()) {
            // lien créé dans l'éditeur : bleu souligné. Un lien du gabarit garde son style de caractère (rStyle).
            bool hasCharStyle = false;
            for (const XmlChild &e : f.extra) if (e.first == "w:rStyle") hasCharStyle = true;
            if (!hasCharStyle) { if (color.empty()) color = "0563C1"; if (underline < 0) underline = 1; }
        }
        if (!color.empty()) rp.emplace_back("w:color", "<w:color w:val=\"" + xmlAttr(color) + "\"/>");
        if (f.sizeHalfPt > 0) {
            const std::string sz = std::to_string(f.sizeHalfPt);
            rp.emplace_back("w:sz", "<w:sz w:val=\"" + sz + "\"/>");
            rp.emplace_back("w:szCs", "<w:szCs w:val=\"" + sz + "\"/>");
        }
        if (!f.background.empty()) {
            const std::string hl = highlightNameForHex(f.background);
            if (!hl.empty()) rp.emplace_back("w:highlight", "<w:highlight w:val=\"" + hl + "\"/>");
            else rp.emplace_back("w:shd", "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + xmlAttr(f.background) + "\"/>");
        }
        if (underline >= 0) rp.emplace_back("w:u", underline ? "<w:u w:val=\"single\"/>" : "<w:u w:val=\"none\"/>");
        for (const XmlChild &e : f.extra) {
            if (f.sizeHalfPt > 0 && e.first == "w:szCs") continue;
            if (!keepFontsExtra && e.first == "w:rFonts") continue;
            rp.push_back(e);
        }

        std::string content, cur;
        auto flush = [&] { if (!cur.empty()) { content += "<w:t xml:space=\"preserve\">" + xmlText(cur) + "</w:t>"; cur.clear(); } };
        for (char ch : r.text) {
            if (ch == '\t') { flush(); content += "<w:tab/>"; }
            else if (ch == '\n') { flush(); content += "<w:br/>"; }
            else cur += ch;
        }
        flush();
        if (content.empty()) return std::string();

        std::string x = "<w:r>";
        if (!rp.empty()) x += "<w:rPr>" + assemble(rp, detail::rprOrderIndex) + "</w:rPr>";
        x += content + "</w:r>";

        if (!f.link.empty()) {
            auto it = m_linkRid.find(f.link);
            if (it == m_linkRid.end()) {
                const std::string rid = nextRid();
                newRelationships.push_back("<Relationship Id=\"" + rid + "\" Type=\"" + kRelNs + "/hyperlink\" Target=\"" + xmlAttr(f.link) + "\" TargetMode=\"External\"/>");
                it = m_linkRid.emplace(f.link, rid).first;
            }
            x = "<w:hyperlink xmlns:r=\"" + std::string(kRelNs) + "\" r:id=\"" + it->second + "\" w:history=\"1\">" + x + "</w:hyperlink>";
        }
        return x;
    }

    // --- paragraphes ---
    std::string paragraph(const Paragraph &p)
    {
        std::vector<XmlChild> pp;
        const std::string sid = styleId(p.style);
        if (!sid.empty()) pp.emplace_back("w:pStyle", "<w:pStyle w:val=\"" + xmlAttr(sid) + "\"/>");
        if (p.list > 0) {
            int numId = p.numId;
            if (numId <= 0) numId = (p.list == 1) ? bulletNumId() : orderedNumId(p.listGroup);
            pp.emplace_back("w:numPr", "<w:numPr><w:ilvl w:val=\"" + std::to_string(std::max(0, std::min(8, p.listLevel)))
                                       + "\"/><w:numId w:val=\"" + std::to_string(numId) + "\"/></w:numPr>");
        }
        if (p.align) {
            const char *v = p.align == 'c' ? "center" : p.align == 'r' ? "right" : p.align == 'j' ? "both" : "left";
            pp.emplace_back("w:jc", std::string("<w:jc w:val=\"") + v + "\"/>");
        }
        for (const XmlChild &e : p.pprExtra) {
            if (p.list > 0 && e.first == "w:numPr") continue;
            if (p.align && e.first == "w:jc") continue;
            pp.push_back(e);
        }

        std::string x = "<w:p>";
        if (!pp.empty()) x += "<w:pPr>" + assemble(pp, detail::pprOrderIndex) + "</w:pPr>";
        x += p.prefixXml;
        for (const Run &r : p.runs) x += run(r);
        x += p.suffixXml;
        return x + "</w:p>";
    }

    // --- tableaux ---
    std::string table(const Table &t)
    {
        size_t ncols = 0;
        for (const TableRow &r : t.rows) {
            size_t n = 0;
            for (const TableCell &c : r.cells) if (!c.padding) ++n;
            ncols = std::max(ncols, n);
        }
        if (ncols == 0) return std::string();

        const bool keepRaw = t.grid.size() == ncols;   // grille cohérente : largeurs et propriétés de cellules conservées
        const int colW = 9072 / static_cast<int>(ncols);
        std::vector<int> grid(ncols, colW);
        if (keepRaw) grid = t.grid;

        std::string x = "<w:tbl>";
        if (!t.tblPr.empty()) x += t.tblPr;
        else {
            std::string gridId = styleId("Table Grid");
            x += "<w:tblPr>" + (gridId.empty() ? std::string() : "<w:tblStyle w:val=\"" + xmlAttr(gridId) + "\"/>")
                 + "<w:tblW w:w=\"" + std::to_string(colW * static_cast<int>(ncols)) + "\" w:type=\"dxa\"/><w:tblLayout w:type=\"fixed\"/></w:tblPr>";
        }
        x += "<w:tblGrid>";
        for (int w : grid) x += "<w:gridCol w:w=\"" + std::to_string(w) + "\"/>";
        x += "</w:tblGrid>";

        for (const TableRow &r : t.rows) {
            x += "<w:tr>";
            x += r.trPr.empty() ? std::string("<w:trPr><w:cantSplit/></w:trPr>") : r.trPr;
            size_t col = 0;
            for (const TableCell &cell : r.cells) {
                if (cell.padding) continue;   // case ajoutée pour l'affichage : la ligne d'origine n'en avait pas
                x += "<w:tc>";
                if (keepRaw && !cell.tcPr.empty()) x += cell.tcPr;
                else {
                    std::string shd;   // l'ombrage de la cellule est conservé même si la largeur est recalculée
                    const size_t sp = cell.tcPr.find("<w:shd");
                    const size_t se = sp == std::string::npos ? std::string::npos : cell.tcPr.find("/>", sp);
                    if (se != std::string::npos) shd = cell.tcPr.substr(sp, se + 2 - sp);
                    x += "<w:tcPr><w:tcW w:w=\"" + std::to_string(col < grid.size() ? grid[col] : colW) + "\" w:type=\"dxa\"/>" + shd + "</w:tcPr>";
                }
                std::string inner = blocks(cell.blocks);
                if (cell.blocks.empty() || cell.blocks.back().kind == Block::Tbl) inner += "<w:p/>";   // une cellule finit par un paragraphe
                x += inner + "</w:tc>";
                ++col;
            }
            x += "</w:tr>";
        }
        return x + "</w:tbl>";
    }

    const Package &m_pkg;
    const Document &m_doc;
    int m_nextRid = 10, m_nextDocPr = 1, m_mediaCounter = 0;
    std::map<int, std::string> m_imageRid;
    std::map<std::string, std::string> m_linkRid;

    std::string m_numberingXml;
    int m_nextAbstract = 0, m_nextNum = 1;
    int m_bulletNum = 0, m_orderedAbstract = 0;
    std::map<int, int> m_orderedByGroup;
    std::vector<std::string> m_newAbstracts, m_newNums;
};

} // namespace

bool saveDocx(const Package &pkg, const Document &doc, std::string &bytes, std::string &error)
{
    error.clear();
    bytes.clear();

    Emitter em(pkg, doc);
    const std::string body = em.body();

    std::vector<ZipEntry> entries = pkg.entries;

    // document.xml
    ZipEntry *docEntry = findEntryMut(entries, pkg.docPath);
    if (!docEntry) { error = "document part missing"; return false; }
    docEntry->data = pkg.docPrefix + body + pkg.sectPr + pkg.docSuffix;

    const std::string dir = (pkg.docPath.rfind('/') == std::string::npos) ? std::string() : pkg.docPath.substr(0, pkg.docPath.rfind('/') + 1);

    // numérotation (créée ou complétée)
    if (em.numberingChanged()) {
        bool created = false;
        const std::string xml = em.finalNumbering(created);
        if (ZipEntry *ne = findEntryMut(entries, pkg.numberingPath)) ne->data = xml;
        else entries.push_back({pkg.numberingPath, xml});
        if (created) {
            std::string target = pkg.numberingPath;
            if (target.compare(0, dir.size(), dir) == 0) target = target.substr(dir.size());
            em.newRelationships.push_back(std::string("<Relationship Id=\"rId9\" Type=\"") + kRelNs + "/numbering\" Target=\"" + target + "\"/>");
            if (ZipEntry *ct = findEntryMut(entries, "[Content_Types].xml")) {
                const size_t p = ct->data.rfind("</Types>");
                if (p != std::string::npos)
                    ct->data.insert(p, "<Override PartName=\"/" + pkg.numberingPath + "\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
            }
        }
    }

    if (!em.newRelationships.empty()) {
        ZipEntry *rels = findEntryMut(entries, pkg.relsPath);   // recherché ici : `entries` a pu être réalloué plus haut
        std::string add;
        for (const std::string &r : em.newRelationships) add += r;
        if (rels) {
            const size_t p = rels->data.rfind("</Relationships>");
            if (p == std::string::npos) { error = "malformed document relationships"; return false; }
            rels->data.insert(p, add);
        } else {
            entries.push_back({pkg.relsPath, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" + add + "</Relationships>"});
        }
    }

    // styles intégrés réellement utilisés et absents du gabarit
    if (!em.usedStyleIds.empty()) {
        if (ZipEntry *se = findEntryMut(entries, pkg.stylesPath)) {
            std::string add;
            for (const std::string &id : em.usedStyleIds) {
                const StyleInfo *s = pkg.styles.byId(id);
                if (!s || s->xml.empty()) continue;
                std::string xml = s->xml;
                if (!pkg.styles.byId("Normal") && !pkg.styles.defParaStyleId.empty()) {
                    const std::string from = "<w:basedOn w:val=\"Normal\"/>";
                    const size_t p = xml.find(from);
                    if (p != std::string::npos) xml.replace(p, from.size(), "<w:basedOn w:val=\"" + pkg.styles.defParaStyleId + "\"/>");
                }
                add += xml;
            }
            const size_t p = se->data.rfind("</w:styles>");
            if (p != std::string::npos) se->data.insert(p, add);
        }
    }

    // images ajoutées
    for (ZipEntry &m : em.newMedia) entries.push_back(std::move(m));

    // Un sommaire (champ TOC) enregistré n'est pas recalculé : on demande à Word de mettre les champs à jour à
    // l'ouverture (il pose alors une question) pour que titres, numéros et pages du sommaire soient à jour.
    bool hasToc = false;
    for (const Block &b : doc.blocks) if (b.kind == Block::Raw && rawHasToc(b.raw)) hasToc = true;
    if (hasToc) {
        const std::string settingsPath = dir + "settings.xml";
        if (ZipEntry *se = findEntryMut(entries, settingsPath)) {
            if (se->data.find("w:updateFields") == std::string::npos) {
                static const char *const after[] = {"<w:hdrShapeDefaults", "<w:footnotePr", "<w:endnotePr", "<w:compat", "<w:docVars",
                    "<w:rsids", "<m:mathPr", "<w:attachedSchema", "<w:themeFontLang", "<w:clrSchemeMapping", "<w:doNotIncludeSubdocsInStats",
                    "<w:doNotAutoCompressPictures", "<w:forceUpgrade", "<w:captions", "<w:readModeInkLockDown", "<w:smartTagType",
                    "<w:schemaLibrary", "<w:shapeDefaults", "<w:doNotEmbedSmartTags", "<w:decimalSymbol", "<w:listSeparator", "</w:settings>"};
                size_t pos = std::string::npos;
                for (const char *tag : after) {
                    const size_t p = se->data.find(tag);
                    if (p != std::string::npos && (pos == std::string::npos || p < pos)) pos = p;
                }
                if (pos != std::string::npos) se->data.insert(pos, "<w:updateFields w:val=\"true\"/>");
            }
        }
    }

    // types de contenu
    if (ZipEntry *ct = findEntryMut(entries, "[Content_Types].xml")) {
        auto ensureDefault = [&](const char *ext, const char *type) {
            const std::string key = std::string("Extension=\"") + ext + "\"";
            std::string low = ct->data;
            for (char &c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (low.find(key) != std::string::npos) return;
            const size_t t = ct->data.find("<Types");
            const size_t gt = (t == std::string::npos) ? std::string::npos : ct->data.find('>', t);
            if (gt != std::string::npos)
                ct->data.insert(gt + 1, std::string("<Default Extension=\"") + ext + "\" ContentType=\"" + type + "\"/>");
        };
        if (em.needPng) ensureDefault("png", "image/png");
        if (em.needJpeg) ensureDefault("jpeg", "image/jpeg");

        // un modèle (.dotx) enregistré en .docx doit déclarer un document ordinaire
        const std::string tpl = "wordprocessingml.template.main+xml";
        size_t p = ct->data.find(tpl);
        if (p != std::string::npos) ct->data.replace(p, tpl.size(), "wordprocessingml.document.main+xml");
    }

    bytes = writeZip(entries);
    return true;
}

} // namespace docx
