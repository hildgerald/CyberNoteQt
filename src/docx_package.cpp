#include "docx_internal.h"

#include <algorithm>
#include <cctype>

namespace docx {
namespace {

using detail::slice;

bool endsWith(const std::string &s, const std::string &suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }
int toInt(const std::string &s, int def = 0)
{
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

struct Rel { std::string type, target; bool external = false; };

std::map<std::string, Rel> parseRels(const std::string &xml)
{
    std::map<std::string, Rel> rels;
    XmlNode root; std::string err;
    if (!parseXml(xml, root, err)) return rels;
    for (const XmlNode &c : root.children) {
        if (c.name != "Relationship") continue;
        Rel r;
        r.type = c.attrOr("Type");
        r.target = c.attrOr("Target");
        r.external = (c.attrOr("TargetMode") == "External");
        rels[c.attrOr("Id")] = r;
    }
    return rels;
}

const ZipEntry *findEntry(const std::vector<ZipEntry> &entries, const std::string &name)
{
    for (const ZipEntry &e : entries) if (e.name == name) return &e;
    return nullptr;
}

std::string normalizePath(const std::string &dir, const std::string &target)
{
    std::string full = (!target.empty() && target[0] == '/') ? target.substr(1) : dir + target;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= full.size()) {
        const size_t j = full.find('/', i);
        const std::string seg = full.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::string out;
    for (size_t k = 0; k < parts.size(); ++k) { if (k) out += '/'; out += parts[k]; }
    return out;
}

// --- dimensions d'une image PNG / JPEG ---
bool parseImage(const std::string &b, ImageData &img)
{
    auto u8 = [&](size_t p) { return static_cast<unsigned char>(b[p]); };
    if (b.size() > 24 && b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) {
        img.isJpeg = false;
        img.wPx = static_cast<int>((u8(16) << 24) | (u8(17) << 16) | (u8(18) << 8) | u8(19));
        img.hPx = static_cast<int>((u8(20) << 24) | (u8(21) << 16) | (u8(22) << 8) | u8(23));
        img.bytes = b;
        return img.wPx > 0 && img.hPx > 0;
    }
    if (b.size() > 4 && u8(0) == 0xFF && u8(1) == 0xD8) {
        size_t p = 2;
        while (p + 9 < b.size()) {
            if (u8(p) != 0xFF) { ++p; continue; }
            const unsigned char m = u8(p + 1);
            if (m == 0xFF) { ++p; continue; }
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
            const size_t len = (static_cast<size_t>(u8(p + 2)) << 8) | u8(p + 3);
            if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                img.hPx = static_cast<int>((u8(p + 5) << 8) | u8(p + 6));
                img.wPx = static_cast<int>((u8(p + 7) << 8) | u8(p + 8));
                img.isJpeg = true;
                img.bytes = b;
                return img.wPx > 0 && img.hPx > 0;
            }
            p += 2 + len;
        }
    }
    return false;
}

// --- styles ---
void parseRPrInto(const XmlNode &rPr, StyleInfo &s)
{
    for (const XmlNode &c : rPr.children) {
        if (c.isText()) continue;
        if (c.name == "w:rFonts") { const std::string f = detail::fontOf(c); if (!f.empty()) s.font = f; }
        else if (c.name == "w:b") s.bold = detail::triState(c);
        else if (c.name == "w:i") s.italic = detail::triState(c);
        else if (c.name == "w:u") s.underline = detail::triState(c);
        else if (c.name == "w:strike") s.strike = detail::triState(c);
        else if (c.name == "w:color") { const std::string v = c.attrOr("w:val"); if (v != "auto") s.color = v; }
        else if (c.name == "w:sz") s.sizeHalfPt = toInt(c.attrOr("w:val"));
    }
}

void parseStyleSheet(const std::string &xml, StyleSheet &sheet, bool builtin)
{
    XmlNode root; std::string err;
    if (!parseXml(xml, root, err)) return;
    for (const XmlNode &c : root.children) {
        if (c.name == "w:docDefaults") {
            if (const XmlNode *rd = c.child("w:rPrDefault"))
                if (const XmlNode *rp = rd->child("w:rPr")) {
                    StyleInfo tmp; parseRPrInto(*rp, tmp);
                    if (!tmp.font.empty()) sheet.defFont = tmp.font;
                    if (tmp.sizeHalfPt > 0) sheet.defSizeHalfPt = tmp.sizeHalfPt;
                }
        } else if (c.name == "w:style") {
            StyleInfo s;
            s.id = c.attrOr("w:styleId");
            s.type = c.attrOr("w:type");
            const std::string def = c.attrOr("w:default");
            s.isDefault = (def == "1" || def == "true");
            if (const XmlNode *n = c.child("w:name")) s.name = n->attrOr("w:val");
            if (const XmlNode *b = c.child("w:basedOn")) s.basedOn = b->attrOr("w:val");
            if (const XmlNode *pp = c.child("w:pPr")) {
                if (const XmlNode *np = pp->child("w:numPr")) {
                    const XmlNode *nid = np->child("w:numId");
                    const XmlNode *lvl = np->child("w:ilvl");
                    if (nid) s.numId = toInt(nid->attrOr("w:val"), -1);
                    s.numLevel = lvl ? toInt(lvl->attrOr("w:val")) : 0;
                }
                if (const XmlNode *jc = pp->child("w:jc")) {
                    const std::string v = jc->attrOr("w:val");
                    s.align = (v == "center") ? 'c' : (v == "right" || v == "end") ? 'r' : (v == "both" || v == "distribute") ? 'j' : 0;
                }
                if (const XmlNode *sh = pp->child("w:shd")) {   // ~ v2.6.2 : fond de paragraphe du style (zone de code...)
                    const std::string fill = sh->attrOr("w:fill");
                    if (!fill.empty() && fill != "auto") s.shading = fill;
                }
                if (const XmlNode *sp = pp->child("w:spacing")) {
                    if (sp->attr("w:before")) s.spaceBefore = toInt(sp->attrOr("w:before"));
                    if (sp->attr("w:after")) s.spaceAfter = toInt(sp->attrOr("w:after"));
                }
            }
            if (const XmlNode *rp = c.child("w:rPr")) parseRPrInto(*rp, s);
            if (builtin) { s.builtin = true; s.xml = slice(xml, c); }
            if (s.isDefault && s.type == "paragraph" && sheet.defParaStyleId.empty()) sheet.defParaStyleId = s.id;
            if (!s.id.empty()) sheet.styles.push_back(s);
        }
    }
}

void parseNumbering(const std::string &xml, Numbering &num)
{
    XmlNode root; std::string err;
    if (!parseXml(xml, root, err)) return;
    num.present = true;
    for (const XmlNode &c : root.children) {
        if (c.name == "w:abstractNum") {
            const int id = toInt(c.attrOr("w:abstractNumId"), -1);
            std::vector<std::string> fmts;
            for (const XmlNode &l : c.children) {
                if (l.name != "w:lvl") continue;
                const int lvl = toInt(l.attrOr("w:ilvl"), 0);
                if (lvl < 0 || lvl > 20) continue;
                if (static_cast<int>(fmts.size()) <= lvl) fmts.resize(static_cast<size_t>(lvl) + 1);
                if (const XmlNode *nf = l.child("w:numFmt")) fmts[static_cast<size_t>(lvl)] = nf->attrOr("w:val");

                std::vector<NumLevel> &defs = num.levels[id];
                if (defs.size() <= static_cast<size_t>(lvl)) defs.resize(static_cast<size_t>(lvl) + 1);
                NumLevel &d = defs[static_cast<size_t>(lvl)];
                d.fmt = fmts[static_cast<size_t>(lvl)];
                if (const XmlNode *lt = l.child("w:lvlText")) d.text = lt->attrOr("w:val");
                if (const XmlNode *st = l.child("w:start")) d.start = toInt(st->attrOr("w:val"), 1);
                if (const XmlNode *sf = l.child("w:suff")) d.suff = sf->attrOr("w:val", "tab");
            }
            num.abstractFormats[id] = fmts;
        } else if (c.name == "w:num") {
            const int nid = toInt(c.attrOr("w:numId"), -1);
            if (const XmlNode *a = c.child("w:abstractNumId")) num.numToAbstract[nid] = toInt(a->attrOr("w:val"), -1);
        }
    }
}

// ============================================================================
//  Conversion du corps
// ============================================================================
class Reader
{
public:
    Reader(Package &pkg, Document &doc, const std::string &docXml, const std::map<std::string, Rel> &rels, const std::string &docDir)
        : m_pkg(pkg), m_doc(doc), m_src(docXml), m_rels(rels), m_dir(docDir) {}

    void convertBody(const XmlNode &body)
    {
        for (size_t i = 0; i < body.children.size(); ++i) {
            const XmlNode &c = body.children[i];
            if (c.isText()) continue;
            if (c.name == "w:sectPr") {
                m_pkg.sectPr = slice(m_src, c);
                continue;
            }
            m_doc.blocks.push_back(convertElement(c));
        }
    }

private:
    bool parsePPr(const XmlNode &pPr, Paragraph &out)
    {
        std::string styleId;
        for (const XmlNode &c : pPr.children) {
            if (c.isText()) continue;
            const std::string &n = c.name;
            if (n == "w:pStyle") styleId = c.attrOr("w:val");
            else if (n == "w:jc") {
                const std::string v = c.attrOr("w:val");
                out.align = (v == "center") ? 'c' : (v == "right" || v == "end") ? 'r' : (v == "both" || v == "distribute") ? 'j' : 0;
                if (out.align == 0 && v != "left" && v != "start") out.pprExtra.emplace_back(n, slice(m_src, c));
            } else if (n == "w:numPr") {
                const XmlNode *nid = c.child("w:numId");
                const XmlNode *lvl = c.child("w:ilvl");
                const int numId = nid ? toInt(nid->attrOr("w:val")) : 0;
                const int level = lvl ? toInt(lvl->attrOr("w:val")) : 0;
                if (numId > 0) {
                    out.numId = numId;
                    out.listLevel = std::max(0, std::min(8, level));
                    out.list = m_pkg.numbering.kindFor(numId, level);
                    out.listGroup = numId;
                } else {
                    out.pprExtra.emplace_back(n, slice(m_src, c)); // numId 0 : "pas de liste" explicite
                }
            } else if (n == "w:sectPr" || n == "w:pPrChange") {
                return false;
            } else {
                out.pprExtra.emplace_back(n, slice(m_src, c));
            }
        }
        if (!styleId.empty() && styleId != m_pkg.styles.defParaStyleId)
            if (const StyleInfo *s = m_pkg.styles.byId(styleId)) out.style = s->name;
        return true;
    }

    bool parseRun(const XmlNode &r, const std::string &link, std::vector<Run> &runs)
    {
        RunFormat fmt;
        fmt.link = link;
        for (const XmlNode &c : r.children) {
            if (c.isText() || c.name != "w:rPr") continue;
            for (const XmlNode &p : c.children) {
                if (p.isText()) continue;
                const std::string &n = p.name;
                if (n == "w:rFonts") {
                    // police latine uniquement (ascii / hAnsi) ; l'élément d'origine est TOUJOURS conservé tel quel
                    // (eastAsia, cs, thèmes...) et n'est remplacé que si l'utilisateur change la police.
                    for (const char *a : {"w:ascii", "w:hAnsi"})
                        if (fmt.font.empty()) if (const std::string *v = p.attr(a)) if (!v->empty()) fmt.font = *v;
                    fmt.extra.emplace_back(n, slice(m_src, p));
                }
                else if (n == "w:b") fmt.bold = detail::triState(p);
                else if (n == "w:i") fmt.italic = detail::triState(p);
                else if (n == "w:u") fmt.underline = detail::triState(p);
                else if (n == "w:strike") fmt.strike = detail::triState(p);
                else if (n == "w:color") { const std::string v = p.attrOr("w:val"); if (v != "auto" && !v.empty()) fmt.color = v; }
                else if (n == "w:sz") fmt.sizeHalfPt = toInt(p.attrOr("w:val"));
                else if (n == "w:highlight") { const std::string v = p.attrOr("w:val"); if (v != "none") fmt.background = highlightColorHex(v); }
                else if (n == "w:shd") { const std::string v = p.attrOr("w:fill"); if (v != "auto" && !v.empty()) fmt.background = v; }
                else fmt.extra.emplace_back(n, slice(m_src, p));
            }
        }

        std::string text;
        auto flush = [&] {
            if (!text.empty()) {
                Run run; run.kind = Run::Text; run.text = text; run.fmt = fmt;
                runs.push_back(std::move(run));
                text.clear();
            }
        };

        for (const XmlNode &c : r.children) {
            if (c.isText()) continue;
            const std::string &n = c.name;
            if (n == "w:rPr" || n == "w:lastRenderedPageBreak" || n == "w:softHyphen") continue;
            if (n == "w:t") text += c.textContent();
            else if (n == "w:tab") text += '\t';
            else if (n == "w:cr") text += '\n';
            else if (n == "w:noBreakHyphen") text += "\xE2\x80\x91";
            else if (n == "w:br") {
                const std::string t = c.attrOr("w:type");
                if (!t.empty() && t != "textWrapping") return false;   // saut de page / colonne
                text += '\n';
            } else if (n == "w:drawing" || n == "mc:AlternateContent") {
                flush();
                const XmlNode *drawing = &c;
                if (n == "mc:AlternateContent") {
                    // Word enveloppe certaines images dans mc:AlternateContent : le premier mc:Choice contient
                    // le w:drawing ordinaire (le mc:Fallback est l'ancienne forme VML, abandonnée).
                    drawing = nullptr;
                    for (const XmlNode &ch : c.children) {
                        if (ch.isText() || ch.name != "mc:Choice") continue;
                        for (const XmlNode &d : ch.children)
                            if (!d.isText() && d.name == "w:drawing" && !drawing) drawing = &d;
                        break;
                    }
                    if (!drawing) return false;
                }
                Run run;
                if (!parseDrawing(*drawing, run)) return false;
                run.fmt = fmt;
                runs.push_back(std::move(run));
            } else {
                return false;   // champ, symbole, note, objet... : non pris en charge
            }
        }
        flush();
        return true;
    }

    bool parseDrawing(const XmlNode &drawing, Run &run)
    {
        const XmlNode *inl = nullptr;
        for (const XmlNode &c : drawing.children) {
            if (c.isText()) continue;
            if (c.name == "wp:inline" && !inl) inl = &c;
            else return false;   // image flottante (wp:anchor)...
        }
        if (!inl) return false;
        const XmlNode *ext = inl->child("wp:extent");
        const XmlNode *dp = inl->child("wp:docPr");
        std::string rid;
        findBlip(*inl, rid);
        if (!ext || rid.empty()) return false;

        auto rel = m_rels.find(rid);
        if (rel == m_rels.end() || rel->second.external) return false;

        int idx = -1;
        auto cached = m_imageByRid.find(rid);
        if (cached != m_imageByRid.end()) {
            idx = cached->second;
        } else {
            const ZipEntry *e = findEntry(m_pkg.entries, normalizePath(m_dir, rel->second.target));
            ImageData img;
            if (!e || !parseImage(e->data, img)) return false;
            img.origRid = rid;
            m_doc.images.push_back(std::move(img));
            idx = static_cast<int>(m_doc.images.size()) - 1;
            m_imageByRid[rid] = idx;
        }
        run.kind = Run::Image;
        run.image = idx;
        try { run.cx = std::stoll(ext->attrOr("cx", "0")); run.cy = std::stoll(ext->attrOr("cy", "0")); } catch (...) { return false; }
        if (run.cx <= 0 || run.cy <= 0) return false;
        if (dp) run.alt = dp->attrOr("descr");
        return true;
    }

    static void findBlip(const XmlNode &n, std::string &rid)
    {
        if (!rid.empty()) return;
        if (n.name == "a:blip") { rid = n.attrOr("r:embed"); return; }
        for (const XmlNode &c : n.children) if (!c.isText()) findBlip(c, rid);
    }

    bool convertParagraph(const XmlNode &p, Paragraph &out)
    {
        for (const XmlNode &c : p.children) {
            if (c.isText()) continue;
            const std::string &n = c.name;
            if (n == "w:pPr") {
                if (!parsePPr(c, out)) return false;
            } else if (n == "w:r") {
                if (!parseRun(c, std::string(), out.runs)) return false;
            } else if (n == "w:hyperlink") {
                if (c.attr("w:anchor")) return false;
                const std::string rid = c.attrOr("r:id");
                auto it = m_rels.find(rid);
                if (it == m_rels.end() || !it->second.external) return false;
                for (const XmlNode &h : c.children) {
                    if (h.isText()) continue;
                    if (h.name == "w:r") { if (!parseRun(h, it->second.target, out.runs)) return false; }
                    else if (h.name == "w:proofErr") continue;
                    else return false;
                }
            } else if (n == "w:bookmarkStart") {
                out.prefixXml += slice(m_src, c);
            } else if (n == "w:bookmarkEnd") {
                out.suffixXml += slice(m_src, c);
            } else if (n == "w:proofErr") {
                continue;
            } else {
                return false;   // champ simple, contrôle de contenu, révision, commentaire...
            }
        }
        return true;
    }

    // Convertit un élément de corps (ou de cellule) : paragraphe, tableau, sinon bloc brut recopié tel quel.
    Block convertElement(const XmlNode &c)
    {
        Block b;
        if (c.name == "w:p" && convertParagraph(c, b.para)) { b.kind = Block::Para; return b; }
        if (c.name == "w:tbl") {
            Block t;
            t.kind = Block::Tbl;
            if (convertTable(c, t.table)) return t;
        }
        Block r;
        r.kind = Block::Raw;
        r.raw = slice(m_src, c);
        return r;
    }

    bool convertTable(const XmlNode &tbl, Table &out)
    {
        for (const XmlNode &c : tbl.children) {
            if (c.isText()) continue;
            if (c.name == "w:tblPr") out.tblPr = slice(m_src, c);
            else if (c.name == "w:tblGrid") {
                for (const XmlNode &g : c.children)
                    if (!g.isText() && g.name == "w:gridCol") out.grid.push_back(toInt(g.attrOr("w:w")));
            } else if (c.name == "w:tr") {
                TableRow row;
                for (const XmlNode &rc : c.children) {
                    if (rc.isText()) continue;
                    if (rc.name == "w:trPr") row.trPr = slice(m_src, rc);
                    else if (rc.name == "w:tc") {
                        TableCell cell;
                        for (const XmlNode &cc : rc.children) {
                            if (cc.isText()) continue;
                            if (cc.name == "w:tcPr") {
                                for (const XmlNode &pr : cc.children)
                                    if (!pr.isText() && (pr.name == "w:gridSpan" || pr.name == "w:vMerge" || pr.name == "w:hMerge"))
                                        return false;   // cellules fusionnées : tableau conservé tel quel
                                cell.tcPr = slice(m_src, cc);
                            } else {
                                cell.blocks.push_back(convertElement(cc));   // paragraphe, tableau imbriqué ou bloc brut
                            }
                        }
                        if (cell.blocks.empty()) { Block e; e.kind = Block::Para; cell.blocks.push_back(std::move(e)); }
                        row.cells.push_back(std::move(cell));
                    } else {
                        return false;   // w:tblPrEx, contrôle de contenu au niveau ligne...
                    }
                }
                if (row.cells.empty()) return false;
                out.rows.push_back(std::move(row));
            } else {
                return false;
            }
        }
        return !out.rows.empty();
    }

    Package &m_pkg;
    Document &m_doc;
    const std::string &m_src;
    const std::map<std::string, Rel> &m_rels;
    std::string m_dir;
    std::map<std::string, int> m_imageByRid;
};

} // namespace

// ============================================================================
//  StyleSheet::addBuiltins
// ============================================================================
void StyleSheet::addBuiltins()
{
    StyleSheet b;
    parseStyleSheet(detail::builtinStylesXml(), b, true);
    for (const StyleInfo &s : b.styles) {
        if (s.id == "Normal" || s.id == "DefaultParagraphFont" || s.id == "TableNormal") continue;
        if (byId(s.id) || byName(s.name)) continue;
        styles.push_back(s);
    }
    if (defFont.empty()) defFont = b.defFont;
}

// ============================================================================
//  loadDocx / blankDocx
// ============================================================================
bool loadDocx(const std::string &bytes, Package &pkg, Document &doc, std::string &error)
{
    pkg = Package();
    doc = Document();
    if (!readZip(bytes, pkg.entries, error)) return false;

    // partie principale
    std::string docPath = "word/document.xml";
    if (const ZipEntry *rr = findEntry(pkg.entries, "_rels/.rels")) {
        for (const auto &kv : parseRels(rr->data))
            if (endsWith(kv.second.type, "/officeDocument")) docPath = normalizePath("", kv.second.target);
    }
    const ZipEntry *docEntry = findEntry(pkg.entries, docPath);
    if (!docEntry) { error = "word/document.xml not found: this is not a Word document"; return false; }
    pkg.docPath = docPath;

    const size_t slash = docPath.rfind('/');
    const std::string dir = (slash == std::string::npos) ? std::string() : docPath.substr(0, slash + 1);
    const std::string fileName = (slash == std::string::npos) ? docPath : docPath.substr(slash + 1);

    std::map<std::string, Rel> rels;
    if (const ZipEntry *re = findEntry(pkg.entries, dir + "_rels/" + fileName + ".rels")) rels = parseRels(re->data);

    // styles et numérotation
    std::string stylesPath = dir + "styles.xml", numberingPath = dir + "numbering.xml";
    for (const auto &kv : rels) {
        if (kv.second.external) continue;
        if (endsWith(kv.second.type, "/styles")) stylesPath = normalizePath(dir, kv.second.target);
        else if (endsWith(kv.second.type, "/numbering")) numberingPath = normalizePath(dir, kv.second.target);
    }
    pkg.stylesPath = stylesPath;
    pkg.numberingPath = numberingPath;
    pkg.relsPath = dir + "_rels/" + fileName + ".rels";
    if (const ZipEntry *se = findEntry(pkg.entries, stylesPath)) parseStyleSheet(se->data, pkg.styles, false);
    pkg.styles.addBuiltins();
    if (const ZipEntry *ne = findEntry(pkg.entries, numberingPath)) parseNumbering(ne->data, pkg.numbering);

    // document
    pkg.originalDocXml = docEntry->data;
    XmlNode root;
    if (!parseXml(pkg.originalDocXml, root, error)) { error = "document.xml: " + error; return false; }
    const XmlNode *body = root.child("w:body");
    if (!body) { error = "document.xml has no <w:body>"; return false; }

    if (body->srcEnd == body->openEnd) {   // <w:body/> vide
        pkg.docPrefix = pkg.originalDocXml.substr(0, body->srcBegin) + "<w:body>";
        pkg.docSuffix = "</w:body>" + pkg.originalDocXml.substr(body->srcEnd);
    } else {
        pkg.docPrefix = pkg.originalDocXml.substr(0, body->openEnd);
        pkg.docSuffix = pkg.originalDocXml.substr(body->closeBegin);
    }

    Reader reader(pkg, doc, pkg.originalDocXml, rels, dir);
    reader.convertBody(*body);
    return true;
}

void blankDocx(Package &pkg, Document &doc)
{
    pkg = Package();
    doc = Document();
    const std::string decl = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    pkg.entries.push_back({"[Content_Types].xml", decl +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
        "</Types>"});
    pkg.entries.push_back({"_rels/.rels", decl +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>"});
    pkg.originalDocXml = decl + std::string("<w:document ") + detail::kNs + "><w:body></w:body></w:document>";
    pkg.docPrefix = decl + std::string("<w:document ") + detail::kNs + "><w:body>";
    pkg.sectPr = "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
                 "<w:pgMar w:top=\"1417\" w:right=\"1417\" w:bottom=\"1417\" w:left=\"1417\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr>";
    pkg.docSuffix = "</w:body></w:document>";
    pkg.stylesPath = "word/styles.xml";
    pkg.numberingPath = "word/numbering.xml";
    pkg.relsPath = "word/_rels/document.xml.rels";
    pkg.entries.push_back({"word/document.xml", pkg.originalDocXml});
    pkg.entries.push_back({"word/_rels/document.xml.rels", decl +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
        "</Relationships>"});
    pkg.entries.push_back({"word/styles.xml", detail::builtinStylesXml()});
    pkg.entries.push_back({"word/settings.xml", decl +
        "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:zoom w:percent=\"100\"/><w:defaultTabStop w:val=\"708\"/></w:settings>"});
    parseStyleSheet(detail::builtinStylesXml(), pkg.styles, false);
    pkg.styles.addBuiltins();
}

} // namespace docx
