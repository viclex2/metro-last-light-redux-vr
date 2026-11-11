# Metro Last Light Redux VR — traspaso para la IA

**Lee esto entero antes de tocar nada.** Resume lo aprendido en semanas de pruebas: muchas cosas que parecen buena idea ya se probaron y fallaron, y aquí pone por qué.

## Quién y cómo trabajar
- El usuario es **Alex** (GitHub `viclex2`). Habla español informal y quiere las explicaciones **"en cristiano"**: sencillas, con símiles de hardware y sin jerga de programación. Los comandos van en bloques de código. Prefiere que le pases enlaces antes que pedirle capturas.
- Él **prueba en las gafas** y tú no puedes, así que no afirmes que algo funciona sin su prueba o sin el log. El ciclo es: cambio → compilar → publicar → él prueba → leer el log.
- **Regla del usuario:** si ya existe una herramienta o un código que funciona (MinHook, código de D-Rey, 3DMigoto…), se usa. No reinventar. Solo hacemos nosotros lo que no ha hecho nadie.
- Trabaja **una cosa por conversación** (el usuario tiene pocos tokens).

## Equipo y juego
- Windows 11, i5-7500, **AMD RX 580**, **Quest 3S** por Steam Link (SteamVR), 72 Hz.
- Juego: `C:\Program Files (x86)\Steam\steamapps\common\Metro Last Light Redux\`, en la **rama de Steam anterior a la actualización "next-gen"** (22/10/2026). Con la next-gen el mod no funciona: las direcciones cambian.
- `user.cfg` activo: `C:\Users\vicle\AppData\Local\4A Games\Metro Last Light\1100001431778f2\user.cfg` (la carpeta no lleva "Redux"). Ajustes necesarios: `r_base_fov 90` (el máximo que acepta), `r_blur_level 0` y `r_fullscreen off`, porque en pantalla completa se cuelga al perder el foco. HUD quitado: `show_game_ui 0`, `g_show_crosshair off`, `g_quick_hints 0`. Gráficos en Medio, sin SSAA.
- Log: `%TEMP%\metro_vr_log.txt`. Config: `%TEMP%\metro_vr_config.txt` (`clave=valor`). Calibración de la mano: `%TEMP%\metro_vr_mano.txt`. Brazos ocultos: `%TEMP%\metro_vr_brazos.txt`. Ahí `%TEMP%` es `C:\Users\vicle\AppData\Local\Temp\`.
- Si la sesión está enlazada a su PC, puedes **leer el log tú mismo** con `device_stage_files` (no hace falta pedírselo).

## Repositorio, compilar y publicar
- `https://github.com/viclex2/metro-last-light-redux-vr` (antes se llamaba `metro-ll-redux-vr`; el nombre viejo redirige).
- Ramas: **`main`** = versión estable para jugar. **`manos`** = pruebas del arma en la mano y los brazos.
- Todo el mod está en `src/d3d11_proxy.c` (más MinHook en `src/minhook`, que compila `build.sh`) (C, unas 4300 líneas, **codificación latin1**). Compilar: `sh build.sh` (mingw-w64) → `d3d11.dll`; luego copiarla a `bin/d3d11.dll`.
- **Publicar = cambiar `VERSION`** (por ejemplo `E18`), añadir una sección `## E18 …` arriba del todo en `CHANGELOG.md` y hacer push. GitHub Actions crea la release sola; desde la rama `manos` sale como pre-release.
- Enlace directo para el usuario: `https://github.com/viclex2/metro-last-light-redux-vr/raw/<rama>/bin/d3d11.dll` o la página de la release.
- Cada versión cambia la marca `=== HITO Exx` del log (búscala y súbela): así se sabe qué DLL tiene puesta.
- `docs/ESTADO.md` es el diario técnico largo y `docs/PLAN_MANDOS.md` el plan de las manos. `CHANGELOG.md` explica cada versión y lo que se aprendió en ella.

## Cómo funciona (versión E16/E17)
- **Proxy de `d3d11.dll`**: el juego carga nuestra DLL, que carga la real de System32. Ganchos por tabla (vtable) en Map/Unmap/UpdateSubresource/VS·PS·CSSetConstantBuffers/CopyResource, y el Present con una copia propia de la tabla de la swapchain. OpenVR por su FnTable (`IVRSystem_026`, `IVRCompositor`), sin cabeceras oficiales.
- **Estéreo "sincronizado"**: el juego dibuja un ojo por fotograma; el reloj se para en el 2.º ojo para que los dos vean el mismo instante. Se manda a SteamVR después del ojo derecho, con `WaitGetPoses` una vez por pareja.
- **Cámara nativa** (lo que por fin funcionó, E9/E10): gancho en línea en `metro.exe+0x69B900` (función que monta la base de la cámara: rdx = posición, r8 = adelante, r9 = arriba). Solo actúa si se llama desde `+0x1F3960` (vuelve a `+0x1F3965`). Se le pasa **toda la cabeza**: giro, inclinación, ladeo, posición y **el ojo** que se va a dibujar. Así el juego dibuja ya desde el ojo y **H (nuestra corrección) es la identidad**: solo se cambia la lente por la del visor.
  - Qué ojo toca se detecta solo, comparando la posición de la cámara que llega a la gráfica con las últimas que dimos (`NATIVA ojos: bien= mal= flip=`).
- **Andar hacia donde miras**: gancho en `+0x28D110` (llamado desde `+0x1F32C5`). Se gira el vector de movimiento con el giro de cabeza desde la referencia.
- **Oclusión de CPU**: `+0x826B50`. Con la cámara nativa la dejamos PUESTA (va más rápido). `Shift+F10` la quita y la pone.
- **Arma**: el juego la pinta con **su propia cámara** (cajón de cámara con lente más estrecha, P00 ≈ 0.96 frente a 0.55 de la principal). Como la cámara va en cada ojo, el arma se iba con el ojo; se le resta el ojo (E10c) en esa cámara y en las piezas del cajón 0 con esa lente.
- **Al apuntar** el juego hace zoom: la lente principal se estrecha. Por eso se acepta hasta un 15 % por paso (E16).
- **Mandos** (E13): estado "heredado" (`GetControllerState`) → teclas por **código físico** (W 0x11, A 0x1E, S 0x1F, D 0x20, E 0x12, R 0x13…), ratón para disparar y girar.
- Todas las direcciones son del `metro.exe` de Steam desempaquetado (SteamStub v3, con Steamless). El código del motor es **idéntico a Metro 2033 Redux en las mismas direcciones** que usa D-Rey86.

## Cajones de la gráfica (constant buffers)
- Hueco 0, 208 B: `m_W`, `m_iW`, `m_WV`, `m_WVP` (este en los floats 36–51). Hueco 1, 352 B: la cámara (V, iV, P, iP, VP, iVP); posición = floats 15/19/23.
- Hueco 3 (272 B), hueco 7 (144 B): matrices del fotograma anterior (reflejos, P·T).
- **Hueco 8 (3856 B) y 13: huesos. No tocar.**

## Lo que NO funcionó (no repetir)
- Mover la cámara escribiendo en su memoria → mundo marrón.
- Corrección "H" por encima, con el ratón o por objeto (E7–E8) → **mundo fantasma** u objetos que bailan. Se resolvió con la cámara nativa.
- HUD recolocado con viewport (E11) y F6 para quitar brazos (E17): **el mod no ve ninguna orden de dibujo** (`dibujos=0`). Windows (el runtime de d3d11) reescribe las casillas Draw/DrawIndexed/… de la tabla del contexto **en cada fotograma** (`desvios_repuestos=288/s`). **Por tabla no se puede: hay que usar MinHook** (gancho en línea en la función real; el log de la E17d apunta sus direcciones y primeros bytes: `DIBUJO real …`).
- El "detective de lentes" y similares: herramientas de diagnóstico que gastan mucho. Están apagadas.

## Estado (11/11/2026)
- **Estable (main, E16)**: 3D, cabeza 6DOF, cámara nativa, andar hacia donde miras, mandos, sin HUD, apuntar sin tirones. Unos 72 fps = 36 parejas por segundo (cada ojo a 36 Hz). Es jugable.
- **Pruebas (manos)**: E17d probada (11/11): **arma + brazos siguen al mando derecho** (`mano: ok=1`); los brazos van rígidos y se ven las mangas flotando. Calibrar: mano donde se ve el arma y mantener 1,5 s el clic del stick derecho.
- **E18 (manos) — FUNCIONA (probada 11/11)**: MinHook (`src/minhook`, BSD-2) engancha por dentro las funciones de dibujo. Windows alterna **dos versiones** de cada Draw* (d3d11.dll+0x153540/0x153610 Draw, +0x1535a0/0x153b70 DrawIndexed, +0x14ebc0/0x1b9740 DrawInstanced, +0x1531b0/0x1a4580 DrawIndexedInstanced): las 8 enganchadas `MH_OK`, `dibujos≈85000/s`. Con F6/Shift+F6 el usuario quitó los brazos enteros: **solo se ve el arma en la mano**. 13 piezas guardadas en `metro_vr_brazos.txt` (la grande `16734/177912` y 12 pequeñas con inicio 849921–855738). Esas piezas salen igual con otras armas (en el log aparecen marcadas `(brazo)` al cambiar de arma).

## Siguientes pasos
1. Comprobar con varias armas que no queda ningún trozo de brazo (si queda, F6 + Shift+F6 con esa arma).
2. Pasar también los ganchos del `metro.exe` (cámara, andar) a MinHook.
3. Balas hacia donde apunta el arma: `+0x3D7FF0` crea el proyectil (origen en RDX, dirección en R8; llama a `+0x3D6970`). Filtrar solo las del jugador (los NPC usan la misma función).
4. Mano izquierda y reloj: copiar la solución de D-Rey (sus `docs/history/31-*.md` y `32-*.md`).
5. Rendimiento (36 Hz por ojo).

## D-Rey86 / Metro 2033 Redux VR
- `https://github.com/D-Rey86/Metro2033ReduxVR`. Licencia **GPL-3**, montado sobre 3DMigoto. Sacará una versión grande el fin de semana del 14–15/11/2026.
- Usar su código está permitido citándole. **Ojo:** si se copia código suyo, nuestro repositorio también tiene que ser GPL-3; ahora mismo no tiene archivo LICENSE. Pregunta al usuario antes de añadirlo.
- Discord: canal `new-vr-mods`, hilo "Metro 2033 Redux VR [WIP]". D-Rey está al tanto de nuestro mod y nos anima.
