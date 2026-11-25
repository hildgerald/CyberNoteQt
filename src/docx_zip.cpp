#include "docx_zip.h"

#include <algorithm>
#include <ctime>

namespace docx {

uint32_t crc32(const std::string &data)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : data)
        crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================================
//  Inflate (d'après l'algorithme de référence "puff" de Mark Adler, domaine public)
// ============================================================================
namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLCodes = 286;
constexpr int kMaxDCodes = 30;
constexpr int kFixLCodes = 288;
constexpr size_t kMaxOutput = 1024u * 1024u * 1024u; // garde-fou : 1 Go

struct State {
    const unsigned char *in = nullptr;
    size_t inLen = 0, inPos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    std::string *out = nullptr;
    bool eof = false;
};

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[kFixLCodes];
};

int getBits(State &s, int need)
{
    long val = static_cast<long>(s.bitBuf);
    while (s.bitCnt < need) {
        if (s.inPos >= s.inLen) { s.eof = true; return 0; }
        val |= static_cast<long>(s.in[s.inPos++]) << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = static_cast<uint32_t>(val >> need);
    s.bitCnt -= need;
    return static_cast<int>(val & ((1L << need) - 1));
}

int decode(State &s, const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= getBits(s, 1);
        if (s.eof) return -1;
        const int count = h.count[len];
        if (code - count < first)
            return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}

int construct(Huffman &h, const short *length, int n)
{
    for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
    for (int symbol = 0; symbol < n; ++symbol) ++h.count[length[symbol]];
    if (h.count[0] == n) return 0;

    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
    for (int symbol = 0; symbol < n; ++symbol)
        if (length[symbol] != 0)
            h.symbol[offs[length[symbol]]++] = static_cast<short>(symbol);
    return left;
}

int codes(State &s, const Huffman &lencode, const Huffman &distcode)
{
    static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return symbol;
        if (symbol < 256) {
            s.out->push_back(static_cast<char>(symbol));
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            const int len = lens[symbol] + getBits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0) return symbol;
            const size_t dist = static_cast<size_t>(dists[symbol] + getBits(s, dext[symbol]));
            if (s.eof) return -1;
            if (dist > s.out->size()) return -11;
            if (s.out->size() + static_cast<size_t>(len) > kMaxOutput) return -12;
            size_t from = s.out->size() - dist;
            for (int k = 0; k < len; ++k)
                s.out->push_back((*s.out)[from + static_cast<size_t>(k)]); // peut recouvrir (copie octet par octet voulue)
        }
        if (s.out->size() > kMaxOutput) return -12;
    } while (symbol != 256);
    return 0;
}

int fixedBlock(State &s)
{
    static bool virgin = true;
    static Huffman lencode, distcode;
    if (virgin) {
        short lengths[kFixLCodes];
        int symbol = 0;
        for (; symbol < 144; ++symbol) lengths[symbol] = 8;
        for (; symbol < 256; ++symbol) lengths[symbol] = 9;
        for (; symbol < 280; ++symbol) lengths[symbol] = 7;
        for (; symbol < kFixLCodes; ++symbol) lengths[symbol] = 8;
        construct(lencode, lengths, kFixLCodes);
        for (symbol = 0; symbol < kMaxDCodes; ++symbol) lengths[symbol] = 5;
        construct(distcode, lengths, kMaxDCodes);
        virgin = false;
    }
    return codes(s, lencode, distcode);
}

int dynamicBlock(State &s)
{
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    short lengths[kMaxLCodes + kMaxDCodes];
    Huffman lencode, distcode;

    const int nlen = getBits(s, 5) + 257;
    const int ndist = getBits(s, 5) + 1;
    const int ncode = getBits(s, 4) + 4;
    if (s.eof) return -1;
    if (nlen > kMaxLCodes || ndist > kMaxDCodes) return -3;

    int index = 0;
    for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(getBits(s, 3));
    for (; index < 19; ++index) lengths[order[index]] = 0;
    if (s.eof) return -1;

    int err = construct(lencode, lengths, 19);
    if (err != 0) return -4;

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, lencode);
        if (symbol < 0) return symbol;
        if (symbol < 16) {
            lengths[index++] = static_cast<short>(symbol);
        } else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + getBits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + getBits(s, 3);
            } else {
                symbol = 11 + getBits(s, 7);
            }
            if (s.eof) return -1;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = static_cast<short>(len);
        }
    }
    if (lengths[256] == 0) return -9;

    err = construct(lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
    err = construct(distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
    return codes(s, lencode, distcode);
}

int storedBlock(State &s)
{
    s.bitBuf = 0;
    s.bitCnt = 0;
    if (s.inPos + 4 > s.inLen) return -1;
    unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
    const unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
    s.inPos += 4;
    if (len != (~nlen & 0xFFFFu)) return -2;
    if (s.inPos + len > s.inLen) return -1;
    s.out->append(reinterpret_cast<const char *>(s.in + s.inPos), len);
    s.inPos += len;
    return 0;
}

} // namespace

bool inflateRaw(const unsigned char *in, size_t inLen, std::string &out, std::string &error)
{
    State s;
    s.in = in;
    s.inLen = inLen;
    s.out = &out;
    int err = 0;
    int last;
    do {
        last = getBits(s, 1);
        const int type = getBits(s, 2);
        if (s.eof) { err = -1; break; }
        if (type == 0) err = storedBlock(s);
        else if (type == 1) err = fixedBlock(s);
        else if (type == 2) err = dynamicBlock(s);
        else err = -3;
        if (err != 0) break;
    } while (!last);
    if (err != 0) {
        error = "deflate error " + std::to_string(err);
        return false;
    }
    return true;
}

// ============================================================================
//  Lecture ZIP
// ============================================================================
namespace {

uint32_t rd16(const std::string &b, size_t p) { return static_cast<unsigned char>(b[p]) | (static_cast<unsigned char>(b[p + 1]) << 8); }
uint32_t rd32(const std::string &b, size_t p) { return rd16(b, p) | (rd16(b, p + 2) << 16); }

} // namespace

bool readZip(const std::string &bytes, std::vector<ZipEntry> &entries, std::string &error)
{
    entries.clear();
    if (bytes.size() < 22) { error = "not a zip file"; return false; }

    // fin du répertoire central : signature 0x06054b50, recherchée depuis la fin
    size_t eocd = std::string::npos;
    const size_t minPos = bytes.size() > 22 + 65535 ? bytes.size() - 22 - 65535 : 0;
    for (size_t p = bytes.size() - 22 + 1; p-- > minPos;) {
        if (rd32(bytes, p) == 0x06054b50) { eocd = p; break; }
    }
    if (eocd == std::string::npos) { error = "zip end of central directory not found"; return false; }

    const size_t count = rd16(bytes, eocd + 10);
    size_t pos = rd32(bytes, eocd + 16);
    if (count == 0xFFFF || pos == 0xFFFFFFFFu) { error = "zip64 is not supported"; return false; }

    for (size_t i = 0; i < count; ++i) {
        if (pos + 46 > bytes.size() || rd32(bytes, pos) != 0x02014b50) { error = "corrupted zip directory"; return false; }
        const uint32_t method = rd16(bytes, pos + 10);
        const size_t csize = rd32(bytes, pos + 20);
        const size_t usize = rd32(bytes, pos + 24);
        const size_t nameLen = rd16(bytes, pos + 28);
        const size_t extraLen = rd16(bytes, pos + 30);
        const size_t commentLen = rd16(bytes, pos + 32);
        const size_t localOff = rd32(bytes, pos + 42);
        if (pos + 46 + nameLen > bytes.size()) { error = "corrupted zip directory"; return false; }
        const std::string name = bytes.substr(pos + 46, nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        if (!name.empty() && name.back() == '/')
            continue; // dossier

        if (localOff + 30 > bytes.size() || rd32(bytes, localOff) != 0x04034b50) { error = "corrupted zip entry " + name; return false; }
        const size_t dataOff = localOff + 30 + rd16(bytes, localOff + 26) + rd16(bytes, localOff + 28);
        if (dataOff + csize > bytes.size()) { error = "truncated zip entry " + name; return false; }

        ZipEntry e;
        e.name = name;
        if (method == 0) {
            e.data = bytes.substr(dataOff, csize);
        } else if (method == 8) {
            e.data.reserve(usize);
            std::string err;
            if (!inflateRaw(reinterpret_cast<const unsigned char *>(bytes.data()) + dataOff, csize, e.data, err)) {
                error = "cannot decompress " + name + " (" + err + ")";
                return false;
            }
        } else {
            error = "unsupported compression method in " + name;
            return false;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

// ============================================================================
//  Écriture ZIP (stored)
// ============================================================================
namespace {
void u16(std::string &o, uint16_t v) { o += static_cast<char>(v & 0xFF); o += static_cast<char>(v >> 8); }
void u32(std::string &o, uint32_t v) { u16(o, static_cast<uint16_t>(v & 0xFFFF)); u16(o, static_cast<uint16_t>(v >> 16)); }
}

std::string writeZip(const std::vector<ZipEntry> &entries)
{
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
    const std::tm *lt = std::localtime(&now);
    if (lt) tmv = *lt;
    const int year = std::max(1980, tmv.tm_year + 1900);
    const uint16_t dosTime = static_cast<uint16_t>((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    const uint16_t dosDate = static_cast<uint16_t>(((year - 1980) << 9) | ((tmv.tm_mon + 1) << 5) | std::max(1, tmv.tm_mday));

    struct Meta { uint32_t crc, size, offset; };
    std::vector<Meta> metas;
    std::string out;

    for (const ZipEntry &e : entries) {
        Meta m{crc32(e.data), static_cast<uint32_t>(e.data.size()), static_cast<uint32_t>(out.size())};
        u32(out, 0x04034b50);
        u16(out, 20); u16(out, 0x0800); u16(out, 0); // version, drapeau UTF-8, stored
        u16(out, dosTime); u16(out, dosDate);
        u32(out, m.crc); u32(out, m.size); u32(out, m.size);
        u16(out, static_cast<uint16_t>(e.name.size())); u16(out, 0);
        out += e.name;
        out += e.data;
        metas.push_back(m);
    }

    const uint32_t cdOffset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry &e = entries[i];
        const Meta &m = metas[i];
        u32(out, 0x02014b50);
        u16(out, 20); u16(out, 20); u16(out, 0x0800); u16(out, 0);
        u16(out, dosTime); u16(out, dosDate);
        u32(out, m.crc); u32(out, m.size); u32(out, m.size);
        u16(out, static_cast<uint16_t>(e.name.size())); u16(out, 0); u16(out, 0);
        u16(out, 0); u16(out, 0); u32(out, 0);
        u32(out, m.offset);
        out += e.name;
    }
    const uint32_t cdSize = static_cast<uint32_t>(out.size()) - cdOffset;
    u32(out, 0x06054b50);
    u16(out, 0); u16(out, 0);
    u16(out, static_cast<uint16_t>(entries.size())); u16(out, static_cast<uint16_t>(entries.size()));
    u32(out, cdSize); u32(out, cdOffset);
    u16(out, 0);
    return out;
}

} // namespace docx
