#!/bin/sh
# build_deps.sh - obtiene y compila las dos dependencias que NO se versionan en
# el repositorio (por licencia y por tamano):
#
#   * fdk-aac  -> terceros/libfdk-aac.a    encoder AAC (HE-AAC / AAC-LC)
#   * OpenH264 -> terceros/libopenh264.so  encoder H.264
#
# Nada de esto se sube a git: cada quien lo descarga y lo compila aqui.
# Todo el trafico de salida pasa por el proxy local (override con PROXY=none).
#
#   sh tools/build_deps.sh [fdk|h264|all]
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/third_party/src"
BUILD="$ROOT/third_party/build"
JOBS=$(nproc 2>/dev/null || echo 4)
PROXY="${PROXY:-http://localhost:8091}"

FDK_VERSION=3.0.0
OH_VERSION=2.4.1
OH_SOVER=7

mkdir -p "$BUILD/include/wels" "$BUILD/lib" "$BUILD/obj" "$SRC"

have() { command -v "$1" >/dev/null 2>&1; }

dl() {  # dl <url> <destino>
    if [ -n "$PROXY" ] && [ "$PROXY" != "none" ]; then
        curl -fSL --retry 3 --retry-delay 2 --proxy "$PROXY" -o "$2" "$1"
    else
        curl -fSL --retry 3 --retry-delay 2 -o "$2" "$1"
    fi
}

# ================================================================== fdk-aac
build_fdk_aac() {
    if [ -f "$BUILD/lib/libfdk-aac.a" ]; then
        echo "== fdk-aac: ya compilado, salto"
        return 0
    fi
    echo "== fdk-aac $FDK_VERSION =="

    if [ ! -d "$SRC/fdk-aac/cpp" ]; then
        tar="$SRC/fdk-aac-$FDK_VERSION.tar.gz"
        if [ ! -s "$tar" ]; then
            dl "https://downloads.sourceforge.net/project/opencore-amr/fdk-aac/fdk-aac-$FDK_VERSION.tar.gz" "$tar"
        fi
        rm -rf "$SRC/fdk-aac"
        mkdir -p "$SRC/fdk-aac"
        tar xzf "$tar" -C "$SRC/fdk-aac" --strip-components=1
    fi

    cd "$SRC/fdk-aac"

    INC=""
    DIRS="cpp/libAACdec cpp/libAACenc cpp/libArithCoding cpp/libDRCdec \
          cpp/libFDK cpp/libMpegTPDec cpp/libMpegTPEnc cpp/libPCMutils \
          cpp/libSACdec cpp/libSACenc cpp/libSBRdec cpp/libSBRenc cpp/libSYS"
    for d in $DIRS; do
        INC="$INC -I$d/include"
    done

    OBJ="$BUILD/obj/fdk"
    mkdir -p "$OBJ"

    # fuentes del core; se excluyen las variantes ARM/MIPS, que solo se
    # distribuyen pero no se compilan en x86
    find $DIRS -name '*.cpp' -o -name '*.c' | grep -v '/arm/' | grep -v '/mips/' > "$OBJ/files.txt"

    n=0
    while read -r f; do
        o="$OBJ/$(echo "$f" | tr '/' '_').o"
        [ -f "$o" ] && continue
        case "$f" in
            *.cpp) g++ -c -O2 -fPIC -w -std=c++11 $INC "$f" -o "$o" & ;;
            *.c)   gcc -c -O2 -fPIC -w $INC "$f" -o "$o" & ;;
        esac
        n=$((n + 1))
        [ $((n % JOBS)) -eq 0 ] && wait
    done < "$OBJ/files.txt"
    wait

    ar rcs "$BUILD/lib/libfdk-aac.a" "$OBJ"/*.o
    for d in $DIRS; do
        cp -f $d/include/*.h "$BUILD/include/" 2>/dev/null || true
    done
    echo "   -> $BUILD/lib/libfdk-aac.a"
}

# ================================================================= OpenH264
oh_headers() {
    for h in codec_api.h codec_app_def.h codec_def.h codec_ver.h; do
        [ -s "$BUILD/include/wels/$h" ] && continue
        if [ -f "$SRC/openh264/codec/api/wels/$h" ]; then
            cp -f "$SRC/openh264/codec/api/wels/$h" "$BUILD/include/wels/$h"
        else
            dl "https://raw.githubusercontent.com/cisco/openh264/v$OH_VERSION/codec/api/wels/$h" \
               "$BUILD/include/wels/$h"
        fi
    done
}

build_openh264() {
    if [ -f "$BUILD/lib/libopenh264.so" ] || [ -f "$BUILD/lib/libopenh264.a" ]; then
        echo "== OpenH264: ya instalado, salto"
        oh_headers
        return 0
    fi
    echo "== OpenH264 $OH_VERSION =="

    # --- 1) intento preferido: compilar desde fuentes -------------------
    if [ ! -d "$SRC/openh264/codec" ]; then
        tar="$SRC/openh264-$OH_VERSION.tar.gz"
        ok=0
        if [ -s "$tar" ] && gzip -t "$tar" 2>/dev/null; then
            ok=1
        else
            rm -f "$tar"
            if dl "https://github.com/cisco/openh264/archive/refs/tags/v$OH_VERSION.tar.gz" "$tar" 2>/dev/null \
               && gzip -t "$tar" 2>/dev/null; then
                ok=1
            fi
        fi
        if [ "$ok" = 1 ]; then
            rm -rf "$SRC/openh264"
            mkdir -p "$SRC/openh264"
            tar xzf "$tar" -C "$SRC/openh264" --strip-components=1
        else
            echo "   (el tarball de fuentes llego incompleto; se usara el binario oficial)"
            rm -f "$tar"
            rm -rf "$SRC/openh264"
        fi
    fi

    if [ -f "$SRC/openh264/Makefile" ]; then
        if have nasm || have yasm; then ASM=Yes; else
            echo "   (sin nasm/yasm: compilando sin ensamblador)"
            ASM=No
        fi
        ( cd "$SRC/openh264" && make -j"$JOBS" USE_ASM=$ASM BUILDTYPE=Release ) \
            >/dev/null 2>&1 || true
        if [ -f "$SRC/openh264/libopenh264.a" ]; then
            cp -f "$SRC/openh264/libopenh264.a" "$BUILD/lib/"
            echo "   -> $BUILD/lib/libopenh264.a (compilado desde fuentes)"
        elif [ -f "$SRC/openh264/libopenh264.so" ]; then
            cp -f "$SRC/openh264/libopenh264.so"* "$BUILD/lib/"
            echo "   -> $BUILD/lib/libopenh264.so (compilado desde fuentes)"
        fi
    fi

    # --- 2) respaldo: binario precompilado oficial de Cisco -------------
    if [ ! -f "$BUILD/lib/libopenh264.a" ] && [ ! -f "$BUILD/lib/libopenh264.so" ]; then
        case "$(uname -m)" in
            x86_64|amd64) OH_ARCH=linux64 ;;
            i386|i486|i586|i686) OH_ARCH=linux32 ;;
            *)            OH_ARCH="" ;;
        esac
        if [ -z "$OH_ARCH" ]; then
            echo "   ERROR: no hay binario oficial de Cisco para $(uname -m)." >&2
            echo "          Compila OpenH264 desde fuentes y copia libopenh264.so a" >&2
            echo "          third_party/build/lib/ (ver README)." >&2
            exit 1
        fi
        base="libopenh264-$OH_VERSION-$OH_ARCH.$OH_SOVER.so"
        bz="$SRC/$base.bz2"
        [ -s "$bz" ] || dl "https://ciscobinary.openh264.org/$base.bz2" "$bz"
        bunzip2 -c "$bz" > "$BUILD/lib/libopenh264.so.$OH_SOVER"
        ln -sf "libopenh264.so.$OH_SOVER" "$BUILD/lib/libopenh264.so"
        echo "   -> $BUILD/lib/libopenh264.so -> libopenh264.so.$OH_SOVER (binario de Cisco)"
    fi

    oh_headers
}

case "${1:-all}" in
    fdk)  build_fdk_aac ;;
    h264) build_openh264 ;;
    all)  build_fdk_aac; build_openh264 ;;
    *)    echo "uso: $0 [fdk|h264|all]"; exit 1 ;;
esac

echo
echo "Dependencias listas en third_party/build/"
