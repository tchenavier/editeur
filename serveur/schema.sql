-- schema.sql — tables du serveur de synchronisation (MariaDB / MySQL).
--
--     mysql -u root -p < schema.sql
--
-- CE QUI N'EST PAS ICI : les manuscrits. Ce sont des fichiers, rangés
-- dans un dossier par projet (voir « racine » dans config.json). Une base
-- sert aux relations et aux recherches ; y mettre des documents
-- compliquerait les sauvegardes sans rien apporter.
--
-- AUCUN SECRET EN CLAIR : les mots de passe sont hachés avec scrypt et un
-- sel par compte, et ce sont les EMPREINTES des jetons qui sont
-- conservées, jamais les jetons eux-mêmes. Une copie de cette base ne
-- permet donc de se faire passer pour personne.

CREATE DATABASE IF NOT EXISTS editeur_sync
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE editeur_sync;

CREATE TABLE IF NOT EXISTS comptes (
  id       INT UNSIGNED NOT NULL AUTO_INCREMENT,
  -- Le nom est unique ET sensible à la casse pour la comparaison :
  -- « Elise » et « elise » sont deux comptes différents, ce qui évite
  -- qu'on puisse s'inscrire sous le nom d'un autre à une majuscule près.
  nom      VARCHAR(64)  NOT NULL COLLATE utf8mb4_bin,
  sel      CHAR(32)     NOT NULL,   -- 16 octets en hexadécimal
  hachage  CHAR(128)    NOT NULL,   -- 64 octets en hexadécimal (scrypt)
  cree_le  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY nom_unique (nom)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS jetons (
  empreinte  CHAR(64)     NOT NULL,   -- SHA-256 du jeton, jamais le jeton
  compte     INT UNSIGNED NOT NULL,
  expiration DATETIME     NOT NULL,
  PRIMARY KEY (empreinte),
  KEY par_compte (compte),
  KEY par_expiration (expiration),
  CONSTRAINT jeton_compte FOREIGN KEY (compte) REFERENCES comptes (id)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS projets (
  -- L'identifiant vient du manuscrit lui-même (en-tête YAML « project: »),
  -- pas du serveur : c'est lui qui voyage avec le fichier.
  id            VARCHAR(64)  NOT NULL,
  proprietaire  INT UNSIGNED NOT NULL,
  cree_le       TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY par_proprietaire (proprietaire),
  CONSTRAINT projet_proprietaire FOREIGN KEY (proprietaire) REFERENCES comptes (id)
) ENGINE=InnoDB;

-- Partage en LECTURE SEULE (décision du 26 juillet 2026 : l'édition à
-- plusieurs viendra plus tard, si elle vient). Il n'y a volontairement
-- pas de colonne « droit » : tant qu'une seule valeur est possible, une
-- colonne qui pourrait en contenir d'autres serait un piège — quelqu'un
-- finirait par y écrire « ecriture » sans que le code le prévoie.
CREATE TABLE IF NOT EXISTS partages (
  projet   VARCHAR(64)  NOT NULL,
  compte   INT UNSIGNED NOT NULL,
  cree_le  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (projet, compte),
  KEY par_compte (compte),
  CONSTRAINT partage_projet FOREIGN KEY (projet) REFERENCES projets (id)
    ON DELETE CASCADE,
  CONSTRAINT partage_compte FOREIGN KEY (compte) REFERENCES comptes (id)
    ON DELETE CASCADE
) ENGINE=InnoDB;
