// test_cards.cpp
// Tests du modèle de fiches de worldbuilding (Phase 2), sans pont ni UI.
// L'accent est mis sur l'EXTENSIBILITÉ : ajouter un type de fiche ne doit
// rien casser et ne demander aucune modification du reste du modèle.

#include "CardModel.h"

#include <cassert>
#include <iostream>

using core::Card;
using core::CardLibrary;
using core::CardSchema;
using core::FieldType;
using core::FieldValue;
using core::SchemaRegistry;

int main() {
    // --- Test 1 : les six types du cadrage sont fournis ---
    {
        SchemaRegistry reg;
        for (const char* id : {"personnage", "lieu", "langue", "religion", "creature", "magie"}) {
            assert(reg.contains(id));
            assert(reg.schema(id) != nullptr);
        }
        assert(reg.all().size() == 6);

        const CardSchema* perso = reg.schema("personnage");
        assert(perso->field("relations") != nullptr);
        assert(perso->field("relations")->type == FieldType::CardRefList);
        assert(perso->field("illustrations")->type == FieldType::ImageList);
        assert(perso->field("nexiste_pas") == nullptr);
        std::cout << "Test 1 OK (schemas integres)\n";
    }

    // --- Test 2 : EXTENSIBILITÉ — un nouveau type sans toucher au modèle ---
    {
        SchemaRegistry reg;
        CardSchema vaisseau;
        vaisseau.typeId = "vaisseau";
        vaisseau.label = "Vaisseau";
        vaisseau.fields = {
            {"classe",     "Classe",     FieldType::Text},
            {"equipage",   "Équipage",   FieldType::CardRefList},
            {"armement",   "Armement",   FieldType::List},
        };
        reg.registerSchema(vaisseau);

        assert(reg.contains("vaisseau"));
        assert(reg.all().size() == 7);

        // Une fiche du nouveau type se manipule exactement comme les autres.
        Card c("v1", "vaisseau");
        c.setTitle("L'Aurore");
        c.editField("classe").setScalar("Frégate");
        c.editField("armement").add("Balistes");
        c.editField("armement").add("Feu grégeois");
        assert(c.field("classe")->scalar() == "Frégate");
        assert(c.field("armement")->size() == 2);
        std::cout << "Test 2 OK (ajout d'un type de fiche)\n";
    }

    // --- Test 3 : un schéma intégré peut être personnalisé ---
    {
        SchemaRegistry reg;
        CardSchema perso = *reg.schema("personnage");
        perso.fields.push_back({"blason", "Blason", FieldType::ImageList});
        reg.registerSchema(perso); // remplace celui de même typeId

        assert(reg.all().size() == 6); // toujours 6 types, pas un de plus
        assert(reg.schema("personnage")->field("blason") != nullptr);
        std::cout << "Test 3 OK (personnalisation d'un schema)\n";
    }

    // --- Test 4 : valeurs scalaires et listes, sans doublon ni vide ---
    {
        FieldValue v;
        assert(v.empty());
        assert(v.scalar().empty()); // lecture sûre d'un champ vide

        v.setScalar("Aria");
        assert(v.scalar() == "Aria" && v.size() == 1);

        FieldValue list;
        list.add("rouge");
        list.add("bleu");
        list.add("rouge"); // doublon ignoré
        list.add("");      // valeur vide ignorée
        assert(list.size() == 2);
        assert(list.contains("bleu"));
        assert(list.remove("rouge"));
        assert(!list.remove("rouge")); // déjà retiré
        assert(list.size() == 1);
        std::cout << "Test 4 OK (valeurs de champ)\n";
    }

    // --- Test 5 : tags transversaux ---
    {
        Card c("p1", "personnage");
        c.addTag("noblesse");
        c.addTag("chapitre-1");
        c.addTag("noblesse"); // doublon ignoré
        assert(c.tags().size() == 2);
        assert(c.hasTag("noblesse"));
        assert(c.removeTag("noblesse"));
        assert(!c.hasTag("noblesse"));
        std::cout << "Test 5 OK (tags)\n";
    }

    // --- Test 6 : bibliothèque, consultation et recherche accentuée ---
    {
        SchemaRegistry reg;
        CardLibrary lib;

        Card aria("p1", "personnage");
        aria.setTitle("Élise de Val");
        aria.addTag("noblesse");
        lib.put(std::move(aria));

        Card bran("p2", "personnage");
        bran.setTitle("Bran");
        bran.addTag("garde");
        lib.put(std::move(bran));

        Card tour("l1", "lieu");
        tour.setTitle("Tour de Brume");
        tour.addTag("noblesse");
        lib.put(std::move(tour));

        assert(lib.size() == 3);
        assert(lib.byType("personnage").size() == 2);
        assert(lib.byType("lieu").size() == 1);
        assert(lib.byTag("noblesse").size() == 2);

        // Tri par titre : "Bran" avant "Élise de Val".
        const auto persos = lib.byType("personnage");
        assert(persos[0]->title() == "Bran");

        // Recherche insensible à la casse ET aux majuscules accentuées.
        assert(lib.searchByTitle("élise").size() == 1);
        assert(lib.searchByTitle("ÉLISE").size() == 1);
        assert(lib.searchByTitle("brume").size() == 1);
        assert(lib.searchByTitle("zzz").empty());

        // "noblesse" est porté par deux fiches : allTags dédoublonne, donc
        // deux tags uniques, triés par ordre alphabétique.
        const auto tags = lib.allTags();
        assert(tags.size() == 2);
        assert(tags[0] == "garde");
        assert(tags[1] == "noblesse");
        std::cout << "Test 6 OK (bibliotheque, tri, recherche accentuee)\n";
    }

    // --- Test 7 : suppression d'une fiche -> pas de lien mort ---
    {
        SchemaRegistry reg;
        CardLibrary lib;

        Card roi("p1", "personnage");
        roi.setTitle("Le roi");
        roi.editField("relations").add("p2"); // lié à la reine
        lib.put(std::move(roi));

        Card reine("p2", "personnage");
        reine.setTitle("La reine");
        reine.editField("relations").add("p1");
        lib.put(std::move(reine));

        assert(lib.find("p1")->field("relations")->contains("p2"));

        // On supprime la reine : la référence dans la fiche du roi disparaît.
        assert(lib.remove("p2", /*purgeReferences=*/true, &reg));
        assert(lib.find("p2") == nullptr);
        assert(!lib.find("p1")->field("relations")->contains("p2"));

        // Suppression d'un identifiant inconnu : refus propre.
        assert(!lib.remove("inconnu", true, &reg));
        std::cout << "Test 7 OK (suppression sans lien mort)\n";
    }

    // --- Test 8 : la purge ne touche QUE les champs de référence ---
    {
        SchemaRegistry reg;
        CardLibrary lib;

        Card lieu("l1", "lieu");
        lieu.setTitle("Le port");
        lieu.editField("sous_lieux").add("l2");           // champ de référence
        lieu.editField("points_interet").add("l2");       // liste libre : à préserver
        lib.put(std::move(lieu));

        Card quai("l2", "lieu");
        quai.setTitle("Le quai");
        lib.put(std::move(quai));

        lib.remove("l2", true, &reg);
        const Card* port = lib.find("l1");
        assert(!port->field("sous_lieux")->contains("l2"));    // lien retiré
        assert(port->field("points_interet")->contains("l2")); // texte libre intact
        std::cout << "Test 8 OK (purge ciblee sur les references)\n";
    }

    // --- Test 9 : champs hors schéma conservés (tolérance aux évolutions) ---
    {
        SchemaRegistry reg;
        Card c("p1", "personnage");
        // Champ écrit par une version future de l'application.
        c.editField("couleur_des_yeux").setScalar("vert");
        assert(reg.schema("personnage")->field("couleur_des_yeux") == nullptr);
        // Le modèle le conserve malgré tout : on ne perd jamais de données.
        assert(c.field("couleur_des_yeux")->scalar() == "vert");
        assert(c.fields().size() == 1);
        std::cout << "Test 9 OK (champs inconnus preserves)\n";
    }

    // --- Test 10 : put() remplace la fiche de même identifiant ---
    {
        CardLibrary lib;
        Card v1("p1", "personnage");
        v1.setTitle("Ancien titre");
        lib.put(std::move(v1));

        Card v2("p1", "personnage");
        v2.setTitle("Nouveau titre");
        lib.put(std::move(v2));

        assert(lib.size() == 1);
        assert(lib.find("p1")->title() == "Nouveau titre");
        std::cout << "Test 10 OK (remplacement par identifiant)\n";
    }

    std::cout << "Tous les tests du modele de fiches sont passes.\n";
    return 0;
}
