// Roadmap.h
// -----------------------------------------------------------------------
// La feuille de route du roman : ce qui est prévu, et quand.
//
// Une liste ORDONNÉE de parties. Chacune porte un titre, un résumé libre,
// un état d'avancement, éventuellement une échéance, un chapitre du
// manuscrit auquel elle se rattache, et les fiches (personnages, lieux…)
// qui la concernent.
//
// LA PARTIE EXISTE D'ABORD TOUTE SEULE. On planifie ce qui n'est pas
// encore écrit — c'est même l'usage principal : noter une scène avant
// d'avoir le chapitre qui l'accueillera. Le rattachement à un chapitre est
// FACULTATIF, et c'est lui qui permet ensuite d'afficher un avancement
// réel plutôt qu'une case à cocher.
//
// OÙ C'EST RANGÉ : dans un fichier voisin du manuscrit, comme les fiches
// et le dictionnaire personnel. Conséquence voulue : la feuille de route
// VOYAGE AVEC LE PROJET, se sauvegarde avec lui, et se synchronise avec
// lui sans une ligne de code de plus — l'inventaire la ramasse comme
// n'importe quel autre fichier du dossier.
//
// C'est aussi ce qui corrige un défaut des rappels de la première version,
// qui vivaient dans le navigateur : ils ne suivaient pas le manuscrit, et
// s'affichaient donc pour le mauvais roman dès qu'on en ouvrait un autre.
//
// Le format est en clair et se relit à l'œil nu : l'auteur doit pouvoir
// ouvrir ce fichier et comprendre son propre plan sans l'éditeur.
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace io {

enum class PartState {
    Planned,    // prévu, pas commencé
    InProgress, // en cours d'écriture
    Done,       // terminé
};

const char* partStateName(PartState state);
bool partStateFromName(const std::string& name, PartState& out);

struct RoadmapPart {
    // Identifiant stable, attribué à la création. Il sert aux liens et
    // survit à un renommage du titre — sans quoi renommer une partie
    // casserait tout ce qui la désigne.
    std::string id;

    std::string title;
    std::string summary; // texte libre, éventuellement sur plusieurs lignes
    PartState state = PartState::Planned;

    // Échéance, ou chaîne vide. Deux formes acceptées, et la différence
    // compte :
    //   « AAAA-MM-JJ »        : une date. On prévient le jour même.
    //   « AAAA-MM-JJTHH:MM »  : un moment précis. On prévient au bout du
    //                           préavis (une heure par défaut).
    // Une date seule ne peut pas déclencher un préavis d'une heure qui ait
    // un sens : « une heure avant le 15 mars » ne veut rien dire. On ne
    // devine donc pas une heure de rendu — c'est à l'auteur de la donner
    // s'il la veut.
    std::string deadline;

    // Titre du chapitre rattaché, ou vide. On garde le TITRE et non un
    // indice : les chapitres se réorganisent par glisser-déposer, et un
    // indice désignerait le mauvais dès le premier déplacement.
    std::string chapter;

    // Identifiants des fiches rattachées (personnages, lieux…).
    std::vector<std::string> cards;

    // Rappel supplémentaire, en minutes AVANT l'échéance, en plus du
    // préavis habituel. Zéro = aucun. C'est le « et un rappel de plus »
    // demandé : prévenir la veille en plus de l'heure d'avant.
    uint32_t extraLeadMinutes = 0;

    // Rappels déjà donnés, pour ne pas les répéter à chaque vérification.
    // Ce sont des faits d'usage, pas du contenu : ils sont enregistrés
    // avec le reste, mais leur perte n'a aucune conséquence fâcheuse.
    bool notified = false;
    bool extraNotified = false;

    bool hasDeadline() const { return !deadline.empty(); }
    bool isDone() const { return state == PartState::Done; }
};

struct Roadmap {
    std::vector<RoadmapPart> parts; // l'ordre est celui voulu par l'auteur

    const RoadmapPart* find(const std::string& id) const;
    RoadmapPart* find(const std::string& id);

    // Déplace la partie d'un rang vers le haut ou vers le bas. Sans effet
    // aux extrémités : réorganiser ne doit jamais faire disparaître une
    // partie ni en dupliquer une.
    bool move(const std::string& id, int delta);
};

// Emplacement : un fichier voisin du manuscrit, au nom explicite, comme
// « <nom>_fiches » et « <nom>_backups ». Rien de caché.
std::filesystem::path roadmapPathFor(const std::filesystem::path& manuscriptPath);

// --- Sérialisation ----------------------------------------------------
// Pur calcul, donc testable sans disque.
std::string serializeRoadmap(const Roadmap& roadmap);

// Lecture TOLÉRANTE : une partie abîmée est ignorée, le reste est gardé.
// Perdre tout son plan parce qu'une ligne est fausse serait le pire
// résultat possible.
Roadmap parseRoadmap(const std::string& text);

Roadmap loadRoadmap(const std::filesystem::path& manuscriptPath);
bool saveRoadmap(const Roadmap& roadmap, const std::filesystem::path& manuscriptPath,
                 std::string* errorMessage = nullptr);

// --- Échéances --------------------------------------------------------

// Ce qu'une échéance vaut à un instant donné.
enum class DueState {
    None,     // aucune échéance
    Far,      // encore loin
    Soon,     // dans le préavis : c'est le moment de prévenir
    Today,    // échéance à la date du jour (forme sans heure)
    Late,     // dépassée
};

// 'now' est un instant en secondes depuis l'époque Unix, en temps LOCAL
// converti par l'appelant (voir Bridge/RoadmapApi). Passer l'instant en
// argument plutôt que de lire l'horloge rend cette fonction testable : on
// peut se placer une minute avant une échéance sans attendre.
DueState dueStateOf(const RoadmapPart& part, int64_t nowLocal, uint32_t leadMinutes);

// Le rappel SUPPLÉMENTAIRE de cette partie doit-il être donné ? Séparé de
// dueStateOf parce qu'il répond à une autre question : « prévenez-moi
// aussi la veille » n'est pas un état d'échéance, c'est une demande de
// l'auteur qui peut tomber bien avant que quoi que ce soit soit urgent.
bool extraReminderDue(const RoadmapPart& part, int64_t nowLocal);

// Analyse « AAAA-MM-JJ » ou « AAAA-MM-JJTHH:MM » en secondes depuis
// l'époque, dans le même repère que 'nowLocal'. Retourne false si la
// chaîne n'est pas une échéance valide.
bool parseDeadline(const std::string& text, int64_t& seconds, bool& hasTime);

} // namespace io
