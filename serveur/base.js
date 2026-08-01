// base.js
// -----------------------------------------------------------------------
// Le magasin : comptes, jetons, projets, partages.
//
// DEUX IMPLÉMENTATIONS, derrière la même poignée de méthodes :
//   - magasinMemoire()  : rien à installer, pour les essais et les tests ;
//   - magasinMariaDB()  : la vraie, sur votre Debian.
//
// Le serveur ne sait pas laquelle il utilise, ce qui permet d'éprouver
// tout son comportement sans base de données — et, le jour où MariaDB
// devrait céder la place à autre chose, de ne réécrire que ce fichier.
//
// LES MANUSCRITS NE SONT PAS EN BASE. Ce sont des fichiers, rangés dans un
// dossier par projet. Une base sert à ce qu'elle fait bien : des relations
// et des recherches. Mettre des documents dedans compliquerait les
// sauvegardes sans rien apporter — sauvegarder ce serveur, c'est copier un
// dossier et exporter une petite base.
//
// SEULE DÉPENDANCE npm DU PROJET : mysql2, et seulement pour la variante
// MariaDB. Les tests, eux, n'installent rien.
// -----------------------------------------------------------------------

'use strict';

// --- Magasin en mémoire -------------------------------------------------

function magasinMemoire(donnees) {
  const comptes = new Map();  // nom -> { id, nom, sel, hachage }
  const jetons = new Map();   // empreinte -> { compte, expiration }
  const projets = new Map();  // id -> { id, proprietaire }
  const partages = [];        // { compte, projet }

  for (const compte of (donnees && donnees.comptes) || []) comptes.set(compte.nom, compte);
  for (const projet of (donnees && donnees.projets) || []) projets.set(projet.id, projet);
  for (const partage of (donnees && donnees.partages) || []) partages.push(partage);

  return {
    async compteParNom(nom) { return comptes.get(nom) || null; },

    async creerJeton(idCompte, empreinte, expiration) {
      jetons.set(empreinte, { compte: idCompte, expiration });
    },

    async compteParJeton(empreinte, maintenant) {
      const entree = jetons.get(empreinte);
      if (!entree) return null;
      if (entree.expiration <= maintenant) {
        jetons.delete(empreinte); // expiré : on le retire au passage
        return null;
      }
      for (const compte of comptes.values()) {
        if (compte.id === entree.compte) return { id: compte.id, nom: compte.nom };
      }
      return null;
    },

    async supprimerJeton(empreinte) { jetons.delete(empreinte); },

    async projet(id) { return projets.get(id) || null; },

    async creerProjet(id, proprietaire) {
      if (!projets.has(id)) projets.set(id, { id, proprietaire });
    },

    async partages(idProjet) {
      return partages.filter((p) => p.projet === idProjet);
    },

    // Réservé aux essais.
    _interne: { comptes, jetons, projets, partages },
  };
}

// --- Magasin MariaDB ----------------------------------------------------

async function magasinMariaDB(config) {
  let mysql;
  try {
    mysql = require('mysql2/promise');
  } catch (_) {
    throw new Error(
      'Le pilote mysql2 est absent. Installez-le dans le dossier serveur :\n' +
      '    npm install mysql2');
  }

  const pool = mysql.createPool({
    host: config.hote || '127.0.0.1',
    port: config.port || 3306,
    user: config.utilisateur,
    password: config.motDePasse,
    database: config.nom,
    connectionLimit: 8,
    // Requêtes préparées partout (voir plus bas) : aucune concaténation
    // de valeurs dans du SQL, donc aucune injection possible.
    namedPlaceholders: false,
  });

  const q = async (sql, valeurs) => {
    const [lignes] = await pool.execute(sql, valeurs || []);
    return lignes;
  };

  return {
    async compteParNom(nom) {
      const lignes = await q(
        'SELECT id, nom, sel, hachage FROM comptes WHERE nom = ? LIMIT 1', [nom]);
      return lignes.length ? lignes[0] : null;
    },

    async creerJeton(idCompte, empreinte, expiration) {
      await q('INSERT INTO jetons (empreinte, compte, expiration) VALUES (?, ?, ?)',
              [empreinte, idCompte, new Date(expiration)]);
      // Ménage opportuniste : les jetons périmés n'ont aucune raison de
      // s'accumuler, et cela évite une tâche planifiée de plus.
      await q('DELETE FROM jetons WHERE expiration <= NOW()');
    },

    async compteParJeton(empreinte, maintenant) {
      const lignes = await q(
        'SELECT c.id AS id, c.nom AS nom FROM jetons j ' +
        'JOIN comptes c ON c.id = j.compte ' +
        'WHERE j.empreinte = ? AND j.expiration > ? LIMIT 1',
        [empreinte, new Date(maintenant)]);
      return lignes.length ? lignes[0] : null;
    },

    async supprimerJeton(empreinte) {
      await q('DELETE FROM jetons WHERE empreinte = ?', [empreinte]);
    },

    async projet(id) {
      const lignes = await q(
        'SELECT id, proprietaire FROM projets WHERE id = ? LIMIT 1', [id]);
      return lignes.length ? lignes[0] : null;
    },

    async creerProjet(id, proprietaire) {
      await q('INSERT IGNORE INTO projets (id, proprietaire) VALUES (?, ?)',
              [id, proprietaire]);
    },

    async partages(idProjet) {
      return q('SELECT compte, projet FROM partages WHERE projet = ?', [idProjet]);
    },

    async fermer() { await pool.end(); },
  };
}

module.exports = { magasinMemoire, magasinMariaDB };
