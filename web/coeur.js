// coeur.js
// -----------------------------------------------------------------------
// La logique du serveur web, SANS le réseau et SANS la base.
//
// Même découpage que pour le serveur de synchronisation, et pour la même
// raison : tout ce qui peut être faux de façon dangereuse doit pouvoir
// être éprouvé sans rien installer. « node test_coeur.js » suffit.
//
// MAIS L'ENJEU N'EST PAS LE MÊME. Le serveur de synchronisation garde des
// fichiers pour leur propriétaire. Celui-ci FABRIQUE DES PAGES QUE DES
// INCONNUS VONT OUVRIR, à partir de textes écrits par d'autres inconnus.
// C'est la définition même d'une surface à injection.
//
// D'où la règle qui gouverne ce fichier : **on n'accepte jamais de
// balisage, et on n'en produit que depuis une liste fermée**. Le texte de
// l'auteur est échappé, toujours, sans exception et sans option pour
// désactiver l'échappement. Il n'existe aucun chemin dans ce code par
// lequel une chaîne venue de l'extérieur atteint la page sans passer par
// echapper().
//
// AUCUNE DÉPENDANCE : uniquement « crypto » de Node.
// -----------------------------------------------------------------------

'use strict';

const crypto = require('crypto');

const VERSION_API = 1;

// Bornes. Un texte publié est du contenu d'auteur : généreux, mais borné.
const TAILLE_MAX_TEXTE = 4 * 1024 * 1024;   // ~700 000 mots
const TAILLE_MAX_FORMULAIRE = 8 * 1024;
const MAX_BLOCS = 20000;
const MAX_TITRE = 200;
const MAX_RESUME = 1000;
const MAX_ADRESSE = 80;

// Les seuls types de bloc produits. Liste FERMÉE : ce qui n'y figure pas
// n'est pas rendu. Même règle que l'export EPUB de l'éditeur.
const BLOCS_AUTORISES = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6'];

const ETATS = ['brouillon', 'lien', 'publie'];

// --- Échappement --------------------------------------------------------

// LA fonction dont tout dépend. Les cinq caractères qui comptent, et rien
// d'autre à retenir.
//
// L'apostrophe et le guillemet sont échappés aussi : ce texte peut se
// retrouver dans un attribut (un titre de page, un lien), et un
// échappement qui ne vaudrait que hors attribut est un échappement qui
// finira par manquer.
function echapper(texte) {
  return String(texte === undefined || texte === null ? '' : texte)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

// --- Adresses lisibles --------------------------------------------------

// « Le Crépuscule de Regina » -> « le-crepuscule-de-regina ».
// Les accents sont dépliés plutôt que supprimés : « forêt » donne
// « foret » et non « frt ».
function adresseDepuis(titre) {
  // La plage ̀-ͯ est celle des accents combinants : après une
  // décomposition NFD, « é » devient « e » + accent, et retirer l'accent
  // laisse le « e ». Écrite en échappements plutôt qu'en caractères, pour
  // qu'elle reste lisible et vérifiable dans n'importe quel éditeur.
  const sansAccents = String(titre || '')
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '');

  const adresse = sansAccents
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, MAX_ADRESSE)
    .replace(/-+$/g, '');

  return adresse;
}

// Une adresse acceptable dans une URL, et rien de plus : minuscules,
// chiffres, tirets. Aucun point (pas de « ../ », pas de « .html »), aucune
// barre oblique.
function adresseSure(adresse) {
  return typeof adresse === 'string' &&
         adresse.length > 0 && adresse.length <= MAX_ADRESSE &&
         /^[a-z0-9]+(-[a-z0-9]+)*$/.test(adresse);
}

// Un nom de compte, tel qu'il arrive du serveur de synchronisation. Il
// apparaîtra dans une adresse (« /auteur/Élise ») : les accents sont donc
// acceptés — ils s'encodent — mais pas ce qui découperait l'adresse ou en
// changerait le sens.
//
// Les caractères interdits sont donnés par leur POINT DE CODE, et non par
// une plage d'expression régulière. Une plage comme « [ -/] » couvre en
// réalité seize caractères que personne ne relit — et c'est exactement le
// genre de raccourci qui laisse passer ce qu'on croyait exclure.
//        /    \    :    ?    #    %    &    "    '    <    >    [    ]    {    }    |    ^    `
const INTERDITS = [0x2f, 0x5c, 0x3a, 0x3f, 0x23, 0x25, 0x26, 0x22, 0x27,
                   0x3c, 0x3e, 0x5b, 0x5d, 0x7b, 0x7d, 0x7c, 0x5e, 0x60];

function nomSur(nom) {
  if (typeof nom !== 'string') return false;
  if (nom.length === 0 || nom.length > 64) return false;
  if (nom !== nom.trim()) return false; // espaces au bord : source de confusion
  for (const c of nom) {
    const code = c.codePointAt(0);
    if (code < 0x20 || code === 0x7f) return false;
    if (INTERDITS.indexOf(code) >= 0) return false;
  }
  return true;
}

// --- Codes d'activation -------------------------------------------------

// Un code lisible à voix haute et recopiable sans erreur : ni O/0, ni
// I/1/L, ni caractères qui se ressemblent d'une police à l'autre. Il sera
// dicté au téléphone ou recopié depuis un terminal.
const ALPHABET_CODE = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';

function nouveauCode() {
  const groupes = [];
  for (let g = 0; g < 3; g++) {
    let groupe = '';
    for (let i = 0; i < 4; i++) {
      // randomInt, pas Math.random : ce code vaut un mot de passe le temps
      // de son usage.
      groupe += ALPHABET_CODE[crypto.randomInt(ALPHABET_CODE.length)];
    }
    groupes.push(groupe);
  }
  return groupes.join('-');
}

// La saisie est tolérante — minuscules, espaces, tirets oubliés — parce
// que c'est un code qu'on recopie à la main. La COMPARAISON, elle, ne
// l'est pas.
function normaliserCode(code) {
  return String(code || '').toUpperCase().replace(/[^0-9A-Z]/g, '');
}

function memeSecret(a, b) {
  const bufA = Buffer.from(String(a), 'utf8');
  const bufB = Buffer.from(String(b), 'utf8');
  if (bufA.length !== bufB.length) return false;
  return crypto.timingSafeEqual(bufA, bufB);
}

// --- Mots de passe et jetons -------------------------------------------
// Identiques au serveur de synchronisation, et volontairement recopiés
// plutôt que partagés : les deux serveurs ne doivent avoir AUCUN code
// commun, sans quoi une dépendance finirait par relier deux machines qu'on
// a justement séparées.

const SCRYPT_N = 16384;
const SCRYPT_R = 8;
const SCRYPT_P = 1;
const SCRYPT_OCTETS = 64;

function hacherMotDePasse(motDePasse, selHex) {
  const sel = selHex ? Buffer.from(selHex, 'hex') : crypto.randomBytes(16);
  const derive = crypto.scryptSync(motDePasse, sel, SCRYPT_OCTETS,
                                   { N: SCRYPT_N, r: SCRYPT_R, p: SCRYPT_P });
  return { sel: sel.toString('hex'), hachage: derive.toString('hex') };
}

function motDePasseValide(motDePasse, selHex, hachageAttendu) {
  if (!motDePasse || !selHex || !hachageAttendu) return false;
  const { hachage } = hacherMotDePasse(motDePasse, selHex);
  return memeSecret(hachage, hachageAttendu);
}

function nouveauJeton() {
  const jeton = crypto.randomBytes(32).toString('hex');
  return { jeton, empreinte: empreinteJeton(jeton) };
}

function empreinteJeton(jeton) {
  return crypto.createHash('sha256').update(String(jeton), 'utf8').digest('hex');
}

// --- Lecture d'un texte déposé -----------------------------------------

// Le format : un en-tête « clé valeur », une ligne « -- », puis un bloc
// par ligne (« type texte »). Voir PROTOCOLE.md.
//
// Lecture STRICTE, contrairement aux fichiers du disque : ici l'entrée
// vient d'Internet. Une ligne qu'on ne comprend pas est écartée, et un
// texte sans bloc valide est refusé — mieux vaut un refus explicite
// qu'une page vide publiée sous le nom de quelqu'un.
function lireTexte(corps) {
  const resultat = { titre: '', adresse: '', resume: '', etat: 'brouillon',
                     blocs: [], erreur: null };

  if (typeof corps !== 'string' || corps.length === 0) {
    resultat.erreur = 'Texte vide.';
    return resultat;
  }
  if (corps.length > TAILLE_MAX_TEXTE) {
    resultat.erreur = 'Texte trop volumineux.';
    return resultat;
  }

  const lignes = corps.split('\n');
  let i = 0;
  for (; i < lignes.length; i++) {
    const ligne = lignes[i].replace(/\r$/, '');
    if (ligne.trim() === '--') { i++; break; }
    const espace = ligne.indexOf(' ');
    if (espace <= 0) continue;
    const cle = ligne.slice(0, espace);
    const valeur = ligne.slice(espace + 1).trim();
    if (cle === 'titre') resultat.titre = valeur.slice(0, MAX_TITRE);
    else if (cle === 'adresse') resultat.adresse = valeur.slice(0, MAX_ADRESSE);
    else if (cle === 'resume') resultat.resume = valeur.slice(0, MAX_RESUME);
    else if (cle === 'etat' && ETATS.includes(valeur)) resultat.etat = valeur;
  }

  for (; i < lignes.length; i++) {
    const ligne = lignes[i].replace(/\r$/, '');
    if (ligne.trim() === '') continue;
    const espace = ligne.indexOf(' ');
    if (espace <= 0) continue;
    const type = ligne.slice(0, espace);
    const texte = ligne.slice(espace + 1);
    // Type inconnu : la ligne est écartée. On ne devine pas, et on ne
    // rabat pas sur « p » — un texte mal formé doit se voir.
    if (!BLOCS_AUTORISES.includes(type)) continue;
    if (texte.trim() === '') continue;
    resultat.blocs.push({ type, texte });
    if (resultat.blocs.length >= MAX_BLOCS) break;
  }

  if (!resultat.titre) resultat.erreur = 'Titre manquant.';
  else if (resultat.blocs.length === 0) resultat.erreur = 'Aucun texte à publier.';

  if (!resultat.adresse) resultat.adresse = adresseDepuis(resultat.titre);
  if (!adresseSure(resultat.adresse) && !resultat.erreur) {
    resultat.erreur = 'Adresse de publication invalide.';
  }
  return resultat;
}

// --- Rendu --------------------------------------------------------------

// Fabrique le corps de la page de lecture. TOUT passe par echapper() :
// le titre, le résumé, le nom de l'auteur, chaque bloc. Les seules balises
// produites viennent de BLOCS_AUTORISES, qui est une liste fermée de
// noms d'éléments — jamais une valeur venue de l'extérieur.
function rendreTexte(texte) {
  const morceaux = [];
  morceaux.push('<article class="texte">');
  morceaux.push('<h1>' + echapper(texte.titre) + '</h1>');
  if (texte.auteur) {
    morceaux.push('<p class="auteur">par ' + echapper(texte.auteur) + '</p>');
  }
  if (texte.resume) {
    morceaux.push('<p class="resume">' + echapper(texte.resume) + '</p>');
  }
  for (const bloc of texte.blocs || []) {
    const type = BLOCS_AUTORISES.includes(bloc.type) ? bloc.type : 'p';
    morceaux.push('<' + type + '>' + echapper(bloc.texte) + '</' + type + '>');
  }
  morceaux.push('</article>');
  return morceaux.join('\n');
}

// Sérialise les blocs pour le stockage : le MÊME format qu'à l'entrée,
// donc relisible par lireTexte. Un seul format, un seul analyseur.
function serialiserBlocs(blocs) {
  return (blocs || [])
    .filter((b) => BLOCS_AUTORISES.includes(b.type))
    .map((b) => b.type + ' ' + String(b.texte).replace(/[\r\n]+/g, ' '))
    .join('\n');
}

function lireBlocs(texte) {
  return lireTexte('titre x\n--\n' + String(texte || '')).blocs;
}

// --- Formulaires --------------------------------------------------------

function lireFormulaire(corps) {
  const champs = {};
  if (typeof corps !== 'string' || corps.length > TAILLE_MAX_FORMULAIRE) return champs;
  let compte = 0;
  for (const paire of corps.split('&')) {
    if (++compte > 20) break;
    const egal = paire.indexOf('=');
    if (egal <= 0) continue;
    try {
      const nom = decodeURIComponent(paire.slice(0, egal).replace(/\+/g, ' '));
      const valeur = decodeURIComponent(paire.slice(egal + 1).replace(/\+/g, ' '));
      if (!(nom in champs)) champs[nom] = valeur;
    } catch (_) { /* échappement invalide : champ ignoré */ }
  }
  return champs;
}

class Limiteur {
  constructor(maxTentatives = 10, fenetreMs = 5 * 60 * 1000) {
    this.max = maxTentatives;
    this.fenetre = fenetreMs;
    this.essais = new Map();
  }
  autorise(cle, maintenant = Date.now()) {
    const liste = (this.essais.get(cle) || []).filter((t) => maintenant - t < this.fenetre);
    if (liste.length >= this.max) { this.essais.set(cle, liste); return false; }
    liste.push(maintenant);
    this.essais.set(cle, liste);
    return true;
  }
  reussite(cle) { this.essais.delete(cle); }
}

module.exports = {
  VERSION_API,
  TAILLE_MAX_TEXTE,
  TAILLE_MAX_FORMULAIRE,
  BLOCS_AUTORISES,
  ETATS,
  echapper,
  adresseDepuis,
  adresseSure,
  nomSur,
  nouveauCode,
  normaliserCode,
  memeSecret,
  hacherMotDePasse,
  motDePasseValide,
  nouveauJeton,
  empreinteJeton,
  lireTexte,
  rendreTexte,
  serialiserBlocs,
  lireBlocs,
  lireFormulaire,
  Limiteur,
};
