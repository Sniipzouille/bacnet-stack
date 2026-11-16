# Image Docker du serveur BACnet SmartCampus (bacnet-stack + apps/server).
#
# Construction (depuis la racine du dépôt) :
#   docker build -f smartcampus/docker/Dockerfile -t smartcampus/bacnet .
# Pour le Raspberry Pi depuis un PC x86 :
#   docker buildx build --platform linux/arm64 -f smartcampus/docker/Dockerfile \
#     -t smartcampus/bacnet --load .
# Lancement : voir smartcampus/docker/compose.yaml

# ---------------------------------------------------------------------------
# Étape 1 : compilation. On part d'une Debian stable légère et maintenue
# (Ubuntu focal, utilisée par le Dockerfile d'origine, n'a plus de mises à jour).
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS builder

# Outils de compilation C (gcc, make) et jq pour lire le fichier de config.
# --no-install-recommends évite les paquets inutiles ; on vide le cache apt
# dans la même couche pour garder l'image petite.
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential jq \
 && rm -rf /var/lib/apt/lists/*

# Fichier de configuration utilisé pour la section "static" (objets dont le
# nombre est fixé à la compilation). Modifiable avec --build-arg CONFIG=...
ARG CONFIG=smartcampus/config/objects.json

# Copie des sources de la stack (le .dockerignore exclut .git et les binaires).
WORKDIR /src
COPY . .

# Compilation du serveur et des seuls outils utiles, au lieu de "make all" :
# - BACNET_DEFINES : options calculées depuis la section "static" du JSON ;
# - BACNET_PORT=linux : couche réseau Linux ;
# - outils : bacwi (Who-Is), bacrp/bacwp (lecture/écriture), bacco/bacdo
#   (création/suppression d'objets), bacrd (ReinitializeDevice) ;
# - cibles "<dossier>.all" de apps/Makefile : la bibliothèque (lib) n'est
#   compilée qu'une fois, et writeprop n'a pas de cible propre.
RUN export BACNET_DEFINES="$(sh smartcampus/scripts/static-flags.sh "$CONFIG")" \
 && echo "Options statiques : $BACNET_DEFINES" \
 && make -s -C apps BACNET_PORT=linux lib.all server.all whois.all \
      readprop.all writeprop.all create-object.all delete-object.all reinit.all \
 && mkdir -p /out \
 && cp bin/bacserv bin/bacwi bin/bacrp bin/bacwp bin/bacco bin/bacdo bin/bacrd /out/

# ---------------------------------------------------------------------------
# Étape 2 : image finale. Seulement les binaires, sans compilateur.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS bacnet

# Utilisateur sans droits root : le port 47808 (> 1024) n'en a pas besoin.
RUN useradd --system --create-home --home-dir /data bacnet

# Binaires compilés à l'étape 1, et configuration par défaut des objets
# (remplaçable au lancement par un fichier monté sur le même chemin).
COPY --from=builder /out/ /opt/bacnet/bin/
COPY smartcampus/config/objects.json /etc/smartcampus/objects.json

# Variables lues par le serveur :
# - BACNET_OBJECTS_CONFIG : fichier JSON des objets créés au démarrage ;
# - BACNET_BACKUP_FILE : sauvegarde automatique, relative au dossier de
#   travail /data (la stack refuse les chemins absolus) ;
# - BACNET_VERBOSE : 1 = log de chaque requête (modification du client).
ENV PATH="/opt/bacnet/bin:${PATH}" \
    BACNET_OBJECTS_CONFIG=/etc/smartcampus/objects.json \
    BACNET_BACKUP_FILE=backup.bin \
    BACNET_VERBOSE=1

# /data contient la sauvegarde : à monter sur un volume pour la conserver
# quand le conteneur est recréé.
WORKDIR /data
VOLUME /data
USER bacnet

# Port BACnet/IP standard (UDP).
EXPOSE 47808/udp

# stdbuf -oL : affiche les logs ligne par ligne dans "docker logs" (sinon la
# sortie est mise en mémoire tampon quand elle ne va pas vers un terminal).
# Sans argument, le numéro et le nom du device viennent du fichier JSON ;
# "docker run ... smartcampus/bacnet 4321 Autre-Nom" les remplace.
ENTRYPOINT ["stdbuf", "-oL", "/opt/bacnet/bin/bacserv"]
CMD []
