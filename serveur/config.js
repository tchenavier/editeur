// config.js
// -----------------------------------------------------------------------
// La configuration du serveur, lue en UN SEUL endroit.
//
// Avant, `serveur.js` et `compte.js` lisaient chacun leur fichier de
// configuration. Deux lectures, c'est deux occasions de diverger — et le
// jour où l'une des deux oublie un réglage, cela se voit à l'exécution,
// pas à l'écriture. Ici, il n'y en a plus qu'une.
//
// D'OÙ VIENNENT LES VALEURS : de l'environnement (`process.env`). Le
// fichier `.env` n'est qu'une façon commode de le remplir en
// développement ; en production, systemd peut le faire directement avec
// `EnvironmentFile=` (voir README.md). C'est pour cela que `dotenv` est
// FACULTATIF : s'il n'est pas installé, on lit l'environnement tel quel,
// et rien ne casse. Les tests, eux, n'ont besoin d'aucun des deux.
//
// UN RÉGLAGE MANQUANT ARRÊTE LE SERVEUR, avec la liste de ce qui manque.
// Un serveur qui démarre avec un mot de passe vide, échoue à joindre la
// base et se contente de répondre « panne » à ses clients est bien pire
// qu'un serveur qui refuse de démarrer en disant pourquoi.
//
// AUCUNE VALEUR SECRÈTE N'EST AFFICHÉE, même dans les messages d'erreur :
// on nomme la variable qui manque, jamais son contenu.
// -----------------------------------------------------------------------

'use strict';

// dotenv est chargé DANS charger(), pas au chargement du module.
//
// Deux raisons. La première est de principe : exiger ce module ne devrait
// avoir aucun effet de bord — or lire un .env en est un. La seconde est
// concrète : les tests importent ce fichier pour vérifier le dossier des
// projets, et n'ont aucune raison d'aller lire la configuration réelle de
// la machine (ni d'en afficher le décompte au milieu de leur sortie).
//
// Facultatif : si dotenv est absent, l'environnement suffit.
//
// DEUX EMPLACEMENTS SONT LUS, et ce n'est pas de l'indécision. dotenv
// cherche par défaut dans le DOSSIER COURANT : selon qu'on lance
// « node serveur.js » depuis serveur/ ou « node serveur/serveur.js »
// depuis la racine, ce n'est pas le même fichier. Deux configurations
// possibles pour la même installation, c'est une source de confusion
// garantie — et une occasion de croire qu'un réglage est pris en compte
// alors qu'il ne l'est pas.
//
// On lit donc d'abord celui qui est À CÔTÉ DE CE FICHIER, emplacement qui
// ne dépend pas de la commande tapée, puis celui du dossier courant.
// dotenv ne remplace jamais une variable déjà définie : le premier lu
// l'emporte, et l'environnement du système l'emporte sur les deux.
function chargerEnv() {
  try {
    const dotenv = require('dotenv');
    const path = require('path');
    dotenv.config({ path: path.join(__dirname, '.env') });
    dotenv.config();
  } catch (_) {
    // Rien à signaler : c'est un cas de fonctionnement normal.
  }
}

function texte(nom, defaut) {
  const valeur = process.env[nom];
  if (valeur === undefined || valeur === '') return defaut;
  return valeur;
}

function entier(nom, defaut) {
  const valeur = process.env[nom];
  if (valeur === undefined || valeur === '') return defaut;
  if (!/^[0-9]+$/.test(valeur)) {
    throw new Error(nom + ' doit être un nombre entier.');
  }
  const nombre = Number(valeur);
  if (nombre <= 0 || nombre > 65535) {
    throw new Error(nom + ' doit être un port valide (1 à 65535).');
  }
  return nombre;
}

function charger() {
  chargerEnv();

  const manquants = [];
  const requis = (nom) => {
    const valeur = process.env[nom];
    if (valeur === undefined || valeur === '') { manquants.push(nom); return ''; }
    return valeur;
  };

  const config = {
    adresse: texte('SYNC_ADRESSE', '127.0.0.1'),
    port: entier('SYNC_PORT', 8080),
    racine: requis('SYNC_RACINE'),
    base: {
      hote: texte('DB_HOTE', '127.0.0.1'),
      port: entier('DB_PORT', 3306),
      nom: requis('DB_NOM'),
      utilisateur: requis('DB_UTILISATEUR'),
      motDePasse: requis('DB_MOTDEPASSE'),
    },
  };

  if (manquants.length > 0) {
    throw new Error(
      'Configuration incomplète. Variables manquantes :\n' +
      manquants.map((n) => '    ' + n).join('\n') + '\n\n' +
      'Renseignez-les dans un fichier .env à côté du serveur (voir\n' +
      '.env.exemple), ou dans l\'environnement du service.');
  }
  return config;
}

// Le dossier des projets est-il utilisable ?
//
// POURQUOI CE CONTRÔLE EXISTE. Le serveur crée les dossiers manquants au
// moment d'écrire un fichier. Sans cette vérification, un dossier
// inaccessible ne se découvrait donc qu'au PREMIER DÉPÔT : le démarrage
// se passait bien, la connexion aussi, et l'auteur recevait « panne du
// serveur » au moment d'envoyer son manuscrit — le pire moment, et le
// message le moins utile.
//
// C'est exactement le travers dénoncé plus haut pour la base de données.
// Il n'y a pas de raison de l'appliquer à l'une et pas à l'autre.
//
// Retourne null si tout va bien, sinon une explication qui dit quoi faire.
function verifierRacine(racine) {
  const fs = require('fs');
  const path = require('path');
  const os = require('os');

  const qui = (() => {
    try { return os.userInfo().username; } catch (_) { return 'ce compte'; }
  })();

  const conseil =
    '\n\nDeux façons de régler cela :\n' +
    '  — en production, donner le dossier au compte qui fait tourner le serveur :\n' +
    '        sudo mkdir -p ' + racine + '\n' +
    '        sudo chown -R ' + qui + ' ' + path.dirname(racine) + '\n' +
    '  — en développement, pointer SYNC_RACINE vers votre dossier personnel :\n' +
    '        SYNC_RACINE=' + path.join(os.homedir() || '/home/' + qui,
                                       'editeur-sync', 'projets');

  try {
    fs.mkdirSync(racine, { recursive: true });
  } catch (erreur) {
    return 'Le dossier des projets ne peut pas être créé :\n    ' + racine +
           '\nRaison : ' + erreur.code + ', sous l\'utilisateur « ' + qui + ' ».' +
           conseil;
  }

  // Le droit de créer un dossier ne garantit pas celui d'y écrire : on
  // essaie pour de bon plutôt que de le supposer.
  const sonde = path.join(racine, '.sonde-' + process.pid);
  try {
    fs.writeFileSync(sonde, 'x');
    fs.unlinkSync(sonde);
  } catch (erreur) {
    return 'Le dossier des projets existe mais n\'est pas accessible en écriture :\n    ' +
           racine + '\nRaison : ' + erreur.code + ', sous l\'utilisateur « ' + qui + ' ».' +
           conseil;
  }
  return null;
}

module.exports = { charger, verifierRacine };
