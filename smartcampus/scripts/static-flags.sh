#!/bin/sh
# Traduit la section "static" du fichier de configuration en options de
# compilation pour la stack. Ces objets (trend-log, access-*...) ne peuvent
# pas etre crees par CreateObject : leur nombre est fixe a la compilation.
#
# Usage : BACNET_DEFINES="$(smartcampus/scripts/static-flags.sh config.json)" make server
# Necessite jq.
set -eu

config="${1:-$(dirname "$0")/../config/objects.json}"

# type d'objet BACnet -> constante de taille dans src/bacnet/basic/object/*.c|h
macro_for() {
    case "$1" in
        trend-log) echo MAX_TREND_LOGS ;;
        access-credential) echo MAX_ACCESS_CREDENTIALS ;;
        access-door) echo MAX_ACCESS_DOORS ;;
        access-point) echo MAX_ACCESS_POINTS ;;
        access-rights) echo MAX_ACCESS_RIGHTS ;;
        access-user) echo MAX_ACCESS_USERS ;;
        access-zone) echo MAX_ACCESS_ZONES ;;
        credential-data-input) echo MAX_CREDENTIAL_DATA_INPUTS ;;
        *) echo "static-flags: type statique inconnu: $1" >&2; exit 1 ;;
    esac
}

flags=""
for pair in $(jq -r '.static // {} | to_entries[] | "\(.key)=\(.value)"' "$config"); do
    type="${pair%%=*}"
    count="${pair#*=}"
    flags="$flags -D$(macro_for "$type")=$count"
done
echo "${flags# }"
