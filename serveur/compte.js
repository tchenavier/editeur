// compte.js
// -----------------------------------------------------------------------
// Créer un compte, ou changer un mot de passe. À lancer SUR LE SERVEUR.
//
//     node compte.js creer elise
//     node compte.js motdepasse elise
//     node compte.js partager <id-de-projet> elise
//     node compte.js lister
//
// POURQUOI PAS DEPUIS L'ÉDITEUR : le protocole ne comporte volontairement
// aucun point d'entrée d'inscription. Un serveur personnel n'a pas
// vocation à accepter des comptes venus d'Internet ; les créer à la main
// est plus sûr, et suffit largement pour un usage familial ou entre
// relecteurs.
//
// LE MOT DE PASSE N'EST JAMAIS PASSÉ EN ARGUMENT : il serait visible dans
// la liste des processus et resterait dans l'historique du terminal. Il
// est demandé au clavier, sans écho.
// -----------------------------------------------------------------------

'use strict';

const readline = require('readline');
const coeur = require('./coeur');

function lireConfig() {
  const fichier = process.env.EDITEUR_SYNC_CONFIG || 'config.json';
  return JSON.parse(require('fs').readFileSync(fichier, 'utf8'));
}

// Saisie sans écho : rien ne s'affiche, pas même des étoiles (leur nombre
// renseignerait quiconque regarde par-dessus l'épaule).
function demanderSecret(invite) {
  return new Promise((resolve) => {
    const sortie = process.stdout;
    const entree = process.stdin;
    sortie.write(invite);

    const rl = readline.createInterface({ input: entree, output: sortie, terminal: true });
    // On remplace l'écriture des caractères frappés par... rien.
    const ecrire = rl._writeToOutput;
    rl._writeToOutput = function (chaine) {
      if (chaine.includes('\n') || chaine.includes('\r')) ecrire.call(rl, '\n');
    };
    rl.question('', (reponse) => {
      rl.close();
      resolve(reponse);
    });
  });
}

async function principal() {
  const [action, argument, argument2] = process.argv.slice(2);
  const config = lireConfig();
  const { magasinMariaDB } = require('./base');

  if (!action) {
    console.log('Usage : node compte.js creer|motdepasse|partager|lister [arguments]');
    process.exit(1);
  }

  const mysql = require('mysql2/promise');
  const lien = await mysql.createConnection({
    host: config.base.hote || '127.0.0.1',
    port: config.base.port || 3306,
    user: config.base.utilisateur,
    password: config.base.motDePasse,
    database: config.base.nom,
  });

  try {
    if (action === 'lister') {
      const [comptes] = await lien.execute('SELECT id, nom, cree_le FROM comptes ORDER BY id');
      for (const c of comptes) console.log(c.id + '  ' + c.nom + '  ' + c.cree_le);
      const [projets] = await lien.execute(
        'SELECT p.id, c.nom FROM projets p JOIN comptes c ON c.id = p.proprietaire');
      for (const p of projets) console.log('projet ' + p.id + '  -> ' + p.nom);
      return;
    }

    if (action === 'creer' || action === 'motdepasse') {
      if (!argument) throw new Error('Indiquez le nom du compte.');
      if (argument.length > 64) throw new Error('Nom de compte trop long.');

      const secret = await demanderSecret('Mot de passe pour « ' + argument + ' » : ');
      const encore = await demanderSecret('Confirmez : ');
      if (secret !== encore) throw new Error('Les deux saisies diffèrent.');
      if (secret.length < 10) {
        throw new Error('Choisissez au moins dix caractères : ce mot de passe protège vos textes.');
      }

      const { sel, hachage } = coeur.hacherMotDePasse(secret);
      if (action === 'creer') {
        await lien.execute('INSERT INTO comptes (nom, sel, hachage) VALUES (?, ?, ?)',
                           [argument, sel, hachage]);
        console.log('Compte « ' + argument + ' » créé.');
      } else {
        const [r] = await lien.execute('UPDATE comptes SET sel = ?, hachage = ? WHERE nom = ?',
                                       [sel, hachage, argument]);
        if (r.affectedRows === 0) throw new Error('Compte inconnu : ' + argument);
        // Changer de mot de passe invalide les sessions ouvertes : c'est
        // le geste qu'on fait justement quand on craint que quelqu'un
        // d'autre soit connecté.
        await lien.execute(
          'DELETE j FROM jetons j JOIN comptes c ON c.id = j.compte WHERE c.nom = ?', [argument]);
        console.log('Mot de passe de « ' + argument + ' » remplacé, sessions fermées.');
      }
      return;
    }

    if (action === 'partager') {
      if (!argument || !argument2) throw new Error('Usage : partager <id-projet> <compte>');
      if (!coeur.projetSur(argument)) throw new Error('Identifiant de projet invalide.');
      const [comptes] = await lien.execute('SELECT id FROM comptes WHERE nom = ?', [argument2]);
      if (!comptes.length) throw new Error('Compte inconnu : ' + argument2);
      await lien.execute('INSERT IGNORE INTO partages (projet, compte) VALUES (?, ?)',
                         [argument, comptes[0].id]);
      console.log('Projet ' + argument + ' partagé en LECTURE SEULE avec ' + argument2 + '.');
      return;
    }

    throw new Error('Action inconnue : ' + action);
  } finally {
    await lien.end();
  }
}

principal().catch((e) => {
  console.error('Erreur : ' + e.message);
  process.exit(1);
});
