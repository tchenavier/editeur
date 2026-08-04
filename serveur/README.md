# Serveur de synchronisation

Le serveur qui tient les projets synchronisés, à installer sur une machine
Debian. Il applique le contrat [`../net/PROTOCOLE.md`](../net/PROTOCOLE.md) et
rien d'autre.

> **Ce serveur n'a jamais été exécuté par son auteur.** Il a été écrit sur une
> machine Windows sans Node.js. Sa logique sensible est couverte par des tests
> qui tournent sans rien installer, et son comportement complet par des tests
> de bout en bout qui ne demandent pas de base de données — mais **lancez-les
> avant de lui confier quoi que ce soit** (voir « Vérifier » plus bas). C'est
> une précaution qui prend dix secondes.

---

## Ce qu'il fait, et ce qu'il ne fait pas

**Il fait** : des comptes, des sessions, le dépôt et la lecture de fichiers par
projet, la détection des écritures concurrentes, et le partage en lecture seule.

**Il ne fait pas** : d'inscription. Il n'existe aucun point d'entrée pour créer
un compte depuis Internet ; les comptes se créent à la main sur le serveur
(`node compte.js creer`). Un serveur personnel n'a pas vocation à accepter
n'importe qui, et l'absence de cette porte est la façon la plus sûre de la
garder fermée.

**Il ne fait pas non plus** : de TLS. Placez-le derrière nginx ou Caddy le jour
où il sortira de votre réseau local. Un serveur qui déchiffre lui-même est un
serveur de plus à tenir à jour sur ce terrain-là.

---

## Choix qui méritent d'être connus

**Deux dépendances npm au plus** : `mysql2`, indispensable, et `dotenv`, qui
est **facultatif**. Le module `http` de Node fournit un serveur éprouvé par des
millions de sites : ce projet écrit lui-même son archive ZIP et son empreinte
SHA-256, mais pas son analyseur HTTP — celui-ci reçoit ce que n'importe qui lui
envoie, et c'est historiquement l'une des sources de failles les plus
productives.

**La configuration vient de l'environnement, pas d'un fichier.** `.env` n'est
qu'une façon commode de remplir cet environnement ; systemd sait le faire tout
seul avec `EnvironmentFile=`. C'est pourquoi `dotenv` est chargé dans un
`try` : absent, le serveur lit l'environnement tel quel et fonctionne
normalement. Les tests, eux, n'ont besoin ni de l'un ni de l'autre — c'est ce
qui leur permet de tourner sans aucune installation.

**Les manuscrits sont des fichiers, pas des lignes de base.** Un dossier par
projet. La base ne contient que ce qu'elle fait bien : comptes, jetons,
projets, partages. Sauvegarder ce serveur, c'est donc copier un dossier et
exporter une petite base.

**Rien de déchiffrable n'est stocké.** Les mots de passe sont hachés avec
scrypt et un sel par compte ; ce sont les *empreintes* des jetons qui sont
conservées, jamais les jetons. Une copie de la base ne permet de se faire
passer pour personne.

**Le serveur ne fait pas confiance au client.** Les chemins sont revalidés ici,
puis résolus en chemin absolu et vérifiés comme restant dans le dossier du
projet. Le client fait déjà cette validation ; ce n'est pas un doublon, c'est
la seule qui compte — l'autre peut être contournée en n'utilisant pas notre
client.

---

## Installer

```bash
sudo apt install nodejs mariadb-server
sudo mkdir -p /var/lib/editeur-sync/projets
sudo cp -r serveur /opt/editeur-sync
cd /opt/editeur-sync
npm install mysql2
```

Base de données :

```bash
sudo mysql < schema.sql
sudo mysql -e "CREATE USER 'editeur_sync'@'localhost' IDENTIFIED BY 'un-mot-de-passe-solide';
               GRANT SELECT, INSERT, UPDATE, DELETE ON editeur_sync.* TO 'editeur_sync'@'localhost';"
```

Le compte de base n'a **que** les quatre droits nécessaires : ni `DROP`, ni
`ALTER`. Une faille dans le serveur ne pourra pas effacer les tables.

Configuration :

```bash
cp .env.exemple .env
chmod 600 .env            # il contient le mot de passe de la base
```

Puis remplissez `.env` — le fichier d'exemple explique chaque variable.

Premier compte :

```bash
node compte.js creer elise
```

---

## Vérifier

**Avant tout usage réel**, et sans rien installer de plus :

```bash
node test_coeur.js      # chemins, mots de passe, jetons, permissions
node test_serveur.js    # le serveur entier, sans base de données
```

Le second lance un vrai serveur sur un port attribué par le système, dans un
dossier temporaire effacé à la fin. Il vérifie surtout ce que le serveur
**refuse** : mauvais mot de passe, jeton absent, projet d'un autre, chemin qui
remonte, écriture sur un partage en lecture seule, écrasement sans `If-Match`.

Puis, en conditions réelles :

```bash
node serveur.js
curl http://127.0.0.1:8080/api/version
```

Si une variable manque, le serveur **refuse de démarrer** et dit laquelle. Un
serveur qui démarrerait avec un mot de passe vide, échouerait à joindre la base
et répondrait « panne » à chaque client serait bien plus difficile à
diagnostiquer.

---

## Lancer au démarrage

`/etc/systemd/system/editeur-sync.service` :

```ini
[Unit]
Description=Serveur de synchronisation de l'editeur
After=network.target mariadb.service

[Service]
Type=simple
User=editeur-sync
WorkingDirectory=/opt/editeur-sync

# Deux façons de fournir la configuration, au choix :
#   - laisser le fichier .env dans WorkingDirectory (dotenv le lira) ;
#   - ou la donner à systemd, et se passer entièrement de dotenv :
# EnvironmentFile=/etc/editeur-sync.env
ExecStart=/usr/bin/node serveur.js
Restart=on-failure

# Le serveur n'a besoin que de son dossier de projets.
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/editeur-sync

[Install]
WantedBy=multi-user.target
```

```bash
sudo useradd --system --no-create-home editeur-sync
sudo chown -R editeur-sync /var/lib/editeur-sync
sudo systemctl enable --now editeur-sync
sudo journalctl -u editeur-sync -f
```

---

## Partager un projet en lecture

```bash
node compte.js lister                       # retrouver l'identifiant du projet
node compte.js partager <id-projet> relecteur
```

Le relecteur voit alors le projet et peut le récupérer ; toute écriture de sa
part est refusée par le serveur, quoi qu'en dise son client.

---

## Sauvegarder

```bash
tar czf projets-$(date +%F).tar.gz /var/lib/editeur-sync/projets
mysqldump editeur_sync > base-$(date +%F).sql
```

Rappel utile : ces sauvegardes ne sont pas la sécurité de l'auteur. **Le
fichier sur sa machine reste la référence** ; ce serveur n'en est qu'un miroir.
Le perdre entièrement ne perdrait aucun manuscrit.
