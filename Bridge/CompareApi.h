// CompareApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "compare.*" : comparer le manuscrit ouvert à
// une autre version.
//
// Commandes exposées :
//   compare.withFile -> { path }
//       Compare le manuscrit EN MÉMOIRE au fichier désigné. Retourne des
//       lignes APPARIÉES, prêtes à être affichées côte à côte, chacune
//       portant le texte des deux versions et les plages de mots à mettre
//       en évidence.
//
// Convention : la GAUCHE est l'autre version (le fichier), la DROITE est
// le manuscrit ouvert. Elle vaut pour les deux usages :
//   - à la demande, on relit sa version corrigée (à droite) en regard de
//     la précédente ;
//   - en cas de conflit, on voit ce que contient le fichier sur le disque
//     (à gauche) avant de décider de l'écraser.
//
// Le calcul lui-même est dans core/Diff : ce module ne fait que charger
// l'autre version, apparier les blocs et mettre le résultat en forme.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"

namespace bridge {

class CompareApi {
public:
    // 'document' doit rester valide aussi longtemps que le Bridge.
    explicit CompareApi(core::Document& document);

    bool registerOn(Bridge& bridge);

    // Garde-fou : au-delà, on renvoie les statistiques sans le détail.
    // Un manuscrit de cette taille produirait plusieurs mégaoctets de
    // JSON, que l'interface ne pourrait de toute façon pas afficher
    // utilement d'un seul tenant.
    static constexpr size_t kMaxRows = 20000;

private:
    core::Document& m_document;

    json withFile(const json& payload);
};

} // namespace bridge
