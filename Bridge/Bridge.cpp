// Bridge.cpp
// Implémentation du dispatcher. Voir Bridge.h pour le protocole.

#include "Bridge.h"

#include <stdexcept>

namespace bridge {

bool Bridge::registerCommand(const std::string& name, CommandHandler handler) {
    if (!handler) return false; // un gestionnaire vide serait un bug
    auto [it, inserted] = m_handlers.emplace(name, std::move(handler));
    (void)it;
    return inserted;
}

std::string Bridge::dispatch(const std::string& rawRequest) noexcept {
    // Étape 1 : parser le JSON. json::parse avec l'option "no exceptions"
    // (troisième argument à false) retourne une valeur "discarded" au
    // lieu de lancer, ce qui nous permet de garantir le noexcept.
    json request = json::parse(rawRequest, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (request.is_discarded() || !request.is_object()) {
        return makeError("Requete JSON invalide.");
    }

    // Étape 2 : valider la structure minimale attendue.
    if (!request.contains("command") || !request["command"].is_string()) {
        return makeError("Champ 'command' manquant ou invalide.");
    }
    const std::string command = request["command"].get<std::string>();

    // Le payload est optionnel (certaines commandes n'ont pas de
    // paramètres). Un payload absent OU null est traité comme vide :
    // le JS produit facilement null (ex: JSON.stringify d'une variable
    // non initialisée), inutile d'en faire une erreur. En revanche un
    // payload d'un autre type (chaîne, nombre...) est un vrai bug
    // d'appel et reste rejeté.
    json payload = json::object();
    if (request.contains("payload") && !request["payload"].is_null()) {
        if (!request["payload"].is_object()) {
            return makeError("Champ 'payload' invalide (objet attendu).");
        }
        payload = request["payload"];
    }

    // Étape 3 : trouver le gestionnaire.
    auto it = m_handlers.find(command);
    if (it == m_handlers.end()) {
        // On renvoie le nom pour faciliter le débogage côté JS, mais
        // rien d'autre (pas de liste des commandes existantes : inutile
        // de renseigner un éventuel code malveillant sur la surface
        // d'attaque disponible).
        return makeError("Commande inconnue : " + command);
    }

    // Étape 4 : exécuter, en convertissant toute exception en erreur
    // propre. Un bug dans un gestionnaire ne doit jamais faire tomber
    // l'application entière (et donc perdre le travail de l'auteur).
    try {
        json result = it->second(payload);
        return makeSuccess(result);
    } catch (const std::exception& e) {
        return makeError(e.what());
    } catch (...) {
        return makeError("Erreur interne inattendue.");
    }
}

std::string Bridge::makeError(const std::string& message) {
    json response = {{"ok", false}, {"error", message}};
    return response.dump();
}

std::string Bridge::makeSuccess(const json& result) {
    json response = {{"ok", true}, {"result", result}};
    return response.dump();
}

// -------------------------------------------------------------------
// Aides de validation partagées (voir Bridge.h).
// -------------------------------------------------------------------

std::string requireString(const json& payload, const char* field) {
    if (!payload.contains(field) || !payload[field].is_string()) {
        throw std::runtime_error(std::string("Champ manquant ou invalide : ") + field);
    }
    return payload[field].get<std::string>();
}

size_t requireIndex(const json& payload, const char* field) {
    if (!payload.contains(field)) {
        throw std::runtime_error(std::string("Champ manquant : ") + field);
    }
    const auto& value = payload[field];
    // On exige un entier non signé natif : un nombre négatif ou un
    // flottant est le signe d'un bug côté JS, autant le détecter tôt.
    if (!value.is_number_unsigned()) {
        throw std::runtime_error(std::string("Champ invalide (entier >= 0 attendu) : ") + field);
    }
    return value.get<size_t>();
}

std::string optString(const json& payload, const char* field,
                      const std::string& fallback) {
    if (payload.contains(field) && payload[field].is_string()) {
        return payload[field].get<std::string>();
    }
    return fallback;
}

bool optBool(const json& payload, const char* field, bool fallback) {
    if (payload.contains(field) && payload[field].is_boolean()) {
        return payload[field].get<bool>();
    }
    return fallback;
}

size_t optIndex(const json& payload, const char* field, size_t fallback) {
    if (payload.contains(field) && payload[field].is_number_unsigned()) {
        return payload[field].get<size_t>();
    }
    return fallback;
}

} // namespace bridge
