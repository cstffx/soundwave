#!/bin/sh
# demo.sh - genera un audio de prueba y un video de ejemplo.
#
#   sh tools/demo.sh [audio.wav]
#
# Sin argumentos crea el audio con tools/tone.c (no hace falta ffmpeg).
# Escribe examples/demo.wav y examples/demo.mp4.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/examples"
mkdir -p "$OUT"

if [ ! -x "$ROOT/soundwave" ]; then
    echo "primero compila el binario:  make"
    exit 1
fi

# un avatar de prueba si no hay ninguno
AVATAR=""
if [ -f "$OUT/avatar.png" ]; then
    AVATAR="$OUT/avatar.png"
elif command -v python3 >/dev/null 2>&1; then
    # degradado radial, sin dependencias externas
    python3 - "$OUT/avatar.png" <<'EOF'
import sys, zlib, struct, math
n = 256
rows = []
for y in range(n):
    row = bytearray([0])
    for x in range(n):
        dx, dy = (x - n/2) / (n/2), (y - n/2) / (n/2)
        d = min(1.0, math.hypot(dx, dy))
        row += bytes([int(220 - 120*d), int(90 - 40*d), int(70 + 40*d)])
    rows.append(bytes(row))
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(b''.join(rows)))
       + chunk(b'IEND', b''))
open(sys.argv[1], 'wb').write(png)
EOF
    AVATAR="$OUT/avatar.png"
fi

AUDIO="${1:-}"
if [ -z "$AUDIO" ]; then
    AUDIO="$OUT/demo.wav"
    cc -O2 -o "$OUT/tone" "$ROOT/tools/tone.c" -lm
    "$OUT/tone" "$AUDIO" 20
fi

exec "$ROOT/soundwave" \
    -i "$AUDIO" \
    -o "$OUT/demo.mp4" \
    --start 0 --end 15 \
    --artist "Artista Demo" --title "Cancion de prueba" \
    ${AVATAR:+--avatar "$AVATAR"} \
    --preset balanced
