// Pairing.h
// -----------------------------------------------------------------------
// L'appairage d'un projet : la réponse de l'auteur à « voulez-vous
// synchroniser ce manuscrit ? », et rien d'autre.
//
// TROIS RÉPONSES, parce que deux ne suffisent pas. « Oui » et « non » ne
// disent pas la même chose qu'« oui », « pas maintenant » et « plus
// jamais pour celui-ci ». Sans le troisième, l'application redemanderait
// à chaque connexion pour un projet que l'auteur ne veut délibérément pas
// envoyer — et une question posée en boucle finit par être acceptée par
// lassitude, ce qui n'est pas un consentement.
//
// OÙ C'EST RANGÉ, et pourquoi : dans le dossier de synchronisation du
// projet, à côté de l'inventaire et du journal. La décision VOYAGE donc
// avec le manuscrit — copier son projet sur une autre machine y emmène le
// « jamais », et c'est bien le comportement attendu. Un réglage global
// dirait le contraire de ce que l'auteur a demandé projet par projet.
//
// Format en clair, lisible et modifiable à la main : l'auteur doit
// pouvoir ouvrir ce fichier et comprendre ce qu'il dit de lui.
//
// CE QUI N'EST PAS ICI : aucun secret. Ni jeton, ni mot de passe. Ce
// fichier peut être lu par n'importe qui sans conséquence, et c'est
// délibéré — voir net/Connection.h sur le sort du jeton.
// -----------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <string>

namespace io {

enum class PairingState {
    None,   // jamais répondu : la question peut être posée
    Paired, // appairé à un serveur
    Never,  // « jamais pour ce projet » : ne plus demander
};

// Sens de la synchronisation. Lecture seule d'abord, conformément à la
// décision du 26 juillet 2026 : on récupère, on n'envoie pas.
enum class PairingMode { ReadOnly, TwoWay };

struct Pairing {
    PairingState state = PairingState::None;
    PairingMode mode = PairingMode::ReadOnly;
    std::string server;    // adresse du serveur appairé
    std::string pairedAt;  // « AAAA-MM-JJTHH:MM:SSZ », informatif

    // La synchronisation automatique est DÉSACTIVÉE par défaut, et c'est
    // un choix : rien ne part sans que l'auteur l'ait demandé, même une
    // fois le projet appairé.
    bool autoSync = false;

    bool canPropose() const { return state == PairingState::None; }
    bool isPaired() const { return state == PairingState::Paired; }
};

const char* pairingStateName(PairingState state);
const char* pairingModeName(PairingMode mode);

std::filesystem::path pairingPathFor(const std::filesystem::path& manuscriptPath);

// Lecture tolérante : un fichier absent, vide ou abîmé rend un appairage
// « None ». Ne jamais faire échouer l'ouverture d'un manuscrit pour un
// fichier technique illisible — au pire, la question sera reposée.
Pairing loadPairing(const std::filesystem::path& manuscriptPath);

// Écriture atomique (fichier temporaire puis renommage), comme le reste
// du projet : une coupure ne doit pas laisser un appairage à moitié écrit
// qui ferait croire à un « jamais » jamais demandé.
bool savePairing(const Pairing& pairing, const std::filesystem::path& manuscriptPath,
                 std::string* errorMessage = nullptr);

// Sérialisation exposée pour les tests : c'est du pur calcul.
std::string serializePairing(const Pairing& pairing);
Pairing parsePairing(const std::string& text);

} // namespace io
