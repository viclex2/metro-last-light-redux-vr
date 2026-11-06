# Estado del proyecto

*Este archivo sustituye al antiguo `contexto_metro_vr.md`. Se actualiza con cada versión.*

**Versión actual: E6** (la linterna: FOV del juego agrandado; **pendiente de probar**). Antes: **E5h** (se engancha bajo RenderDoc). Antes: **E5g** (cabeza falsa con las flechas para capturar con RenderDoc sin visor). Antes: E5f (RenderDoc sin bucle) y E5e (igual que la E5d pero encuentra mejor la cámara). Antes: **E5d** (Shift+F8 formas de escribir la cámara, Shift+F7 buscador del FOV). Anteriores: E5c y E5b (probada el 5/11: la cámara sigue a la cabeza sin soltarse; en el vídeo las luces se ven normales; pendiente de que el usuario lo confirme)

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
- ~~La cámara del juego sigue a la cabeza (`camara_juego=1`)~~: funcionaba, pero daba el mundo marrón. **Quitada en la E6** (ver "Cambio de rumbo").

## ❌ Problemas conocidos
| # | Problema | Pistas / qué se sabe |
|---|---|---|
| 1 | **Mundo marrón, sin luces ni sombras**, con la cámara siguiendo a la cabeza | La pasada de luces usa los huecos 7 (lente) y 3 (receta de rayos, casillas 36–43) con la lente original. **La E5 los corrige; falta probarla.** Puede haber más: el hueco 12 (256 bytes) también tiene 1/P11 y otra matriz. |
| 2 | Paredes y cosas que aparecen y desaparecen; imagen borrosa | **La E6 ataca lo de desaparecer con la linterna (falta probar).** Otras posibles causas: oclusión con el fotograma anterior, desenfoque de movimiento (`r_blur_level`) o AA temporal. Pendiente probar `r_blur_level 0` y `estereo=0`. |
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
- **FOV del juego en memoria (E6):** se busca solo al entrar en partida, en 6 formas (grados/radianes/medio ángulo/tangente en vertical, grados/radianes en horizontal). Se confirma si al subirlo 20° la lente (P11) cambia justo lo esperado. Con F9 se pone a `linterna_grados` (140). Con 140° en vertical: P11 ≈ 0,36 y P00 ≈ 0,20.
- **user.cfg:** `r_base_fov 50.625` (FOV vertical; no tocar), `show_game_ui`, `r_blur_level`, `r_vsync`.

## Notas de la última prueba (E5b)
- Parejas leídas al encontrar la cámara: (-0.751, -0.370) (0.000, -0.370) (0.000, -0.370). Las copias +12/+24 tienen la **inclinación a 0**, así que podrían ser solo el giro del cuerpo. Habría que escribir solo el giro en ellas y no la inclinación.
- La búsqueda por firma, más permisiva, da 16 candidatas en 0,6 s.

## E9 (6/11): cámara nativa — LEER PRIMERO
- `metro.exe` de Last Light Redux (rama de Steam anterior al "next-gen", sha256 74c123b4…) desempaquetado con Steamless: el motor es el mismo que Metro 2033 Redux en las direcciones de D-Rey86.
  - `+0x69B900` base de la cámara (rcx gestor, rdx posición, r8 adelante, r9 arriba, +2 en pila). Prólogo de 18 bytes `48 89 5C 24 18 48 89 74 24 20 57 48 81 EC A0 00 00 00`. Solo actuamos si la vuelta es `+0x1F3965` (llamada de la cámara del jugador en `+0x1F3960`).
  - `+0x826B50` oclusión de CPU → `mov eax,1; ret` en modo VR.
  - Comando de cámara 0x70000014 en `+0xd6f2f` (función `+0xD6BE0`), mismos 12 llamadores que en 2033.
- Gancho: salto absoluto de 14 bytes + trampolín con el prólogo copiado; parcheo en caliente con `EB FE` primero.
- Cuerpo para H: yaw = juego − yaw de cabeza, pitch = 0; así H queda en ladeo + ojos + posición.
- Pendiente de probar: que la cabeza mueva la cámara sin fantasma y sin objetos que se muevan; cinemáticas (el gancho solo actúa en la cámara del jugador; si no, F8).

## 🧭 Cambio de rumbo (6/11/2026) — LEER PRIMERO
Llevábamos muchas versiones (E4–E5h) intentando que **la cámara del juego siga a la cabeza** (`camara_juego`, F8). Resultado: el arma te sigue, pero sale el **mundo marrón** y la cámara se suelta. Estábamos en bucle, así que se **abandona ese camino**.

**Pista clave:** en la vieja D5c, el "modo 2" (vaciar todos los cajones del VS menos el hueco 1) daba **el mismo marrón**. El marrón aparece cuando los dibujos de las luces reciben datos que no cuadran. Tocar la cámara del juego lo provoca.

**El nuevo plan ("la linterna")**
- El juego solo dibuja lo que cae dentro de su "linterna" (su cono de visión: unos 80° × 50°, centrado donde apunta el ratón). Las gafas enseñan 104° × 98°. Lo de fuera no se dibuja: por eso desaparecen paredes y salen huecos negros.
- **No tocar la cámara del juego.** El ratón mueve el cuerpo, el arma y la linterna. La cabeza solo mueve lo que se ve en las gafas (F9 sin F8, que ya funciona y no da marrón).
- **Agrandar la linterna:** la DLL busca en memoria el FOV del juego y lo sube (unos 140°) solo para decidir qué se dibuja; la imagen se sigue haciendo con la lente del visor. Así, al girar la cabeza, lo que miras ya está dibujado.
- **Lo que se pierde:**
  - El arma apunta con el ratón y no con la mirada, como en muchos mods VR. Se arreglará con los mandos.
  - Si giras la cabeza más de unos 20–25° respecto al ratón, el borde puede verse cortado; para mirar muy a un lado, giras el cuerpo.
- **Coste:** la gráfica dibuja algo más; la RX 580 va a 72 fps con margen.

**Otros caminos guardados para más adelante**
- **Geo-11 y el arreglo 3D Vision de Helifax para Metro Redux:** para comparar cómo deberían verse las luces en estéreo.
- **Rehacer el mod sobre 3DMigoto:** más sólido, pero es casi empezar de cero.

## Hecho en la E6
1. ✅ Quitado F8 / `camara_juego` del todo.
2. ✅ Buscador automático del FOV al entrar en partida (tres pitidos subiendo al encontrarlo). Con F9 lo sube a 140°; al quitar F9, lo devuelve. F8 = linterna sí/no; Shift+F7 = buscar otra vez.

## Siguiente paso: probar la E6
1. Entrar en partida y esperar los **tres pitidos subiendo** (FOV encontrado). Si suena un pitido grave, no lo encontró: pasar el log.
2. F9, ratón quieto, mirar con la cabeza arriba, abajo y a los lados. ¿Siguen desapareciendo cosas? ¿Sigue el mundo marrón?
3. Comparar con **F8** (linterna sí/no) mirando a un lado: con ella deberían dejar de salir huecos.
4. Pasar el log (`fov_juego=` debería pasar de 50,6 a 140 con F9).
5. Después: el HUD (problema 3) y luego los mandos (manos y arma).

**Si algo falla:** puede que el juego también use el FOV para el detalle a distancia (LOD) y se vean cosas más simples de lejos; o que algún efecto (niebla, reflejos) dependa del FOV. Se puede bajar `linterna_grados` (p. ej. 120) para probar.

## Resultado de la E6 (6/11, log del usuario)
- El buscador encuentra entre 556 y 984 números que valen 50,625, pero **ninguno de los 300 probados cambia la lente** al escribirlo en directo. Lo más probable es que el juego lea el FOV solo al arrancar o al cargar.
- Mientras prueba candidatos con F9 activo, los fps bajan a unos 24 durante unos 30 s.
- **Siguiente prueba, sin DLL nueva:** poner `r_base_fov 90` en los **dos** user.cfg (el de la carpeta del juego y el de `%LOCALAPPDATA%\4A Games\...\<id>\`) y `linterna=0` en la config. Mirar en el log si `fov_juego=` pasa de 50.6 a 90. Si funciona, la linterna se agranda desde el arranque y no hace falta buscar nada en memoria.

## E7: la cabeza mueve el ratón (6/11)
- **FOV del juego:** `r_base_fov 90` en los **dos** user.cfg funciona (más de 90 lo ignora). El de AppData es `%LOCALAPPDATA%\4A Games\Metro Last Light\<id>\user.cfg`; la carpeta se llama "Metro Last Light", sin "Redux". Desenfoque: `r_blur_level 0`.
- La linterna sola no basta: al mirar atrás con el ratón quieto, el juego no dibuja nada (zonas grises).
- **Solución en prueba (E7):** la DLL manda movimientos de ratón (SendInput) para que **el juego gire con la cabeza**, por la puerta de delante. Lo que sobra lo corrige H = [Rd^T·Rcam | −Rg^T·dg], con Rd = cuerpo·cabeza, y cuerpo = cámara del juego menos lo girado por nuestro ratón.
  - Se calibra sola: radianes por paso de ratón, en giro e inclinación.
  - Si el mundo "patina" al girar, probar `raton_retraso=1`.
- Pendiente: probar en VR y ver `desajuste=` en el log. Riesgos:
  - suavizado o aceleración del ratón en el juego;
  - el tope de inclinación del juego.

## E7d–E8 (6/11): fantasma resuelto, objetos pendientes
- **El fantasma** era el fotograma anterior mal recolocado. Se resolvió fijando en Present el H de los dibujos (hueco 0 y luces), y dando a la cámara (hueco 1) un H hecho con su propia cámara.
- **Objetos** (barandillas, raíles, carteles): se desplazan un poco al mover el visor y vuelven al pararte.
  - Con el H propio de cada dibujo (V = WV·W⁻¹, E7g–E7i) los objetos quedan bien, pero vuelve el fantasma, también con las luces del hueco 7 corregidas con su cámara.
  - Los huecos 8 y 13 son huesos: no tocar.
- **Siguiente paso:** una captura de RenderDoc moviendo el ratón (sin F9, sin gafas), convertida a XML con `renderdoccmd convert`. Sirve para ver qué cámara lleva cada cajón en cada dibujo.

## Cómo dibuja el juego (sacado de sus shaders, 6/11)
- **G-buffer** (3 destinos + profundidad): `t_position` (RGBA16F) guarda la posición **en la vista del juego**. Los objetos sueltos (VS con `cb_main_matrices0`) usan `m_WV` y `m_WVP`. Los objetos en grupo (DrawIndexedInstanced) usan `xform` (WV, por instancia, en la lista de vértices) y `m_P` (hueco 1); si llevan huesos, también `cb_bones` (hueco 8).
- **Pasada previa de profundidad**: VS con solo `m_WVP`, sin PS (la barandilla del Pixel History, EID 224).
- **Hueco 3 = `cb_misc_0`**, con `m_screen` en las casillas 36..51. Cambia según la pasada:
  - de píxel a NDC;
  - "rayos" (de la pantalla a la vista);
  - P;
  - **reflejos**: S·(V_ant·V_act⁻¹), que lee `t_bloom`, la imagen borrosa del fotograma anterior.
- **Hueco 7**: [P | P·T], con T = V_ant·V_act⁻¹, o [P | P·V_ant].
- **Hueco 12 = `cb_sun`** (sombras del sol); **hueco 2 = `cb_screen`** (armónicos esféricos).
- **Oclusión**: una profundidad de 120×67 que se copia a la CPU (CopyResource a STAGING) para decidir qué se dibuja en el siguiente fotograma. Explica cosas que desaparecen un instante.
- **Cámara del arma**: P00 ≈ 0,96 (más estrecha que la principal, 0,5625 con FOV 90). Antes se confundía con la principal.

## Cómo trabajamos
- El usuario lo prueba todo en Windows y pasa `%TEMP%\metro_vr_log.txt`.
- Claude compila la DLL con mingw-w64 para probarla y, en cada versión, actualiza `CHANGELOG.md`, este archivo y `VERSION`. Al subirlo, GitHub Actions crea la Release con la DLL.
- Mensajes cortos y explicaciones "en cristiano", con símiles de hardware.
- Enlaces mejor que capturas.
