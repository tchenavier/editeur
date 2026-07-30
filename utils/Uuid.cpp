// Uuid.cpp
// Génération et validation des identifiants de projet. Voir Uuid.h.

#include "Uuid.h"

#include <random>

namespace utils {

namespace {

// Source d'aléa du système, initialisée une seule fois. std::random_device
// s'appuie sur le générateur du système d'exploitation (RtlGenRandom sur
// Windows) : on ne fabrique pas notre propre aléa, et on ne dépend de rien
// d'extérieur au standard.
std::mt19937_64& generator() {
    static std::mt19937_64 engine = [] {
        std::random_device device;
        // 64 bits de graine, assemblés depuis deux tirages de 32 bits :
        // une seule valeur de 32 bits rendrait les identifiants
        // prévisibles au bout de quelques milliards de tirages.
        const uint64_t high = static_cast<uint64_t>(device());
        const uint64_t low = static_cast<uint64_t>(device());
        return std::mt19937_64((high << 32) ^ low);
    }();
    return engine;
}

char hexDigit(unsigned value) {
    static const char* kHex = "0123456789abcdef";
    return kHex[value & 0xFu];
}

} // namespace

std::string newUuid() {
    std::uniform_int_distribution<uint64_t> distribution;
    const uint64_t a = distribution(generator());
    const uint64_t b = distribution(generator());

    std::string hex;
    hex.reserve(32);
    for (int shift = 60; shift >= 0; shift -= 4) hex += hexDigit(static_cast<unsigned>(a >> shift));
    for (int shift = 60; shift >= 0; shift -= 4) hex += hexDigit(static_cast<unsigned>(b >> shift));

    // Marqueurs de la norme : version 4 (aléatoire) et variante. Sans eux
    // la chaîne resterait 32 chiffres quelconques, qu'un outil tiers
    // pourrait refuser.
    hex[12] = '4';
    hex[16] = '8';

    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

bool isValidUuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else {
            const bool digit = (c >= '0' && c <= '9');
            const bool lower = (c >= 'a' && c <= 'f');
            const bool upper = (c >= 'A' && c <= 'F'); // toléré en lecture
            if (!digit && !lower && !upper) return false;
        }
    }
    return true;
}

} // namespace utils
