// Connection.h
// -----------------------------------------------------------------------
// La connexion au serveur — et surtout, son ABSENCE.
//
// LA PROPRIÉTÉ CENTRALE DE CE MODULE : tant que l'auteur ne s'est pas
// connecté, cet objet ne coûte rien. Pas de session WinHTTP ouverte, pas
// de poignée système retenue, pas de fil d'exécution, pas de minuterie,
// pas un octet de tampon réseau. Il pèse une poignée d'octets et ne fait
// rien du tout.
//
// Ce n'est pas une optimisation : c'est la règle numéro un de la Phase 5
// prise au pied de la lettre. « La connexion n'est jamais obligatoire »
// ne veut pas seulement dire « on peut refuser de se connecter », mais
// « celui qui ne se connecte pas ne paie rien » — ni en mémoire, ni en
// démarrage, ni en ressources système. Un éditeur de roman doit s'ouvrir
// instantanément sur une machine hors ligne.
//
// COMMENT C'EST OBTENU : le transport n'est pas un membre, c'est un
// pointeur nul. Il est CRÉÉ dans signIn(), et DÉTRUIT dans signOut() —
// libérant les poignées système avec lui. Une fabrique injectable permet
// aux tests de vérifier tout cela sans ouvrir la moindre connexion.
//
// Un test (voir Bridge/test_bridge) monte l'application entière et vérifie
// qu'aucun transport n'a été créé. Si quelqu'un branche un jour une
// initialisation réseau au démarrage, il tombe.
//
// LE JETON NE VA PAS SUR LE DISQUE. Il vit en mémoire, et la session
// s'achève avec l'application. C'est délibéré pour ce jalon : un jeton
// écrit dans un fichier demande d'être protégé, et une protection
// approximative vaut moins que pas de fichier du tout.
// -----------------------------------------------------------------------

#pragma once

#include "SyncClient.h"
#include "Transport.h"

#include <functional>
#include <memory>
#include <string>

namespace net {

enum class ConnectionState { Disconnected, Connected };

class Connection {
public:
    // Ne construit RIEN. Voir l'en-tête : c'est tout l'intérêt.
    Connection();
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Fabrique de transport. Par défaut, elle produit le transport réel
    // (WinHTTP) — mais seulement quand on l'appelle, c'est-à-dire à la
    // connexion. Les tests en injectent une autre.
    using TransportFactory =
        std::function<std::unique_ptr<Transport>(const std::string& baseUrl)>;
    void setTransportFactory(TransportFactory factory);

    // --- État ---------------------------------------------------------
    ConnectionState state() const;
    bool isConnected() const;
    const std::string& serverUrl() const;
    const std::string& userName() const;

    // Un transport existe-t-il RÉELLEMENT ? C'est la question que pose le
    // test de paresse, et la seule raison d'être de cet accesseur.
    bool hasTransport() const;

    // --- Ouverture et fermeture ---------------------------------------

    // Se connecte. C'est ICI, et nulle part avant, que le transport naît.
    // En cas d'échec, rien n'est conservé : ni transport, ni jeton.
    //
    // 'secret' est effacé de la mémoire de l'appelant au retour, dans la
    // mesure où le langage le permet (voir l'implémentation).
    Result signIn(const std::string& baseUrl, const std::string& user,
                  std::string& secret);

    // Se déconnecte : prévient le serveur si possible, oublie le jeton,
    // DÉTRUIT le transport. Ne peut pas échouer du point de vue de
    // l'application — on est déconnecté quoi qu'en dise le serveur.
    void signOut();

    // Le client de protocole, ou nullptr si déconnecté. Rien au-dessus ne
    // doit supposer qu'il existe.
    SyncClient* client();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Encodage d'une valeur de formulaire (application/x-www-form-urlencoded).
// Plus strict que SyncClient::percentEncode : ici la barre oblique n'est
// pas un séparateur de chemin mais un caractère comme un autre.
std::string formEncode(const std::string& value);

// Efface le contenu d'une chaîne avant de la vider. Au mieux de ce que
// permet le langage : rien n'interdit à une implémentation d'avoir déjà
// recopié le tampon ailleurs. C'est une précaution, pas une garantie, et
// il vaut mieux le dire que le laisser croire.
void wipe(std::string& secret);

} // namespace net
