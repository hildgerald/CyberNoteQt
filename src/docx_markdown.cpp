#include "docx_internal.h"

#include <algorithm>
#include <cctype>

namespace docx {
namespace {

constexpr long long kMaxImageWidthEmu = 5760000;    // ~ 16 cm
constexpr long long kMaxImageHeightEmu = 8100000;   // ~ 22,5 cm
constexpr int kTextWidthTwips = 9072;               // A4 - marges 2,5 cm

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}
bool startsWith(const std::string &s, const char *prefix) { return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0; }

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    lines.push_back(cur);
    return lines;
}

// ----------------------------------------------------------------------------- mise en forme en ligne
struct Fmt {
    bool bold = false, italic = false, strike = false, underline = false, highlight = false, code = false;
    std::string link;
};

struct Item {
    enum Kind { Text, Image, Break } kind = Text;
    std::string text;
    Fmt fmt;
};

bool matchBr(const std::string &s, size_t i, size_t &len)
{
    static const char *const forms[] = {"<br>", "<br/>", "<br />", "<BR>"};
    for (const char *f : forms) {
        const size_t l = std::char_traits<char>::length(f);
        if (s.compare(i, l, f) == 0) { len = l; return true; }
    }
    return false;
}

void parseInline(const std::string &s, const Fmt &fmt, std::vector<Item> &out)
{
    std::string buf;
    auto flush = [&] {
        if (!buf.empty()) { Item r; r.text = buf; r.fmt = fmt; out.push_back(r); buf.clear(); }
    };
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const char c = s[i];

        if (c == '\\' && i + 1 < n && std::ispunct(static_cast<unsigned char>(s[i + 1]))) { buf += s[i + 1]; i += 2; continue; }

        if (c == '!' && s.compare(i, 3, "![[") == 0) {
            const size_t close = s.find("]]", i + 3);
            if (close != std::string::npos && close > i + 3) {
                flush();
                Item r; r.kind = Item::Image; r.text = s.substr(i + 3, close - (i + 3)); r.fmt = fmt;
                out.push_back(r);
                i = close + 2;
                continue;
            }
        }
        if (c == '<') {
            size_t len = 0;
            if (matchBr(s, i, len)) { flush(); Item r; r.kind = Item::Break; out.push_back(r); i += len; continue; }
        }
        if (c == '`') {
            const size_t close = s.find('`', i + 1);
            if (close != std::string::npos && close > i + 1) {
                flush();
                Item r; r.text = s.substr(i + 1, close - i - 1); r.fmt = fmt; r.fmt.code = true;
                out.push_back(r);
                i = close + 1;
                continue;
            }
        }
        if (c == '[') {
            const size_t rb = s.find(']', i + 1);
            if (rb != std::string::npos && rb + 1 < n && s[rb + 1] == '(') {
                const size_t rp = s.find(')', rb + 2);
                if (rp != std::string::npos && rp > rb + 2) {
                    flush();
                    Fmt f = fmt;
                    f.link = trim(s.substr(rb + 2, rp - rb - 2));
                    parseInline(s.substr(i + 1, rb - i - 1), f, out);
                    i = rp + 1;
                    continue;
                }
            }
        }
        {
            static const char *const toks[] = {"**", "~~", "==", "__", "%%"};
            bool matched = false;
            for (int t = 0; t < 5 && !matched; ++t) {
                if (s.compare(i, 2, toks[t]) != 0) continue;
                size_t close = s.find(toks[t], i + 2);
                if (close == std::string::npos || close == i + 2) continue;
                if (t == 0) while (close + 2 < n && s[close + 2] == '*') ++close;   // ***gras italique***
                flush();
                const std::string inner = s.substr(i + 2, close - i - 2);
                if (t != 4) {   // %% : commentaire, omis
                    Fmt f = fmt;
                    if (t == 0) f.bold = true; else if (t == 1) f.strike = true; else if (t == 2) f.highlight = true; else f.underline = true;
                    parseInline(inner, f, out);
                }
                i = close + 2;
                matched = true;
            }
            if (matched) continue;
        }
        if (c == '*' && i + 1 < n && s[i + 1] != '*' && s[i + 1] != ' ' && s[i + 1] != '\t') {
            size_t close = std::string::npos;
            for (size_t j = i + 2; j < n; ++j) {
                if (s[j] == '*' && s[j - 1] != ' ' && s[j - 1] != '\t' && s[j - 1] != '*' && (j + 1 >= n || s[j + 1] != '*')) { close = j; break; }
            }
            if (close != std::string::npos) {
                flush();
                Fmt f = fmt; f.italic = true;
                parseInline(s.substr(i + 1, close - i - 1), f, out);
                i = close + 1;
                continue;
            }
        }
        buf += c;
        ++i;
    }
    flush();
}

// ----------------------------------------------------------------------------- aides de ligne
std::vector<std::string> splitTableRow(const std::string &line)
{
    std::string s = trim(line);
    if (!s.empty() && s[0] == '|') s.erase(0, 1);
    if (!s.empty() && s.back() == '|' && !(s.size() >= 2 && s[s.size() - 2] == '\\')) s.pop_back();
    std::vector<std::string> cells;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '|') { cur += '|'; ++i; }
        else if (s[i] == '|') { cells.push_back(trim(cur)); cur.clear(); }
        else cur += s[i];
    }
    cells.push_back(trim(cur));
    return cells;
}

bool isSeparatorRow(const std::vector<std::string> &cells)
{
    if (cells.empty()) return false;
    for (const std::string &c : cells) {
        if (c.empty()) return false;
        size_t b = 0, e = c.size();
        if (c[b] == ':') ++b;
        if (e > b && c[e - 1] == ':') --e;
        if (e <= b) return false;
        for (size_t i = b; i < e; ++i) if (c[i] != '-') return false;
    }
    return true;
}

bool isHorizontalRule(const std::string &line)
{
    size_t i = 0;
    int lead = 0;
    while (i < line.size() && line[i] == ' ') { ++i; ++lead; }
    if (lead > 3 || i >= line.size()) return false;
    const char m = line[i];
    if (m != '-' && m != '*' && m != '_') return false;
    int count = 0;
    for (; i < line.size(); ++i) {
        if (line[i] == m) ++count;
        else if (line[i] != ' ' && line[i] != '\t') return false;
    }
    return count >= 3;
}

// ----------------------------------------------------------------------------- convertisseur
class Converter
{
public:
    Converter(const ImageLoader &loader, std::vector<Block> &blocks, std::vector<ImageData> &images, int &groupCounter)
        : m_loader(loader), m_blocks(blocks), m_images(images), m_group(groupCounter) {}

    void run(const std::string &markdown);

private:
    std::vector<Run> toRuns(const std::string &text, const Fmt &base);
    void addPara(const std::string &style, const std::string &text, const Fmt &base, char align = 0,
                 const std::vector<XmlChild> &extra = {});
    void addCode(const std::string &line, bool first, bool last);
    void addTable(const std::vector<std::vector<std::string>> &rows, const std::vector<char> &aligns);
    void addListItem(const std::string &indent, bool ordered, const std::string &text);
    void resetList() { m_indents.clear(); m_lastOrdered = false; m_lastLevel = -1; m_curGroup = 0; }
    int imageIndex(const std::string &path, long long &cx, long long &cy);

    const ImageLoader &m_loader;
    std::vector<Block> &m_blocks;
    std::vector<ImageData> &m_images;
    int &m_group;
    std::map<std::string, int> m_imageByPath;

    std::vector<int> m_indents;
    bool m_lastOrdered = false;
    int m_lastLevel = -1;
    int m_curGroup = 0;
};

int Converter::imageIndex(const std::string &path, long long &cx, long long &cy)
{
    int idx = -1;
    auto it = m_imageByPath.find(path);
    if (it != m_imageByPath.end()) idx = it->second;
    else {
        ImageData img;
        if (m_loader && m_loader(path, img) && img.wPx > 0 && img.hPx > 0 && !img.bytes.empty()) {
            m_images.push_back(std::move(img));
            idx = static_cast<int>(m_images.size()) - 1;
        }
        m_imageByPath[path] = idx;
    }
    if (idx < 0) return -1;
    const ImageData &img = m_images[static_cast<size_t>(idx)];
    cx = static_cast<long long>(img.wPx) * 9525;
    cy = static_cast<long long>(img.hPx) * 9525;
    if (cx > kMaxImageWidthEmu) { cy = cy * kMaxImageWidthEmu / cx; cx = kMaxImageWidthEmu; }
    if (cy > kMaxImageHeightEmu) { cx = cx * kMaxImageHeightEmu / cy; cy = kMaxImageHeightEmu; }
    cx = std::max<long long>(cx, 1);
    cy = std::max<long long>(cy, 1);
    return idx;
}

std::vector<Run> Converter::toRuns(const std::string &text, const Fmt &base)
{
    std::vector<Item> items;
    parseInline(text, base, items);
    std::vector<Run> runs;
    for (const Item &it : items) {
        Run r;
        if (it.kind == Item::Break) { r.kind = Run::Text; r.text = "\n"; runs.push_back(r); continue; }
        if (it.kind == Item::Image) {
            long long cx = 0, cy = 0;
            const int idx = imageIndex(it.text, cx, cy);
            if (idx >= 0) { r.kind = Run::Image; r.image = idx; r.cx = cx; r.cy = cy; r.alt = it.text; }
            else { r.kind = Run::Text; r.text = "[image introuvable : " + it.text + "]"; r.fmt.italic = 1; r.fmt.color = "C00000"; }
            runs.push_back(r);
            continue;
        }
        r.kind = Run::Text;
        r.text = it.text;
        const Fmt &f = it.fmt;
        if (f.bold) r.fmt.bold = 1;
        if (f.italic) r.fmt.italic = 1;
        if (f.strike) r.fmt.strike = 1;
        if (f.underline) r.fmt.underline = 1;
        if (f.highlight) r.fmt.background = "FFFF00";
        if (f.code) { r.fmt.font = "Consolas"; r.fmt.sizeHalfPt = 19; if (r.fmt.background.empty()) r.fmt.background = "EDEDED"; }
        r.fmt.link = f.link;
        runs.push_back(r);
    }
    return runs;
}

void Converter::addPara(const std::string &style, const std::string &text, const Fmt &base, char align, const std::vector<XmlChild> &extra)
{
    Block b;
    b.kind = Block::Para;
    b.para.style = style;
    b.para.align = align;
    b.para.pprExtra = extra;
    b.para.runs = toRuns(text, base);
    m_blocks.push_back(std::move(b));
}

void Converter::addCode(const std::string &line, bool first, bool last)
{
    Block b;
    b.kind = Block::Para;
    b.para.style = "Code Block";
    if (first || last)
        b.para.pprExtra.emplace_back("w:spacing", "<w:spacing w:before=\"" + std::string(first ? "100" : "0") + "\" w:after=\"" + std::string(last ? "160" : "0") + "\"/>");
    if (!line.empty()) { Run r; r.kind = Run::Text; r.text = line; b.para.runs.push_back(r); }
    m_blocks.push_back(std::move(b));
}

void Converter::addListItem(const std::string &indentStr, bool ordered, const std::string &text)
{
    int width = 0;
    for (char c : indentStr) width += (c == '\t') ? 4 : 1;
    while (!m_indents.empty() && width < m_indents.back()) m_indents.pop_back();
    if (m_indents.empty() || width > m_indents.back()) m_indents.push_back(width);
    const int level = std::min(8, static_cast<int>(m_indents.size()) - 1);

    if (m_curGroup == 0 || (level == 0 && m_lastLevel == 0 && m_lastOrdered != ordered))
        m_curGroup = ++m_group;   // nouvelle liste : la numérotation repart à 1
    m_lastOrdered = ordered;
    m_lastLevel = level;

    std::string t = text, prefix;
    if (startsWith(t, "[ ] ")) { prefix = "\xE2\x98\x90 "; t = t.substr(4); }
    else if (startsWith(t, "[x] ") || startsWith(t, "[X] ")) { prefix = "\xE2\x98\x91 "; t = t.substr(4); }

    Block b;
    b.kind = Block::Para;
    b.para.style = "List Paragraph";
    b.para.list = ordered ? 2 : 1;
    b.para.listLevel = level;
    b.para.listGroup = m_curGroup;
    if (!prefix.empty()) { Run r; r.kind = Run::Text; r.text = prefix; b.para.runs.push_back(r); }
    for (Run &r : toRuns(t, Fmt())) b.para.runs.push_back(std::move(r));
    m_blocks.push_back(std::move(b));
}

void Converter::addTable(const std::vector<std::vector<std::string>> &rows, const std::vector<char> &aligns)
{
    size_t ncols = 0;
    for (const auto &r : rows) ncols = std::max(ncols, r.size());
    if (ncols == 0) return;
    const int colW = kTextWidthTwips / static_cast<int>(ncols);

    Block b;
    b.kind = Block::Tbl;
    Table &t = b.table;
    t.grid.assign(ncols, colW);
    for (size_t r = 0; r < rows.size(); ++r) {
        TableRow row;
        row.trPr = (r == 0) ? "<w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>" : "<w:trPr><w:cantSplit/></w:trPr>";
        for (size_t c = 0; c < ncols; ++c) {
            TableCell cell;
            cell.tcPr = "<w:tcPr><w:tcW w:w=\"" + std::to_string(colW) + "\" w:type=\"dxa\"/>"
                        + (r == 0 ? "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>" : "") + "</w:tcPr>";
            Block pb;
            pb.kind = Block::Para;
            Paragraph &p = pb.para;
            p.style = "Table Text";
            const char a = c < aligns.size() ? aligns[c] : 'l';
            p.align = (a == 'c') ? 'c' : (a == 'r') ? 'r' : 0;
            Fmt base; base.bold = (r == 0);
            p.runs = toRuns(c < rows[r].size() ? rows[r][c] : std::string(), base);
            cell.blocks.push_back(std::move(pb));
            row.cells.push_back(std::move(cell));
        }
        t.rows.push_back(std::move(row));
    }
    m_blocks.push_back(std::move(b));

    Block spacer;
    spacer.kind = Block::Para;
    spacer.para.style = "Table Text";
    m_blocks.push_back(std::move(spacer));
}

void Converter::run(const std::string &markdown)
{
    const std::vector<std::string> lines = splitLines(markdown);
    const size_t n = lines.size();
    bool inCode = false;
    std::vector<std::string> codeLines;

    auto flushCode = [&] {
        if (codeLines.empty()) addCode(std::string(), true, true);
        else for (size_t k = 0; k < codeLines.size(); ++k) addCode(codeLines[k], k == 0, k + 1 == codeLines.size());
        codeLines.clear();
    };

    for (size_t i = 0; i < n; ++i) {
        const std::string &line = lines[i];
        const std::string t = trim(line);

        if (startsWith(t, "```")) {
            if (inCode) { flushCode(); inCode = false; }
            else { inCode = true; resetList(); }
            continue;
        }
        if (inCode) { codeLines.push_back(line); continue; }
        if (t.empty()) continue;

        // tableau GFM
        if (t[0] == '|' && i + 1 < n) {
            const std::string next = trim(lines[i + 1]);
            const std::vector<std::string> sep = splitTableRow(lines[i + 1]);
            if (!next.empty() && next[0] == '|' && isSeparatorRow(sep)) {
                std::vector<std::vector<std::string>> rows;
                rows.push_back(splitTableRow(line));
                std::vector<char> aligns;
                for (const std::string &c : sep) {
                    const bool l = c.front() == ':', r = c.back() == ':';
                    aligns.push_back(l && r ? 'c' : (r ? 'r' : 'l'));
                }
                size_t j = i + 2;
                while (j < n) {
                    const std::string tj = trim(lines[j]);
                    if (tj.empty() || tj[0] != '|') break;
                    rows.push_back(splitTableRow(lines[j]));
                    ++j;
                }
                resetList();
                addTable(rows, aligns);
                i = j - 1;
                continue;
            }
        }

        if (isHorizontalRule(line)) {
            resetList();
            Block b; b.kind = Block::Para;
            b.para.pprExtra.emplace_back("w:pBdr", "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"999999\"/></w:pBdr>");
            b.para.pprExtra.emplace_back("w:spacing", "<w:spacing w:before=\"60\" w:after=\"120\"/>");
            m_blocks.push_back(std::move(b));
            continue;
        }

        // titre
        {
            size_t h = 0;
            while (h < t.size() && t[h] == '#') ++h;
            if (h >= 1 && h <= 6 && h < t.size() && (t[h] == ' ' || t[h] == '\t')) {
                std::string text = trim(t.substr(h));
                while (!text.empty() && text.back() == '#') text.pop_back();
                resetList();
                addPara(headingStyleName(static_cast<int>(h)), trim(text), Fmt());
                continue;
            }
        }

        // citation
        if (t[0] == '>') {
            size_t p = 0;
            int depth = 0;
            while (p < t.size() && (t[p] == '>' || t[p] == ' ')) { if (t[p] == '>') ++depth; ++p; }
            resetList();
            addPara("Quote", t.substr(p), Fmt(), 0, {{"w:ind", "<w:ind w:left=\"" + std::to_string(360 + 360 * depth) + "\"/>"}});
            continue;
        }

        // liste
        {
            size_t p = 0;
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
            const std::string indent = line.substr(0, p);
            size_t q = p;
            bool ordered = false, isItem = false;
            if (q < line.size() && (line[q] == '-' || line[q] == '*' || line[q] == '+')) { ++q; isItem = true; }
            else {
                size_t d = q;
                while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d])) && d - q < 9) ++d;
                if (d > q && d < line.size() && (line[d] == '.' || line[d] == ')')) { q = d + 1; ordered = true; isItem = true; }
            }
            if (isItem && q < line.size() && (line[q] == ' ' || line[q] == '\t')) {
                addListItem(indent, ordered, trim(line.substr(q)));
                continue;
            }
        }

        resetList();
        addPara(std::string(), t, Fmt());
    }
    if (inCode) flushCode();
}

} // namespace

void markdownToBlocks(const std::string &markdown, const ImageLoader &loader, std::vector<Block> &blocks,
                      std::vector<ImageData> &images, int &listGroupCounter)
{
    Converter c(loader, blocks, images, listGroupCounter);
    c.run(markdown);
}

} // namespace docx
