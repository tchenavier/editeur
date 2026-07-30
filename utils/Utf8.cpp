// Utf8.cpp
// Implémentation de la conversion UTF-8 <-> UTF-32. Voir Utf8.h.

#include "Utf8.h"

namespace utils {

std::u32string utf8ToU32(const std::string& utf8) {
    std::u32string out;
    out.reserve(utf8.size()); // majorant : jamais plus de codepoints que d'octets

    const auto* bytes = reinterpret_cast<const unsigned char*>(utf8.data());
    const size_t size = utf8.size();
    size_t i = 0;

    while (i < size) {
        const unsigned char b0 = bytes[i];
        char32_t cp = U'�'; // caractère de remplacement par défaut
        size_t len = 1;

        if (b0 < 0x80) {                      // 1 octet : ASCII
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {     // 2 octets
            if (i + 1 < size && (bytes[i+1] & 0xC0) == 0x80) {
                cp = ((b0 & 0x1Fu) << 6) | (bytes[i+1] & 0x3Fu);
                len = 2;
                if (cp < 0x80) cp = U'�'; // encodage sur-long : rejeté
            }
        } else if ((b0 & 0xF0) == 0xE0) {     // 3 octets
            if (i + 2 < size && (bytes[i+1] & 0xC0) == 0x80 && (bytes[i+2] & 0xC0) == 0x80) {
                cp = ((b0 & 0x0Fu) << 12) | ((bytes[i+1] & 0x3Fu) << 6) | (bytes[i+2] & 0x3Fu);
                len = 3;
                if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = U'�';
            }
        } else if ((b0 & 0xF8) == 0xF0) {     // 4 octets
            if (i + 3 < size && (bytes[i+1] & 0xC0) == 0x80 &&
                (bytes[i+2] & 0xC0) == 0x80 && (bytes[i+3] & 0xC0) == 0x80) {
                cp = ((b0 & 0x07u) << 18) | ((bytes[i+1] & 0x3Fu) << 12) |
                     ((bytes[i+2] & 0x3Fu) << 6) | (bytes[i+3] & 0x3Fu);
                len = 4;
                if (cp < 0x10000 || cp > 0x10FFFF) cp = U'�';
            }
        }
        // Si la séquence était invalide, cp reste U+FFFD et len avance
        // d'un seul octet : on resynchronise sans boucler à l'infini.
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string u32ToUtf8(const std::u32string& u32) {
    std::string out;
    out.reserve(u32.size() * 2); // estimation raisonnable pour du français

    for (char32_t cp : u32) {
        // Un codepoint invalide dans le modèle ne devrait pas exister,
        // mais on le remplace par sécurité plutôt que d'émettre de
        // l'UTF-8 corrompu qui casserait le JSON côté JS.
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = U'�';

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace utils
