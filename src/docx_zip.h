#ifndef DOCX_ZIP_H
#define DOCX_ZIP_H

// ~ nouveau (v2.2.0) : lecture / écriture d'archives ZIP (un .docx en est une), en C++17 standard,
// sans zlib : décompresseur Deflate intégré (inflate), écriture en mode "stored".

#include <cstdint>
#include <string>
#include <vector>

namespace docx {

struct ZipEntry {
    std::string name;
    std::string data;   // contenu décompressé
};

uint32_t crc32(const std::string &data);

// Décompresse un flux Deflate brut (RFC 1951).
bool inflateRaw(const unsigned char *in, size_t inLen, std::string &out, std::string &error);

// Lit toutes les entrées (fichiers) d'une archive ZIP (méthodes stored et deflate ; pas de Zip64).
bool readZip(const std::string &bytes, std::vector<ZipEntry> &entries, std::string &error);

// Écrit une archive ZIP (entrées dans l'ordre donné, sans compression).
std::string writeZip(const std::vector<ZipEntry> &entries);

} // namespace docx

#endif // DOCX_ZIP_H
