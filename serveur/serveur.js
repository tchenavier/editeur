// serveur.js
// -----------------------------------------------------------------------
// Le serveur de synchronisation : il applique le contrat de
// net/PROTOCOLE.md, et rien d'autre.
//
// AUCUNE DÉPENDANCE npm ici : le module « http » de Node fournit un
// serveur éprouvé par des millions de sites. C'est précisément la raison
// pour laquelle ce projet, qui écrit lui-même son archive ZIP et son
// SHA-256, n'écrit PAS son analyseur HTTP : celui-ci reçoit ce que
// n'importe qui lui envoie.
//
// CE FICHIER N'EST QUE DU CÂBLAGE. Tout ce qui peut être faux de façon
// dangereuse — chemins, mots de passe, jetons, permissions — est dans
// coeur.js, et se teste sans réseau ni base (« node test_coeur.js »).
//
// Le magasin (comptes, jetons, projets, partages) est passé en argument :
// MariaDB en production, un magasin en mémoire pour les essais. Le
// serveur ne sait pas lequel il utilise.
// -----------------------------------------------------------------------

'use strict';

const http = require('http');
const fsp = require('fs/promises');
const path = require('path');

const coeur = require('./coeur');

const DUREE_JETON_MS = 30 * 24 * 60 * 60 * 1000; // 30 jours

function creerServeur(options) {
  const magasin = options.magasin;
  const racine = path.resolve(options.racine);
  const limiteur = new coeur.Limiteur();
  const journal = options.journal || (() => {});

  // --- Réponses ---------------------------------------------------------
  // Le code d'état porte l'information machine, le corps la phrase pour
  // l'auteur. Pas de format d'erreur à analyser (voir PROTOCOLE.md).
  function repondre(res, code, corps, entetes) {
    const donnees = Buffer.isBuffer(corps) ? corps : Buffer.from(corps || '', 'utf8');
    const tous = Object.assign({
      'Content-Length': donnees.length,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
    }, entetes || {});
    if (!tous['Content-Type']) tous['Content-Type'] = 'text/plain; charset=utf-8';
    res.writeHead(code, tous);
    res.end(donnees);
  }

  const texte = (res, code, message) => repondre(res, code, message + '\n');

  // --- Lecture du corps, bornée ----------------------------------------
  function lireCorps(req, maximum) {
    return new Promise((resolve, reject) => {
      const morceaux = [];
      let total = 0;
      req.on('data', (m) => {
        total += m.length;
        if (total > maximum) {
          // On refuse AVANT d'avoir tout gardé : un client ne doit pas
          // pouvoir remplir la mémoire du serveur en envoyant sans fin.
          reject(Object.assign(new Error('corps trop volumineux'), { tropGros: true }));
          req.destroy();
          return;
        }
        morceaux.push(m);
      });
      req.on('end', () => resolve(Buffer.concat(morceaux)));
      req.on('error', reject);
    });
  }

  // --- Authentification -------------------------------------------------
  async function compteDeLaRequete(req) {
    const entete = req.headers['authorization'] || '';
    if (!entete.startsWith('Bearer ')) return null;
    const jeton = entete.slice(7).trim();
    if (!jeton || jeton.length > 512) return null;
    return magasin.compteParJeton(coeur.empreinteJeton(jeton), Date.now());
  }

  // --- Écriture atomique ------------------------------------------------
  // Même règle que côté client : un fichier à moitié écrit vaut moins que
  // pas de fichier du tout.
  async function ecrireAtomique(destination, donnees) {
    await fsp.mkdir(path.dirname(destination), { recursive: true });
    const temporaire = destination + '.tmp-' + process.pid + '-' + Date.now();
    await fsp.writeFile(temporaire, donnees);
    await fsp.rename(temporaire, destination);
  }

  // --- Inventaire d'un projet, lu sur le disque -------------------------
  async function inventaire(idProjet) {
    const dossier = path.resolve(racine, idProjet);
    const fichiers = [];

    async function parcourir(courant, prefixe) {
      let entrees;
      try {
        entrees = await fsp.readdir(courant, { withFileTypes: true });
      } catch (_) {
        return; // dossier absent : inventaire vide, ce n'est pas une erreur
      }
      for (const entree of entrees) {
        const relatif = prefixe ? prefixe + '/' + entree.name : entree.name;
        if (entree.name.startsWith('.')) continue;
        if (entree.isDirectory()) {
          await parcourir(path.join(courant, entree.name), relatif);
        } else if (entree.isFile()) {
          if (entree.name.includes('.tmp-')) continue; // écriture en cours
          const complet = path.join(courant, entree.name);
          const infos = await fsp.stat(complet);
          const contenu = await fsp.readFile(complet);
          fichiers.push({
            chemin: relatif,
            taille: infos.size,
            date: String(Math.floor(infos.mtimeMs / 1000)),
            sha256: coeur.sha256(contenu),
          });
        }
      }
    }

    await parcourir(dossier, '');
    return coeur.serialiserInventaire(idProjet, fichiers);
  }

  // --- Routes -----------------------------------------------------------

  async function traiter(req, res) {
    const url = new URL(req.url, 'http://interne');
    const chemin = url.pathname;

    // 1. Poignée de main : sans authentification, par contrat.
    if (chemin === '/api/version' && req.method === 'GET') {
      return repondre(res, 200,
        'api ' + coeur.VERSION_API + '\nserver editeur-sync 1.0\n');
    }

    // 2. Ouverture de session.
    if (chemin === '/api/session' && req.method === 'POST') {
      const corps = (await lireCorps(req, coeur.TAILLE_MAX_FORMULAIRE)).toString('utf8');
      const champs = coeur.lireFormulaire(corps);
      const nom = champs.user || '';
      const secret = champs.secret || '';

      // Ralentir les essais. La clé est le nom demandé : un attaquant ne
      // doit pas pouvoir bloquer tout le serveur en visant un seul compte.
      if (!limiteur.autorise(nom)) {
        journal('trop de tentatives pour « ' + nom + ' »');
        return texte(res, 429, 'Trop de tentatives. Réessayez dans quelques minutes.');
      }

      const compte = await magasin.compteParNom(nom);
      const bon = compte && coeur.motDePasseValide(secret, compte.sel, compte.hachage);
      if (!bon) {
        // MÊME message dans les deux cas : distinguer « compte inconnu »
        // de « mot de passe faux » permettrait d'énumérer les comptes.
        journal('connexion refusée pour « ' + nom + ' »');
        return texte(res, 401, 'Identifiant ou mot de passe incorrect.');
      }

      limiteur.reussite(nom);
      const { jeton, empreinte } = coeur.nouveauJeton();
      await magasin.creerJeton(compte.id, empreinte, Date.now() + DUREE_JETON_MS);
      journal('session ouverte pour « ' + nom + ' »');
      return repondre(res, 200, 'token ' + jeton + '\nuser ' + compte.nom + '\n');
    }

    if (chemin === '/api/session' && req.method === 'DELETE') {
      const entete = req.headers['authorization'] || '';
      if (entete.startsWith('Bearer ')) {
        await magasin.supprimerJeton(coeur.empreinteJeton(entete.slice(7).trim()));
      }
      return repondre(res, 204, '');
    }

    // 3. Tout le reste exige une session valide.
    const compte = await compteDeLaRequete(req);
    if (!compte) return texte(res, 401, 'Connexion expirée ou absente.');

    const morceaux = chemin.split('/').filter((m) => m !== '');
    // api / projects / <id> / (index | files / <chemin...>)
    if (morceaux.length < 3 || morceaux[0] !== 'api' || morceaux[1] !== 'projects') {
      return texte(res, 404, 'Adresse inconnue.');
    }
    const idProjet = decodeURIComponent(morceaux[2]);
    if (!coeur.projetSur(idProjet)) return texte(res, 400, 'Identifiant de projet invalide.');

    let projet = await magasin.projet(idProjet);
    let droit = coeur.droitSur(projet, await magasin.partages(idProjet), compte.id);

    // Un projet inconnu appartient à celui qui le dépose en premier :
    // c'est l'appairage vu du serveur.
    if (!projet && req.method === 'PUT') {
      await magasin.creerProjet(idProjet, compte.id);
      projet = await magasin.projet(idProjet);
      droit = coeur.DROIT_PROPRIETAIRE;
      journal('projet ' + idProjet + ' créé par ' + compte.nom);
    }
    if (!coeur.peutLire(droit)) {
      // Même réponse pour « n'existe pas » et « pas à vous » : sinon on
      // pourrait deviner quels projets existent.
      return texte(res, 404, 'Projet inconnu.');
    }

    // 3a. Inventaire.
    if (morceaux.length === 4 && morceaux[3] === 'index' && req.method === 'GET') {
      return repondre(res, 200, await inventaire(idProjet));
    }

    // 3b. Fichiers.
    if (morceaux.length >= 5 && morceaux[3] === 'files') {
      const relatif = morceaux.slice(4).map(decodeURIComponent).join('/');
      const complet = coeur.resoudreChemin(racine, idProjet, relatif);
      if (!complet) return texte(res, 400, 'Chemin de fichier refusé.');

      if (req.method === 'GET') {
        let contenu;
        try {
          contenu = await fsp.readFile(complet);
        } catch (_) {
          return texte(res, 404, 'Fichier inconnu.');
        }
        return repondre(res, 200, contenu, {
          'Content-Type': 'application/octet-stream',
          'ETag': '"' + coeur.sha256(contenu) + '"',
        });
      }

      if (req.method === 'PUT' || req.method === 'DELETE') {
        if (!coeur.peutEcrire(droit)) {
          return texte(res, 403, 'Ce projet vous est partagé en lecture seule.');
        }

        // « If-Match » est OBLIGATOIRE : écrire sans dire ce qu'on croit
        // écraser est exactement ce que ce protocole rend impossible.
        const attendu = coeur.nettoyerEtag(req.headers['if-match']);
        if (!attendu) return texte(res, 400, 'En-tête If-Match manquant.');

        let actuel = null;
        try {
          actuel = coeur.sha256(await fsp.readFile(complet));
        } catch (_) {
          actuel = null; // le fichier n'existe pas
        }

        const accord = (attendu === '*') ? (actuel === null) : (actuel === attendu);
        if (!accord) {
          return texte(res, 409,
            'Le fichier a été modifié sur le serveur depuis votre dernière synchronisation.');
        }

        if (req.method === 'DELETE') {
          try { await fsp.unlink(complet); } catch (_) { /* déjà parti */ }
          journal('supprimé ' + idProjet + '/' + relatif);
          return repondre(res, 204, '');
        }

        const donnees = await lireCorps(req, coeur.TAILLE_MAX_FICHIER);
        await ecrireAtomique(complet, donnees);
        journal('reçu ' + idProjet + '/' + relatif + ' (' + donnees.length + " octets)");
        return repondre(res, 200, '', { 'ETag': '"' + coeur.sha256(donnees) + '"' });
      }
    }

    return texte(res, 404, 'Adresse inconnue.');
  }

  const serveur = http.createServer((req, res) => {
    traiter(req, res).catch((erreur) => {
      if (erreur && erreur.tropGros) {
        return texte(res, 413, 'Contenu trop volumineux.');
      }
      // Le détail va au journal, jamais au client : un message d'erreur
      // interne renseigne sur la machine.
      journal('ERREUR ' + req.method + ' ' + req.url + ' : ' + (erreur && erreur.stack));
      try { texte(res, 500, 'Le serveur a rencontré une panne.'); } catch (_) {}
    });
  });

  // Un client qui ouvre une connexion sans rien envoyer ne doit pas
  // occuper le serveur indéfiniment.
  serveur.requestTimeout = 60000;
  serveur.headersTimeout = 20000;
  return serveur;
}

module.exports = { creerServeur };

// Lancement direct : « node serveur.js »
//
// La configuration vient de l'environnement, éventuellement rempli par un
// fichier .env (voir config.js). Elle n'est chargée QU'ICI : le module
// lui-même reste utilisable sans aucun réglage, ce qui permet aux tests
// de le monter avec un magasin en mémoire et un dossier temporaire.
if (require.main === module) {
  const { charger, verifierRacine } = require('./config');
  const { magasinMariaDB } = require('./base');

  let config;
  try {
    config = charger();
  } catch (erreur) {
    // Un réglage manquant arrête le serveur, avec la liste de ce qui
    // manque. Démarrer à moitié et répondre « panne » à chaque client
    // serait bien pire.
    console.error(erreur.message);
    process.exit(1);
  }

  // Même exigence pour le dossier des projets : un dossier inaccessible
  // se découvrait autrefois au premier dépôt de manuscrit, c'est-à-dire
  // au plus mauvais moment possible.
  const soucis = verifierRacine(config.racine);
  if (soucis) {
    console.error(soucis);
    process.exit(1);
  }

  magasinMariaDB(config.base).then((magasin) => {
    const serveur = creerServeur({
      magasin,
      racine: config.racine,
      journal: (m) => console.log(new Date().toISOString() + ' ' + m),
    });
    serveur.listen(config.port, config.adresse, () => {
      console.log('editeur-sync écoute sur ' + config.adresse + ':' + config.port);
      console.log('racine des projets : ' + path.resolve(config.racine));
    });
  }).catch((e) => {
    // Jamais le mot de passe dans un message d'erreur, même ici.
    console.error('Démarrage impossible : ' + e.message);
    process.exit(1);
  });
}
