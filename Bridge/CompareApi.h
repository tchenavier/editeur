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
//   compare.reparagraph -> { path }
//       Rend ses paragraphes au manuscrit ouvert en suivant la structure
//       du fichier désigné. Pour le cas — fréquent — où un correcteur
//       extérieur rend le texte corrigé d'un seul tenant. Le calcul est
//       dans core/Reflow ; l'opération s'annule d'un Ctrl+Z.
//
// Convention : la GAUCHE est l'autre version (le fichier), la DROITE est
// le manuscrit ouvert. Elle vaut pour les deux usages :
//   - à la demande, on relit sa version corrigée (à droite) en regard de
//     la précédente ;
//   - en cas de conflit, on voit ce que contient le fichier sur le disque
//     (à gauche) avant de décider de l'écraser.
//
// DEUX UNITÉS DE COMPARAISON. Par défaut on apparie les paragraphes :
// c'est ce qui se lit le mieux. Mais quand un côté a perdu son découpage,
// un bloc unique ne peut s'apparier avec aucun des cent d'en face, et tout
// ressort comme réécrit. La commande MESURE donc ce que l'appariement
// retrouve ; si c'est trop peu, elle recommence phrase par phrase et garde
// le meilleur des deux. Le résultat annonce l'unité retenue et le côté
// aplati, pour que l'interface le dise et propose le redécoupage.
//
// Le calcul lui-même est dans core/Diff et core/Reflow : ce module ne fait
// que charger l'autre version, choisir l'unité et mettre en forme.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"
#include "UndoManager.h"

namespace bridge {

class CompareApi {
public:
    // 'document' et 'undo' doivent rester valides aussi longtemps que le
    // Bridge (le redécoupage modifie le manuscrit, donc l'historique).
    CompareApi(core::Document& document, core::UndoManager& undo);

    bool registerOn(Bridge& bridge);

    // Garde-fou : au-delà, on renvoie les statistiques sans le détail.
    // Un manuscrit de cette taille produirait plusieurs mégaoctets de
    // JSON, que l'interface ne pourrait de toute façon pas afficher
    // utilement d'un seul tenant.
    static constexpr size_t kMaxRows = 20000;

    // En dessous de cette part de texte retrouvée à l'identique,
    // l'appariement des paragraphes est jugé sans valeur et la comparaison
    // est retentée phrase par phrase. La moitié est un seuil confortable :
    // une relecture ordinaire retrouve plus de 90 % du texte, une structure
    // perdue en retrouve moins de 5 %.
    static constexpr double kStructureThreshold = 0.5;

    // Part des phrases tenant dans un seul bloc au-delà de laquelle une
    // version est dite « aplatie » (voir core::flatness).
    static constexpr double kFlatThreshold = 0.5;

private:
    core::Document& m_document;
    core::UndoManager& m_undo;

    json withFile(const json& payload);
    json reparagraph(const json& payload);
};

} // namespace bridge
