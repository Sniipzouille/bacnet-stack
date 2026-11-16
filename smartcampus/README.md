# SmartCampus – serveur BACnet

Serveur BACnet/IP libre pour Raspberry Pi, basé sur
[bacnet-stack](https://github.com/bacnet-stack/bacnet-stack) (serveur de démo `apps/server`).
Projet M1 Informatique, Université Savoie Mont Blanc.

Ce dossier (`smartcampus/`) contient tout ce que nous avons ajouté à la stack :
configuration des objets, Docker, scripts, test du client BACnet.js.
Les fichiers de la stack modifiés sont listés [à la fin](#fichiers-modifiés-par-rapport-à-bacnet-stack).

## Ce que fait le serveur

- **Démarre sans objets d'exemple** : seulement le device, le port réseau, le fichier de sauvegarde
  et les objets décrits dans [`config/objects.json`](config/objects.json).
- **Crée les objets du fichier de configuration** au démarrage, avec leurs propriétés
  (nom, description, unité, valeur...), sans recompiler.
- **Sauvegarde automatiquement** tous les objets 2 secondes après chaque WriteProperty,
  CreateObject ou DeleteObject, et les **restaure au redémarrage**.
- **Log chaque requête reçue** (ajout du client) ; `BACNET_VERBOSE=0` pour les couper.
- Tourne dans un **conteneur Docker** qui redémarre tout seul.

## Démarrage rapide (sans Docker)

Prérequis : `git`, `make`, un compilateur C (`build-essential` sur Debian, Xcode CLT sur Mac), `jq`.

```bash
git clone -b smartcampus https://github.com/Sniipzouille/bacnet-stack.git
cd bacnet-stack
BACNET_DEFINES="$(smartcampus/scripts/static-flags.sh)" make -s server
BACNET_OBJECTS_CONFIG=smartcampus/config/objects.json ./bin/bacserv
```

Les lignes `[CFG] ... OK` montrent les objets créés. Sur le Pi en point d'accès, ajouter
`BACNET_IFACE=wlan0` devant la commande.

Pour compiler aussi les outils (`bacrp`, `bacwp`, `bacco`...) : `make -s all`
(en gardant le `BACNET_DEFINES=...` devant).

## Fichier de configuration des objets

[`config/objects.json`](config/objects.json) a trois sections :

```json
{
  "device": { "instance": 1234, "name": "SmartCampus" },
  "static": { "trend-log": 0, "access-door": 0 },
  "objects": [
    { "type": "analog-value", "instance": 1,
      "properties": { "object-name": "Consigne-B12", "units": "degrees-celsius",
                      "present-value": 19.0 } }
  ]
}
```

| Section | Rôle | Pris en compte |
|---|---|---|
| `device` | Numéro et nom du device. Les arguments `bacserv <numéro> <nom>` restent prioritaires. | Au démarrage |
| `static` | Nombre d'objets des types **qui ne peuvent pas être créés par CreateObject** (`trend-log`, `access-*`, `credential-data-input`). Leur taille est fixée dans le code C : le script `scripts/static-flags.sh` traduit cette section en options de compilation (`-DMAX_TREND_LOGS=0`...). | **À la compilation** (reconstruire l'image Docker) |
| `objects` | Objets créés au démarrage. `type` et noms de propriétés sont ceux de la norme BACnet (comme dans YABE ou `bacrp`). Le type de chaque valeur est déduit automatiquement : tout type d'objet créable fonctionne. | Au démarrage |

Le chemin du fichier est donné par la variable `BACNET_OBJECTS_CONFIG`. Sans elle, aucun objet n'est créé.

## Sauvegarde des objets

Le serveur réutilise la procédure *Backup/Restore* déjà présente dans la stack
(`Device_Start_Backup` / `Device_End_Restore` dans `src/bacnet/basic/object/device.c`) :
chaque objet est enregistré comme une requête CreateObject avec toutes ses propriétés modifiables.

- Fichier : variable `BACNET_BACKUP_FILE` (défaut `backup_1.bin`). **Chemin relatif au dossier
  de lancement**, la stack refuse les chemins absolus. Dans Docker : `/data/backup.bin`.
- Au démarrage : objets de la config, puis restauration de la sauvegarde si elle existe
  (**la sauvegarde l'emporte** sur la config).
- Pour repartir de la configuration : supprimer le fichier de sauvegarde
  (Docker : `docker compose -f smartcampus/docker/compose.yaml down -v`).

## Docker

### Construire l'image

Depuis la racine du dépôt :

```bash
# sur un Mac Apple Silicon ou sur le Pi (ARM 64 bits) :
docker build -f smartcampus/docker/Dockerfile -t smartcampus/bacnet .
# depuis un PC x86 pour le Pi (émulation ARM) :
docker buildx build --platform linux/arm64 -f smartcampus/docker/Dockerfile -t smartcampus/bacnet --load .
```

Construire sur le PC évite les ~3 minutes de compilation du Pi 3 à chaque essai.
Le [Dockerfile](docker/Dockerfile) est commenté ligne par ligne.

Sur Mac, il faut un moteur Docker. Nous utilisons Colima :
`brew install colima docker docker-buildx docker-compose && colima start`.

### Envoyer l'image sur le Pi et la lancer

```bash
docker save smartcampus/bacnet | ssh smartcampus@smartcampus.local docker load
```

Sur le Pi (une seule fois : `curl -fsSL https://get.docker.com | sh` puis
`sudo usermod -aG docker $USER`, se reconnecter) :

```bash
cd ~/SmartCampus/bacnet-stack            # le dépôt, pour compose.yaml et objects.json
sudo systemctl disable --now bacserv     # l'ancien service systemd, s'il existe
BACNET_IFACE=wlan0 docker compose -f smartcampus/docker/compose.yaml up -d
docker logs -f smartcampus-bacnet        # logs du serveur en direct
```

[`compose.yaml`](docker/compose.yaml) utilise le **réseau de l'hôte** (obligatoire pour le
broadcast Who-Is de YABE), **`restart: unless-stopped`** (relance au démarrage du Pi, remplace
le service systemd) et un **volume** pour la sauvegarde. `objects.json` est monté depuis le dépôt :
modifier les objets = modifier le fichier puis `docker compose ... restart`.

Interface réseau : `BACNET_IFACE=wlan0` en point d'accès, `eth0` sur le réseau filaire.

### Créer des objets après le démarrage

Exemple de l'idée du client (objets « dynamiques » créés par un script une fois le conteneur lancé) :

```bash
smartcampus/scripts/create-dynamic-objects.sh smartcampus/config/dynamic-objects.txt wlan0 1234
```

Le script lance `bacco` dans le conteneur. Les objets créés sont ensuite sauvegardés comme les autres.

## Tester le serveur

### Avec les outils de la stack (depuis un autre PC du même réseau)

```bash
./bin/bacwi -1 1234
./bin/bacrp 1234 analog-value 1 present-value
./bin/bacwp 1234 analog-value 1 present-value 16 -1 4 21.5
./bin/bacco 1234 analog-value 10 object-name 0 7 Consigne-C04 present-value 0 4 19.5
./bin/bacdo 1234 2 10
```

- `bacdo` n'accepte que le **numéro** du type d'objet (`2` = analog-value), pas son nom.
- Sur **la même machine** que le serveur, les outils doivent utiliser un autre port et s'enregistrer
  auprès du serveur : `export BACNET_IP_PORT=47809 BACNET_BBMD_PORT=47808 BACNET_BBMD_ADDRESS=<ip du serveur>`
  (+ `BACNET_IFACE` si la machine n'a pas de route par défaut).
- Avec zsh (Mac), ne pas coller de commentaires `# ...` après les commandes.

### Avec le client BACnet.js

```bash
cd smartcampus/client-test
npm install
node test.mjs <ip-du-serveur> 1234
```

Le script [`test.mjs`](client-test/test.mjs) teste ReadProperty, CreateObject, WriteProperty et
DeleteObject avec [`@bacnet-js/client`](https://www.npmjs.com/package/@bacnet-js/client) 3.3.2 :
les 4 services fonctionnent avec notre serveur.

**Bug trouvé dans `@bacnet-js/client` 3.3.2** : pour CreateObject, si `property.index` et `priority`
ne sont pas donnés, la bibliothèque encode un index de tableau 0 et une priorité 0 (elle compare
`undefined` à ses constantes) et le serveur refuse la création. Contournement : passer
`index: ASN1_ARRAY_ALL` et `priority: ASN1_NO_PRIORITY` (voir `initialValue()` dans `test.mjs`).

### Avec YABE (Windows)

*Add device* → *BACnet/IP over Udp* → choisir l'IP du PC sur le réseau du Pi → *Start*.

## Raspberry Pi en point d'accès Wi-Fi

Raspberry Pi 3 B+, Raspberry Pi OS récent (NetworkManager) :

```bash
sudo raspi-config nonint do_wifi_country FR
sudo nmcli radio wifi on
sudo nmcli device wifi hotspot ifname wlan0 con-name SmartCampus-AP ssid SmartCampus band bg password "<mot de passe>"
sudo nmcli connection modify SmartCampus-AP connection.autoconnect yes connection.autoconnect-priority 100
```

Le Pi est alors en `10.42.0.1`. SSH par câble Ethernet direct : `ssh smartcampus@smartcampus.local`.

## Interface graphique (proposition, non développée)

Un back Node.js utilisant `@bacnet-js/client` exposerait une petite API REST
(`GET /objects`, `GET/PUT /objects/:type/:instance/:property`, `POST /objects`, `DELETE /objects/:type/:instance`)
et une page web servirait de « YABE simplifié » : liste des objets avec leurs valeurs,
modification d'une valeur, création et suppression d'objets. Le back peut tourner dans un second
conteneur sur le Pi (réseau hôte, port BACnet local différent, enregistrement auprès du serveur
comme les outils ci-dessus) ou sur une autre machine.

## Fichiers modifiés par rapport à bacnet-stack

Base : commit `b7f1e995` de bacnet-stack (19/10/2026). Liste complète : `git diff --stat b7f1e995..smartcampus`.

| Fichier | Modification | Auteur |
|---|---|---|
| `apps/server/main.c` | Logs détaillés des requêtes (`BACNET_VERBOSE`) ; objets d'exemple seulement avec `-DSERVER_STATIC_OBJECTS` | Client |
| `apps/server/main.c` | Lecture de `BACNET_OBJECTS_CONFIG` (cJSON, déjà dans la stack) ; sauvegarde/restauration automatiques (`BACNET_BACKUP_FILE`) | Équipe |
| `src/bacnet/basic/object/schedule.c` | Écriture de tout l'`Exception_Schedule` en une requête | Client |
| `src/bacnet/basic/object/schedule.h` | Limites `BACNET_SCHEDULE_OBJ_PROP_REF_SIZE` et `BACNET_EXCEPTION_SCHEDULE_SIZE` portées à 128 | Client |
| `.dockerignore` | Exclut `.git` et les binaires du contexte Docker | Équipe |
| `smartcampus/**` | Nouveau dossier (config, Docker, scripts, test BACnet.js, ce README) | Équipe |

Aucun autre fichier de la stack n'est modifié : la section `static` passe par des options de
compilation (`BACNET_DEFINES`) déjà prévues par `apps/Makefile`.
