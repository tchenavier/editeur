// Sha256.h
// -----------------------------------------------------------------------
// Empreinte SHA-256, écrite à la main (Phase 5, jalon A).
//
// À QUOI ÇA SERT ICI : l'inventaire du projet doit répondre à « ce fichier
// a-t-il VRAIMENT changé ? ». La date de modification ne suffit pas — elle
// change quand on réenregistre un fichier identique, et peut ne PAS changer
// quand deux écritures se suivent dans la même seconde. Une empreinte du
// CONTENU tranche sans ambiguïté. Elle servira aussi à la vérification
// d'intégrité prévue au cadrage pour la partie en ligne.
//
// POURQUOI PAS UNE BIBLIOTHÈQUE : même raisonnement que pour l'archive ZIP.
// SHA-256 est un algorithme figé depuis 2001, spécifié publiquement et
// vérifiable contre des vecteurs de référence officiels : cent lignes qui
// ne bougeront plus jamais, entièrement couvertes par nos tests et notre
// audit, valent mieux qu'une dépendance externe à suivre dans le temps.
//
// ATTENTION À CE QUE CE N'EST PAS : une empreinte détecte les
// modifications ACCIDENTELLES et identifie un contenu. Elle ne protège
// rien à elle seule contre un adversaire capable de modifier À LA FOIS le
// fichier et son empreinte : ce sera le rôle du transport authentifié
// (TLS) et de la signature côté serveur.
// -----------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace utils {

// Calcul en flux : on n'a jamais tout le contenu en mémoire, ce qui
// permet d'empreinter une illustration de plusieurs dizaines de Mio sans
// la charger d'un bloc.
class Sha256 {
public:
    Sha256();

    void update(const void* data, size_t length);
    void update(const std::string& data);

    // Termine le calcul et retourne les 64 chiffres hexadécimaux, en
    // minuscules. L'objet n'est plus réutilisable après cet appel (il
    // faut en construire un nouveau) : c'est volontaire, un état
    // « finalisé » réutilisé par erreur donnerait des empreintes fausses.
    std::string finalHex();

private:
    uint32_t m_state[8];
    uint64_t m_bitCount = 0;
    unsigned char m_buffer[64];
    size_t m_bufferLength = 0;
    bool m_finished = false;

    void transform(const unsigned char block[64]);
};

// Empreinte d'une chaîne d'octets (texte ou binaire, \0 compris).
std::string sha256Hex(const std::string& data);

// Empreinte du CONTENU d'un fichier, lu par morceaux. Retourne une chaîne
// vide si le fichier est illisible (et *ok, si fourni, passe à false) :
// un fichier disparu ne doit pas faire échouer tout un inventaire.
std::string sha256File(const std::filesystem::path& path, bool* ok = nullptr);

} // namespace utils
