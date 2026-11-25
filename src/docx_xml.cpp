#include "docx_xml.h"

#include <cstdint>

namespace docx {

const std::string *XmlNode::attr(const std::string &n) const
{
    for (const auto &a : attrs)
        if (a.first == n) return &a.second;
    return nullptr;
}

std::string XmlNode::attrOr(const std::string &n, const std::string &def) const
{
    const std::string *a = attr(n);
    return a ? *a : def;
}

const XmlNode *XmlNode::child(const std::string &n) const
{
    for (const XmlNode &c : children)
        if (c.name == n) return &c;
    return nullptr;
}

std::string XmlNode::textContent() const
{
    if (isText()) return text;
    std::string t;
    for (const XmlNode &c : children) t += c.textContent();
    return t;
}

namespace {

void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

struct Parser {
    const std::string &s;
    size_t pos = 0;
    std::string err;
    explicit Parser(const std::string &src) : s(src) {}

    bool fail(const std::string &m) { if (err.empty()) err = m + " at offset " + std::to_string(pos); return false; }
    void skipWs() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) ++pos; }
    bool starts(const char *lit) const { return s.compare(pos, std::char_traits<char>::length(lit), lit) == 0; }

    bool skipUntil(const char *lit)
    {
        const size_t p = s.find(lit, pos);
        if (p == std::string::npos) return fail(std::string("missing ") + lit);
        pos = p + std::char_traits<char>::length(lit);
        return true;
    }

    static bool isNameChar(char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '/' && c != '>' && c != '=' && c != '<'; }

    std::string readName()
    {
        const size_t b = pos;
        while (pos < s.size() && isNameChar(s[pos])) ++pos;
        return s.substr(b, pos - b);
    }

    bool parseElement(XmlNode &node, int depth)
    {
        if (depth > 256) return fail("document too deeply nested");
        node.srcBegin = pos;
        ++pos; // '<'
        node.name = readName();
        if (node.name.empty()) return fail("empty element name");

        // attributs
        while (true) {
            skipWs();
            if (pos >= s.size()) return fail("unexpected end in tag");
            if (s[pos] == '>') { ++pos; break; }
            if (s[pos] == '/') {
                if (pos + 1 < s.size() && s[pos + 1] == '>') {
                    pos += 2;
                    node.openEnd = node.closeBegin = node.srcEnd = pos;
                    return true;
                }
                return fail("malformed tag");
            }
            const std::string an = readName();
            if (an.empty()) return fail("malformed attribute");
            skipWs();
            if (pos >= s.size() || s[pos] != '=') return fail("attribute without value");
            ++pos;
            skipWs();
            if (pos >= s.size() || (s[pos] != '"' && s[pos] != '\'')) return fail("attribute value not quoted");
            const char q = s[pos++];
            const size_t e = s.find(q, pos);
            if (e == std::string::npos) return fail("unterminated attribute value");
            node.attrs.emplace_back(an, xmlDecode(s.substr(pos, e - pos)));
            pos = e + 1;
        }
        node.openEnd = pos;

        // contenu
        while (true) {
            if (pos >= s.size()) return fail("unexpected end of document in <" + node.name + ">");
            if (s[pos] != '<') {
                const size_t e = s.find('<', pos);
                const size_t stop = (e == std::string::npos) ? s.size() : e;
                XmlNode t;
                t.name = "#text";
                t.text = xmlDecode(s.substr(pos, stop - pos));
                node.children.push_back(std::move(t));
                pos = stop;
                continue;
            }
            if (starts("</")) {
                node.closeBegin = pos;
                pos += 2;
                const std::string en = readName();
                if (en != node.name) return fail("mismatched end tag </" + en + "> for <" + node.name + ">");
                skipWs();
                if (pos >= s.size() || s[pos] != '>') return fail("malformed end tag");
                ++pos;
                node.srcEnd = pos;
                return true;
            }
            if (starts("<!--")) { if (!skipUntil("-->")) return false; continue; }
            if (starts("<![CDATA[")) {
                pos += 9;
                const size_t e = s.find("]]>", pos);
                if (e == std::string::npos) return fail("unterminated CDATA");
                XmlNode t;
                t.name = "#text";
                t.text = s.substr(pos, e - pos);
                node.children.push_back(std::move(t));
                pos = e + 3;
                continue;
            }
            if (starts("<?")) { if (!skipUntil("?>")) return false; continue; }
            XmlNode c;
            if (!parseElement(c, depth + 1)) return false;
            node.children.push_back(std::move(c));
        }
    }
};

} // namespace

std::string xmlDecode(const std::string &s)
{
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) { out += s[i]; continue; }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            uint32_t cp = 0;
            bool ok = ent.size() > 1;
            if (ok && (ent[1] == 'x' || ent[1] == 'X')) {
                ok = ent.size() > 2;
                for (size_t k = 2; ok && k < ent.size(); ++k) {
                    const char c = ent[k];
                    if (c >= '0' && c <= '9') cp = cp * 16 + static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') cp = cp * 16 + static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') cp = cp * 16 + static_cast<uint32_t>(c - 'A' + 10);
                    else ok = false;
                }
            } else {
                for (size_t k = 1; ok && k < ent.size(); ++k) {
                    if (ent[k] >= '0' && ent[k] <= '9') cp = cp * 10 + static_cast<uint32_t>(ent[k] - '0');
                    else ok = false;
                }
            }
            if (ok && cp > 0 && cp <= 0x10FFFF) appendUtf8(out, cp);
            else { out += s.substr(i, semi - i + 1); }
        } else {
            out += s.substr(i, semi - i + 1); // entité inconnue : conservée telle quelle
        }
        i = semi;
    }
    return out;
}

std::string xmlEsc(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

bool parseXml(const std::string &src, XmlNode &root, std::string &error)
{
    Parser p(src);
    // prologue : déclaration, commentaires, DOCTYPE
    while (true) {
        p.skipWs();
        if (p.starts("\xEF\xBB\xBF")) { p.pos += 3; continue; }
        if (p.starts("<?")) { if (!p.skipUntil("?>")) { error = p.err; return false; } continue; }
        if (p.starts("<!--")) { if (!p.skipUntil("-->")) { error = p.err; return false; } continue; }
        if (p.starts("<!DOCTYPE")) { if (!p.skipUntil(">")) { error = p.err; return false; } continue; }
        break;
    }
    if (p.pos >= src.size() || src[p.pos] != '<') { error = "no root element"; return false; }
    root = XmlNode();
    if (!p.parseElement(root, 0)) { error = p.err; return false; }
    return true;
}

} // namespace docx
