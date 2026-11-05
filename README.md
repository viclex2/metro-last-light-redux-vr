# Metro Last Light Redux VR (mod hecho desde cero)

Un mod de realidad virtual para **Metro Last Light Redux** (PC, DirectX 11), hecho desde cero para aprender cómo funciona un mod VR por dentro.

Funciona como una `d3d11.dll` "adaptadora" (proxy) que se pone en la carpeta del juego. Pasa todo a la `d3d11.dll` real de Windows y, por el camino, lee y cambia lo que hace falta: la cámara, la lente, la imagen final. Además habla con **SteamVR** (OpenVR) para leer el visor y mandarle la imagen.

> Estado: **experimental**. Ver [`docs/ESTADO.md`](docs/ESTADO.md) para lo que funciona y lo que falta, y [`CHANGELOG.md`](CHANGELOG.md) para el historial de versiones.

## Equipo de pruebas
- Windows 11, Intel i5-7500, AMD RX 580.
- Meta Quest 3S con **Steam Link** (también vale ALVR: lo único que importa es que pase por SteamVR).

## Instalación
1. Descarga la última `d3d11.dll` de la pestaña **Releases**.
2. Descarga `openvr_api.dll` (64 bits) de Valve: https://github.com/ValveSoftware/openvr/blob/master/bin/win64/openvr_api.dll
3. Copia las dos en la carpeta del juego, junto a `metro.exe`:
   `C:\Program Files (x86)\Steam\steamapps\common\Metro Last Light Redux\`
4. Abre **SteamVR** con el visor conectado y luego el juego.
5. En partida, espera **dos pitidos agudos**: la DLL ha encontrado la cámara del juego.
6. Pulsa **F9** para activar el modo VR.

Para quitar el mod, borra o renombra `d3d11.dll` (por ejemplo, a `d3d11.dll.off`).

## Teclas
| Tecla | Qué hace |
|---|---|
| F9 / Insert | Activar o desactivar el modo VR |
| Shift + B | Recentrar el visor (y poner a cero el giro del teclado) |
| Shift + Y | Arma con el mundo / arma pegada a la pantalla |
| Shift + H | Buscador automático de la cámara del juego (herramienta de desarrollo) |
| Flechas, Shift + J/L/I/K/U/O/N/M | Girar o mover la vista a mano (pruebas) |

## Archivos que crea
- `%TEMP%\metro_vr_log.txt`: el registro de todo lo que hace. Pásalo cuando algo falle.
- `%TEMP%\metro_vr_config.txt`: los interruptores. Se renueva solo al cambiar de versión; los más útiles:
  - `estereo=2` sincronizado, `1` fotogramas alternos, `0` mono.
  - `camara_juego=1`: la cámara de verdad del juego sigue a la cabeza.
  - `luces_visor=1`: corrige los cajones de luces y sombras.
  - `fov_visor=1`: el juego dibuja con el campo de visión del visor.
  - `visor_imagen=1`: manda la imagen al visor.
  - `quitar_vsync=1`: quita el V-Sync del juego mientras se manda imagen al visor.
  - `ipd_escala=1.0`: separación de los ojos.

## Compilar
Con mingw-w64 (en Windows o en Linux):
```
x86_64-w64-mingw32-gcc -shared -O2 -o d3d11.dll src/d3d11_proxy.c src/d3d11.def -static -static-libgcc -s -ldxguid -luuid -ldxgi
```
También hay un `build.sh` que hace lo mismo.

## Consejos
- Para saltar los vídeos de inicio, vacía los `.ogv` de logos y del aviso legal (guarda una copia `.bak`).
- En `user.cfg` (`%LOCALAPPDATA%\4A Games\Metro Last Light Redux\<id>\`), `show_game_ui 0` quita el HUD.
- **No cambies opciones gráficas con el mod puesto**, porque el juego se cierra (ver problemas conocidos).
