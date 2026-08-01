// coeur.js
// -----------------------------------------------------------------------
// La logique du serveur, SANS le réseau et SANS la base de données.
//
// POURQUOI CETTE SÉPARATION. Ce fichier contient tout ce qui peut être
// faux de façon dangereuse : la validation des chemins, le hachage des
// mots de passe, la comparaison des jetons, les règles de permission. Ce
// sont exactement les parties qu'il faut pouvoir éprouver sans rien
// installer — sans MariaDB, sans port ouvert, sans configuration.
//
// « node test_coeur.js » suffit donc à tout vérifier, sur n'importe quelle
// machine où Node est présent. Le reste (serveur.js, base.js) n'est que du
// câblage : recevoir une requête, appeler d'ici, répondre.
//
// AUCUNE DÉPENDANCE ici : uniquement le module « crypto » de Node.
// -----------------------------------------------------------------------

'use strict';

const crypto = require('crypto');
const path = require('path');

// Version du contrat parlée par ce serveur (voir net/PROTOCOLE.md).
const VERSION_API = 1;

// Bornes. Elles font partie du contrat : au-delà, on refuse plutôt que de
// laisser un client remplir le disque ou la mémoire du serveur.
const TAILLE_MAX_FICHIER = 64 * 1024 * 1024;
const TAILLE_MAX_FORMULAIRE = 8 * 1024;
const LONGUEUR_MAX_CHEMIN = 1024;

// --- Chemins -----------------------------------------------------------

// Un chemin acceptable : relatif au projet, séparateur « / », sans
// remontée, sans antislash, sans caractère de contrôle.
//
// CETTE FONCTION EST REFAITE ICI alors que le client la fait déjà. Ce
// n'est pas un doublon : le client peut être n'importe quel programme, y
// compris un qui nous veut du mal. Un serveur qui fait confiance à la
// validation de son client ne valide rien du tout.
function cheminSur(chemin) {
  if (typeof chemin !== 'string') return false;
  if (chemin.length === 0 || chemin.length > LONGUEUR_MAX_CHEMIN) return false;
  if (chemin.startsWith('/')) return false;
  if (chemin.includes('\\')) return false;
  if (chemin.includes(':')) return false;
  if (chemin.includes('\0')) return false;

  for (const c of chemin) {
    const code = c.codePointAt(0);
    if (code < 0x20 || code === 0x7f) return false;
  }
  for (const segment of chemin.split('/')) {
    if (segment === '' || segment === '.' || segment === '..') return false;
  }
  return true;
}

// Identifiant de projet : un UUID, rien d'autre. Il finit dans un nom de
// dossier, donc il ne doit contenir que ce qui est inoffensif.
function projetSur(identifiant) {
  return typeof identifiant === 'string' &&
         /^[0-9a-fA-F-]{8,64}$/.test(identifiant) &&
         !identifiant.includes('--');
}

// LA DERNIÈRE BARRIÈRE avant de toucher au disque. cheminSur() écarte
// déjà tout ce qui ressemble à une remontée, mais une validation par
// motifs peut toujours laisser passer ce à quoi on n'a pas pensé — un
// encodage inattendu, un lien symbolique, une particularité du système de
// fichiers. On calcule donc le chemin absolu et on VÉRIFIE qu'il reste
// dans le dossier du projet. Cette vérification-là ne dépend d'aucune
// hypothèse sur la forme de l'entrée.
//
// Retourne le chemin absolu, ou null.
function resoudreChemin(racine, idProjet, chemin) {
  if (!projetSur(idProjet) || !cheminSur(chemin)) return null;

  const dossierProjet = path.resolve(racine, idProjet);
  const complet = path.resolve(dossierProjet, chemin);

  // Le séparateur final évite qu'un dossier « projet-bis » passe pour un
  // sous-dossier de « projet ».
  if (complet !== dossierProjet &&
      !complet.startsWith(dossierProjet + path.sep)) {
    return null;
  }
  return complet;
}

// --- Mots de passe -----------------------------------------------------
//
// scrypt, fourni par Node, avec un sel par compte. Jamais de mot de passe
// en clair, jamais de hachage rapide (un SHA-256 seul se force au
// milliard d'essais par seconde sur une carte graphique).

const SCRYPT_N = 16384; // coût processeur/mémoire
const SCRYPT_R = 8;
const SCRYPT_P = 1;
const SCRYPT_OCTETS = 64;

function hacherMotDePasse(motDePasse, selHex) {
  const sel = selHex ? Buffer.from(selHex, 'hex') : crypto.randomBytes(16);
  const derive = crypto.scryptSync(motDePasse, sel, SCRYPT_OCTETS,
                                   { N: SCRYPT_N, r: SCRYPT_R, p: SCRYPT_P });
  return { sel: sel.toString('hex'), hachage: derive.toString('hex') };
}

// Comparaison à temps constant : comparer deux chaînes avec « === »
// s'arrête au premier caractère différent, ce qui renseigne un attaquant
// sur le nombre de caractères qu'il a devinés.
function memeSecret(a, b) {
  const bufA = Buffer.from(String(a), 'utf8');
  const bufB = Buffer.from(String(b), 'utf8');
  if (bufA.length !== bufB.length) return false;
  return crypto.timingSafeEqual(bufA, bufB);
}

function motDePasseValide(motDePasse, selHex, hachageAttendu) {
  if (!motDePasse || !selHex || !hachageAttendu) return false;
  const { hachage } = hacherMotDePasse(motDePasse, selHex);
  return memeSecret(hachage, hachageAttendu);
}

// --- Jetons ------------------------------------------------------------

// Le jeton livré au client est aléatoire ; ce qui est CONSERVÉ est son
// empreinte. Une copie de la base ne permet donc pas de se faire passer
// pour quelqu'un : c'est le même raisonnement que pour les mots de passe.
function nouveauJeton() {
  const jeton = crypto.randomBytes(32).toString('hex');
  return { jeton, empreinte: empreinteJeton(jeton) };
}

function empreinteJeton(jeton) {
  return crypto.createHash('sha256').update(String(jeton), 'utf8').digest('hex');
}

// --- Empreintes de contenu ---------------------------------------------

function sha256(contenu) {
  return crypto.createHash('sha256').update(contenu).digest('hex');
}

// L'en-tête « If-Match » arrive entre guillemets, parfois précédé de W/.
function nettoyerEtag(valeur) {
  if (typeof valeur !== 'string') return '';
  let v = valeur.trim();
  if (v.startsWith('W/')) v = v.slice(2);
  if (v.startsWith('"')) v = v.slice(1);
  if (v.endsWith('"')) v = v.slice(0, -1);
  return v;
}

// --- Contrôle d'accès ---------------------------------------------------
//
// LA RÈGLE : le propriétaire écrit, les autres lisent — et seulement s'ils
// ont été invités. Le partage en lecture seule vient d'abord (décision du
// 26 juillet 2026) ; aucun chemin de ce fichier n'accorde l'écriture à
// quelqu'un d'autre que le propriétaire.
const DROIT_AUCUN = 'aucun';
const DROIT_LECTURE = 'lecture';
const DROIT_PROPRIETAIRE = 'proprietaire';

function droitSur(projet, partages, idCompte) {
  if (!projet) return DROIT_AUCUN;
  if (projet.proprietaire === idCompte) return DROIT_PROPRIETAIRE;
  for (const partage of partages || []) {
    if (partage.compte === idCompte && partage.projet === projet.id) {
      return DROIT_LECTURE; // jamais mieux que la lecture, à ce jalon
    }
  }
  return DROIT_AUCUN;
}

function peutLire(droit) {
  return droit === DROIT_LECTURE || droit === DROIT_PROPRIETAIRE;
}

function peutEcrire(droit) {
  return droit === DROIT_PROPRIETAIRE;
}

// --- Limitation des tentatives ------------------------------------------
//
// Ralentir les essais de mots de passe. En mémoire, volontairement : un
// serveur personnel n'a pas besoin de plus, et un compteur en base
// ajouterait une écriture à chaque tentative — donc un moyen de le
// fatiguer.
class Limiteur {
  constructor(maxTentatives = 10, fenetreMs = 5 * 60 * 1000) {
    this.max = maxTentatives;
    this.fenetre = fenetreMs;
    this.essais = new Map();
  }

  // Retourne true si la tentative est autorisée.
  autorise(cle, maintenant = Date.now()) {
    const liste = (this.essais.get(cle) || []).filter((t) => maintenant - t < this.fenetre);
    if (liste.length >= this.max) {
      this.essais.set(cle, liste);
      return false;
    }
    liste.push(maintenant);
    this.essais.set(cle, liste);
    return true;
  }

  reussite(cle) {
    this.essais.delete(cle);
  }
}

// --- Formulaires --------------------------------------------------------

// Lecture d'un corps « application/x-www-form-urlencoded ». Écrite à la
// main plutôt que par URLSearchParams pour BORNER le nombre de champs :
// un corps contenant cent mille paramètres ne doit pas occuper le serveur.
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
    } catch (_) {
      // Séquence d'échappement invalide : on ignore ce champ plutôt que
      // de faire échouer toute la requête.
    }
  }
  return champs;
}

// --- Inventaire ---------------------------------------------------------
//
// Le MÊME format que celui écrit sur le disque de l'auteur (voir
// io/ProjectIndex). Un seul format pour les deux côtés : ce qui s'écrit
// ici se relit là-bas par du code déjà éprouvé, déjà passé au fuzzer.
//
// L'ORDRE DES CHAMPS EST CELUI DU C++ et rien d'autre :
//     f <empreinte> <taille> <date> <chemin>
// Le chemin vient en DERNIER parce qu'il peut contenir des espaces ; les
// autres champs, non. Une inversion ici produirait un inventaire que le
// client rejetterait ligne par ligne, en silence — d'où le test qui
// compare la sortie caractère par caractère à un exemplaire de référence.
function serialiserInventaire(idProjet, fichiers) {
  const lignes = ['# inventaire de projet 1'];
  if (idProjet) lignes.push('projet ' + idProjet);
  const triees = fichiers.slice().sort((a, b) => (a.chemin < b.chemin ? -1 : 1));
  for (const f of triees) {
    lignes.push(['f', f.sha256, String(f.taille), f.date || '-', f.chemin].join(' '));
  }
  return lignes.join('\n') + '\n';
}

module.exports = {
  VERSION_API,
  TAILLE_MAX_FICHIER,
  TAILLE_MAX_FORMULAIRE,
  cheminSur,
  projetSur,
  resoudreChemin,
  hacherMotDePasse,
  motDePasseValide,
  memeSecret,
  nouveauJeton,
  empreinteJeton,
  sha256,
  nettoyerEtag,
  droitSur,
  peutLire,
  peutEcrire,
  DROIT_AUCUN,
  DROIT_LECTURE,
  DROIT_PROPRIETAIRE,
  Limiteur,
  lireFormulaire,
  serialiserInventaire,
};
