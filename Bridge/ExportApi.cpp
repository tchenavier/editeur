// ExportApi.cpp
// Implémentation des commandes export.*. Voir ExportApi.h.

#include "ExportApi.h"

#include "Book.h"
#include "Epub.h"
#include "PathUtf8.h"
#include "Zip.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace bridge {

namespace {

// Résout un chemin reçu du JS, éventuellement relatif à 'base' (dossier
// du manuscrit ou des fiches).
std::filesystem::path resolve(const std::string& utf8Path, const std::string& base) {
    std::filesystem::path p = utils::pathFromUtf8(utf8Path);
    if (p.is_relative() && !base.empty()) return utils::pathFromUtf8(base) / p;
    return p;
}

// Date et heure courantes, en heure locale (pour l'horodatage des
// fichiers de l'archive) et en UTC (pour dcterms:modified, que la norme
// EPUB veut en temps universel).
struct Now {
    int year, month, day, hour, minute, second;
    std::string utcStamp; // "AAAA-MM-JJTHH:MM:SSZ"
};

Now currentTime() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm utc{};
#ifdef _WIN32
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    // Tampon initialisé : si strftime échouait (résultat trop long), il
    // retourne 0 en laissant le contenu INDÉTERMINÉ — construire une
    // std::string dessus lirait alors de la mémoire non initialisée.
    char stamp[32] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return Now{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec, std::string(stamp)};
}

} // namespace

std::string base64Encode(const std::string& data) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        const uint32_t block = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8) |
                               static_cast<unsigned char>(data[i + 2]);
        out += kAlphabet[(block >> 18) & 0x3F];
        out += kAlphabet[(block >> 12) & 0x3F];
        out += kAlphabet[(block >> 6) & 0x3F];
        out += kAlphabet[block & 0x3F];
        i += 3;
    }
    // Reste de 1 ou 2 octets : complété par des '=' (le décodeur retrouve
    // ainsi la longueur exacte).
    const size_t remaining = data.size() - i;
    if (remaining == 1) {
        const uint32_t block = static_cast<unsigned char>(data[i]) << 16;
        out += kAlphabet[(block >> 18) & 0x3F];
        out += kAlphabet[(block >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        const uint32_t block = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8);
        out += kAlphabet[(block >> 18) & 0x3F];
        out += kAlphabet[(block >> 12) & 0x3F];
        out += kAlphabet[(block >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

ExportApi::ExportApi(core::Document& document,
                     core::CardLibrary& library,
                     core::SchemaRegistry& registry)
    : m_document(document), m_library(library), m_registry(registry) {}

bool ExportApi::registerOn(Bridge& bridge) {
    bool ok = true;
    ok &= bridge.registerCommand("export.epub",
        [this](const json& p) { return epub(p); });
    ok &= bridge.registerCommand("export.image",
        [this](const json& p) { return image(p); });
    return ok;
}

json ExportApi::epub(const json& payload) {
    const std::string path = requireString(payload, "path");
    const std::string kind = optString(payload, "kind", "manuscript");
    const std::string base = optString(payload, "base");

    if (kind != "manuscript" && kind != "bible") {
        throw std::runtime_error("Type d'export inconnu : " + kind);
    }

    const std::filesystem::path baseDir =
        base.empty() ? std::filesystem::path() : utils::pathFromUtf8(base);

    io::Book book = (kind == "bible")
                        ? io::bookFromCards(m_library, m_registry, baseDir)
                        : io::bookFromDocument(m_document, baseDir);

    if (kind == "bible") {
        // La bible accompagne le roman : elle en reprend le titre et
        // l'auteur, pour que les deux fichiers se rangent ensemble dans
        // une bibliothèque de liseuse.
        const auto& meta = m_document.metadata();
        if (!meta.title.empty()) book.meta.title = "Bible narrative — " + meta.title;
        book.meta.author = meta.author;
    }

    if (book.chapters.empty()) {
        throw std::runtime_error(
            kind == "bible" ? "Aucune fiche à exporter."
                            : "Le manuscrit est vide : rien à exporter.");
    }

    // Un EPUB contient un fichier par chapitre, un par image, plus une
    // poignée de fichiers fixes (package, navigation, style...). L'archive
    // ne peut pas en contenir plus de 65 535 : au-delà, on refuse
    // clairement plutôt que de livrer un livre amputé sans le dire.
    constexpr size_t kFixedFiles = 8;
    if (book.chapters.size() + book.images.size() + kFixedFiles >
        io::ZipWriter::kMaxEntries) {
        throw std::runtime_error(
            "Livre trop volumineux pour un seul fichier EPUB : trop de "
            "chapitres ou d'illustrations.");
    }

    const Now now = currentTime();
    io::EpubOptions options;
    options.titlePage = optBool(payload, "titlePage", true);
    options.tableOfContents = optBool(payload, "toc", true);
    options.modified = now.utcStamp;
    options.year = now.year;
    options.month = now.month;
    options.day = now.day;
    options.hour = now.hour;
    options.minute = now.minute;
    options.second = now.second;

    const std::filesystem::path target = utils::pathFromUtf8(path);
    std::string error;
    if (!io::writeEpub(book, target, options, &error)) {
        throw std::runtime_error(error);
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(target, ec);
    return {{"path", utils::pathToUtf8(target)},
            {"bytes", ec ? 0 : static_cast<uint64_t>(size)},
            {"chapters", book.chapters.size()},
            {"images", book.images.size()}};
}

json ExportApi::image(const json& payload) {
    const std::string path = requireString(payload, "path");
    const std::filesystem::path file = resolve(path, optString(payload, "base"));

    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) throw std::runtime_error("Image introuvable : " + utils::pathToUtf8(file));
    if (size == 0) throw std::runtime_error("Image vide : " + utils::pathToUtf8(file));
    if (size > kMaxDataUriSize) {
        throw std::runtime_error("Image trop lourde pour l'aperçu (plus de 8 Mio).");
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Image illisible : " + utils::pathToUtf8(file));
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    // Le type vient des octets, pas de l'extension : exactement la même
    // règle que pour l'embarquement dans un livre (io::sniffImageType).
    const std::string mediaType = io::sniffImageType(data);
    if (mediaType.empty()) {
        throw std::runtime_error("Ce fichier n'est pas une image PNG, JPEG ou GIF.");
    }

    return {{"dataUri", "data:" + mediaType + ";base64," + base64Encode(data)},
            {"mediaType", mediaType},
            {"bytes", static_cast<uint64_t>(data.size())}};
}

} // namespace bridge
