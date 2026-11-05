# Historial de versiones

Las versiones van por letra (hito) y número. La más nueva, arriba.

## E5h — Se engancha también bajo RenderDoc
- Con RenderDoc el juego arrancaba pero la DLL no se enganchaba: ni pitidos, ni F9, ni resumen en el log. Bajo RenderDoc la creación del dispositivo llega distinta (con D3D11CreateDeviceAndSwapChain y quizá sin pedir el contexto).
- Ahora, si no viene el contexto, se saca del dispositivo. El log apunta qué llega ("enganchando: dispositivo=... contexto=... swapchain=...").

## E5g — Cabeza falsa (para probar sin visor y capturar con RenderDoc)
- Con la E5f, RenderDoc ya no entra en bucle y el juego arranca **sin el visor**. Con el visor conectado no arranca (RenderDoc y SteamVR chocan).
- Nuevo `cabeza_falsa=1`: sin visor, las **flechas** hacen de cabeza y mueven la cámara del juego igual que el visor (con F9 y F8). Así se puede reproducir lo marrón y capturarlo con RenderDoc.
- Para RenderDoc: `cabeza=0`, `visor_imagen=0` y `cabeza_falsa=1` (así no se intenta arrancar SteamVR).

## E5f — Compatible con RenderDoc
- Con RenderDoc el juego moría en el arranque, por un desbordamiento de pila tras 2.382 llamadas a D3D11CreateDeviceAndSwapChain. RenderDoc engancha "la" d3d11.dll y, como el proxy también se llama d3d11.dll, al llamar a la real nos llamaba a nosotros: bucle.
- Ahora, si nos llaman desde dentro de una creación de dispositivo (mismo hilo), vamos directos a la función de Windows, buscándola a mano en su tabla de exportaciones, sin pasar por GetProcAddress, que RenderDoc también engancha.

## E5e — La cámara se encuentra mejor y el buscador de FOV es más fino
- En la prueba de la E5d no llegó a encontrarse la cámara del juego, así que Shift+F8 no tenía efecto visible. La comprobación olvidaba la candidata en cuanto movías el ratón; ahora le da unos 5 s y el margen es de 0,05 rad.
- El buscador de FOV tenía un margen demasiado ancho (400 candidatos, el máximo); ahora busca el valor casi exacto.
- Pitidos: cámara encontrada = 2 agudos; FOV encontrado = 3 subiendo.

## E5d — Cazar lo marrón y agrandar el FOV del juego
- Resultado de la E5c (F5–F8): **lo marrón lo causa F8 (camara_juego)**. Las luces (F6) y la lente (F7) no lo cambian. `r_base_fov 100` en user.cfg no se aplica (P00 sigue en 1,18931).
- **Shift+F8:** va pasando por 5 formas de escribir la cámara del juego (0 = todo, 1 = solo giro, 2 = solo inclinación, 3 = solo la pareja principal, 4 = giro en las 3 parejas e inclinación solo en la principal). Pitidos = número de forma + 1. Lo que no se escribe en el juego lo pone H.
- **Shift+F7:** busca en memoria el FOV del juego (por grados, radianes o medio ángulo), prueba cada candidato mirando si cambia la lente y, si lo confirma, lo pone a **110°** en vertical mientras F9 y fov_visor estén activos. Al quitar F9 lo devuelve a su valor.

## E5c — Interruptores en directo (F5–F8)
- **F5** estéreo, **F6** luces_visor, **F7** fov_visor (lente del visor), **F8** camara_juego. Se encienden y apagan sin reiniciar, con pitidos (subiendo = encendido) y una línea en el log.
- Sirve para averiguar qué pieza causa el "mundo marrón", que solo aparece con camara_juego.
- Prueba de la E5b con `r_base_fov 100` en user.cfg: **no se aplicó** (el log sigue mostrando P00 = 1,18931). Seguramente Steam Cloud o el juego restauran el user.cfg.

## E5b — La cámara de la cabeza no se suelta
- Probada la E5: los cajones de luces se corrigen (unos 216 por segundo del hueco 7 y 72 del hueco 3).
- Problema: la cabeza dejaba de mover la cámara tras unos segundos, porque el juego rehacía el giro desde las otras copias al mover el ratón.
- Ahora se escriben **siempre las tres parejas** (inclinación, giro) del objeto cámara: +0/+4, +12/+16 y +24/+28.
- La búsqueda por firma admite copias que no sean idénticas (hasta unos 34°).
- La vigilancia es más tolerante: suelta la cámara si no cuadra por más de 0,6 rad durante 30 fotogramas.

## E5 — Luces y sombras con la lente del visor
- Corrige dos cajones que usaban la lente original del juego (80°):
  - **Hueco 7 (144 bytes):** la lente completa más una matriz de reproyección. Se cambian por `lente del visor × H`.
  - **Hueco 3 (272 bytes), casillas 36–43:** la "receta" con la que cada píxel saca su dirección para iluminarse. Se rehace para la lente del visor, que está descentrada.
- Objetivo: quitar el "mundo marrón" sin luces ni sombras.
- Nuevo interruptor `luces_visor`. El log cuenta `luces7=` y `luces3=` cada segundo.

## E4h / E4g / E4f — El "detective"
- Busca en todos los cajones (Map y UpdateSubresource) los números de la lente original del juego y apunta en qué hueco de VS, PS o CS están.
- Se corrigieron falsos positivos por números redondos (1.0) y por NaN.

## E4e / E4d / E4c / E4b / E4 — La cámara de verdad del juego sigue la cabeza
- **E4:** con F9, escribe ratón + cabeza en la cámara del juego (inclinación en +0x650, giro en +0x654). El resto (inclinar la cabeza de lado, moverla, la posición de cada ojo) va en `H = [Cd^T·Cg | −Cd^T·Ym·dg]`.
- **E4b:** solo se fía de una dirección si cuadra en dos direcciones distintas. Vigila cada fotograma y la suelta si deja de cuadrar. *(La E4 pillaba una casilla vacía durante la carga y el juego petaba.)*
- **E4c:** arreglado un bucle infinito al ajustar ángulos con números basura; dejaba muerto el F9.
- **E4d:** si el juego borra lo nuestro al mover el ratón, lo detecta y lo vuelve a poner. Escribe valores absolutos.
- **E4e:** los punteros fijos no siempre sirven. Si fallan, busca la cámara por su "firma" (tres copias seguidas). Dos pitidos agudos al encontrarla.

## E3 a E3g — Buscador de la cámara en memoria (Shift+H)
- Busca entre unos 2,3 GB de memoria los números que siguen al ratón (8 formas de ángulo, en radianes o grados).
- Solo sigue la cámara principal (16:9).
- Compara con el ratón quieto y permite 2 fallos por candidato.
- **Prueba de escritura** automática.
- **E3g: totalmente automático** (mueve el ratón él solo).
- Encontrado: inclinación y giro, con punteros desde metro.exe+0xD08E18 (y otros) más +0x650.

## E2 a E2e — Estéreo sincronizado
- **E2:** los dos ojos del mismo instante. En el segundo ojo, el reloj del juego se para (se enganchan QueryPerformanceCounter, GetTickCount y timeGetTime en la tabla de importaciones de metro.exe).
- **E2b:** el reloj se para en el último valor ya visto; antes iba hacia atrás y daba cámara rápida.
- **E2c:** los hilos que esperan al reloj ven la hora real.
- **E2d:** solo se para el reloj al hilo que dibuja. Válvula de seguridad de 25 ms.
- **E2e:** el log ya no frena al juego (bandeja en memoria más un hilo escritor). *(Los cortes de cada segundo eran del wifi del Quest.)*

## E1 a E1c — Estéreo por fotogramas alternos
- Un ojo por fotograma, desde la posición real de cada ojo (58 mm), con su propia lente.
- Submit con postura (`Submit_TextureWithPose`).
- **E1b:** apunta en el log los fps reales y la espera a SteamVR. Resultado: 72 fps, con margen.
- **E1c:** lee los avisos de SteamVR (PollNextEvent).

## D9 a D9d — Lente del visor
- **D9:** el juego dibuja con la lente del Quest 3S (104 × 98 grados) y cada ojo recibe su trozo.
- **D9b:** usa la postura de WaitGetPoses; quitó el retraso raro. Fuerza el V-Sync del juego a 0.
- **D9c/D9d:** rehace la copia si cambia el dispositivo o la pantalla del juego. El log distingue nuestra DLL en los errores.

## D8 a D8c — Imagen al visor
- Metro arranca como aplicación "Scene" de OpenVR y manda la imagen al visor.
- **D8c:** textura compartida en un dispositivo D3D11 propio, en la gráfica de SteamVR (antes daba el error 106).

## D7 a D7c — La cabeza mueve la vista
- **D7:** lee la postura del visor (OpenVR); el teclado se suma como "cuerpo".
- **D7c:** Present enganchado con una **tabla propia** en la swapchain del juego (desde CreateRenderTargetView). Así no hay bucle con la Steam Overlay.

## D6 — El arma gira con el mundo
- El arma (lente estrecha 1,83, EID 355 en RenderDoc) también gira. Shift+Y para cambiar.

## D5c y anteriores
- La proxy carga bien. Se encuentra la cámara en el hueco 1 (`cb_main_matrices1`, 352 bytes) y la geometría en el hueco 0 (208 bytes).
- Giro coherente de todo el mundo controlado por el teclado.
