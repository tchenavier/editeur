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

// Facultatif : si dotenv est absent, l'environnement suffit.
try {
  require('dotenv').config();
} catch (_) {
  // Rien à signaler : c'est un cas de fonctionnement normal.
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

module.exports = { charger };
