# soundwave

Convierte un archivo de audio en un **video vertical 9:16** (formato estado de
WhatsApp): la foto del artista, su nombre y una **onda** que representa la
energía del sonido, con efecto de progreso mientras avanza la canción.

Es un programa en C, chico y sin dependencias pesadas: decodifica el audio,
calcula la envolvente, dibuja la escena y codifica H.264 + AAC dentro de un MP4.

```
        ╭───────────╮
        │  avatar   │
        ╰───────────╯
         Artista Dem
        Canción de prueba

    ▁▃▅█▅▃▁▃▅█▅▃▁▃▅█▅▃▁   <- onda (barras)
    ▔▔▔▔▔│▔▔▔▔▔▔▔▔▔       <- cursor de avance
```

## Requisitos

* Linux x86-64 (o arm64) con `gcc`, `g++`, `make`, `curl`, `bzip2`.
* Sin `cmake`, sin `meson`, sin `sudo`, sin `libtool`.

## Compilar

```sh
make deps     # descarga y compila las dependencias (una sola vez, tarda un rato)
make          # compila el binario ./soundwave
```

`make deps` hace dos cosas:

1. `tools/fetch_deps.sh` — baja los headers de dominio público / MIT que se
   compilan dentro del programa: **dr_mp3 / dr_wav / dr_flac**, **stb_image**,
   **stb_image_resize2**, **stb_truetype**, **stb_vorbis** y **minimp4**.
2. `tools/build_deps.sh` — obtiene y compila las dos dependencias grandes, que
   **no se versionan** en este repositorio:
   * **fdk-aac** → `libfdk-aac.a` (encoder AAC).
   * **OpenH264** → `libopenh264.so` (encoder H.264). Se intenta compilar desde
     fuentes; si el tarball no se puede descargar entero, usa el **binario
     precompilado oficial de Cisco**.

Todo se instala en `third_party/build/` y queda ignorado por git.

> Los scripts bajan todo a través de `http://localhost:8091` (proxies locales).
> Si no lo necesitás, corré `PROXY=none sh tools/build_deps.sh`.
> Si en tu arquitectura no hay binario de Cisco y el tarball de fuentes no se
> puede descargar, compilá OpenH264 a mano y copiá `libopenh264.so` a
> `third_party/build/lib/`.

## Uso

```sh
./soundwave -i cancion.mp3 -o estado.mp4 \
    --avatar foto.jpg --artist "Mi Artista" --title "Mi Canción" \
    --start 0:45 --end 1:15 \
    --font /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
```

### Recorte

| Opción | Descripción |
|---|---|
| `--start <tiempo>` | donde empieza el video |
| `--end <tiempo>`   | donde termina |

`<tiempo>` acepta segundos sueltos (`45`, `45.5`), el formato `m:s` (`1:30`)
o `h:m:s` (`1:02:03`). También se admite el campo vacío (`:45` = 45 s).
Si `--end` supera la duración del audio se recorta al final; si `--start`
es mayor o igual que `--end` el programa avisa y no genera nada.

```sh
# los cuatro son equivalentes
./soundwave -i c.mp3 -o estado.mp4 --start 90 --end 105
./soundwave -i c.mp3 -o estado.mp4 --start 1:30 --end 1:45
./soundwave -i c.mp3 -o estado.mp4 --start 0:01:30 --end 0:01:45
./soundwave -i c.mp3 -o estado.mp4 --start :90 --end :105
```

### Apariencia

| Opción | Descripción | Default |
|---|---|---|
| `--background-color <#RRGGBB>` | color de fondo | `#0A0A0C` (casi negro) |
| `--foreground <#RRGGBB>` | color de las barras y del texto | blanco |
| `--bars <n>` | cantidad de barras de la onda | automático (`ancho/14`) |
| `--bars-shade` | cada barra toma un gris según su intensidad | desactivado |
| `--wave <static\|flow\|pulse>` | `static` = toda la pista; `flow` = ventana que se desplaza; `pulse` = cada barra late con su banda de frecuencia | `static` |
| `--wave-span <seg>` | segundos visibles en modo `flow` | `2.5` |
| `--avatar <imagen>` | foto del artista (png/jpg) | cuadrado gris |
| `--artist <texto>` | nombre del artista | |
| `--title <texto>` | título o subtítulo | |
| `--font <archivo.ttf>` | tipografía de los textos | se autodetecta |
| `--credit <texto>` | crédito pequeño al pie; `--credit ""` lo quita | `Made with soundwave by cstffx` |

`--artist` y `--title` se dibujan solo si hay tipografía. Si no pasas `--font`
el programa busca una TTF del sistema (DejaVu, Liberation, Ubuntu, Noto,
Arial…); si no encuentra ninguna, omite el texto en vez de fallar. Lo mismo vale
para `--credit`, que se dibuja atenuado a 0.955 de la altura (debajo de la onda).

#### Modos de onda

**`static`** (default) reparte la pista completa entre las barras y un cursor la
recorre de izquierda a derecha. Las barras todavía no reproducidas se ven
atenuadas.

**`flow`** muestra una ventana de ancho fijo (`--wave-span`, 2.5 s por defecto)
que se desplaza: las barras se corren hacia la izquierda y las muestras nuevas
entran por la derecha. Cada barra cubre `span / barras` de audio. Al principio
la onda crece desde la derecha, porque todavía no hay historia que mostrar.

```sh
# onda que fluye, con gris según intensidad, 3 segundos visibles
./soundwave -i tema.mp3 -o estado.mp4 --wave flow --wave-span 3 --bars-shade
```

**`pulse`** es el clásico analizador de espectro: cada barra es una **banda de
frecuencia** (los graves a la izquierda, los agudos a la derecha) y late con la
energía de esa banda. Aquí sí se usa FFT (ventana de Hann de 2048 muestras, un
espectro por cuadro), a diferencia de los otros dos modos que trabajan en el
dominio del tiempo.

Las bandas se reparten de forma geométrica entre 40 Hz y 16 kHz, así que los
graves ocupan menos ancho que los agudos. Cada banda se normaliza con su propio
percentil 98, porque los graves tienen muchísima más energía y sin eso la mitad
derecha quedaría muerta; además se descartan las bandas cuyo pico queda 42 dB por
debajo del pico global ("puerta de silencio"): una banda vacía —por ejemplo por
encima del corte de un MP3— no se dibuja, así que la onda termina antes en vez de
dejar puntos sueltos.

```sh
# analizador de espectro de 34 bandas
./soundwave -i tema.mp3 -o estado.mp4 --wave pulse --bars 34 --bars-shade
```

**`--bars-shade`** hace que el color de cada barra dependa de su intensidad: se
interpola desde una mezcla oscura (fondo y color de barras) hasta `--foreground`.
Con el fondo casi negro y las barras blancas el resultado es una escala de
grises. El rango se ajusta solo con los percentiles 10 y 98 de la envolvente del
recorte, así que un pico aislado no aplasta al resto.

### Formato

| Opción | Descripción |
|---|---|
| `--preset <min\|balanced>` | `min` = 480x854 @ 15 fps, HE-AAC 32k; `balanced` = 720x1280 @ 24 fps, HE-AAC 48k (**default**) |
| `--size <WxH>` | resolución, p. ej. `1080x1920` |
| `--fps <n>` | cuadros por segundo |
| `--video-bitrate <kbps>` | bitrate objetivo de video |
| `--audio-bitrate <kbps>` | bitrate de audio |
| `--audio-mode <he\|lc>` | HE-AAC (más chico) o AAC-LC |
| `--stereo` | conserva estéreo (por defecto mono) |
| `--profile <baseline\|main\|high>` | perfil H.264 |
| `--complexity <low\|medium\|high>` | esfuerzo del encoder |
| `--keyframe <seg>` | separación entre keyframes |
| `--qp-min <n>` | calidad máxima del video: 1 = casi sin pérdida, 51 = peor |
| `--max-size <N[K\|M]>` | ajusta automáticamente el video para entrar en ese tamaño |

### Ejemplos

Estado corto y liviano que entra en cualquier límite:

```sh
./soundwave -i tema.flac -o estado.mp4 --avatar yo.png \
    --artist "Yo" --start 0:30 --end 1:00 --preset min
```

Forzar un presupuesto de 8 MB (el encoder reintenta bajando el bitrate de video
hasta cumplirlo; el audio **nunca** se degrada para ahorrar):

```sh
./soundwave -i tema.wav -o estado.mp4 --avatar yo.png --max-size 8M
```

Con colores propios:

```sh
./soundwave -i tema.mp3 -o estado.mp4 --avatar yo.png \
    --background-color "#12071F" --foreground "#FFB020"
```

## Qué genera

* MP4 con **H.264 yuv420p** y **AAC**, pensado para WhatsApp (≤ 30 s, ≤ 16 MB).
* La escena (fondo liso, avatar quieto y barras finas) es muy barata de
  codificar, así que los archivos salen **chicos**: un estado de 30 s con
  `balanced` ronda **0.5 MB** y con `min` **0.3 MB**, muy por debajo del
  límite de WhatsApp. El video se ve prácticamente sin pérdida (~48 dB de PSNR).
* Si querés gastar más bits, subí `--video-bitrate`; si querés garantizar un
  tope, usá `--max-size`.

Medido en un x86-64 común, 30 s de video:

| Preset | Resolución | Archivo | Tiempo de codificación |
|---|---|---|---|
| `min` | 480×854 @ 15 fps | ~0.3 MB | ~2 s (15× tiempo real) |
| `balanced` | 720×1280 @ 24 fps | ~0.5 MB | ~7 s (4× tiempo real) |

## Cómo está hecho

Todo el dibujo es propio: no hay ffmpeg ni ImageMagick en tiempo de ejecución.

```
src/main.c        CLI, recorte del audio y orquestación
src/audio.c       decodifica wav/mp3/flac/ogg (dr_libs + stb_vorbis)
src/envelope.c    envolvente RMS/pico por cuadro (sin FFT)
src/image.c       carga y recorta el avatar a cuadrado (stb_image + resize2)
src/draw.c        canvas RGBA, primitivas, antialiasing y conversión a I420
src/scene.c       geometría de la escena y dibujo de cada cuadro
src/audio_enc.c   encoder AAC (fdk-aac)
src/video_enc.cpp encoder H.264 (OpenH264)
src/mux.c         empaquetado MP4 (minimp4)
```

Detalles que vale la pena conocer:

* **La onda es una envolvente**, no un espectro: se promedian los valores
  absolutos y el pico de cada cuadro, y se remuestrean a N barras mezclando
  máximo (65 %) y media (35 %) para que los golpes se noten sin perder la forma.
* **Fondo, avatar y textos se dibujan una sola vez** al arrancar. Cada cuadro
  empieza con un `memcpy` de esa imagen y sólo se redibujan las barras, así que
  el costo por cuadro es mínimo.
* **Efecto de progreso**: las barras ya reproducidas van en el color elegido y
  las que faltan en ese mismo color atenuado.
* Las dimensiones se fuerzan a números pares porque YUV 4:2:0 lo exige.

## Licencia

El código de `soundwave` es **MIT** (ver `LICENSE`).

Las dependencias **no** se incluyen en el repositorio. Cada una conserva su
licencia:

| Dependencia | Licencia |
|---|---|
| dr_libs, stb, minimp4 | dominio público / MIT |
| OpenH264 | BSD-2-Clause (Cisco) |
| fdk-aac | licencia propia de Fraunhofer FDK AAC |

Por eso `tools/build_deps.sh` las descarga y las compila aparte: quien use el
programa decide qué binario enlaza.
