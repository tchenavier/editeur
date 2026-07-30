// Zip.cpp
// Implémentation de l'écrivain ZIP (méthode « stockée »). Voir Zip.h.

#include "Zip.h"

#include "PathUtf8.h"

#include <array>
#include <fstream>

namespace io {

namespace {

// --- Écriture en petit-boutiste EXPLICITE -----------------------------
// Le format ZIP stocke ses entiers en petit-boutiste. On les écrit octet
// par octet plutôt que de recopier la mémoire d'un entier : le résultat
// est alors identique quelle que soit la machine, et il n'y a aucun
// accès mémoire à surveiller.
void put16(std::string& out, uint16_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}

void put32(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
    out += static_cast<char>((v >> 16) & 0xFF);
    out += static_cast<char>((v >> 24) & 0xFF);
}

// Table du CRC-32, construite une seule fois au premier appel.
// (Initialisation d'une statique locale : garantie sans course par le
// standard C++11 et suivants.)
const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        // Parcours par RÉFÉRENCE plutôt que par indice : il n'y a alors
        // aucun accès indexé à justifier (l'analyseur statique de MSVC ne
        // savait pas prouver que l'indice restait < 256, et le code est de
        // toute façon plus direct ainsi).
        uint32_t value = 0;
        for (uint32_t& entry : t) {
            uint32_t c = value++;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            entry = c;
        }
        return t;
    }();
    return table;
}

// Un nom d'entrée contient-il des octets non ASCII ? (Si oui, le drapeau
// « nom en UTF-8 » doit être posé pour que les lecteurs ne devinent pas
// une page de codes locale.)
bool hasNonAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return true;
    }
    return false;
}

} // namespace

uint32_t ZipWriter::crc32(const std::string& data) {
    const auto& table = crcTable();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc = table[(crc ^ c) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

bool ZipWriter::isValidEntryName(const std::string& name) {
    if (name.empty() || name.size() > 1024) return false;
    if (name.front() == '/') return false;             // chemin absolu
    if (name.back() == '/') return false;              // dossier : inutile ici
    if (name.find('\\') != std::string::npos) return false; // séparateur Windows
    if (name.find(':') != std::string::npos) return false;  // lettre de lecteur
    if (name.find("//") != std::string::npos) return false; // segment vide

    // Aucun segment ne doit être « . » ou « .. » : une fois extraite,
    // l'archive ne peut donc pas écrire en dehors de son dossier.
    size_t start = 0;
    while (start <= name.size()) {
        const size_t slash = name.find('/', start);
        const size_t end = (slash == std::string::npos) ? name.size() : slash;
        const std::string segment = name.substr(start, end - start);
        if (segment == "." || segment == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }

    // Pas d'octet de contrôle (le format ne les interdit pas, mais ils
    // n'ont aucune raison d'être là et perturbent les extracteurs).
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

bool ZipWriter::addFile(const std::string& name, const std::string& content) {
    if (!isValidEntryName(name)) return false;
    if (contains(name)) return false;
    if (m_entries.size() >= kMaxEntries) return false; // champ 16 bits du format
    if (content.size() > kMaxEntrySize) return false;
    if (content.size() > kMaxArchiveSize - m_totalSize) return false; // sans débordement

    Entry entry;
    entry.name = name;
    entry.content = content;
    entry.crc = crc32(content);
    entry.utf8Name = hasNonAscii(name);
    m_entries.push_back(std::move(entry));
    m_names.insert(name);
    m_totalSize += content.size();
    return true;
}

bool ZipWriter::contains(const std::string& name) const {
    return m_names.find(name) != m_names.end();
}

void ZipWriter::setTimestamp(int year, int month, int day,
                             int hour, int minute, int second) {
    // Le format MS-DOS ne sait représenter que 1980-2107, par pas de deux
    // secondes. Toute valeur hors bornes est ramenée dans la plage plutôt
    // que tronquée n'importe comment (aucune date exotique dans le fichier).
    if (year < 1980)  year = 1980;
    if (year > 2107)  year = 2107;
    if (month < 1)    month = 1;
    if (month > 12)   month = 12;
    if (day < 1)      day = 1;
    if (day > 31)     day = 31;
    if (hour < 0)     hour = 0;
    if (hour > 23)    hour = 23;
    if (minute < 0)   minute = 0;
    if (minute > 59)  minute = 59;
    if (second < 0)   second = 0;
    if (second > 59)  second = 59;

    m_dosDate = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
    m_dosTime = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

std::string ZipWriter::build() const {
    std::string out;
    std::vector<uint32_t> offsets; // position de chaque en-tête local
    offsets.reserve(m_entries.size());

    // --- 1. En-têtes locaux + données ---
    for (const Entry& e : m_entries) {
        offsets.push_back(static_cast<uint32_t>(out.size()));
        const uint16_t flags = e.utf8Name ? 0x0800 : 0x0000;
        const uint32_t size = static_cast<uint32_t>(e.content.size());

        put32(out, 0x04034B50);           // signature « PK\3\4 »
        put16(out, 20);                   // version minimale : 2.0
        put16(out, flags);
        put16(out, 0);                    // méthode 0 = stocké
        put16(out, m_dosTime);
        put16(out, m_dosDate);
        put32(out, e.crc);
        put32(out, size);                 // taille compressée
        put32(out, size);                 // taille réelle (identique)
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);                    // pas de champ « extra »
        out += e.name;
        out += e.content;
    }

    // --- 2. Annuaire central ---
    const uint32_t dirOffset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& e = m_entries[i];
        const uint16_t flags = e.utf8Name ? 0x0800 : 0x0000;
        const uint32_t size = static_cast<uint32_t>(e.content.size());

        put32(out, 0x02014B50);           // signature « PK\1\2 »
        put16(out, 20);                   // version d'écriture (MS-DOS/FAT)
        put16(out, 20);                   // version minimale de lecture
        put16(out, flags);
        put16(out, 0);                    // stocké
        put16(out, m_dosTime);
        put16(out, m_dosDate);
        put32(out, e.crc);
        put32(out, size);
        put32(out, size);
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);                    // extra
        put16(out, 0);                    // commentaire
        put16(out, 0);                    // numéro de disque
        put16(out, 0);                    // attributs internes
        put32(out, 0);                    // attributs externes
        put32(out, offsets[i]);           // position de l'en-tête local
        out += e.name;
    }
    const uint32_t dirSize = static_cast<uint32_t>(out.size()) - dirOffset;

    // --- 3. Fin d'annuaire central (EOCD) ---
    const uint16_t n = static_cast<uint16_t>(m_entries.size());
    put32(out, 0x06054B50);               // signature « PK\5\6 »
    put16(out, 0);                        // disque courant
    put16(out, 0);                        // disque de l'annuaire
    put16(out, n);                        // entrées sur ce disque
    put16(out, n);                        // entrées au total
    put32(out, dirSize);
    put32(out, dirOffset);
    put16(out, 0);                        // pas de commentaire d'archive

    return out;
}

bool ZipWriter::writeToFile(const std::filesystem::path& path,
                            std::string* errorMessage) const {
    // Même écriture atomique que le reste du projet : on écrit à côté
    // puis on renomme, pour ne jamais laisser un fichier à moitié écrit.
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    const std::string bytes = build();
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) {
                *errorMessage = "Impossible de créer le fichier temporaire : " +
                                utils::pathToUtf8(tmp);
            }
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            if (errorMessage) {
                *errorMessage = "Échec d'écriture dans : " + utils::pathToUtf8(tmp);
            }
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec); // remplace la destination
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        if (errorMessage) {
            *errorMessage = "Impossible de renommer vers : " + utils::pathToUtf8(path);
        }
        return false;
    }
    return true;
}

} // namespace io
