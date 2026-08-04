// test_serveur.js
// Tests du serveur de bout en bout, avec de vraies requêtes HTTP.
//
//     node test_serveur.js
//
// SANS BASE DE DONNÉES : le magasin en mémoire remplace MariaDB, et les
// fichiers vont dans un dossier temporaire effacé à la fin. Rien à
// installer, rien à configurer, aucun port fixe — le système en attribue
// un. On peut donc lancer ces tests à tout moment, y compris sur une
// machine où MariaDB n'existe pas.
//
// CE QUI EST VÉRIFIÉ n'est pas tant « ça marche » que « ça refuse » :
// mauvais mot de passe, jeton absent, projet d'un autre, chemin qui
// remonte, écriture sur un partage en lecture seule, écrasement sans
// If-Match. C'est là que se joue la sécurité d'un serveur.

'use strict';

const assert = require('assert');
const fs = require('fs/promises');
const os = require('os');
const path = require('path');

const coeur = require('./coeur');
const { magasinMemoire } = require('./base');
const { creerServeur } = require('./serveur');

let reussis = 0;
const aFaire = [];
function cas(titre, fn) { aFaire.push({ titre, fn }); }

async function principal() {
  const racine = await fs.mkdtemp(path.join(os.tmpdir(), 'editeur-sync-test-'));

  // Deux comptes : Alice possède le projet, Bob s'en verra offrir la
  // lecture, Carole n'aura rien.
  const motAlice = coeur.hacherMotDePasse('secret-alice');
  const motBob = coeur.hacherMotDePasse('secret-bob');
  const PROJET = '11111111-2222-4333-8444-555555555555';
  const AUTRE = '99999999-8888-4777-8666-555555555555';

  const magasin = magasinMemoire({
    comptes: [
      { id: 1, nom: 'alice', sel: motAlice.sel, hachage: motAlice.hachage },
      { id: 2, nom: 'bob', sel: motBob.sel, hachage: motBob.hachage },
    ],
    projets: [{ id: AUTRE, proprietaire: 1 }],
    partages: [{ projet: AUTRE, compte: 2 }],
  });

  const serveur = creerServeur({ magasin, racine });
  await new Promise((r) => serveur.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + serveur.address().port;

  const appel = (chemin, options) => fetch(base + chemin, options);

  // Ajoute le jeton aux en-têtes sans écraser ceux qu'on donne.
  function avecJeton(jeton, options) {
    const o = Object.assign({}, options || {});
    o.headers = Object.assign({ Authorization: 'Bearer ' + jeton },
                              (options && options.headers) || {});
    return o;
  }

  async function connecter(nom, secret) {
    const r = await appel('/api/session', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'user=' + encodeURIComponent(nom) + '&secret=' + encodeURIComponent(secret),
    });
    const texte = await r.text();
    const jeton = (texte.match(/^token (.+)$/m) || [])[1];
    return { statut: r.status, jeton, texte };
  }

  // --- Poignée de main ---------------------------------------------------
  cas('la version s\'obtient sans jeton', async () => {
    const r = await appel('/api/version');
    assert.strictEqual(r.status, 200);
    const t = await r.text();
    assert.ok(t.includes('api ' + coeur.VERSION_API));
    assert.ok(t.includes('server '));
  });

  // --- Session -----------------------------------------------------------
  cas('connexion réussie', async () => {
    const r = await connecter('alice', 'secret-alice');
    assert.strictEqual(r.statut, 200);
    assert.ok(r.jeton && r.jeton.length === 64);
    assert.ok(r.texte.includes('user alice'));
  });

  cas('mot de passe faux et compte inconnu : MÊME réponse', async () => {
    // Une différence permettrait d'énumérer les comptes existants.
    const faux = await connecter('alice', 'pas-le-bon');
    const inconnu = await connecter('zorglub', 'peu-importe');
    assert.strictEqual(faux.statut, 401);
    assert.strictEqual(inconnu.statut, 401);
    assert.strictEqual(faux.texte, inconnu.texte);
    assert.ok(!faux.jeton);
  });

  cas('les tentatives sont limitées', async () => {
    let dernier = 0;
    for (let i = 0; i < 12; i++) {
      dernier = (await connecter('cible', 'essai' + i)).statut;
    }
    assert.strictEqual(dernier, 429);
    // Et un autre compte n'est pas puni pour celui-là.
    assert.strictEqual((await connecter('alice', 'secret-alice')).statut, 200);
  });

  // --- Accès ------------------------------------------------------------
  cas('sans jeton, rien n\'est accessible', async () => {
    const r = await appel('/api/projects/' + PROJET + '/index');
    assert.strictEqual(r.status, 401);

    const bidon = await appel('/api/projects/' + PROJET + '/index',
                              { headers: { Authorization: 'Bearer inexistant' } });
    assert.strictEqual(bidon.status, 401);
  });

  cas('le projet d\'un autre est « inconnu », pas « interdit »', async () => {
    // Répondre 403 confirmerait que le projet existe.
    const { jeton } = await connecter('bob', 'secret-bob');
    const r = await appel('/api/projects/' + PROJET + '/index',
                          avecJeton(jeton));
    assert.strictEqual(r.status, 404);
  });

  // --- Dépôt et reprise --------------------------------------------------
  cas('déposer un fichier, puis le relire', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');
    const contenu = '# Chapitre premier\n\nIl faisait nuit.\n';

    // If-Match: * — « je crois ce fichier absent ».
    const depot = await appel('/api/projects/' + PROJET + '/files/roman.md',
      avecJeton(jeton, { method: 'PUT', headers: { 'If-Match': '*' }, body: contenu }));
    assert.strictEqual(depot.status, 200);
    assert.strictEqual(depot.headers.get('etag'), '"' + coeur.sha256(contenu) + '"');

    const lecture = await appel('/api/projects/' + PROJET + '/files/roman.md',
                                avecJeton(jeton));
    assert.strictEqual(lecture.status, 200);
    assert.strictEqual(await lecture.text(), contenu);
    assert.strictEqual(lecture.headers.get('etag'), '"' + coeur.sha256(contenu) + '"');

    // Le fichier est bien sur le disque, à sa place.
    const surDisque = await fs.readFile(path.join(racine, PROJET, 'roman.md'), 'utf8');
    assert.strictEqual(surDisque, contenu);
  });

  cas('l\'inventaire est au format exact du client', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');
    const r = await appel('/api/projects/' + PROJET + '/index', avecJeton(jeton));
    assert.strictEqual(r.status, 200);
    const t = await r.text();
    assert.ok(t.startsWith('# inventaire de projet 1\n'));
    assert.ok(t.includes('projet ' + PROJET + '\n'));
    // « f <empreinte de 64 caractères> <taille> <date> <chemin> »
    assert.ok(/\nf [0-9a-f]{64} \d+ \S+ roman\.md\n/.test(t));
  });

  // --- Concurrence -------------------------------------------------------
  cas('écrire sans If-Match est refusé', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');
    const r = await appel('/api/projects/' + PROJET + '/files/roman.md',
                          avecJeton(jeton, { method: 'PUT', body: 'texte' }));
    assert.strictEqual(r.status, 400);
  });

  cas('If-Match qui ne correspond plus : conflit, pas écrasement', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');

    const mauvais = await appel('/api/projects/' + PROJET + '/files/roman.md',
      avecJeton(jeton, { method: 'PUT', headers: { 'If-Match': '"' + 'f'.repeat(64) + '"' },
                         body: 'ÉCRASÉ' }));
    assert.strictEqual(mauvais.status, 409);

    // Le fichier n'a PAS bougé : c'est tout l'intérêt.
    const surDisque = await fs.readFile(path.join(racine, PROJET, 'roman.md'), 'utf8');
    assert.ok(surDisque.includes('Il faisait nuit'));

    // « * » sur un fichier qui existe est un conflit aussi.
    const etoile = await appel('/api/projects/' + PROJET + '/files/roman.md',
      avecJeton(jeton, { method: 'PUT', headers: { 'If-Match': '*' }, body: 'x' }));
    assert.strictEqual(etoile.status, 409);
  });

  // --- Partage en lecture seule ------------------------------------------
  cas('lecture seule : Bob lit le projet partagé, mais n\'y écrit pas', async () => {
    const { jeton } = await connecter('bob', 'secret-bob');

    // Alice dépose dans le projet partagé.
    const jetonAlice = (await connecter('alice', 'secret-alice')).jeton;
    await appel('/api/projects/' + AUTRE + '/files/partage.md',
      avecJeton(jetonAlice, { method: 'PUT', headers: { 'If-Match': '*' }, body: 'à lire' }));

    const lecture = await appel('/api/projects/' + AUTRE + '/files/partage.md',
                                avecJeton(jeton));
    assert.strictEqual(lecture.status, 200);
    assert.strictEqual(await lecture.text(), 'à lire');

    // Mais toute écriture est refusée — et ici 403, car Bob SAIT que le
    // projet existe : lui répondre 404 serait mentir sans rien protéger.
    const ecriture = await appel('/api/projects/' + AUTRE + '/files/partage.md',
      avecJeton(jeton, { method: 'PUT', headers: { 'If-Match': '*' }, body: 'modifié' }));
    assert.strictEqual(ecriture.status, 403);

    const suppression = await appel('/api/projects/' + AUTRE + '/files/partage.md',
      avecJeton(jeton, { method: 'DELETE', headers: { 'If-Match': '*' } }));
    assert.strictEqual(suppression.status, 403);

    // Le fichier est intact.
    const surDisque = await fs.readFile(path.join(racine, AUTRE, 'partage.md'), 'utf8');
    assert.strictEqual(surDisque, 'à lire');
  });

  // --- Chemins hostiles ---------------------------------------------------
  cas('aucun chemin ne sort du dossier du projet', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');
    const hostiles = [
      '/api/projects/' + PROJET + '/files/..%2F..%2Fsecret.md',
      '/api/projects/' + PROJET + '/files/../../secret.md',
      '/api/projects/' + PROJET + '/files/a%00b.md',
      '/api/projects/' + PROJET + '/files/C%3A%2Fwindows',
    ];
    for (const chemin of hostiles) {
      const r = await appel(chemin, avecJeton(jeton));
      assert.ok(r.status === 400 || r.status === 404,
                'attendu 400 ou 404 pour ' + chemin + ', reçu ' + r.status);
    }
    // Rien n'a été créé en dehors du dossier du projet.
    const contenu = await fs.readdir(racine);
    for (const nom of contenu) {
      assert.ok(nom === PROJET || nom === AUTRE, 'fichier inattendu : ' + nom);
    }
  });

  // --- Suppression et fin de session --------------------------------------
  cas('supprimer un fichier, puis fermer la session', async () => {
    const { jeton } = await connecter('alice', 'secret-alice');
    const contenu = 'à effacer';
    await appel('/api/projects/' + PROJET + '/files/jetable.md',
      avecJeton(jeton, { method: 'PUT', headers: { 'If-Match': '*' }, body: contenu }));

    const r = await appel('/api/projects/' + PROJET + '/files/jetable.md',
      avecJeton(jeton, { method: 'DELETE',
                         headers: { 'If-Match': '"' + coeur.sha256(contenu) + '"' } }));
    assert.strictEqual(r.status, 204);

    const relu = await appel('/api/projects/' + PROJET + '/files/jetable.md', avecJeton(jeton));
    assert.strictEqual(relu.status, 404);

    // Fin de session : le jeton ne vaut plus rien.
    const sortie = await appel('/api/session', avecJeton(jeton, { method: 'DELETE' }));
    assert.strictEqual(sortie.status, 204);
    const apres = await appel('/api/projects/' + PROJET + '/index', avecJeton(jeton));
    assert.strictEqual(apres.status, 401);
  });

  // --- Configuration ------------------------------------------------------
  cas('le dossier des projets est vérifié AVANT de démarrer', async () => {
    const { verifierRacine } = require('./config');

    // Un dossier utilisable ne pose aucun problème.
    assert.strictEqual(verifierRacine(racine), null);

    // Un dossier créable non plus : le serveur le crée.
    const neuf = path.join(racine, 'sous', 'dossier');
    assert.strictEqual(verifierRacine(neuf), null);
    await fs.access(neuf); // il existe bien

    // Un chemin impossible est refusé, et le message dit quoi faire —
    // plutôt que de laisser l'échec surgir au premier dépôt de manuscrit,
    // c'est-à-dire au plus mauvais moment.
    const impossible = path.join(racine, 'fichier-pas-dossier');
    await fs.writeFile(impossible, 'je suis un fichier');
    const souci = verifierRacine(path.join(impossible, 'projets'));
    assert.ok(souci, 'un chemin impossible doit être signalé');
    assert.ok(souci.includes('SYNC_RACINE'), 'le message doit indiquer le réglage');
    assert.ok(souci.includes('chown'), 'le message doit donner la commande de correction');
  });

  // --- Exécution ----------------------------------------------------------
  for (const { titre, fn } of aFaire) {
    await fn();
    reussis++;
    console.log('OK  ' + titre);
  }

  await new Promise((r) => serveur.close(r));
  await fs.rm(racine, { recursive: true, force: true });
  console.log('\n' + reussis + ' cas passés — serveur vérifié de bout en bout.');
}

principal().catch((e) => {
  console.error('\nÉCHEC : ' + (e && e.stack ? e.stack : e));
  process.exit(1);
});
