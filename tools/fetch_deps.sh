#!/bin/sh
# fetch_deps.sh - descarga las dependencias de dominio publico / MIT que se
# compilan dentro del proyecto (headers + stb, todo en un archivo).
#
# NO descarga fdk-aac ni OpenH264: esas dos se obtienen y compilan aparte con
# tools/build_deps.sh (son mas grandes y no se versionan).
#
# Todo el trafico de salida pasa por el proxy local (override con PROXY=none).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="$ROOT/third_party/vendor"
PROXY="${PROXY:-http://localhost:8091}"
mkdir -p "$V/dr_libs" "$V/stb" "$V/minimp4"

get() {  # get <url> <destino>
    if [ -s "$2" ]; then
        printf '  ya existe  %s\n' "$2"
        return 0
    fi
    printf '  bajando    %s\n' "$2"
    if [ -n "$PROXY" ] && [ "$PROXY" != "none" ]; then
        curl -fsSL --retry 3 --proxy "$PROXY" "$1" -o "$2" || { echo "FALLO: $1"; exit 1; }
    else
        curl -fsSL --retry 3 "$1" -o "$2" || { echo "FALLO: $1"; exit 1; }
    fi
}

echo "[dr_libs] decodificadores de audio (Unlicense / MIT-0)   proxy=$PROXY"
get https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h  "$V/dr_libs/dr_mp3.h"
get https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h  "$V/dr_libs/dr_wav.h"
get https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h "$V/dr_libs/dr_flac.h"

echo "[stb] imagen, resize, tipografia, ogg (Public Domain / MIT)"
get https://raw.githubusercontent.com/nothings/stb/master/stb_image.h          "$V/stb/stb_image.h"
get https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h "$V/stb/stb_image_resize2.h"
get https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h       "$V/stb/stb_truetype.h"
get https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c         "$V/stb/stb_vorbis.c"

echo "[minimp4] muxer MP4 (Public Domain / CC0)"
get https://raw.githubusercontent.com/lieff/minimp4/master/minimp4.h "$V/minimp4/minimp4.h"

echo
echo "Listo. Headers en third_party/vendor/"
