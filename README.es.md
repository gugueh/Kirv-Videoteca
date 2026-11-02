# Kirv Videoteca
**Español** · [English](README.md)

Reproductor de vídeos MP4 y visor de imágenes para PS Vita, con biblioteca organizada por carpetas, control táctil y botones.

Se inspira en [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player) de SonicMastr. El código de la aplicación está reescrito para corregir los fallos de memoria y de rutas encontrados al auditar el original, y ya no usa plugins ni módulos precompilados.

> **Estado:** DEV 0.19. La reproducción de vídeo se usa a diario en una PS Vita real desde la DEV 0.18. El visor de imágenes y «Nueva carpeta» son nuevos en la 0.19 y se están probando en la consola.

## Funciones

- **Vídeo:** MP4 (H.264/AAC recomendado), progreso guardado, reanudación, marca de visto, siguiente vídeo automático, subtítulos externos `.srt`, miniaturas al salir del vídeo.
- **Imágenes:** PNG, JPEG y BMP (24/32 bits). Pasar con izquierda/derecha o L/R, deslizar o tocar los bordes. Cuadrado alterna ajustar/llenar; triángulo oculta la barra.
- **Carpetas:** pestañas Todo, Series, Películas, Imágenes y Carpetas. Opciones → **Nueva carpeta** crea una carpeta en la ubicación actual con el teclado del sistema.
- **Táctil y botones**, ajustes de volumen, saltos y ajuste de imagen.

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

Las carpetas `Series/`, `Peliculas/` e `Imagenes/` son opcionales: si existen, esas pestañas arrancan allí. Una `cover.png` dentro de una carpeta se usa como carátula.

### Controles

| | Biblioteca | Vídeo | Imagen |
|---|---|---|---|
| X | abrir | pausa | — |
| O | volver | volver | volver |
| Izq./Der. | mover | saltar | anterior/siguiente |
| L / R | cambiar pestaña | saltar | anterior/siguiente |
| Arriba/Abajo | mover | volumen | — |
| Cuadrado | vista cuadrícula/lista | subtítulos | ajustar/llenar |
| Triángulo / Start | opciones | ocultar controles | ocultar barra |

## Límites

- Vídeo: SceAvPlayer, hasta 1280 × 736, audio mono/estéreo PCM 16 bits.
- Imágenes: hasta 8 megapíxeles, 4096 px por lado y 12 MB por archivo. Las fotos de móvil de 12 MP no se abren; redúcelas antes.
- Rutas de menos de 512 bytes, nombres de hasta 255 bytes, 1.024 entradas por carpeta.
- SRT: UTF-8, hasta 4 MiB y 4.096 fragmentos.
- Nombres de carpeta nuevos: sin `\ / : * ? " < > |`, sin espacios ni punto al borde.

## Seguridad

Esta variante usa un SELF restringido (`authid=0x2F00000000000002`), sin taiHEN, sin red y sin SUPRX incluidos. Solo escribe dentro de `ux0:data/VMPH00001/` (preferencias, registro y miniaturas junto a los vídeos). El verificador `tools/verify_vpk.py` rechaza paquetes con otros permisos o archivos inesperados; es una comprobación estructural, no una auditoría completa.

## Compilar

Requisitos: [VitaSDK](https://vitasdk.org/) (con libvita2d, libpng, libjpeg-turbo y zlib), CMake y Ninja.

```powershell
tools/build.ps1 -VitaSdk C:\vitasdk
python tools/verify_vpk.py <ruta-al-vpk>
```

Si la ruta del proyecto contiene espacios, copia el repositorio a una ruta sin espacios: `vita-pack-vpk` falla con ellos.

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
licenses/  avisos de bibliotecas de terceros
```

## Créditos

- [Vita Media Player](https://github.com/SonicMastr/Vita-Media-Player), de SonicMastr: idea y punto de partida.
- [libvita2d](https://github.com/xerpi/libvita2d), de xerpi (MIT), y [VitaSDK](https://vitasdk.org/).
- Autor, código de la aplicación y diseño de interfaz: gugueh / Kirv Studio. La interfaz es diseño propio, inspirado en la estética de la época de auge de la PS Vita.
