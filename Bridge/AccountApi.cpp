// AccountApi.cpp
// Comptes et appairage. Voir AccountApi.h.

#include "AccountApi.h"

#include "Pairing.h"
#include "PathUtf8.h"

#include <stdexcept>

namespace bridge {

namespace {

// Le manuscrit courant sur le disque, ou une chaîne vide s'il n'a jamais
// été enregistré. L'appairage a besoin d'un dossier où ranger sa
// décision : sans fichier, pas d'appairage possible — et c'est une
// information utile pour l'interface, pas une erreur.
std::string manuscriptPath(const json& payload) {
    return optString(payload, "path");
}

const char* outcomeName(net::Outcome outcome) {
    switch (outcome) {
        case net::Outcome::Ok:           return "ok";
        case net::Outcome::Unreachable:  return "injoignable";
        case net::Outcome::Unauthorized: return "refus";
        case net::Outcome::Forbidden:    return "interdit";
        case net::Outcome::NotFound:     return "introuvable";
        case net::Outcome::Conflict:     return "conflit";
        case net::Outcome::TooLarge:     return "trop-gros";
        case net::Outcome::Busy:         return "occupe";
        case net::Outcome::ServerError:  return "panne-serveur";
        case net::Outcome::BadResponse:  return "reponse-invalide";
        case net::Outcome::Refused:      break;
    }
    return "refuse";
}

} // namespace

AccountApi::AccountApi(net::Connection& connection, core::Document& document)
    : m_connection(connection), m_document(document) {}

bool AccountApi::registerOn(Bridge& bridge) {
    bool ok = bridge.registerCommand("account.state",
                                     [this](const json& p) { return state(p); });
    ok &= bridge.registerCommand("account.signIn", [this](const json& p) { return signIn(p); });
    ok &= bridge.registerCommand("account.signOut", [this](const json& p) { return signOut(p); });
    ok &= bridge.registerCommand("account.pair", [this](const json& p) { return pair(p); });
    ok &= bridge.registerCommand("account.unpair", [this](const json& p) { return unpair(p); });
    ok &= bridge.registerCommand("account.never", [this](const json& p) { return never(p); });
    ok &= bridge.registerCommand("account.setAutoSync",
                                 [this](const json& p) { return setAutoSync(p); });
    return ok;
}

json AccountApi::pairingJson(const std::string& path) const {
    if (path.empty()) {
        // Manuscrit jamais enregistré : rien à proposer, et il faut le
        // dire clairement plutôt que de faire croire à un refus.
        return {{"state", "sans-fichier"},
                {"mode", "lecture-seule"},
                {"autoSync", false},
                {"server", ""},
                {"canPropose", false}};
    }
    const io::Pairing pairing = io::loadPairing(utils::pathFromUtf8(path));
    return {{"state", io::pairingStateName(pairing.state)},
            {"mode", io::pairingModeName(pairing.mode)},
            {"autoSync", pairing.autoSync},
            {"server", pairing.server},
            {"canPropose", pairing.canPropose()}};
}

json AccountApi::state(const json& payload) {
    // AUCUN accès réseau ici. C'est la commande que l'interface appelle
    // au démarrage : elle doit être aussi gratuite qu'une lecture de
    // variable, sans quoi un auteur hors ligne paierait pour un service
    // qu'il n'utilise pas.
    return {{"connected", m_connection.isConnected()},
            {"server", m_connection.serverUrl()},
            {"user", m_connection.userName()},
            {"projectId", m_document.metadata().projectId},
            {"pairing", pairingJson(manuscriptPath(payload))}};
}

json AccountApi::signIn(const json& payload) {
    const std::string server = requireString(payload, "server");
    const std::string user = requireString(payload, "user");
    std::string secret = requireString(payload, "secret");

    // signIn efface le secret qu'on lui passe : l'exemplaire de cette
    // fonction ne survit pas à l'appel. Celui du JSON, en revanche, nous
    // échappe — d'où la consigne, côté interface, de ne jamais le garder
    // dans un champ après envoi.
    const net::Result result = m_connection.signIn(server, user, secret);
    if (!result.ok()) {
        return {{"connected", false},
                {"reason", outcomeName(result.outcome)},
                {"message", result.message},
                {"retryable", result.worthRetrying()}};
    }

    return {{"connected", true},
            {"server", m_connection.serverUrl()},
            {"user", m_connection.userName()},
            {"pairing", pairingJson(manuscriptPath(payload))}};
}

json AccountApi::signOut(const json& payload) {
    (void)payload;
    m_connection.signOut();
    // Vérification explicite plutôt qu'acte de foi : après une
    // déconnexion, plus aucun transport ne doit exister.
    return {{"connected", m_connection.isConnected()},
            {"transportReleased", !m_connection.hasTransport()}};
}

json AccountApi::pair(const json& payload) {
    const std::string path = manuscriptPath(payload);
    if (path.empty()) {
        throw std::runtime_error(
            "Enregistrez d'abord le manuscrit : l'appairage se range à côté du fichier.");
    }
    if (!m_connection.isConnected()) {
        throw std::runtime_error("Connectez-vous avant d'appairer ce projet.");
    }

    const std::string mode = optString(payload, "mode", "lecture-seule");
    io::Pairing pairing = io::loadPairing(utils::pathFromUtf8(path));
    pairing.state = io::PairingState::Paired;
    pairing.mode = (mode == "deux-sens") ? io::PairingMode::TwoWay : io::PairingMode::ReadOnly;
    pairing.server = m_connection.serverUrl();
    // La synchronisation automatique n'est pas activée par l'appairage :
    // appairer dit « ce projet peut être synchronisé », pas « envoyez-le
    // sans me demander ».
    pairing.autoSync = false;

    std::string error;
    if (!io::savePairing(pairing, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }
    return {{"pairing", pairingJson(path)}};
}

json AccountApi::unpair(const json& payload) {
    const std::string path = manuscriptPath(payload);
    if (path.empty()) throw std::runtime_error("Aucun manuscrit enregistré.");

    io::Pairing pairing = io::loadPairing(utils::pathFromUtf8(path));
    // On revient à « aucun », pas à « jamais » : désappairer n'est pas
    // refuser pour toujours. La question pourra donc être reposée.
    pairing.state = io::PairingState::None;
    pairing.server.clear();
    pairing.autoSync = false;

    std::string error;
    if (!io::savePairing(pairing, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }
    return {{"pairing", pairingJson(path)}};
}

json AccountApi::never(const json& payload) {
    const std::string path = manuscriptPath(payload);
    if (path.empty()) throw std::runtime_error("Aucun manuscrit enregistré.");

    io::Pairing pairing = io::loadPairing(utils::pathFromUtf8(path));
    pairing.state = io::PairingState::Never;
    pairing.server.clear();
    pairing.autoSync = false;

    std::string error;
    if (!io::savePairing(pairing, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }
    return {{"pairing", pairingJson(path)}};
}

json AccountApi::setAutoSync(const json& payload) {
    const std::string path = manuscriptPath(payload);
    if (path.empty()) throw std::runtime_error("Aucun manuscrit enregistré.");

    io::Pairing pairing = io::loadPairing(utils::pathFromUtf8(path));
    if (!pairing.isPaired()) {
        throw std::runtime_error(
            "Ce projet n'est pas appairé : il n'y a rien à synchroniser automatiquement.");
    }
    pairing.autoSync = optBool(payload, "enabled", false);

    std::string error;
    if (!io::savePairing(pairing, utils::pathFromUtf8(path), &error)) {
        throw std::runtime_error(error);
    }
    return {{"pairing", pairingJson(path)}};
}

} // namespace bridge
