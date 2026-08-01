// test_coeur.js
// Tests de la logique du serveur, SANS base de données et SANS réseau.
//
//     node test_coeur.js
//
// Aucune installation, aucun port, aucune configuration : c'est voulu. Ce
// qui est vérifié ici est exactement ce qui, faux, ouvrirait une faille —
// validation des chemins, hachage des mots de passe, comparaison des
// jetons, règles de permission. Ces parties-là doivent pouvoir être
// éprouvées partout, tout le temps, sans préparatifs.

'use strict';

const assert = require('assert');
const coeur = require('./coeur');

let reussis = 0;
function cas(titre, fn) {
  fn();
  reussis++;
  console.log('OK  ' + titre);
}

// --- Chemins -----------------------------------------------------------

cas('chemins acceptés', () => {
  assert.ok(coeur.cheminSur('roman.md'));
  assert.ok(coeur.cheminSur('fiches/elise.md'));
  assert.ok(coeur.cheminSur('images/couverture.png'));
  assert.ok(coeur.cheminSur('élise/été.md')); // accents : rien d'anormal
});

cas('chemins refusés — la validation du client ne compte pas', () => {
  // Un serveur qui fait confiance à son client ne valide rien : le client
  // peut être n'importe quel programme, y compris hostile.
  assert.ok(!coeur.cheminSur(''));
  assert.ok(!coeur.cheminSur('/etc/passwd'));
  assert.ok(!coeur.cheminSur('../../etc/passwd'));
  assert.ok(!coeur.cheminSur('fiches/../../secret'));
  assert.ok(!coeur.cheminSur('dossier\\fichier.md'));
  assert.ok(!coeur.cheminSur('C:/windows'));
  assert.ok(!coeur.cheminSur('fichier.md:flux'));
  assert.ok(!coeur.cheminSur('a//b.md'));
  assert.ok(!coeur.cheminSur('a/./b.md'));
  assert.ok(!coeur.cheminSur('a\u0000b.md'));
  assert.ok(!coeur.cheminSur('a\nb.md'));
  assert.ok(!coeur.cheminSur('x'.repeat(2000)));
  assert.ok(!coeur.cheminSur(null));
  assert.ok(!coeur.cheminSur(42));
  assert.ok(!coeur.cheminSur(undefined));
});

cas('identifiants de projet', () => {
  assert.ok(coeur.projetSur('11111111-2222-4333-8444-555555555555'));
  assert.ok(!coeur.projetSur('../autre'));
  assert.ok(!coeur.projetSur('projet/../../etc'));
  assert.ok(!coeur.projetSur(''));
  assert.ok(!coeur.projetSur('a'.repeat(100)));
  assert.ok(!coeur.projetSur('avec espace'));
});

cas('résolution de chemin : la dernière barrière avant le disque', () => {
  const path = require('path');
  const racine = path.resolve('/var/editeur');
  const projet = '11111111-2222-4333-8444-555555555555';

  const bon = coeur.resoudreChemin(racine, projet, 'fiches/elise.md');
  assert.ok(bon !== null);
  assert.ok(bon.startsWith(path.join(racine, projet)));
  assert.ok(bon.endsWith(path.join('fiches', 'elise.md')));

  // Tout ce qui sortirait du dossier du projet est refusé, quelle que
  // soit la ruse employée pour y arriver.
  assert.strictEqual(coeur.resoudreChemin(racine, projet, '../autre/roman.md'), null);
  assert.strictEqual(coeur.resoudreChemin(racine, projet, '/etc/passwd'), null);
  assert.strictEqual(coeur.resoudreChemin(racine, projet, 'a/../../b'), null);
  assert.strictEqual(coeur.resoudreChemin(racine, '../ailleurs', 'roman.md'), null);
  assert.strictEqual(coeur.resoudreChemin(racine, projet, ''), null);
});

// --- Mots de passe ------------------------------------------------------

cas('mots de passe : jamais en clair, jamais deux fois le même hachage', () => {
  const a = coeur.hacherMotDePasse('mon mot de passe');
  const b = coeur.hacherMotDePasse('mon mot de passe');
  // Sels différents, donc hachages différents pour le même mot de passe :
  // deux comptes au même mot de passe ne se reconnaissent pas dans la base.
  assert.notStrictEqual(a.sel, b.sel);
  assert.notStrictEqual(a.hachage, b.hachage);
  assert.strictEqual(a.hachage.length, 128); // 64 octets en hexadécimal

  assert.ok(coeur.motDePasseValide('mon mot de passe', a.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('Mon mot de passe', a.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('', a.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('mon mot de passe', b.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('mon mot de passe', a.sel, null));
});

cas('comparaison à temps constant', () => {
  assert.ok(coeur.memeSecret('abc', 'abc'));
  assert.ok(!coeur.memeSecret('abc', 'abd'));
  assert.ok(!coeur.memeSecret('abc', 'abcd')); // longueurs différentes
  assert.ok(!coeur.memeSecret('', 'a'));
  assert.ok(coeur.memeSecret('', ''));
});

// --- Jetons -------------------------------------------------------------

cas('jetons : aléatoires, et seule leur empreinte est conservable', () => {
  const un = coeur.nouveauJeton();
  const deux = coeur.nouveauJeton();
  assert.notStrictEqual(un.jeton, deux.jeton);
  assert.strictEqual(un.jeton.length, 64);
  // Ce qui va en base est l'empreinte : une copie de la base ne permet
  // pas de se faire passer pour quelqu'un.
  assert.strictEqual(un.empreinte, coeur.empreinteJeton(un.jeton));
  assert.notStrictEqual(un.empreinte, un.jeton);
  assert.strictEqual(un.empreinte.length, 64);
});

// --- Empreintes et ETag -------------------------------------------------

cas('empreintes de contenu', () => {
  // Vecteur connu : SHA-256 de la chaîne vide.
  assert.strictEqual(coeur.sha256(''),
    'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  assert.strictEqual(coeur.sha256('abc'),
    'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
});

cas('ETag : guillemets et validation faible', () => {
  assert.strictEqual(coeur.nettoyerEtag('"abc"'), 'abc');
  assert.strictEqual(coeur.nettoyerEtag('W/"abc"'), 'abc');
  assert.strictEqual(coeur.nettoyerEtag('  "abc"  '), 'abc');
  assert.strictEqual(coeur.nettoyerEtag('*'), '*');
  assert.strictEqual(coeur.nettoyerEtag(undefined), '');
});

// --- Permissions --------------------------------------------------------

cas('permissions : le propriétaire écrit, l\'invité lit', () => {
  const projet = { id: 'p1', proprietaire: 'alice' };
  const partages = [{ compte: 'bob', projet: 'p1' }];

  assert.strictEqual(coeur.droitSur(projet, partages, 'alice'), coeur.DROIT_PROPRIETAIRE);
  assert.strictEqual(coeur.droitSur(projet, partages, 'bob'), coeur.DROIT_LECTURE);
  assert.strictEqual(coeur.droitSur(projet, partages, 'carole'), coeur.DROIT_AUCUN);
  assert.strictEqual(coeur.droitSur(null, partages, 'alice'), coeur.DROIT_AUCUN);

  assert.ok(coeur.peutEcrire(coeur.DROIT_PROPRIETAIRE));
  assert.ok(!coeur.peutEcrire(coeur.DROIT_LECTURE)); // LE point du jalon
  assert.ok(!coeur.peutEcrire(coeur.DROIT_AUCUN));

  assert.ok(coeur.peutLire(coeur.DROIT_PROPRIETAIRE));
  assert.ok(coeur.peutLire(coeur.DROIT_LECTURE));
  assert.ok(!coeur.peutLire(coeur.DROIT_AUCUN));

  // Un partage qui désigne un AUTRE projet ne donne aucun droit ici.
  const autre = [{ compte: 'bob', projet: 'p2' }];
  assert.strictEqual(coeur.droitSur(projet, autre, 'bob'), coeur.DROIT_AUCUN);
});

// --- Limitation des tentatives ------------------------------------------

cas('limitation des tentatives de connexion', () => {
  const limiteur = new coeur.Limiteur(3, 1000);
  const t = 10000;
  assert.ok(limiteur.autorise('alice', t));
  assert.ok(limiteur.autorise('alice', t));
  assert.ok(limiteur.autorise('alice', t));
  assert.ok(!limiteur.autorise('alice', t)); // la quatrième est refusée

  // Un autre compte n'est pas puni pour celui-là.
  assert.ok(limiteur.autorise('bob', t));

  // La fenêtre glisse : plus tard, on réessaie.
  assert.ok(limiteur.autorise('alice', t + 2000));

  // Une connexion réussie remet le compteur à zéro.
  const l2 = new coeur.Limiteur(2, 1000);
  l2.autorise('alice', t);
  l2.autorise('alice', t);
  assert.ok(!l2.autorise('alice', t));
  l2.reussite('alice');
  assert.ok(l2.autorise('alice', t));
});

// --- Formulaires --------------------------------------------------------

cas('lecture de formulaire', () => {
  const champs = coeur.lireFormulaire('user=elise&secret=mot%20de%20passe');
  assert.strictEqual(champs.user, 'elise');
  assert.strictEqual(champs.secret, 'mot de passe');

  // Le « + » vaut espace, comme le veut le format.
  assert.strictEqual(coeur.lireFormulaire('a=un+deux').a, 'un deux');

  // Un mot de passe contenant « & » ou « = » survit à l'aller-retour.
  const dur = coeur.lireFormulaire('secret=' + encodeURIComponent('a&b=c%d'));
  assert.strictEqual(dur.secret, 'a&b=c%d');

  // Bornes : corps démesuré, trop de champs, échappement invalide.
  assert.deepStrictEqual(coeur.lireFormulaire('a=' + 'x'.repeat(20000)), {});
  const beaucoup = [];
  for (let i = 0; i < 200; i++) beaucoup.push('c' + i + '=v');
  assert.ok(Object.keys(coeur.lireFormulaire(beaucoup.join('&'))).length <= 20);
  assert.strictEqual(coeur.lireFormulaire('a=%ZZ').a, undefined);
  assert.deepStrictEqual(coeur.lireFormulaire(null), {});
});

// --- Inventaire ---------------------------------------------------------

cas('inventaire : le format EXACT attendu par le client', () => {
  // Comparaison caractère par caractère avec un exemplaire de référence
  // écrit à la main d'après io/ProjectIndex : une inversion de champs
  // produirait un inventaire que le client rejetterait en silence, ligne
  // par ligne, sans que rien ne le signale.
  const empreinteA = 'a'.repeat(64);
  const empreinteB = 'b'.repeat(64);
  const texte = coeur.serialiserInventaire('11111111-2222-4333-8444-555555555555', [
    { chemin: 'roman.md', taille: 1234, date: '1700000000', sha256: empreinteA },
    { chemin: 'fiches/elise.md', taille: 56, sha256: empreinteB },
  ]);

  const attendu =
    '# inventaire de projet 1\n' +
    'projet 11111111-2222-4333-8444-555555555555\n' +
    'f ' + empreinteB + ' 56 - fiches/elise.md\n' +
    'f ' + empreinteA + ' 1234 1700000000 roman.md\n';
  assert.strictEqual(texte, attendu);

  // Le chemin vient en dernier : il peut donc contenir des espaces.
  const avecEspace = coeur.serialiserInventaire('p', [
    { chemin: 'mon roman.md', taille: 1, sha256: empreinteA },
  ]);
  assert.ok(avecEspace.includes('f ' + empreinteA + ' 1 - mon roman.md\n'));

  // Projet vide : pas de ligne « projet », et rien ne casse.
  assert.strictEqual(coeur.serialiserInventaire('', []), '# inventaire de projet 1\n');
});

console.log('\n' + reussis + ' cas passés — logique du serveur vérifiée.');
