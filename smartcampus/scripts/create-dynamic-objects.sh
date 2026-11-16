#!/bin/sh
# Crée des objets "dynamiques" dans le serveur BACnet une fois le conteneur
# démarré, avec les outils de la stack (CreateObject via bacco). Les objets
# sont ensuite sauvegardés automatiquement par le serveur.
#
# Usage (sur le Pi, à côté du conteneur) :
#   smartcampus/scripts/create-dynamic-objects.sh fichier.txt [interface] [device]
#
# Format du fichier, une ligne par objet (les lignes vides et # sont ignorées) :
#   <type> <instance> <nom> [valeur]
#   analog-value 10 Consigne-C04 19.5
#
# Les outils tournent dans le même conteneur que le serveur : ils utilisent
# un autre port (47809) et s'enregistrent comme "foreign device" auprès du
# serveur (BBMD), comme indiqué dans le Dockerfile d'origine de la stack.
set -eu

list="$1"
iface="${2:-wlan0}"
device="${3:-1234}"
container="${CONTAINER:-smartcampus-bacnet}"

address="$(ip -4 -o addr show "$iface" | awk '{print $4}' | cut -d/ -f1)"
if [ -z "$address" ]; then
    echo "Pas d'adresse IPv4 sur $iface" >&2
    exit 1
fi

grep -v '^[[:space:]]*\(#\|$\)' "$list" | while read -r type instance name value; do
    set -- "$device" "$type" "$instance" object-name 0 7 "$name"
    if [ -n "${value:-}" ]; then
        # 4 = REAL : valeur numérique (analog-value, analog-output...)
        set -- "$@" present-value 0 4 "$value"
    fi
    printf '%s-%s %s : ' "$type" "$instance" "$name"
    docker exec \
        -e BACNET_IFACE="$iface" -e BACNET_IP_PORT=47809 \
        -e BACNET_BBMD_PORT=47808 -e BACNET_BBMD_ADDRESS="$address" \
        "$container" bacco "$@" </dev/null \
        | grep -o '"error-code": "[a-z-]*"' || echo "pas de réponse"
done
