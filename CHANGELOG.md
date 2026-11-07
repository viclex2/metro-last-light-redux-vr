# Historial de versiones

Las versiones van por letra (hito) y número. La más nueva, arriba.

## E11b — HUD apagado desde el juego
- La E11 no encontró los dibujos del HUD (`dibujos_recolocados=0`) y seguía doble. Decisión del usuario: **quitar el HUD** desde el `user.cfg` del juego (`show_game_ui 0`, `g_show_crosshair off`, `g_quick_hints 0`). El reloj de la muñeca (filtro, si te ven) es 3D y se sigue viendo bien.
- El arreglo de la E11 queda apagado por defecto (`hud_vr=0`) para no gastar nada.
- Pendiente para más adelante: avisos útiles (arma o balas en el suelo, la "E" de recoger) como cartel en VR.

## E11 — HUD como un cartel delante de ti (no doble)
- Fotos con F10 (usuario): se ve el aviso de recoger ("E") y el inventario del lateral. En la captura de RenderDoc el juego pinta primero la escena entera en la imagen final (un triángulo que la tapa toda) y luego, encima, el HUD con rectángulos en coordenadas de pantalla.
- Cada ojo tiene la lente descentrada (izq −1.28…0.90, der −0.90…1.28): el mismo píxel apunta a sitios distintos en cada ojo, y el HUD salía doble (unos 20° de separación).
- Ahora, todo lo que se dibuja en la imagen final **después** de la escena se coloca (cambiando el rectángulo de pantalla, sin tocar el juego) como un **cartel** de tamaño fijo a **1,5 m** delante de ti, igual en los dos ojos. Los efectos de pantalla completa no se tocan.
- Config: `hud_vr=1`, `hud_tamano=0.75` (más = HUD más grande), `hud_distancia=1.5` (metros). Log: `hud: dibujos_recolocados=…`.

## E10d — F10 = foto de lo que ve cada ojo
- Prueba de la E10c: **arma y brazos bien** (cerca y de tamaño normal). Es la primera versión jugable.
- Para arreglar el HUD doble hay que ver qué parte sale doble, y desde las gafas no se puede grabar. **F10** guarda lo que ve cada ojo en `%TEMP%\metro_vr_ojo_izq.bmp` y `metro_vr_ojo_der.bmp` (a mitad de tamaño).
- (F10 antes era la prueba de los huecos 8/13, que no servía.)

## E10c — El arma vuelve al centro de la cabeza
- E10b no cambió nada: el log decía que la cámara del arma **sí** sale de nuestra posición (con el ojo puesto).
- Causa real: el juego pega el arma y los brazos a la cámara, y la cámara ya va en cada ojo. El arma se movía con cada ojo, así que los dos la veían en el mismo sitio de la imagen: sin 3D, como en el infinito, y por eso enorme.
- Ahora, todo lo que se dibuja con la lente del arma (más estrecha que la del mundo) se devuelve al centro de la cabeza: en cada ojo se corre la separación del ojo. Log: `arma: piezas_corregidas=…`.

## E10b — El arma y los brazos con profundidad
- Prueba de la E10 (usuario): **sin fantasma, sin objetos que se mueven**. Los ojos se detectan bien (`bien=5000 mal=0`). Quedan dos cosas: **brazos y arma se ven enormes / sin profundidad**, y los FPS van algo irregulares.
- Causa: el juego dibuja el arma y los brazos con **otra cámara** que monta aparte, pegada a la cara, sin pasar por nuestra posición. Así los dos ojos la veían igual (sin 3D), y algo sin 3D parece muy lejos y, por tanto, enorme.
- Ahora a toda cámara que **no** sale de nuestra posición se le pone la separación del ojo. Log: `otras camaras: con_ojo_puesto(arma)=…`. Config `ojo_arma=1`.

## E10 — Cámara nativa completa: ya no corregimos nada objeto por objeto
- Pruebas de la E9 (usuario): con **F11** (sin corrección objeto por objeto) **se va el fantasma**, pero los objetos se mueven en el ojo derecho. Shift+F11 no cambia nada. Con F5 (sin 3D) y moviendo solo el ratón sigue el fantasma. **Conclusión:** el fantasma lo hacía nuestra corrección por objeto, no el juego.
- Ahora a la cámara del juego le damos **todo**: giro, inclinación, **ladeo**, **posición de la cabeza** y la **posición de cada ojo**. Nuestro H pasa a ser "nada" (identidad), así que ya no hay que corregir ningún objeto: solo se cambia la lente por la del visor (igual para todos los dibujos).
- El juego monta la cámara un poco antes de dibujarla. Para saber para qué ojo es, cada cámara que llega a la gráfica se compara con las últimas que dimos; si el ojo no cuadra, se cambia solo (`NATIVA ojos: bien= mal= flip=` en el log).
- Config: `camara_nativa_completa=1` (0 = como la E9).

## E9 — Cámara nativa: la cabeza entra en la cámara del juego por dentro
- He sacado el código del `metro.exe` (quitando la protección SteamStub con Steamless). La parte que **construye la cámara** es idéntica a la de Metro 2033 Redux, en las mismas direcciones que usa el mod de **D-Rey86** (GPL-3): `metro.exe+0x69B900` (base de la cámara: posición, adelante, arriba), llamada desde la cámara del jugador en `+0x1F3960`; y la "oclusión" de CPU en `+0x826B50`. Las 6 firmas coinciden.
- **Ya no se mueve el ratón.** Cada fotograma, cuando el juego monta la cámara del jugador, le sumamos el giro de tu cabeza y le ponemos tu inclinación. El cuerpo sigue con ratón/mando. Así **todo** el juego (dibujo, luces, reflejos, el arma, a dónde apuntas, qué se descarta) usa ya la cabeza, sin ir un fotograma detrás. Nuestro H solo pone lo que falta: ladeo, cada ojo y posición de la cabeza.
- **Oclusión desactivada en VR** (`sin_oclusion=1`): el juego ya no esconde objetos que cree tapados mirando el fotograma anterior (paredes/objetos que desaparecían un instante).
- **F8** = cámara nativa sí/no (apagada vuelve al método del ratón de la E7/E8). Si el juego no tiene esas direcciones (otra versión), lo dice en el log y usa el ratón.
- Log: `NATIVA: gancho puesto…`, `NATIVA: la cabeza entra…`, `OCLUSION: desactivada`, y cada segundo `nativa=1(60/s)`.
- Config: `camara_nativa=1`, `sin_oclusion=1`.
- Escrito por nosotros, con la idea y las direcciones de D-Rey86/Metro2033ReduxVR. ¡Gracias!

## E8h — El "cuerpo" solo se calcula con la cámara principal
- Probada la E8g: sigue igual. O fantasma o los objetos se mueven. **Pista nueva del usuario:** los objetos solo se mueven al mover **las gafas**, nunca al mover **el ratón**.
- Causa probable: desde la E7g (`por_objeto`), cada cámara del hueco 1 (también la del **arma**, que se balancea al girar) recalculaba el "cuerpo" (la cámara sin nuestro ratón) con **su propia** orientación. Los dibujos que venían después usaban ese cuerpo equivocado, así que se colocaban un poco girados respecto al mundo, y ese giro cambiaba al mover la cabeza. En la E7d solo lo calculaba la cámara principal, y por eso no tenía fantasma.
- Ahora el cuerpo se calcula **solo con la cámara principal**, al llegar en cada fotograma. Cada dibujo y cada cámara (también la del arma) llevan su H hecho con **su** vista y **ese** cuerpo.
- Referencia: el mod VR de Metro 2033 Redux (D-Rey86, sobre 3DMigoto) encontró lo mismo: posición con m_WVP en el hueco 0, y objetos en grupo con `xform` (WV) más m_P. Allí acabaron escribiendo la cámara del juego con una función nativa en vez de mover el ratón.

## E8g — Lo que de verdad usa el juego (sacado de sus shaders) y el fantasma de los reflejos
- He leído los shaders del juego desde la captura de RenderDoc (qué variables usa cada uno):
  - El G-buffer guarda las posiciones **en la vista del juego**, sin nuestra corrección. Los objetos sueltos las sacan de `m_WV` (hueco 0) y los objetos en grupo, de la lista de vértices. Así que cualquier pasada que lleve esas posiciones a la pantalla tiene que usar **lente del visor × H**, y la que va al revés, su inversa.
  - Hay una pasada de **reflejos** (`t_bloom`, la imagen borrosa del fotograma **anterior**) que usa una matriz del hueco 3 (`m_screen`) con el paso "vista de ahora → vista de antes". **No la corregíamos en ninguna versión**: ese reflejo del fotograma anterior desplazado es muy probablemente **el fantasma oscuro**.
  - El hueco 7 lleva [P | P·(cámara anterior)], para lo mismo.
  - Con `r_base_fov 90`, la **cámara del arma** también es 16:9 y se colaba como "cámara principal", así que a ratos cambiaba la lente de referencia y algunas correcciones fallaban al azar. Ahora la principal es la de lente **más ancha**.
- Ahora (`previo=1`):
  - Los dibujos del hueco 0 y todas las cámaras llevan su propio H, como la E8e.
  - Hueco 7: P → lente·H de ahora; la cámara anterior → lente·H **del fotograma anterior**.
  - Hueco 3: "rayos" → con la lente y pasados a la vista del juego (H⁻¹); P → lente·H de ahora; **reflejos** → con la lente y el H del fotograma anterior.
- Teclas para comparar en directo: **Shift+F11** = `previo` sí/no; **F11** = `por_objeto` sí/no.
- Probado por Claude en el PC del usuario, sin visor (cabeza falsa que se mueve sola, `cabeza_falsa_auto=1`): se calibra, el juego gira con la "cabeza" y las correcciones nuevas se aplican (en el log, `h7` y `h3`).
- Juego en ventana (`r_fullscreen off`) para que no se cierre al cambiar de ventana.

## E8e — Lo que se vio en RenderDoc con el píxel
- Con Pixel History en la captura de RenderDoc:
  - **La barandilla** (EID 224) es un `DrawIndexed` que usa `cb_main_matrices0` (hueco 0) y su posición sale de **m_WVP**.
  - **El suelo** (EID 313) es un `DrawIndexedInstanced` que usa `cb_main_matrices1` (hueco 1): su posición respecto a la cámara (WV) va en la lista de vértices y la lente sale de m_P.
- En la E7d, el hueco 1 llevaba el H de su propia cámara, pero el hueco 0 el de la cámara anterior. Por eso la barandilla se despegaba del suelo al moverte.
- En la E7g el hueco 0 pasó a llevar su propio H: la barandilla quedó bien, pero la **cámara del arma** (otra cámara en el hueco 1) seguía con el H viejo. El arma se dibuja con piezas de los dos caminos, se partía en dos y salía **el arma fantasma**.
- Ahora (`por_objeto=1`), los dibujos del hueco 0 **y todas las cámaras** del hueco 1 llevan su propio H. El hueco 7 queda como en la E7d.

## E8d — Otra vez la base de la E7d, para capturar con F9 en RenderDoc
- La E8c sigue con fantasma. Se vuelve a la E7d (`modo_h=0`, por defecto) y se estudian los objetos con una captura de RenderDoc **con el modo VR activo**: cabeza falsa con las flechas, F9 y una flecha pulsada mientras se captura.
- `modo_h=1` = como la E8c, por si hace falta comparar.

## E8c — La fórmula buena para el hueco 7
- Probada la E8b: vuelve el fantasma.
- Mirando en la captura qué hay exactamente en el hueco 7: no es la cámara anterior tal cual, sino el **paso de "vista de ahora" a "vista de antes"**, T = V_ant·V_act⁻¹. Con el ratón girando, es un giro pequeño (unos 3°) y sin desplazamiento.
- La pieza de reaprovechar la imagen recibe las posiciones en **nuestra** vista de ahora (H_act·V_act). Así que lo correcto es M2' = Plente_ant·H_ant·T·**H_act⁻¹**. A la E8b le faltaba ese último H_act⁻¹, y a la E7d también, aunque allí casi no se notaba.

## E8b — Lo que enseñó RenderDoc: un H por fotograma, y el hueco 7 con el del fotograma anterior
- Captura de RenderDoc (sin F9, moviendo el ratón), analizada dibujo a dibujo:
  - Dentro de un fotograma **todos** los dibujos usan la misma cámara (la del hueco 1).
  - En la E7d, el H de los dibujos se fijaba en Present con la cámara del fotograma **anterior**. Por eso los objetos se desplazaban al moverte.
  - Los objetos que se dibujan en grupo llevan su posición respecto a la cámara (WV) en un cajón de vértices, y la lente la toman del hueco 1.
  - El hueco 7 guarda la cámara **del fotograma anterior** (para reaprovechar su imagen). Por eso, cuando le dábamos el H de ahora, salía el fantasma.
- Ahora:
  - **Un solo H por fotograma**, hecho con la cámara de ese fotograma (la primera que llega después de Present), para todos: hueco 0, hueco 1 y luces.
  - En el hueco 7, la lente actual lleva el H actual, y la cámara anterior lleva **el H y la lente que se usaron en el fotograma anterior** (en estéreo, los del otro ojo).
- El log cuenta `fotogramas_H=` y `luces7_anterior=`.

## E8 — Volvemos a la base de la E7d
- Probada la E7i: igual (los objetos bien, pero con fantasma). Se decide **quedarse con la E7d**, sin fantasma y con los objetos moviéndose un poco, y estudiar los objetos con **RenderDoc**.
- `por_objeto=0` por defecto: funciona como la E7d. Con `por_objeto=1`, como la E7i, para comparar.
- La última DLL compilada queda también en `bin/d3d11.dll`, por si la Release tarda.

## E7i — Las luces también con su propia cámara
- Probada la E7h: igual que la E7g (los objetos bien, pero vuelve el fantasma). El iW del cajón sí era la inversa de W (`iW_raro=0`), y casi todos los dibujos usan su propia cámara (`fallan=0`).
- Conclusión: el fantasma sale cuando la geometría y **las luces** (hueco 7) no se corrigen igual. En la E7d las dos iban mal "a la vez" y cuadraban entre sí. En la E7g la geometría pasó a ir bien, pero las luces no.
- Ahora el hueco 7 también usa un H hecho con su propia cámara (V = P⁻¹·VP). El log cuenta `luces7_propias=`.

## E7h — Cada dibujo con su cámara, calculada con W
- Probada la E7g: **los objetos dejan de moverse**, pero **vuelve el fantasma**. Sospecha: un mismo objeto se dibuja en varias pasadas y no todas recibían el mismo H (unas el propio y otras el general), así que las pasadas no coincidían.
- Posible causa: el "iW" del cajón no siempre es la inversa de W; podría ser la de las normales. Ahora la inversa la calcula la DLL con W.
- El log cuenta `propios=` (con su cámara), `fallan=` (con el H general) e `iW_raro=` (veces que el iW del cajón no era la inversa de W).

## E7g — Cada dibujo con su propia cámara
- Probada la E7f: los objetos siguen moviéndose (`rescatados=0`: no era eso).
- Nueva idea: cada dibujo del hueco 0 trae, además de WV, la inversa de su posición en el mundo (iW). Así se puede saber **qué cámara usó exactamente ese dibujo**: V = WV·iW.
- Ahora cada dibujo se corrige con un H hecho **con su propia cámara**: H = (cuerpo·cabeza)^T·V del dibujo. Da igual que lleve la cámara de este fotograma o la del anterior: todos acaban mirando a donde mira la cabeza.
- `por_objeto=1` (0 = como la E7f). El log cuenta `propios=`.

## E7f — Rescate de los dibujos que no cuadran
- Probada la E7e: con F10, los objetos se mueven **más**, y la mirilla y el silenciador del arma flotan. Los huecos 8 y 13 son los **huesos** de los modelos (en el espacio del propio modelo) y no se deben tocar. F10 se queda apagada.
- La E7e leía esos cajones aunque F10 estuviera apagada, y el juego iba a unos 24 fps. Arreglado.
- Nueva sospecha: un tercio de los dibujos del hueco 0 se **saltaban** (`cb0_salta`), porque su WV y su WVP no cuadran entre sí. Seguramente uno lleva la cámara de este fotograma y el otro la del anterior, y eso solo pasa con la cámara moviéndose. Esos objetos se quedaban sin corregir.
- Ahora, si su lente se parece a la principal, se corrigen con la lente principal (`rescate=1`). El log cuenta `rescatados=`.

## E7e — Objetos que se mueven al mover la cabeza (F10)
- Probada la E7d: **¡adiós al fantasma!** El arma sigue a la cabeza y apenas desaparece nada (alguna cosa muy lejana, unos milisegundos).
- Nuevo fallo: los **objetos** (barandillas, raíles, carteles...) se desplazan un poco al mover el visor y vuelven a su sitio al pararte. El escenario no.
- Sospecha: esos objetos se dibujan en grupo, con sus matrices en los huecos **8 (3856 bytes) y 13 (3888)**, 80 matrices de 3×4, que no corregíamos.
- **F10** activa y desactiva la corrección (H) de esas matrices en directo: pitido subiendo = activada. El log apunta unas muestras de esos cajones y cuenta `objetos8_13=`.

## E7d — Intento contra el fantasma
- Resultado de Shift+D: los dibujos (hueco 0) y las luces (hueco 7) que llegan **después de Present** usan la cámara que el juego preparó **antes de Present**. El juego prepara la cámara del siguiente fotograma antes de terminar el actual.
- La E7 recalculaba H al llegar la cámara nueva, a mitad de fotograma. Los últimos dibujos del fotograma se corregían con la cámara del siguiente y, al moverte, salía el fantasma.
- Ahora el H de los dibujos se fija en Present, y el cajón de la cámara lleva un H hecho con su propia cámara.
- El log cuenta el orden: cuántos dibujos llegan antes y después de la cámara en cada fotograma (`orden=`).

## E7c — Detective del fotograma anterior (Shift+D)
- Prueba de la E7b: el "marrón" es en realidad **un fantasma**, un trozo de imagen (el arma, el escenario) repetido en otro sitio y sin luz. **Sale en cuanto el visor se mueve lo más mínimo**, también sin estéreo (F5), y **desaparece con el visor totalmente quieto**.
- Eso apunta a que el juego **mezcla el fotograma anterior**, una especie de "estela", con unas matrices de cámara que nosotros no corregimos.
- **Shift+D:** durante 10 s busca en todos los cajones las matrices de la cámara **del fotograma anterior** y apunta en el log dónde están (`ANTERIOR: ...`). Se usa sin F9, moviendo el ratón.

## E7b — Arreglo: el juego daba giros locos al perderse el visor un instante
- Probada la E7: **se calibra bien** (0,09° por paso) y **el juego gira con la cabeza**. En el vídeo, las luces se ven bien y el arma sale delante al mirar a cualquier lado.
- Fallo: si SteamVR perdía el visor un instante, la DLL "volvía a empezar" y mandaba otra vez todo el giro de la cabeza, de modo que el juego daba un giro de más. Pasó docenas de veces seguidas (`RATON: empiezo...`).
- Ahora, si se pierde el visor, solo espera. Solo vuelve a empezar al activar F9 o al recentrar.
- Un salto de más de 45° en un fotograma (SteamVR recentrado, el visor que vuelve) se ignora.

## E7 — La cabeza mueve el ratón: el juego gira de verdad con tu cabeza
- Probada la E6: el FOV del juego **no cambia en directo** (ninguno de los números probados cambia la lente). En cambio, **`r_base_fov 90` en los dos user.cfg sí funciona** (el log pasa de 50.6 a 90). El 100 que había lo ignoraba: 90 parece el máximo. Ahora `linterna=0` por defecto.
- En VR, con el FOV a 90: no hay marrón, 72 fps y de frente se ve bien. Pero, girando mucho la cabeza (más de 120°, mirar atrás) con el ratón quieto, salen **zonas grises sin dibujar**: la linterna no llega detrás.
- **Nuevo `raton_cabeza=1`:** con F9, la DLL **mueve el ratón por ti** según giras la cabeza, como si lo movieras tú. Así es el propio juego el que gira hacia donde miras, por la puerta de delante, sin escribir en su memoria: dibuja lo que miras de verdad y no debería salir marrón. Lo que el juego va por detrás, inclinar la cabeza de lado, moverla y la posición de cada ojo los pone H, calculado con la cámara real de cada fotograma.
  - La primera vez que pulsas F9 se **calibra solo**: gira y vuelve un poco, así que no muevas el ratón 2 s. **Dos pitidos subiendo = listo**; uno grave = no pudo (lo reintenta en 15 s). **Shift+R** = calibrar otra vez.
  - Arriba/abajo, el juego se lleva hasta `raton_max_grados=70`; lo que pase lo pone la DLL.
  - Solo mueve el ratón si el juego está en primer plano.
  - Tu ratón real sigue funcionando: gira el "cuerpo".
- El log, cada segundo: `raton=1` (activo), `desajuste=` (grados que corrige la DLL; poco = bien) y `pasos=` (ratón mandado).
- Desenfoque de movimiento quitado (`r_blur_level 0` en los dos user.cfg).

## E6 — La linterna: el juego mira más ancho (sin tocar su cámara)
- **Fuera `camara_juego` / F8 del todo.** La cámara del juego vuelve a ser solo del ratón. La cabeza solo mueve lo que se ve en las gafas (lo que ya iba bien y no da marrón).
- **Buscador automático del FOV del juego.** Al entrar en partida (tras unos 5 s viendo la cámara) busca solo el número del FOV en memoria y lo comprueba subiéndolo 20° un instante. **Tres pitidos subiendo = encontrado.** Un pitido grave = no lo encontró (lo reintenta solo, hasta 4 veces).
  - Busca 6 formas del número: grados, radianes, medio ángulo o tangente (vertical) y grados o radianes (horizontal). Prueba primero los grados.
  - Ya no mira dentro de nuestra propia DLL (antes se encontraba a sí mismo).
- **Con F9, el FOV del juego sube a 140° en vertical** (`linterna_grados`). Al quitar F9 vuelve a su valor. La imagen se sigue haciendo con la lente del visor: solo cambia **qué** se dibuja, no **cómo** se ve.
- **F8 = linterna sí/no** (para comparar al momento). **Shift+F7 = buscar otra vez** el FOV.
- **Seguridad:** si el número deja de cuadrar con la lente del juego durante 8 s (cambio de nivel, etc.), se suelta y se busca de nuevo.
- La DLL ya admite lentes muy anchas (antes descartaba todo lo que pasara de ~120°).
- El log dice el margen para girar la cabeza y, cada segundo, `fov_juego=` y `linterna=`.

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
