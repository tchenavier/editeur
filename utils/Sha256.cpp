// Sha256.cpp
// Implémentation de SHA-256 (FIPS 180-4). Voir Sha256.h.
//
// Le code suit la spécification pas à pas, sans astuce d'optimisation :
// c'est un algorithme qu'on doit pouvoir relire et comparer au texte de
// référence, pas un morceau de code à rendre malin.

#include "Sha256.h"

#include <fstream>
#include <vector>

namespace utils {

namespace {

// Constantes de la spécification : parties fractionnaires des racines
// cubiques des 64 premiers nombres premiers.
const uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotateRight(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

// Taille des morceaux lus sur disque : assez grand pour que la lecture
// soit efficace, assez petit pour ne jamais peser en mémoire.
constexpr size_t kFileChunk = 64u * 1024u;

} // namespace

Sha256::Sha256() {
    // Valeurs initiales : parties fractionnaires des racines carrées des
    // huit premiers nombres premiers.
    m_state[0] = 0x6a09e667u;
    m_state[1] = 0xbb67ae85u;
    m_state[2] = 0x3c6ef372u;
    m_state[3] = 0xa54ff53au;
    m_state[4] = 0x510e527fu;
    m_state[5] = 0x9b05688cu;
    m_state[6] = 0x1f83d9abu;
    m_state[7] = 0x5be0cd19u;
    for (unsigned char& byte : m_buffer) byte = 0;
}

void Sha256::transform(const unsigned char block[64]) {
    uint32_t w[64];

    // Les 16 premiers mots viennent du bloc, en GROS-BOUTISTE (c'est la
    // spécification, indépendamment de la machine : on assemble donc les
    // octets à la main plutôt que de recopier de la mémoire).
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    // Les 48 suivants sont dérivés des précédents.
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^
                            (w[i - 15] >> 3);
        const uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^
                            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t sigma1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + sigma1 + choose + kRoundConstants[i] + w[i];
        const uint32_t sigma0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sigma0 + majority;

        h = g; g = f; f = e;
        e = d + temp1;
        d = c; c = b; b = a;
        a = temp1 + temp2;
    }

    m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
    m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
}

void Sha256::update(const void* data, size_t length) {
    if (m_finished || data == nullptr || length == 0) return;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);

    m_bitCount += static_cast<uint64_t>(length) * 8u;
    while (length > 0) {
        // On remplit le tampon de 64 octets, et on traite dès qu'il est plein.
        const size_t room = 64 - m_bufferLength;
        const size_t take = (length < room) ? length : room;
        for (size_t i = 0; i < take; ++i) {
            m_buffer[m_bufferLength + i] = bytes[i];
        }
        m_bufferLength += take;
        bytes += take;
        length -= take;
        if (m_bufferLength == 64) {
            transform(m_buffer);
            m_bufferLength = 0;
        }
    }
}

void Sha256::update(const std::string& data) {
    update(data.data(), data.size());
}

std::string Sha256::finalHex() {
    if (!m_finished) {
        // Remplissage réglementaire : un bit à 1, des zéros, puis la
        // LONGUEUR du message en bits sur 64 bits gros-boutistes.
        const uint64_t bitCount = m_bitCount;
        unsigned char one = 0x80;
        update(&one, 1);
        m_bitCount = bitCount; // update() a compté ce remplissage : on rétablit

        unsigned char zero = 0x00;
        while (m_bufferLength != 56) {
            update(&zero, 1);
            m_bitCount = bitCount;
        }

        unsigned char lengthBytes[8];
        for (int i = 0; i < 8; ++i) {
            lengthBytes[i] = static_cast<unsigned char>((bitCount >> (56 - i * 8)) & 0xFF);
        }
        update(lengthBytes, 8);
        m_finished = true;
    }

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t word : m_state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out += kHex[(word >> shift) & 0xFu];
        }
    }
    return out;
}

std::string sha256Hex(const std::string& data) {
    Sha256 hash;
    hash.update(data);
    return hash.finalHex();
}

std::string sha256File(const std::filesystem::path& path, bool* ok) {
    if (ok) *ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();

    Sha256 hash;
    std::vector<char> chunk(kFileChunk);
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize read = in.gcount();
        if (read > 0) hash.update(chunk.data(), static_cast<size_t>(read));
    }
    // eof() sans bad() = lecture menée jusqu'au bout normalement.
    if (in.bad()) return std::string();
    if (ok) *ok = true;
    return hash.finalHex();
}

} // namespace utils
