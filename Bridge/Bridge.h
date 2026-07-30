// Bridge.h
// -----------------------------------------------------------------------
// Pont C++ <-> JS : couche de dispatch des commandes.
//
// Principe central : cette classe ne sait PAS qu'un webview existe.
// Elle reçoit une chaîne JSON (une "commande"), la valide, appelle le
// bon gestionnaire, et retourne une chaîne JSON (la "réponse").
//
// Pourquoi cette séparation :
//   - Testable sans interface graphique (on appelle dispatch() directement
//     dans les tests, sans webview ni moteur de rendu).
//   - Si un jour le transport change (webview remplacé, version navigateur
//     de la Phase 5 via WebSocket...), seule la couche de glue change,
//     jamais la logique des commandes.
//   - C'est la même philosophie que la séparation core/ui du reste du
//     projet, appliquée à la communication.
//
// Protocole (côté JS -> C++) :
//   { "command": "editor.insertText", "payload": { ... } }
// Réponse (C++ -> JS) :
//   { "ok": true,  "result": { ... } }            en cas de succès
//   { "ok": false, "error": "message lisible" }   en cas d'échec
//
// Sécurité : tout ce qui arrive du JS est traité comme non fiable
// (JSON malformé, commande inconnue, champs manquants, types invalides).
// Aucune de ces situations ne doit faire planter l'application : elles
// produisent une réponse d'erreur propre, point.
// -----------------------------------------------------------------------

#pragma once

#include <functional>
#include <map>
#include <string>

#include "json.hpp"

namespace bridge {

using json = nlohmann::json;

// Signature d'un gestionnaire de commande : reçoit le payload (déjà parsé
// et garanti d'être un objet JSON), retourne le résultat. En cas d'erreur
// métier, le gestionnaire lance une std::runtime_error avec un message
// lisible : le Bridge la convertit en réponse { ok: false, error: ... }.
using CommandHandler = std::function<json(const json& payload)>;

class Bridge {
public:
    // Enregistre un gestionnaire pour un nom de commande.
    // Convention de nommage : "module.action" (ex: "editor.insertText"),
    // pour garder les commandes groupées et lisibles quand elles seront
    // nombreuses (fiches, export, etc.).
    // Retourne false si le nom est déjà pris (on refuse d'écraser en
    // silence : un doublon est presque toujours un bug de câblage).
    bool registerCommand(const std::string& name, CommandHandler handler);

    // Point d'entrée unique : traite une requête JSON brute venue du JS
    // et retourne la réponse JSON sérialisée.
    // Ne lance JAMAIS d'exception : toute erreur (parse, commande
    // inconnue, exception d'un gestionnaire) devient une réponse
    // { ok: false, error: ... }.
    std::string dispatch(const std::string& rawRequest) noexcept;

private:
    std::map<std::string, CommandHandler> m_handlers;

    static std::string makeError(const std::string& message);
    static std::string makeSuccess(const json& result);
};

// -------------------------------------------------------------------
// Aides de validation des payloads, PARTAGÉES par tous les modules
// *Api (editor.*, file.*, cards.*, export.*).
//
// Elles étaient au départ recopiées dans chaque module ; à quatre
// modules, la duplication devenait le vrai risque (un message d'erreur
// corrigé à un seul endroit, une vérification oubliée dans le suivant).
// Les versions « require » lancent une std::runtime_error que le Bridge
// transforme en réponse { ok:false, error }. Les versions « opt »
// retournent la valeur par défaut si le champ est absent OU du mauvais
// type : un paramètre facultatif mal formé ne doit jamais faire échouer
// une commande par ailleurs valide.
// -------------------------------------------------------------------
std::string requireString(const json& payload, const char* field);
size_t      requireIndex(const json& payload, const char* field);
std::string optString(const json& payload, const char* field,
                      const std::string& fallback = std::string());
bool        optBool(const json& payload, const char* field, bool fallback = false);
size_t      optIndex(const json& payload, const char* field, size_t fallback);

} // namespace bridge
