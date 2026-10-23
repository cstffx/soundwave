#!/bin/sh
# smoke.sh - prueba de humo: genera un audio, codifica con los dos presets y
# verifica que los MP4 resultantes sean validos (si hay ffprobe disponible).
#
#   sh tests/smoke.sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[ -x "$ROOT/soundwave" ] || { echo "compila primero:  make"; exit 1; }

echo "== generando audio de prueba =="
cc -O2 -o "$TMP/tone" "$ROOT/tools/tone.c" -lm
"$TMP/tone" "$TMP/test.wav" 6

# un avatar solido, para probar la ruta de imagen sin depender de archivos
python3 - "$TMP/avatar.png" <<'EOF' 2>/dev/null || true
import sys, zlib, struct
w = h = 64
raw = b''.join(b'\x00' + bytes([200, 90, 60] * w) for _ in range(h))
def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw))
       + chunk(b'IEND', b''))
open(sys.argv[1], 'wb').write(png)
EOF

check() {
    name="$1"; shift
    file="$TMP/$name.mp4"
    echo "== $name =="
    "$ROOT/soundwave" -i "$TMP/test.wav" -o "$file" \
        --avatar "$TMP/avatar.png" --artist "Test" --title "Prueba" \
        --start 0 --end 5 "$@" 2>/dev/null | sed 's/^/   /'

    [ -s "$file" ] || { echo "FALLO: no se genero $file"; exit 1; }

    if command -v ffprobe >/dev/null 2>&1; then
        info=$(ffprobe -v error -show_entries \
            stream=codec_name,width,height -show_entries format=duration \
            -of default=noprint_wrappers=1 "$file")
        echo "$info" | sed 's/^/   /'
        echo "$info" | grep -q 'codec_name=h264' || { echo "FALLO: sin video H.264"; exit 1; }
        echo "$info" | grep -q 'codec_name=aac'  || { echo "FALLO: sin audio AAC"; exit 1; }
        if command -v ffmpeg >/dev/null 2>&1; then
            errs=$(ffmpeg -v error -i "$file" -f null - 2>&1 | wc -l)
            [ "$errs" -eq 0 ] || { echo "FALLO: ffmpeg reporto $errs lineas de error"; exit 1; }
        fi
    fi
    echo "   ok"
}

check min       --preset min
check balanced  --preset balanced
check colors    --preset balanced --background-color "#1B0E2E" --foreground "#FF9E3D"
check limit     --preset balanced --max-size 300K
check flow      --preset min --wave flow --wave-span 2.0 --bars-shade
check pulse     --preset min --wave pulse --bars 24 --bars-shade

# el recorte acepta segundos sueltos, m:s y h:m:s
echo "== recorte en formato m:s =="
recorte() {
    want="$1"; shift
    got=$("$ROOT/soundwave" -i "$TMP/test.wav" -o "$TMP/tiempo.mp4" \
          --preset min "$@" 2>/dev/null | grep 'segmento' | sed 's/^ *segmento *//')
    echo "   $*  ->  $got"
    echo "$got" | grep -q "$want" || { echo "FALLO: esperaba '$want'"; exit 1; }
}
recorte '0:01 -> 0:03' --start 0:01 --end 0:03
recorte '0:01 -> 0:03' --start 0:00:01 --end 0:00:03
recorte '0:01 -> 0:03' --start 1 --end 3

if "$ROOT/soundwave" -i "$TMP/test.wav" -o "$TMP/x.mp4" --preset min --start 1: >/dev/null 2>&1; then
    echo "FALLO: '--start 1:' deberia rechazarse"; exit 1
fi
if "$ROOT/soundwave" -i "$TMP/test.wav" -o "$TMP/x.mp4" --preset min --start 0:03 --end 0:01 >/dev/null 2>&1; then
    echo "FALLO: start >= end deberia rechazarse"; exit 1
fi
echo "   ok"

echo
echo "todo ok"
