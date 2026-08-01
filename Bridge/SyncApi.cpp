// SyncApi.cpp
// Les commandes sync.*. Voir SyncApi.h.

#include "SyncApi.h"

#include "DiskFileStore.h"
#include "Pairing.h"
#include "PathUtf8.h"
#include "ProjectIndex.h"
#include "SyncPlan.h"
#include "SyncRunner.h"

#include <stdexcept>

namespace fs = std::filesystem;

namespace bridge {

namespace {

// Tout ce qu'il faut pour décider, rassemblé en une fois. Les trois
// inventaires sont chargés ici et nulle part ailleurs : le plan et son
// exécution doivent raisonner sur exactement les mêmes données.
struct Contexte {
    fs::path manuscrit;
    std::string projectId;
    io::Pairing appairage;
    io::ProjectIndex base;
    io::ProjectIndex local;
    io::ProjectIndex distant;
    io::SyncPlan plan;
};

json etapeJson(const io::SyncStep& step) {
    return {{"action", io::syncActionName(step.action)},
            {"path", step.path},
            {"size", step.size}};
}

Contexte preparer(net::Connection& connection, core::Document& document,
                  const json& payload) {
    Contexte contexte;

    const std::string chemin = requireString(payload, "path");
    if (chemin.empty()) {
        throw std::runtime_error(
            "Enregistrez d'abord le manuscrit : sans fichier, il n'y a rien à synchroniser.");
    }
    contexte.manuscrit = utils::pathFromUtf8(chemin);

    if (!connection.isConnected() || connection.client() == nullptr) {
        throw std::runtime_error("Connectez-vous avant de synchroniser.");
    }

    contexte.appairage = io::loadPairing(contexte.manuscrit);
    if (!contexte.appairage.isPaired()) {
        throw std::runtime_error(
            "Ce projet n'est pas appairé. Appairez-le dans Paramètres › Compte.");
    }

    contexte.projectId = document.metadata().projectId;
    if (contexte.projectId.empty()) {
        // L'identifiant est scellé à la première sauvegarde ; s'il manque,
        // c'est que le manuscrit ouvert n'est pas celui du fichier.
        throw std::runtime_error(
            "Ce manuscrit n'a pas encore d'identifiant de projet : enregistrez-le.");
    }

    contexte.local = io::buildIndex(contexte.manuscrit, contexte.projectId);
    io::loadIndex(contexte.base, io::basePathFor(contexte.manuscrit), nullptr);

    const net::Result recu =
        connection.client()->remoteIndex(contexte.projectId, contexte.distant);
    if (!recu.ok()) throw std::runtime_error(recu.message);

    const io::SyncMode mode = (contexte.appairage.mode == io::PairingMode::TwoWay)
                                  ? io::SyncMode::TwoWay
                                  : io::SyncMode::ReadOnly;
    contexte.plan = io::planSync(contexte.base, contexte.local, contexte.distant, mode);
    return contexte;
}

json planJson(const Contexte& contexte) {
    json etapes = json::array();
    for (const io::SyncStep& step : contexte.plan.steps) etapes.push_back(etapeJson(step));

    return {{"mode", io::pairingModeName(contexte.appairage.mode)},
            {"server", contexte.appairage.server},
            {"steps", etapes},
            {"counts", {{"download", contexte.plan.count(io::SyncAction::Download)},
                        {"upload", contexte.plan.count(io::SyncAction::Upload)},
                        {"deleteLocal", contexte.plan.count(io::SyncAction::DeleteLocal)},
                        {"deleteRemote", contexte.plan.count(io::SyncAction::DeleteRemote)},
                        {"conflict", contexte.plan.count(io::SyncAction::Conflict)},
                        {"blocked", contexte.plan.count(io::SyncAction::Blocked)}}},
            {"bytes", contexte.plan.bytesToTransfer()},
            {"upToDate", contexte.plan.empty()}};
}

} // namespace

SyncApi::SyncApi(net::Connection& connection, core::Document& document)
    : m_connection(connection), m_document(document) {}

bool SyncApi::registerOn(Bridge& bridge) {
    bool ok = bridge.registerCommand("sync.plan", [this](const json& p) { return plan(p); });
    ok &= bridge.registerCommand("sync.run", [this](const json& p) { return run(p); });
    return ok;
}

json SyncApi::plan(const json& payload) {
    const Contexte contexte = preparer(m_connection, m_document, payload);
    return planJson(contexte);
}

json SyncApi::run(const json& payload) {
    // Le plan est RECALCULÉ : celui que l'auteur a vu peut dater de
    // plusieurs minutes. Et même ce plan-ci peut vieillir entre son calcul
    // et son exécution — c'est « If-Match » qui aura le dernier mot,
    // transformant une course en conflit visible plutôt qu'en écrasement.
    const Contexte contexte = preparer(m_connection, m_document, payload);

    net::DiskFileStore fichiers(contexte.manuscrit.parent_path());
    const net::SyncReport rapport =
        net::runSync(*m_connection.client(), contexte.projectId, contexte.plan, fichiers);

    // Le témoin n'avance QUE sur ce qui a abouti. Une synchronisation
    // interrompue laisse donc un état exact, et le prochain plan retrouve
    // tout seul ce qui reste à faire.
    std::string erreurBase;
    const io::ProjectIndex nouvelleBase =
        io::advanceBase(contexte.base, contexte.local, contexte.distant,
                        contexte.plan, rapport.done);
    const bool baseEcrite =
        io::saveIndex(nouvelleBase, io::basePathFor(contexte.manuscrit), &erreurBase);

    json soucis = json::array();
    for (const net::SyncProblem& probleme : rapport.problems) {
        soucis.push_back({{"path", probleme.path},
                          {"message", probleme.message},
                          {"conflict", probleme.conflict}});
    }

    return {{"downloaded", rapport.downloaded},
            {"uploaded", rapport.uploaded},
            {"deletedLocal", rapport.deletedLocal},
            {"deletedRemote", rapport.deletedRemote},
            {"conflicts", rapport.conflicts},
            {"blocked", rapport.blocked},
            {"failed", rapport.failed},
            {"changed", rapport.changedFiles()},
            {"interrupted", rapport.interrupted},
            {"interruption", rapport.interruption},
            {"clean", rapport.clean()},
            // Un manuscrit reçu remplace celui qui est à l'écran : le JS
            // doit le recharger, et il faut le lui dire explicitement.
            {"reloadNeeded", rapport.downloaded > 0 || rapport.deletedLocal > 0},
            {"baseSaved", baseEcrite},
            {"baseError", baseEcrite ? std::string() : erreurBase},
            {"problems", soucis}};
}

} // namespace bridge
