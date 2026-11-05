# Estado del proyecto

*Este archivo sustituye al antiguo `contexto_metro_vr.md`. Se actualiza con cada versión.*

**Versión actual: E5b** (probada el 5/11: la cámara sigue a la cabeza sin soltarse; en el vídeo las luces se ven normales; pendiente de que el usuario lo confirme)

**Meta del proyecto:** un mod VR **estable, jugable, bien hecho y gratuito**, no solo un experimento de aprendizaje. Las decisiones se toman pensando en que lo use cualquier jugador: que no se cierre, que se instale fácil y que se vea bien.

## ✅ Funciona
- **Proxy d3d11** estable (Map, Unmap, UpdateSubresource, VS/PS/CS SetConstantBuffers, CopyResource, CreateRenderTargetView).
- **Present** con tabla propia en la swapchain del juego; ya no hay bucle con la Steam Overlay (`gameoverlayrenderer64.dll` se carga siempre).
- **SteamVR / OpenVR:**
  - Lectura del visor (WaitGetPoses) e imagen al visor como aplicación "Scene".
  - Textura compartida en un dispositivo propio.
  - Submit con postura.
  - Lectura de avisos (PollNextEvent).
- **Lente del visor** (Quest 3S, 104 × 98 grados, una por ojo); ojos a 58 mm.
- **Estéreo sincronizado** (`estereo=2`), a 72 fps, que son 36 parejas por segundo.
- **Arma:** reconocida por la lente estrecha (P00 = 1,83) y girando con el mundo.
- **La cámara de verdad del juego sigue a la cabeza** (`camara_juego=1`), a la vez que el ratón.

## ❌ Problemas conocidos
| # | Problema | Pistas / qué se sabe |
|---|---|---|
| 1 | **Mundo marrón, sin luces ni sombras**, con la cámara siguiendo a la cabeza | La pasada de luces usa los huecos 7 (lente) y 3 (receta de rayos, casillas 36–43) con la lente original. **La E5 los corrige; falta probarla.** Puede haber más: el hueco 12 (256 bytes) también tiene 1/P11 y otra matriz. |
| 2 | Paredes y cosas que aparecen y desaparecen; imagen borrosa | Posibles causas: oclusión con el fotograma anterior, desenfoque de movimiento (`r_blur_level`) o AA temporal. Pendiente probar `r_blur_level 0` y `estereo=0`. |
| 3 | HUD, mirilla y punto rojo dobles | El 2D se pinta igual en los dos ojos. Hay que identificar los dibujos del HUD y desplazarlos en cada ojo. |
| 4 | Cambiar las opciones gráficas con el mod puesto cierra el juego | Peta en la d3d11 de Windows (+0x17206B) tras SetFullscreenState(0), aunque esté `visor_imagen=0`. Sin el mod no pasa. Mientras tanto: renombrar la DLL a `.off`. |
| 5 | A veces el panel de SteamVR no se quita | Sin diagnosticar. |
| 6 | Los punteros fijos a la cámara no siempre sirven | Hay búsqueda por firma de respaldo. Falta encontrar una cadena de punteros estable. |

## Datos técnicos clave
- **Hueco 1, `cb_main_matrices1` (352 bytes):** m_V, m_iV, m_P, m_iP, m_VP, m_iVP. Lente ancha P00 = 1,18931, P11 = 2,11432, P22 = 1,00134, P23 = −0,10013.
- **Hueco 0, `cb_main_matrices0` (208 bytes):** m_W, m_iW, m_WV, m_WVP, uno por dibujo.
- **Hueco 7 (144 bytes):** [P | P·algo | 0.5 −0.5 0 0].
- **Hueco 3 (272 bytes):** en 36..43 = (2/P00, 0, 0, −1/P00, 0, −2/P11, 0, 1/P11); en 48..51 = (0, 0, −9.987, 10).
- **Cámara del juego (objeto):**
  - +0x650 = −inclinación (rad), +0x654 = giro (rad), con copias en +12/+16 y +24/+28.
  - Giro = atan2(f.x, f.z); inclinación = asin(f.y) de la fila "adelante" de m_V.
  - Mundo zurdo (+Z adelante, Y arriba).
- **Punteros fijos** (más 0x650): metro.exe+0xD08E18, D08E28, D08E48, D092D8, D092E8, D09318, D09778, D09788, D097B8, D09C28, D09C38 y D09C88.
- **user.cfg:** `r_base_fov 50.625` (FOV vertical; no tocar), `show_game_ui`, `r_blur_level`, `r_vsync`.

## Notas de la última prueba (E5b)
- Parejas leídas al encontrar la cámara: (-0.751, -0.370) (0.000, -0.370) (0.000, -0.370). Las copias +12/+24 tienen la **inclinación a 0**, así que podrían ser solo el giro del cuerpo. Habría que escribir solo el giro en ellas y no la inclinación.
- La búsqueda por firma, más permisiva, da 16 candidatas en 0,6 s.

## Siguiente paso
1. Probar la **E5**: F9, mirar a todos lados, comprobar si desaparece lo marrón (`luces7`/`luces3` > 0 en el log).
2. Si sigue habiendo marrón o luces fuera de sitio, mirar el hueco 12 y el resto de cajones del detective.
3. Después: el HUD (problema 3), y luego las manos y los mandos.

## Cómo trabajamos
- El usuario lo prueba todo en Windows y pasa `%TEMP%\metro_vr_log.txt`.
- Claude compila la DLL con mingw-w64 para probarla y, en cada versión, actualiza `CHANGELOG.md`, este archivo y `VERSION`. Al subirlo, GitHub Actions crea la Release con la DLL.
- Mensajes cortos y explicaciones "en cristiano", con símiles de hardware.
- Enlaces mejor que capturas.
