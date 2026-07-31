// SpellApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "spell.*" : correcteur orthographique et
// dictionnaire de synonymes (fin de la Phase 1).
//
// Commandes exposées :
//   spell.state      -> {}  dictionnaires disponibles ? chargés ? combien
//                       de formes ? mots du dictionnaire personnel.
//   spell.load       -> { path? }  charge les dictionnaires (opération
//                       longue : environ une seconde) et, si un chemin de
//                       manuscrit est fourni, le dictionnaire personnel
//                       qui l'accompagne.
//   spell.check      -> { text }  mots inconnus, avec leur position EN
//                       POINTS DE CODE et leur longueur.
//   spell.suggest    -> { word, max? }  corrections proposées.
//   spell.synonyms   -> { word }  sens et synonymes.
//   spell.addWord    -> { word, path? }  ajoute au dictionnaire personnel
//                       (et l'écrit à côté du manuscrit).
//   spell.removeWord -> { word, path? }
//   spell.ignore     -> { word }  ignoré jusqu'à la fermeture seulement.
//   spell.excludeSynonym -> { word, synonym }  déclare que ce n'est pas un
//                       synonyme. La correction est enregistrée dans un
//                       fichier VERSIONNÉ du projet et vaut dans les deux
//                       sens (voir io/Thesaurus.h pour le pourquoi).
//
// CHARGEMENT PARESSEUX : développer les 84 000 lemmes du dictionnaire
// français en 454 000 formes prend environ une seconde. Le faire au
// démarrage retarderait l'ouverture de l'éditeur pour un service dont
// l'auteur n'a pas encore besoin ; c'est donc l'interface qui déclenche
// le chargement, une fois la fenêtre affichée.
//
// DICTIONNAIRE PERSONNEL : rangé À CÔTÉ DU MANUSCRIT
// (« <nom>_dictionnaire.txt », un mot par ligne). Les noms inventés d'un
// roman appartiennent à ce roman, pas à la machine : le fichier suit donc
// le projet, se lit à l'œil nu, et entre dans l'inventaire du projet.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "Speller.h"
#include "Thesaurus.h"

#include <filesystem>

namespace bridge {

class SpellApi {
public:
    SpellApi();

    bool registerOn(Bridge& bridge);

    // Emplacement des fichiers de dictionnaire, cherché à côté de
    // l'exécutable comme l'interface l'est déjà. Chaîne vide si absent.
    static std::filesystem::path findDictionaryFolder();

    // Chemin du dictionnaire personnel associé à un manuscrit.
    static std::filesystem::path personalPathFor(const std::filesystem::path& manuscript);

    // Fichier des synonymes écartés. Il appartient au PROJET (versionné),
    // pas aux dépendances téléchargées : c'est ce qui lui permet de
    // survivre à un fetch_deps et de suivre le dépôt.
    static std::filesystem::path exclusionsPath();

private:
    core::Speller m_speller;
    io::Thesaurus m_thesaurus;
    bool m_triedLoad = false;      // pour ne pas relire à chaque appel
    std::string m_lastError;
    std::filesystem::path m_personalPath;

    json state(const json& payload);
    json load(const json& payload);
    json check(const json& payload);
    json suggest(const json& payload);
    json synonyms(const json& payload);
    json addWord(const json& payload);
    json removeWord(const json& payload);
    json ignore(const json& payload);
    json excludeSynonym(const json& payload);

    // Charge le dictionnaire personnel du manuscrit donné, et retient son
    // emplacement pour les ajouts ultérieurs.
    void loadPersonal(const std::string& manuscriptPath);
    bool savePersonal(std::string* errorMessage) const;
};

} // namespace bridge
