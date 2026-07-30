// PathUtf8.cpp
// Implémentation des conversions chemin <-> UTF-8. Voir PathUtf8.h pour
// la justification (encodage natif ANSI sur Windows, u8path déprécié,
// char8_t absent en C++17).

#include "PathUtf8.h"

#include "Utf8.h"

#include <system_error>

namespace utils {

std::string fileModToken(const std::filesystem::path& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return std::string(); // fichier absent ou illisible
    // La représentation exacte importe peu : on n'a besoin que de comparer
    // deux jetons par égalité. On sérialise le compte depuis l'époque.
    return std::to_string(t.time_since_epoch().count());
}

#ifdef _WIN32

namespace {

constexpr char32_t kReplacement = 0xFFFD; // caractère de remplacement U+FFFD

// UTF-32 -> UTF-16 (encodage natif des chemins Windows).
std::wstring u32ToWide(const std::u32string& codepoints) {
    std::wstring out;
    out.reserve(codepoints.size() + 8);
    for (char32_t cp : codepoints) {
        // Un codepoint invalide ne doit jamais produire un chemin corrompu.
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
        if (cp < 0x10000) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

// UTF-16 -> UTF-32, en recomposant les paires de substitution.
std::u32string wideToU32(const std::wstring& wide) {
    std::u32string out;
    out.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        const char32_t unit = static_cast<char32_t>(static_cast<unsigned short>(wide[i]));
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < wide.size()) {
            const char32_t low = static_cast<char32_t>(static_cast<unsigned short>(wide[i + 1]));
            if (low >= 0xDC00 && low <= 0xDFFF) {
                out.push_back(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                ++i; // la paire est consommée
                continue;
            }
        }
        // Substitut isolé (chemin malformé) : on remplace plutôt que de
        // produire de l'UTF-8 invalide qui casserait le JSON du pont.
        out.push_back((unit >= 0xD800 && unit <= 0xDFFF) ? kReplacement : unit);
    }
    return out;
}

} // namespace

std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(u32ToWide(utf8ToU32(utf8)));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return u32ToUtf8(wideToU32(path.wstring()));
}

#else // POSIX : les chemins sont des octets, déjà en UTF-8 par convention.

std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(utf8);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return path.string();
}

#endif

} // namespace utils
