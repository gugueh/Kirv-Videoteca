# Kirv Videoteca
**Español** · [English](README.md)

Reproductor de vídeos MP4 y visor de imágenes para PS Vita, con biblioteca organizada por carpetas, control táctil y botones.

Se inspira en [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player) de SonicMastr. El código de la aplicación está reescrito para corregir los fallos de memoria y de rutas encontrados al auditar el original, y ya no usa plugins ni módulos precompilados.

> **Estado:** DEV 0.23. La reproducción de vídeo se usa a diario en una PS Vita real desde la DEV 0.18. El visor de imágenes, las fotos de la consola, renombrar/eliminar y la interfaz en inglés son nuevos en la 0.19–0.20 y se están probando en la consola.

## Funciones

- **Vídeo:** MP4 (H.264/AAC recomendado), progreso guardado, reanudación, marca de visto, siguiente vídeo automático, subtítulos externos `.srt`, miniaturas al salir del vídeo.
- **Imágenes:** PNG, JPEG y BMP (24/32 bits), incluidas las capturas y fotos de la propia consola (`ux0:picture`, solo lectura). La pestaña Imágenes muestra todas, de la más nueva a la más vieja, con miniaturas. Zoom con X/cuadrado, mover con la cruceta o el dedo, cambiar de imagen con L/R, izquierda/derecha, deslizando o tocando los bordes.
- **Archivos:** pestañas Todo, Series, Películas, Imágenes y Carpetas. Opciones → **Nueva carpeta**, **Renombrar seleccionado** y **Eliminar seleccionado** (archivos y carpetas, con confirmación) con el teclado del sistema. Subtítulos, miniaturas y progreso acompañan al vídeo renombrado o eliminado.
- Interfaz en **español o inglés** (Ajustes → Idioma). El progreso se guarda cada 10 segundos y al pausar.

## Dos partes

- **App de PS Vita** (`src/`, `assets/`): el reproductor.
- **Herramienta de PC** (`pc/`): arrastras vídeos, convierte solo lo que la Vita necesita (por ejemplo audio HE-AAC a AAC-LC, o vídeo HEVC/1080p a H.264 720p) y los envía por el FTP de VitaShell. Versión para Windows con ffmpeg incluido en cada Release.

## Instalación y uso

1. Instala el `.vpk` con VitaShell (o similar). La burbuja se llama **Kirv Videoteca DEV**.
2. Copia tus archivos a `ux0:data/VMPH00001/video/`:

```text
ux0:data/VMPH00001/video/
  Series/
    Mi serie/
      01 - Capitulo.mp4
      01 - Capitulo.srt
  Peliculas/
  Imagenes/
    Viaje/
      foto.jpg
```

Las carpetas `Series/` y `Peliculas/` son opcionales: si existen, esas pestañas arrancan allí. Una `cover.png` dentro de una carpeta se usa como carátula.

### Controles

| | Biblioteca | Vídeo | Imagen |
|---|---|---|---|
| X | abrir | pausa | acercar |
| O | volver | volver | volver |
| Izq./Der. | mover | saltar | anterior/siguiente (mover con zoom) |
| L / R | cambiar pestaña | saltar | anterior/siguiente |
| Arriba/Abajo | mover | volumen | mover con zoom |
| Cuadrado | vista cuadrícula/lista | subtítulos | alejar |
| Triángulo / Start | opciones | ocultar controles | ocultar barra |

## Límites

- Vídeo: SceAvPlayer, hasta 1280 × 736, audio mono/estéreo PCM 16 bits.
- Imágenes: JPEG hasta 16384 px por lado y 48 MB (se decodifica reducido); PNG y BMP hasta 8 megapíxeles, 4096 px por lado y 12 MB.
- Las fotos de la consola son de solo lectura. Si el sistema no deja leerlas, la pestaña Imágenes muestra las de la biblioteca y el código de error.
- Rutas de menos de 512 bytes, nombres de hasta 255 bytes, 1.024 entradas por carpeta.
- SRT: UTF-8, hasta 4 MiB y 4.096 fragmentos.
- Nombres: sin `\ / : * ? " < > |`, sin espacios ni punto al borde. Eliminar una carpeta borra su contenido, tras una confirmación con la cantidad de archivos.

## Seguridad

Esta variante usa un SELF restringido (`authid=0x2F00000000000002`), sin taiHEN, sin red y sin SUPRX incluidos. Solo escribe dentro de `ux0:data/VMPH00001/` (preferencias, registro, miniaturas junto a los vídeos y las carpetas y archivos que crees, renombres o elimines ahí). El verificador `tools/verify_vpk.py` rechaza paquetes con otros permisos o archivos inesperados; es una comprobación estructural, no una auditoría completa.

## Compilar

Requisitos: [VitaSDK](https://vitasdk.org/) (con libvita2d, libpng, libjpeg-turbo y zlib), CMake y Ninja.

```powershell
tools/build.ps1 -VitaSdk C:\vitasdk
python tools/verify_vpk.py <ruta-al-vpk>
```

Si la ruta del proyecto contiene espacios, copia el repositorio a una ruta sin espacios: `vita-pack-vpk` falla con ellos.

### Herramienta de PC

```powershell
cd pc
python -m pip install pywebview
python -m kirv_pc                                     # desde el código (necesita ffmpeg en el PATH)
./build.ps1 -Ffmpeg C:/ffmpeg-essentials             # versión de Windows con ffmpeg incluido
python -m unittest discover -s tests
```

### Pruebas del núcleo (sin VitaSDK)

```bash
cmake -S . -B build -G Ninja -DVMP_HOST_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Cubren rutas, SRT, estado guardado, asignaciones, detección y cabeceras de imagen, validación de nombres de carpeta y conversión UTF-16.

## Estructura

```text
src/       aplicación (main, navegador, interfaz, visor) y núcleo portable (core, library)
include/   cabeceras
assets/    iconos, fondos y LiveArea
tests/     pruebas del núcleo y del paquete
tools/     build.ps1 y verify_vpk.py
pc/        herramienta de PC: convertir y enviar vídeos a la Vita
licenses/  avisos de bibliotecas de terceros
```

## Créditos

- [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player), de SonicMastr: idea y punto de partida.
- [libvita2d](https://github.com/xerpi/libvita2d), de xerpi (MIT), y [VitaSDK](https://vitasdk.org/).
- Autor, código de la aplicación y diseño de interfaz: gugueh / Kirv Studio. La interfaz es diseño propio, inspirado en la estética de la época de auge de la PS Vita.
