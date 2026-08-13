// test_coeur.js
// Tests de la logique du serveur web, SANS base et SANS réseau.
//
//     node test_coeur.js
//
// L'ENJEU N'EST PAS CELUI DU SERVEUR DE SYNCHRONISATION. Celui-ci garde
// des fichiers pour leur propriétaire ; celui-là fabrique des pages que
// des inconnus vont ouvrir, à partir de textes écrits par d'autres
// inconnus. C'est la définition d'une surface à injection.
//
// La moitié de ce fichier porte donc sur une seule question : est-il
// possible qu'un texte d'auteur atteigne la page sans être échappé ?

'use strict';

const assert = require('assert');
const coeur = require('./coeur');

let reussis = 0;
function cas(titre, fn) { fn(); reussis++; console.log('OK  ' + titre); }

// --- Échappement : LA fonction dont tout dépend ------------------------

cas('les cinq caractères qui comptent', () => {
  assert.strictEqual(coeur.echapper('<script>'), '&lt;script&gt;');
  assert.strictEqual(coeur.echapper('a & b'), 'a &amp; b');
  assert.strictEqual(coeur.echapper('dit "bonjour"'), 'dit &quot;bonjour&quot;');
  assert.strictEqual(coeur.echapper("l'ami"), 'l&#39;ami');

  // L'esperluette est traitée EN PREMIER, sinon « &lt; » deviendrait
  // « &amp;lt; » et la page afficherait le code au lieu du texte.
  assert.strictEqual(coeur.echapper('&lt;'), '&amp;lt;');

  // Ce qui n'a pas à être touché ne l'est pas : un roman est plein
  // d'accents, de tirets cadratins et de guillemets français.
  assert.strictEqual(coeur.echapper('Été — « forêt »'), 'Été — « forêt »');

  assert.strictEqual(coeur.echapper(null), '');
  assert.strictEqual(coeur.echapper(undefined), '');
  assert.strictEqual(coeur.echapper(42), '42');
});

cas('AUCUN texte d\'auteur n\'atteint la page sans échappement', () => {
  // Le test qui compte vraiment : on publie une charge hostile dans
  // chaque champ, et on vérifie qu'aucune balise ne survit.
  const charges = [
    '<script>alert(1)</script>',
    '"><script>alert(1)</script>',
    "' onmouseover='alert(1)",
    '<img src=x onerror=alert(1)>',
    '<a href="javascript:alert(1)">clic</a>',
    '</p><script>alert(1)</script><p>',
    '<svg/onload=alert(1)>',
    '<!--<script>-->',
  ];

  for (const charge of charges) {
    const page = coeur.rendreTexte({
      titre: charge,
      auteur: charge,
      resume: charge,
      blocs: [{ type: 'p', texte: charge }, { type: 'h2', texte: charge }],
    });

    // Aucune balise ouvrante autre que celles que NOUS produisons.
    const balises = page.match(/<[a-zA-Z!/]/g) || [];
    const attendues = (page.match(/<\/?(article|h1|h2|p)[ >]/g) || []).length;
    assert.strictEqual(balises.length, attendues,
      'balise inattendue rendue pour : ' + charge + '\n' + page);

    assert.ok(!page.includes('<script'), 'script rendu pour : ' + charge);
    assert.ok(!page.includes('onerror='), 'attribut rendu pour : ' + charge);
    assert.ok(!page.includes('onload='), 'attribut rendu pour : ' + charge);
    assert.ok(!page.includes('javascript:'), 'lien actif rendu pour : ' + charge);
  }
});

cas('le type de bloc vient d\'une liste fermée', () => {
  // Un type inventé ne doit pas devenir une balise — c'est l'autre
  // façon d'injecter : par le NOM de l'élément et non par son contenu.
  const page = coeur.rendreTexte({
    titre: 'Essai',
    blocs: [
      { type: 'script', texte: 'alert(1)' },
      { type: 'p onload=alert(1)', texte: 'ruse' },
      { type: 'p', texte: 'normal' },
    ],
  });
  assert.ok(!page.includes('<script'));
  assert.ok(!page.includes('onload'));
  // Les types inconnus retombent sur « p » au rendu : le texte reste lisible.
  assert.ok(page.includes('<p>alert(1)</p>'));
  assert.ok(page.includes('<p>normal</p>'));
});

// --- Adresses ----------------------------------------------------------

cas('adresses lisibles depuis un titre', () => {
  assert.strictEqual(coeur.adresseDepuis('Le Crépuscule de Regina'),
                     'le-crepuscule-de-regina');
  // Les accents sont DÉPLIÉS, pas supprimés : « forêt » donne « foret ».
  assert.strictEqual(coeur.adresseDepuis('La forêt d\'été'), 'la-foret-d-ete');
  assert.strictEqual(coeur.adresseDepuis('  Espaces   multiples  '),
                     'espaces-multiples');
  assert.strictEqual(coeur.adresseDepuis('!!!'), '');
  assert.strictEqual(coeur.adresseDepuis(''), '');
  // Bornée, et sans tiret qui traîne à la fin après la coupe.
  const longue = coeur.adresseDepuis('mot '.repeat(100));
  assert.ok(longue.length <= 80);
  assert.ok(!longue.endsWith('-'));
});

cas('adresses acceptées et refusées', () => {
  assert.ok(coeur.adresseSure('le-crepuscule'));
  assert.ok(coeur.adresseSure('acte1'));

  assert.ok(!coeur.adresseSure(''));
  assert.ok(!coeur.adresseSure('-debut'));
  assert.ok(!coeur.adresseSure('fin-'));
  assert.ok(!coeur.adresseSure('deux--tirets'));
  assert.ok(!coeur.adresseSure('Majuscule'));
  assert.ok(!coeur.adresseSure('avec.point'));   // pas de « .html »
  assert.ok(!coeur.adresseSure('../ailleurs'));
  assert.ok(!coeur.adresseSure('avec/barre'));
  assert.ok(!coeur.adresseSure('accentué'));
  assert.ok(!coeur.adresseSure('a'.repeat(200)));
  assert.ok(!coeur.adresseSure(null));
});

cas('noms de compte : les accents passent, pas ce qui coupe une adresse', () => {
  assert.ok(coeur.nomSur('elise'));
  assert.ok(coeur.nomSur('Élise'));       // il finira encodé dans l'adresse
  assert.ok(coeur.nomSur('jean-marc'));
  assert.ok(coeur.nomSur('Dev'));

  assert.ok(!coeur.nomSur(''));
  assert.ok(!coeur.nomSur(' elise'));     // espace au bord
  assert.ok(!coeur.nomSur('a/b'));
  assert.ok(!coeur.nomSur('a\\b'));
  assert.ok(!coeur.nomSur('a?b'));
  assert.ok(!coeur.nomSur('a#b'));
  assert.ok(!coeur.nomSur('a%2fb'));      // le pourcent, donc le ré-encodage
  assert.ok(!coeur.nomSur('<script>'));
  assert.ok(!coeur.nomSur('a\nb'));
  assert.ok(!coeur.nomSur('a'.repeat(100)));
  assert.ok(!coeur.nomSur(null));
});

// --- Codes d'activation ------------------------------------------------

cas('codes d\'activation : lisibles, aléatoires, tolérants à la saisie', () => {
  const code = coeur.nouveauCode();
  assert.ok(/^[0-9A-Z]{4}-[0-9A-Z]{4}-[0-9A-Z]{4}$/.test(code), code);

  // Aucun caractère qu'on puisse confondre en le recopiant ou en le
  // dictant : ni O/0, ni I/1/L.
  assert.ok(!/[O01IL]/.test(code), 'caractère ambigu dans ' + code);

  // Deux codes ne se ressemblent pas.
  const codes = new Set();
  for (let i = 0; i < 200; i++) codes.add(coeur.nouveauCode());
  assert.strictEqual(codes.size, 200);

  // La SAISIE est tolérante — c'est un code recopié à la main.
  const normal = coeur.normaliserCode(code);
  assert.strictEqual(coeur.normaliserCode(code.toLowerCase()), normal);
  assert.strictEqual(coeur.normaliserCode(code.replace(/-/g, ' ')), normal);
  assert.strictEqual(coeur.normaliserCode(' ' + code + ' '), normal);
  // Mais la COMPARAISON, elle, ne l'est pas.
  assert.ok(!coeur.memeSecret(normal, coeur.normaliserCode(coeur.nouveauCode())));
});

// --- Mots de passe et jetons -------------------------------------------

cas('mots de passe : propres à CE site, jamais en clair', () => {
  const a = coeur.hacherMotDePasse('mon mot de passe');
  const b = coeur.hacherMotDePasse('mon mot de passe');
  assert.notStrictEqual(a.sel, b.sel);
  assert.notStrictEqual(a.hachage, b.hachage);
  assert.ok(coeur.motDePasseValide('mon mot de passe', a.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('autre', a.sel, a.hachage));
  assert.ok(!coeur.motDePasseValide('', a.sel, a.hachage));
});

cas('jetons : seule leur empreinte est conservable', () => {
  const un = coeur.nouveauJeton();
  assert.strictEqual(un.jeton.length, 64);
  assert.strictEqual(un.empreinte, coeur.empreinteJeton(un.jeton));
  assert.notStrictEqual(un.empreinte, un.jeton);
});

// --- Lecture d'un texte déposé -----------------------------------------

cas('lecture d\'un texte déposé', () => {
  const corps = [
    'titre Le Crépuscule de Regina',
    'resume Une reine, un chasseur, un miroir.',
    'etat lien',
    '--',
    'h1 Acte I : Le Destin de Cendres',
    'p Le vent d\'hiver hurlait contre les remparts.',
    '',
    'p À seulement dix-sept ans, elle portait déjà le deuil.',
  ].join('\n');

  const texte = coeur.lireTexte(corps);
  assert.strictEqual(texte.erreur, null);
  assert.strictEqual(texte.titre, 'Le Crépuscule de Regina');
  assert.strictEqual(texte.etat, 'lien');
  // L'adresse est déduite du titre quand elle n'est pas donnée.
  assert.strictEqual(texte.adresse, 'le-crepuscule-de-regina');
  assert.strictEqual(texte.blocs.length, 3);
  assert.strictEqual(texte.blocs[0].type, 'h1');
  assert.strictEqual(texte.blocs[1].texte, 'Le vent d\'hiver hurlait contre les remparts.');
});

cas('lecture STRICTE : ce qui vient d\'Internet n\'est pas deviné', () => {
  // Un type de bloc inconnu est ÉCARTÉ, pas rabattu sur « p » : un texte
  // mal formé doit se voir plutôt que se publier de travers.
  const avecIntrus = coeur.lireTexte(
    'titre Essai\n--\np bon\nscript alert(1)\ndiv autre\np encore');
  assert.strictEqual(avecIntrus.blocs.length, 2);
  assert.ok(avecIntrus.blocs.every((b) => b.type === 'p'));

  // Un état inventé retombe sur le plus prudent : brouillon.
  assert.strictEqual(coeur.lireTexte('titre X\netat public-partout\n--\np a').etat,
                     'brouillon');
  // Et l'état par défaut est brouillon, pas publié.
  assert.strictEqual(coeur.lireTexte('titre X\n--\np a').etat, 'brouillon');

  // Refus explicites.
  assert.ok(coeur.lireTexte('').erreur);
  assert.ok(coeur.lireTexte('--\np sans titre').erreur);
  assert.ok(coeur.lireTexte('titre Vide\n--\n').erreur);
  assert.ok(coeur.lireTexte('titre !!!\n--\np a').erreur); // adresse indéductible
  assert.ok(coeur.lireTexte('titre X\nadresse ../ailleurs\n--\np a').erreur);
  assert.ok(coeur.lireTexte('titre X\n--\np a'.padEnd(5 * 1024 * 1024, ' ')).erreur);
  assert.ok(coeur.lireTexte(null).erreur);
});

cas('aller-retour des blocs par le même format', () => {
  const blocs = [{ type: 'h1', texte: 'Titre' }, { type: 'p', texte: 'Texte accentué : été.' }];
  const relus = coeur.lireBlocs(coeur.serialiserBlocs(blocs));
  assert.deepStrictEqual(relus, blocs);

  // Un saut de ligne dans un bloc casserait le format : il est ramené à
  // une espace à l'écriture, plutôt que de produire un fichier illisible.
  const serialise = coeur.serialiserBlocs([{ type: 'p', texte: 'a\nb' }]);
  assert.strictEqual(serialise, 'p a b');
  // Et un type interdit ne franchit pas non plus l'écriture.
  assert.strictEqual(coeur.serialiserBlocs([{ type: 'script', texte: 'x' }]), '');
});

// --- Formulaires et limitation -----------------------------------------

cas('formulaires et limitation des tentatives', () => {
  const champs = coeur.lireFormulaire('user=elise&secret=mot%20de%20passe');
  assert.strictEqual(champs.user, 'elise');
  assert.strictEqual(champs.secret, 'mot de passe');
  assert.deepStrictEqual(coeur.lireFormulaire('a=' + 'x'.repeat(20000)), {});

  const limiteur = new coeur.Limiteur(3, 1000);
  assert.ok(limiteur.autorise('a', 1000));
  assert.ok(limiteur.autorise('a', 1000));
  assert.ok(limiteur.autorise('a', 1000));
  assert.ok(!limiteur.autorise('a', 1000));
  assert.ok(limiteur.autorise('b', 1000));    // un autre n'est pas puni
  assert.ok(limiteur.autorise('a', 3000));    // la fenêtre glisse
});

console.log('\n' + reussis + ' cas passés — logique du site web vérifiée.');
