# Historial de versiones

Las versiones van por letra (hito) y número. La más nueva, arriba.

## E41 — Exportar el arma, separada por huesos

- La E40 exportó bien la forma del arma, pero sin separar por huesos: el juego llama a esos datos `BONES` y `WEIGHTS` (no con el nombre estándar). Ahora se leen.
- Las posiciones vienen como números enteros (formato 14); se dividen por 2048 para que en Blender salga a un tamaño manejable (no es la escala exacta).
- Plantilla de los vértices del arma (para la siguiente fase): `POSITION` short4 entero, `NORMAL/TANGENT/BINORMAL` byte4, `BONES` byte4 entero, `WEIGHTS` byte4, `TEXCOORD` short2, y una matriz por instancia (`xform0-3`, hueco 1).

## E40 — Exportar el arma a Blender

- **Re Pág** (en partida, con el VR puesto y mirando el arma): el mod guarda el arma que llevas en `%TEMP%\metro_vr_arma_<ic>_<si>.obj` (se abre en Blender: Archivo → Importar → Wavefront (.obj)). Suenan 3 pitidos al terminar.
- Sale **un objeto por pieza y por hueso** (`pieza_<indices>_<inicio>_hueso_<n>`), también las manos (`_brazo`). Así se ve qué hueso mueve el cargador, el cerrojo, cada dedo…
- Para leer los vértices el mod apunta desde el arranque cómo los guarda el juego (gancho en `CreateInputLayout`). El log apunta cada pieza con su formato (`EXPORTAR:`) y los huesos que tiene.
- Sigue todo lo de la E39 (marcar cargador con Av Pág).

## E39 — Cargador de verdad, paso 1: marcar el cargador de cada arma (pruebas)

- **Av Pág** oculta la siguiente pieza del arma (igual que F6). Cuando desaparezca el cargador: **Shift+Av Pág** lo guarda como cargador de esa arma (archivo `%TEMP%\metro_vr_cargadores.txt`, línea `arma_ic arma_si pieza_ic pieza_si`). **Ctrl+Av Pág** borra lo marcado de esa arma. Un arma puede tener varias piezas (cargador + balas, por ejemplo).
- Con el cargador marcado:
  - **B (toque)**: el cargador **desaparece del arma** al momento (antes solo se vaciaba por dentro).
  - **Coger de la cadera (agarre izq)**: el cargador se pinta en la **mano izquierda** (con la cámara de esa mano, como el reloj). Si el del arma no se había sacado, se ven los dos.
  - **Meterlo en el arma**: vuelve a verse en el arma y hace la recarga como en la E36. Mantener B 1 s también lo devuelve.
- Log: `CARGADOR:` al cambiar de arma (si tiene o no cargador marcado), `PIEZAS del arma` marca `(cargador)`, y `MANO IZQ:` añade `fuera= oculto= en_mano=`.
- Lo que queremos saber con esta prueba: si el cargador de cada arma es una **pieza suelta** (se puede ocultar solo él) y cómo se ve en la mano izquierda (seguramente algo desplazado: se colocará en la siguiente versión).

## E38 (rama `manos`) — Expulsar el cargador al momento (sin tener que disparar)
- Prueba de la E37 (usuario, 12/11): B expulsaba, pero **solo al disparar la siguiente bala** (log: `cargador expulsado (tenia 28 balas)` justo tras un disparo). Causa: el gancho que ve el arma (`+0x2B88C0`) solo lo llama el juego al disparar. El cargador no "cae" porque no es un objeto suelto: es una pieza del modelo del arma (eso sería la fase 3).
- Ahora el arma del jugador se recuerda la primera vez que dispara (y qué arma se ve en ese momento), y **al pulsar B se vacía al instante** desde el paso del jugador (hilo del juego, ~100 veces/s).
- Tras **cambiar de arma**, hasta que esa arma dispara una vez el mod no sabe cuál es: B se queda pendiente y se hace en el primer disparo (como en la E37).
- En menús y cargas se olvida el arma (por seguridad).
- Sin probar en las gafas. Lo que no sé: si el modelo del cargador se pone vacío al instante o espera al siguiente disparo.

## E37 (rama `manos`) — Recarga manual (fase 2): expulsar el cargador con B
- Prueba de la E36 (usuario, 12/11): **la recarga desde la cadera sale perfecta a la primera.**
- **B derecho (toque) = expulsar el cargador**: el arma se queda sin balas (vibra el mando der) y no dispara hasta meter otro desde la cadera izq.
- **B mantenido 1 s = recarga de golpe** (la de siempre, por si acaso).
- Cómo: el gancho de la recarga automática (`+0x2BAA90` / `+0x2B88C0`) ve el arma; se comprueba que es la del jugador (misma cuenta que el juego: dueño `arma+0x118` == jugador) y se le ponen las balas a 0 con la función del juego que lo hace al cargar partida (virtual `+0x1350`). Se hace en el hilo del juego.
- Si con `recarga_manual=0`, el toque de B recarga como antes.
- Log: `RECARGA: cargador expulsado (tenia N balas)` y `expulsados=` en la línea de `MANO IZQ`.
- Sin probar en las gafas. Si pulsas B y no pasa nada (sin la línea en el log), es que ese arma no pasa por el gancho: dime cuál.

## E36 (rama `manos`) — Recarga manual (fase 1): el cargador sale de la cadera izquierda
- Investigación (12/11, `docs/PLAN_RECARGA.md`): D-Rey no tiene recarga manual. En el `metro.exe` está la función que decide si el arma recarga sola (`+0x2BAA90`) y una variante de otras armas (`+0x2B88C0`).
- **Ya no recarga sola** (ganchos en esas dos funciones; solo en VR). Log: `RECARGA: gancho en metro.exe+0x… -> MH_OK`.
- **Gesto:** mano izq a la **cadera izquierda** (unos 55 cm bajo la cabeza y 18 cm a la izquierda) y **apretar el agarre** = coges un cargador (vibra el mando izq). **Sin soltar el agarre**, lleva la mano izq al mando derecho (a menos de 20 cm) = se mete: recarga del juego con su animación (vibran los dos) y la mano izq va con el arma ~2,2 s. Si sueltas el agarre antes, el cargador se "cae".
- El agarre izq fuera de la cadera sigue siendo coger el arma con las dos manos. Si sigues apretando tras recargar, pasas a las dos manos.
- **B derecho sigue recargando de golpe** (recarga de emergencia).
- Config: `recarga_manual=1` (0 = como el juego), `recarga_ms=2200`, `cadera_cm=22` (tamaño de la "cartuchera").
- Log: `RECARGA: cargador cogido… / al arma… / soltado…` y en la línea de `MANO IZQ` cada 2 s: `recarga=… (auto_bloqueadas=… cargadores=…)`.
- Escopeta y revólver (cartucho a cartucho) aún no: pueden seguir recargando solos o no; probar y mirar el log.
- Sin probar en las gafas.

## E35 (rama `manos`) — Archivos de serie (para que a cualquiera le salga igual), objetos sin parpadeos y chivato de tirones arreglado
- Prueba de la E34 (usuario, 12/11): **el agarre con el botón funciona** (log: `DOS MANOS SI/NO` al apretar y soltar). En la zona de las cajas, objetos que desaparecían y volvían: era la "oclusión" del juego (descarta lo que cree tapado, calculado con su cámara, no con la del visor). **Shift+F10 lo arregló.** Quedan tirones al girar la cabeza en escenas cargadas (más de ~20.000 objetos por segundo); se mirarán aparte.
- **Archivos de serie**: si a alguien le falta `metro_vr_brazos.txt`, `metro_vr_mano.txt` o `metro_vr_mano_izq.txt` (en `%TEMP%`), la DLL los crea con los datos del autor. Así, quien solo copia las dos DLL ve lo mismo: solo las manos, el reloj, la mano izq en el mando, el agarre a dos manos y el arma más o menos en la mano. **Si el archivo ya existe NO se toca** (la calibración de cada uno manda; para la suya: clic del stick derecho/izquierdo 1,5 s). Log: `ARCHIVOS: … creado con los datos de serie`.
  - Solo vale para la versión de Steam anterior al "next-gen" (la misma que necesita el mod). La calibración de las manos es la del autor (Quest 3S): a otros les servirá de punto de partida.
- **Oclusión del juego QUITADA por defecto** (`sin_oclusion=1`): se dibuja todo lo que está delante, sin cajas que parpadean (algo más pesado). **Shift+F10 sigue cambiándolo y ahora se guarda solo en la config** para la próxima vez.
- **`dos_ojos=2` por defecto** (antes solo lo tenía puesto quien lo añadía a mano en la config). Con esto la config de serie es igual que la del autor.
- **Chivato `SALTITO` arreglado**: con los dos ojos a la vez el límite es 13,9 ms por pareja (antes avisaba desde 9 ms, de un ojo, y llenaba el log de falsos avisos). Cada aviso dice ahora la culpa: `JUEGO`, `juego + MOD` o `juego (justo al limite)`.
- Sin probar en las gafas.

## E34 (rama `manos`) — Agarrar el arma con el botón de agarre izquierdo (y apuntar con las dos manos, como en Pavlov)
- Prueba de la E33 (usuario, 12/11): **el reloj sale bien**. Pero el agarre a dos manos solo funcionaba acercando la mano al sitio del arma (el modo automático de la E32); con el botón no hacía nada, porque el botón seguía siendo el botiquín.
- **Agarre izquierdo (mantener) = coger el arma con las dos manos.** Mientras lo mantienes, la mano izquierda se queda pegada al arma (en el guardamanos, donde la pone el juego) aunque separes el mando. Al soltar, la mano vuelve a tu mando.
- **Dos manos = el arma apunta hacia tu mano izquierda (como en Pavlov).** La empuñadura sigue en la mano derecha y el cañón mira hacia donde está tu mando izquierdo: si mueves la izquierda, el arma la sigue. El giro de la muñeca derecha sigue tumbando el arma. Al soltar, el arma vuelve a ir solo con la derecha en ~0,1 s (sin tirón). Las balas salen hacia donde apunta el cañón, también con las dos manos.
  - Se coge la dirección EN EL MOMENTO de agarrar: no hace falta tener el mando justo en el guardamanos, y al agarrar el arma no pega ningún salto.
  - Tope de 100° (con las manos cruzadas no gira más).
- **Botiquín: quitado** del mando (el agarre izq ya no lo usa).
- **X izquierdo = PAUSA** (toque = Esc). Mantener X medio segundo sigue siendo la máscara. En los menús, X hace de Start (sirve para salir de la pausa).
- **La linterna pasa a Y**: mantener Y medio segundo = linterna. Toque corto en Y = arma siguiente, como antes. (Antes Y mantenido era la pausa, que ahora está en X.)
- Config (`metro_vr_config.txt`): `dos_manos=1` botón (por defecto) · `0` nunca · `2` siempre · `3` automático por cercanía (el de la E32, con `dos_manos_cm`). `dos_manos_ejes=1` el arma apunta a la mano izq (por defecto) · `0` solo se pega la mano, sin girar el arma.
- Log: `MANO IZQ: DOS MANOS SI/NO (boton de agarre izq…)` al agarrar/soltar, y en la línea de cada 2 s `dos_manos=… (boton=… giro=… grados)`.
- Sin probar en las gafas (la cuenta del giro sí se ha probado aparte: el cañón sigue a la mano izq y al soltar vuelve suave).

## E33 (rama `manos`) — Solo las manos, pero con el RELOJ
- Problema (E30): al dejar solo las manos se ocultaban las 13 piezas pequenas guardadas (`ocultar_brazos`), que son **el reloj** de la muneca. El reloj hace falta (no solo es estetico).
- Ahora (`reloj=1`, por defecto): con solo las manos (`manos=1`), **todas las piezas guardadas menos la grande** (la de los brazos, `isl_is_big`) se **pintan**. Con la mano izq en el mando izq (E31) se pintan con **la misma camara que la isla de esa mano** (`piece_skip` devuelve 3 y `LH_WHOLE` en MH_DI/MH_DII), asi el reloj va pegado a la mano izq; si no, con la camara del arma.
- `reloj=0` = como la E32 (reloj oculto). F6 / Shift+F6 / Ctrl+F6 / Inicio / Fin siguen igual: F6 oculta la siguiente pieza, Shift+F6 la guarda como brazo (y desde ahora, si no es la grande, se vera como reloj), Ctrl+F6 borra TODAS las guardadas.
- Log: `reloj=` en la linea `MANO IZQ:` de cada 2 s (piezas del reloj pintadas con la camara de la mano izq).
- Sin probar. Riesgo: que alguna de las 13 piezas no sea del reloj (se veria algo suelto: Inicio ensena cada una) o que el reloj no case con la mano (calibracion de la mano izq).

## E32 (rama `manos`) — Agarrar el arma con las dos manos (o no)
- Prueba de la E31 (usuario, 11/11): la mano izquierda sigue al mando izquierdo y funciona bien; falta calibrarla con la posición real de su mando.
- Con dos manos, la mano izq **pasa de su mando al arma**: el juego ya la pone en el guardamanos con su animación, así que se le da la **misma G que al arma** (G_izq = G_arma). Al soltar, vuelve a su mando.
- **Automático** (`dos_manos=1`, por defecto): cuando acercas el mando izq al sitio donde tiene que estar para agarrar el arma (= donde lo pusiste al calibrar la mano izq, movido con el arma) a menos de `dos_manos_cm` (12 cm), se engancha; se suelta al alejarlo a 1,8 veces esa distancia (21,6 cm). No usa ningún botón (todos los del mando izq ya están ocupados).
- **Para que funcione hay que tener calibrada la mano izq** (mantener el clic del stick izq 1,5 s con el mando donde ves la mano).
- Config: `dos_manos=0` (nunca), `1` (automático), `2` (siempre con el arma); `dos_manos_cm=12`.
- Log: `MANO IZQ: DOS MANOS SI/NO (mando a X cm del sitio de agarre)` al cambiar, y `dos_manos=` en el chivato de cada 2 s.
- No toca la puntería: el arma sigue la mano derecha (apuntar con las dos manos queda para la E33). Sin probar.

## E31 (rama `manos`) — La mano izquierda en el mando izquierdo
- Prueba de la E30 (usuario, 11/11): **bien**, se ven solo las manos. En el corte de la muñeca se ve "hueco" (el juego no pinta la cara de dentro de la malla). Agarre por arma con el revólver: "más o menos" bien.
- La isla de la mano izquierda se pinta con su **propia cámara del arma**: la misma que la del arma, pero movida con el **mando izquierdo** (G_izq = mando_izq_ahora · mando_izq_calibrado⁻¹) en vez del derecho. Se calcula en el Unmap junto a la del arma (también la del ojo derecho con `dos_ojos=2`) y, al pintar la mano izq, se pone en su lugar (VS y PS) y luego se devuelve la del juego.
- **Calibrar la mano izq**: mantener el **clic del stick izquierdo**. A los 0,3 s la mano izq vuelve a donde la pinta el juego; pon el mando ahí (como si la agarraras) y a los 1,5 s pita y se guarda en `%TEMP%\metro_vr_mano_izq.txt`. (Un toque corto sigue siendo correr.)
- Config: `mano_izq_mando=1` (0 = la mano izq con el arma, como la E30).
- Log cada 2 s: `MANO IZQ: mando= calibrada= calibrando= | camaras= dibujos= sin_camara= | islas=`. Si `sin_camara` sube, la mano izq no encuentra su cámara y se pinta con el arma.
- Pendiente: agarrar el arma con las dos manos; postura fija de la mano izq (ahora sigue la animación del juego: al recargar se mueve); tapar el hueco de la muñeca. Sin probar.

## E30 (rama `manos`) — Solo las manos (sin antebrazos)
- Prueba de la E29 (usuario, 11/11): la pieza grande tiene **4 islas**. Con la tecla Fin (de mayor a menor): **n.1 = mano izquierda, n.2 = mano derecha, n.3 = antebrazo izquierdo, n.4 = antebrazo derecho**. Pintada por trozos se ve igual que entera.
- Ahora, en cuanto se encuentran las islas, de la pieza grande **solo se pintan las dos manos**; los antebrazos y el reloj siguen ocultos. Las manos van con el arma (la izquierda, donde la pone el juego). El siguiente paso es llevar la izquierda al mando izquierdo.
- Config (`metro_vr_config.txt`, por si hiciera falta): `manos=1` (0 = brazos ocultos como antes), `mano_izq=1` (0 = sin mano izquierda), `isla_mano_der=2`, `isla_mano_izq=1` (número de isla según la tecla Fin).
- La tecla Fin sigue sirviendo para probar; tras la última pulsación se vuelve a "solo manos". Sin probar.

## E29 (rama `manos`) — Buscar las "islas" de la pieza de los brazos (manos y antebrazos)
- Prueba de la E28 (usuario, 11/11): con Inicio, la 1.ª pieza guardada (la grande, `16734/177912`) enseña **todos los brazos con las manos** (menos el reloj); las otras 13 son piececitas del reloj. O sea: **manos y antebrazos van en una sola pieza** (en Metro 2033 las manos iban solas, 12132).
- Como D-Rey86 (doc 31: partió sus manos en 3 tramos seguidos de la pieza): la primera vez que se pinta la pieza grande en VR, el mod lee sus índices una vez y busca sus **islas** (trozos que no están unidos al resto: cada mano, cada antebrazo…) y los **tramos** seguidos de cada una. Log: `ISLAS: pieza … N islas en M tramos` y una línea por isla (de mayor a menor) con sus tramos `+inicio:cuantos`.
- **Tecla Fin**: 1.ª pulsación = brazos enteros pero pintados por trozos (deben verse igual que siempre); cada pulsación siguiente quita UNA isla (de la más grande a la más pequeña): **lo que desaparece es esa isla**. Tras la última, los brazos se ocultan otra vez. Log: `>>> Fin: brazos SIN la isla n.X …`.
- La pieza grande se sigue ocultando como siempre mientras no se pulse Fin. Sin probar.

## E28 (rama `manos`) — Agarre por arma (como D-Rey86) y buscador de la mano
- Problema: el arma se pega al mando con la calibración que se hizo con UNA arma. El juego pinta cada arma en un sitio distinto (el revólver, con los brazos más estirados), así que con otra arma la empuñadura no queda en el mando y al girar la muñeca el arma "da vueltas" a su alrededor.
- Solución de D-Rey86 (su `docs/history/22` y `15`): un **punto de agarre por arma**, calibrado una vez en las gafas para cada arma. Aquí: cada arma recuerda su propia calibración de la mano.
  - El arma se reconoce por su **pieza más grande que no sea brazo** (como la "pieza cuerpo" de D-Rey). Tiene que salir 15 fotogramas seguidos. Log: `ARMA: ahora <pieza> -> calibracion PROPIA/general`.
  - Calibrar (como siempre: mano donde ves la empuñadura y mantener 1,5 s el clic del stick derecho) ahora guarda la calibración **para el arma que llevas**. Las armas sin la suya usan la general (la de antes). Log: `MANO: calibrada para el arma …`.
  - `metro_vr_mano.txt`: las 3 líneas de antes (general) + una línea `arma <pieza> <inicio> <12 números>` por arma. El archivo viejo vale tal cual.
- **Tecla Inicio**: enseña UNA de las piezas guardadas como brazo (las demás siguen ocultas). Cada pulsación, la siguiente; tras la última, todas ocultas otra vez. No toca el archivo. Sirve para saber **qué pieza es la mano** (siguiente paso). Log: `>>> Inicio: ENSENO la pieza guardada N de 14 (…)`.
- Sin probar.

## E27 (rama `manos`) — Los dos ojos en el mismo fotograma (opción `dos_ojos`, apagada por defecto)
- Lo que dice la E25/E26: los tirones son del **propio juego** (el procesador va al límite). Hasta ahora el juego "pensaba" (física, enemigos, cámara…) **dos veces por pareja**, una por ojo. Con esto piensa **una** y solo se repite el pintado, como hace D-Rey86 en su mod de Metro 2033 (idea y datos de sus `docs/history/17-21`; el código es nuestro, no copiado).
- Cómo va (símil: un PC con dos monitores): cada "pizarra" del juego que depende del ojo (la escena, la profundidad, el brillo, la imagen final…) tiene una **copia** para el ojo derecho. Cada dibujo que va a una pizarra con copia se hace dos veces: en la del juego (ojo izquierdo) y en la copia (ojo derecho), con los cajones de cámara del ojo derecho. Las **sombras** (pizarras cuadradas) se pintan una sola vez y valen para los dos.
  - El juego monta la cámara en el **centro de la cabeza**; cada ojo se aparta en H (igual que ya se hacía con el arma).
  - Cajones (cámara 352, objeto 208, luces 144/272): en el Unmap se guarda lo que escribió el juego y se calcula también la versión del ojo derecho, que va a una copia del cajón. En el dibujo del ojo derecho se ponen esas copias en todas las etapas (vértices, píxeles, teselado, geometría).
  - Borrar, copiar, resolver y mipmaps también se repiten en las copias (MinHook, como los dibujos). Si se copia algo con copia a otra textura, esa también tiene copia (fotos de la escena para el agua/cristal).
  - Las copias se crean la primera vez que se usan y se sueltan solas cuando el juego suelta la suya (una "etiqueta" pegada a cada objeto del juego).
  - A las gafas se mandan los dos ojos en cada fotograma, sin parar el reloj.
- Config `dos_ojos` en `metro_vr_config.txt`:
  - `0` = como la E26 (por defecto).
  - `1` = **PRUEBA**: todo lo que depende del ojo se pinta SOLO en las copias y a las gafas se manda la copia (un ojo por fotograma, como siempre). Mismo coste; **la imagen debería ser igual que con 0**. Si se ve igual, las copias funcionan. (En el monitor se puede ver negro o congelado: normal en esta prueba.)
  - `2` = **de verdad**: los dos ojos en cada fotograma.
- Log nuevo cada segundo: `DOS OJOS: modo= dibujos_dobles= solo_copia= (en_diferido=) cajones_ojo_der= cambios_de_cajones= lecturas_cambiadas= borrados= copias= calculo_con_copia= | coste_mod= | copias: texturas= (MB) vistas= cajones= fallos=`. Al crear cada copia: `DOS OJOS: copia N: AnchoxAlto formato=…`.
  - Si `calculo_con_copia` no es 0, el juego usa "compute" en algo del ojo y eso aún no se repite (siguiente versión).
- Sin probar. Riesgos: objetos que el juego no pinte para el ojo derecho (los quita por estar fuera de su vista), luces o sombras algo corridas en un ojo, o que la gráfica (RX 580) no llegue con el doble de pintado. Mirar `TIEMPOS`, `STEAMVR` y `SALTITO`.

## E26 (rama `manos`) — Borrador también para la cámara
- Chivato de la E25 (usuario a 1280x720): los fotogramas lentos (9-15 ms) son **casi todo del propio juego**; el mod pone 1-2 ms dentro de ellos; el envío a las gafas, el Present y la espera a SteamVR, ~0. En esa zona el juego iba a 72-138 fotogramas/s (no 144 fijos).
- Se recorta lo que queda del mod: el cajón de la cámara (352 B, unas 20 veces por fotograma) también usa el borrador en memoria normal, en vez de leerse de la memoria de la gráfica.
- Lo demás es el juego: para bajar su trabajo, ajustes que cargan el procesador (PhysX avanzado, calidad) — ver respuesta al usuario.

## E25 (rama `manos`) — Chivato de saltitos
- Prueba de la E24 (usuario, a 1280x720): casi todo el rato fluido (144 fotogramas = 72 parejas), pero con "saltitos" al girar la cabeza. Log: algunos segundos hay un fotograma suelto de ~20 ms y esa pareja llega tarde; a ratos baja a 40-60 parejas.
- Nada cambia en el juego. Solo se mide: cada fotograma que tarde más de 9 ms (sin contar la espera a SteamVR) se apunta con su desglose: `SALTITO: fotograma de X ms (ojo N): juego=… (dentro, el mod con los objetos=…) | mod_envio=… present=… espera_steamvr=… resto=…`. Y `saltitos=N/s` en la línea `TIEMPOS`. Con eso se sabe si los picos son del juego, del mod o del envío a las gafas.

## E24 (rama `manos`) — Borrador en memoria normal para los cajones de los objetos
- Prueba de la E23: "va algo diferente, creo". Log: el mod baja de 190-250 a **130-165 ms/s**, y hay segundos con **138-140 fotogramas = ~70 parejas** (antes clavado en 73 = 36 parejas): está rozando el escalón de 72. Pero cada objeto sigue costando **~9,3 us**: el cajón está en memoria de la gráfica, lentísima de LEER aunque se copie directo.
- Ahora, en modo VR, cuando el juego pide escribir un cajón de objeto (208 B) o de luces (144/272 B) con DISCARD, se le da un **borrador en memoria normal** (por hilo). En el Unmap se corrige en el borrador y se copia de golpe a la gráfica (escribir allí es rápido). Config `borrador=1` (0 = como antes). Log `TIEMPOS: … us cada uno … borradores=N/s`.
- Al cambiar la **resolución** desde el menú del juego petó con 0xC00000FD (pila agotada) dentro de d3d11/driver; sin rastro de nuestro código en la pila. Pendiente. Mientras, cambiar la resolución con el juego cerrado (user.cfg).

## E23 (rama `manos`) — Fluidez: el mod gastaba ~3 ms por fotograma leyendo "con red"
- Usuario: la fluidez sigue igual; en el monitor va fluido, en VR no; "le sobran fps pero está capado". `estereo=1` mareó mucho (no repetir).
- Lo que dicen los números (log de la E22): 73 fotogramas/s = 36 parejas; a 72 Hz cada pareja tiene que salir en 13,9 ms, y cuando se pasa aunque sea un poco SteamVR la deja en la mitad (36). Por eso va "clavado": es un escalón, no un tope (D-Rey llegó a lo mismo: "an exact 72/36 plateau is not proof of a limiter"). Cada fotograma cuesta ~10,4 ms; para 72 parejas hacen falta ~6,9.
- **El propio mod se comía 190-250 ms de cada segundo** del hilo que dibuja (~3 ms por fotograma), casi todo al cambiar la lente de cada objeto (~25.000/s): leía su cajón con `ReadProcessMemory`, que es una llamada a Windows por objeto. Entre Map y Unmap el puntero es válido seguro, así que ahora se copia directamente (`lectura_rapida=1`; 0 = como antes).
- Log más fino: `TIEMPOS: … en_el_mod=… (objetos: N/s, ms/s, us cada uno) … esperando_steamvr= fotogramas= imagen=AnchoxAlto`.
- D-Rey (Metro 2033 VR) pinta los dos ojos en el mismo fotograma (duplica los dibujos); su gran ganancia fue la resolución interna del juego (la calidad "Muy alta" dibuja a 1,41x). Apuntado para más adelante.

## E22 (rama `manos`) — Cambio de arma sin salir del VR, y menos "rebote" al mover la cabeza
- Prueba de la E21 (usuario): **controles bien** (mando de Xbox de mentira: `xinput1_4` y `xinput1_3` enganchadas). Falta: al cambiar de arma a veces aún se quita el VR; y la cabeza va con algo de retraso o "rebote" (el arma no).
- **Cambio de arma**: log de la E21: en la **pausa de verdad** el jugador deja de actualizarse (`jugador=0/s`); en los "falsos menús" (cambio de arma, 20-38 dibujos) sigue a ~70/s. Ahora **solo cuenta como menú con el juego parado**. Se quita lo de mirar abajo y los 3 s tras cambiar de arma, que ya no hacen falta.
- **Rebote al mover la cabeza**: el log dice que el juego ya va a ~70 parejas/s (`veces_mostrado=1.00`), así que no es falta de velocidad. Sospecha: el juego monta la cámara un fotograma antes de dibujarla, pero a SteamVR se le mandaba cada imagen con la postura de cabeza más reciente. SteamVR "corregía" la imagen con una postura que no era la suya → rebote al girar. Ahora cada cámara que monta el juego guarda la postura del visor con la que se hizo; cuando esa cámara llega a la gráfica, se apunta, y la imagen se manda a SteamVR **con su postura de verdad**. Config `postura_camara=1` (0 = como antes). Log: `postura: de_su_camara=N (distinta de la ultima M)` en la línea `PANTALLA`; si M es alto, el desfase era real.
- Arreglo: al volver al VR se "apaga" la pantalla flotante, para que la próxima vez se coloque otra vez delante de donde miras.

## E21 (rama `manos`) — Mando de Xbox "de mentira": andar analógico y menús con los mandos VR
- Prueba de la E20h (usuario): casi perfecto. Mirar abajo ya no quita el VR. Pidió: (1) al cambiar de arma se quitaba el VR unos segundos; (2) navegar por los menús solo con los mandos VR; (3) andar despacio o deprisa según cuánto empujes el stick; (4) nota algo de retraso/rebote al mover la cabeza (el arma no).
- (1) Tras cambiar de arma con el mando (Y), durante 3 s los dibujos en pantalla no cuentan como menú (salvo con el juego parado).
- (2)(3) **Mando de Xbox de mentira**: MinHook en `XInputGetState`/`XInputGetCapabilities` de las `xinput*.dll` que cargue el juego (log `MANDO XBOX: … enganchada`). El juego ve un mando de Xbox en el puerto 1 (si hay uno de verdad, se suman).
  - **Partida**: el stick izquierdo va por el mando (analógico: poco = despacio, a tope = rápido); correr = clic del stick izquierdo. Lo demás sigue como teclado/ratón (ya funcionaba).
  - **Menús y pantalla flotante**: todo como mando de consola. Sticks = navegar (el derecho también hace de cruceta), A o gatillo derecho = aceptar, B derecho = volver, X/Y izquierdos = X/Y, agarres = LB/RB, gatillo izquierdo = Start. Nada de teclado ni ratón.
  - Config `mando_xbox=1` (0 = como antes, todo teclado/ratón). Log: `mando_xbox: lecturas=` en la línea `PANTALLA` (si es 0, el juego no lee el mando).
- (4) Pendiente de mirar en el log (no se pudo leer en esta sesión).

## E20h (rama `manos`) — Mirar hacia abajo sin que se quite el VR
- Prueba de la E20g (usuario): "muchísimo mejor" (intro y menús bien). Falta: al mirar hacia abajo se quita el VR.
- Log: mirando abajo salen **24-31 dibujos** en la imagen final, lo mismo que el menú de pausa (24-32): contar dibujos no basta.
- Ahora, para contar como menú hacen falta los dibujos **y** además: o el juego está **parado** (el jugador no se actualiza con el tiempo en marcha: `dt > 0` en la función de mover, que se para en la pausa), o no estás mirando abajo (más de 25°).
- Log nuevo cada segundo (también con la pantalla flotante): `PANTALLA: dibujos_ultimo= max= modo= cine= jugador=N/s (con tiempo parado M) inclinacion=`. Servirá para confirmar que en la pausa el jugador se para.

## E20g (rama `manos`) — Arreglo de la E20f antes de probarla
- En la E20f los brazos solo se miraban con el VR ya puesto, así que la condición "ver los brazos para entrar en VR" no se cumpliría nunca. Ahora, fuera del VR, se mira si se pintan los brazos guardados (sin ocultar nada). El resto, igual que la E20f.

## E20f (rama `manos`) — La intro ya no es "partida", y mirar abajo/derecha ya no quita el VR
- Prueba de la E20e (usuario): el menú principal y la pausa (Esc) van bien, pero: (1) en la intro (el tren con los logos hasta el vagón del menú) se ponía en VR, porque tiene escenario 3D, cámara y **movimiento**; (2) jugando, al mirar mucho hacia abajo o a la derecha se quitaba el VR.
- (1) Log: en la intro los **brazos de Artyom** (las piezas guardadas con F6) no se pintan nunca (`ocultas=0`); jugando, unas 900/s. Ahora, para **entrar** en VR hace falta haber visto los brazos en el último segundo (para seguir en VR no). Si no hay brazos guardados, no se pide.
- (2) Log: al mirar a ciertos sitios los dibujos en la imagen final suben a ~14-15 y el límite era 12. La pausa da 25+ y el submenú de opciones 13-14. Ahora se **entra** en menú con 20 (`menu_dibujos=20`) y se **sigue** en él mientras haya 10 o más (`menu_dibujos_seguir=10`).

## E20e (rama `manos`) — Modo VR solo con la partida cargada
- Prueba de la E20d (usuario): **pantalla flotante perfecta**, **gira bien sobre uno mismo** y **ya no peta al cambiar gráficos** (varias veces seguidas). Falla una cosa: el VR se ponía en el menú principal.
- Log: el menú principal tiene un escenario 3D que usa la cámara del jugador, así que "hay cámara" no basta. Pero la función que **mueve al jugador** (`+0x28D110`, la de andar) no se llama en el menú (`andar=0`) y en la partida va a ~100 veces/s; además se para en la pausa.
- Ahora el modo VR pide las dos cosas: cámara del jugador **y** jugador moviéndose (en el último 0,3 s), durante 1 s seguido, y sin menú encima. Dibujos medidos en la imagen final: partida 1–5, opciones en partida 13–14, pausa 25–32 (`menu_dibujos=12` vale).

## E20d (rama `manos`) — Pantalla flotante propia, menú de pausa y la swapchain al cambiar gráficos
- **Pantalla flotante hecha por el mod** (lo pidió el usuario: nada del overlay de SteamVR). En cada ojo se pinta, sobre negro, un rectángulo con la imagen del juego, colocado en la habitación a 2 m delante (2,6 m de ancho). Shaders propios compilados al arrancar con `d3dcompiler_47.dll` de Windows; 1440×1440 por ojo; se manda con la postura de la cabeza, así la pantalla se queda quieta en el sitio.
- **Menú de pausa / opciones en la partida**: el log de la E20c mostró que en la pausa el juego sigue montando la cámara, así que el automático no se enteraba. Ahora además se cuentan los dibujos que van a la imagen final en cada fotograma: con el HUD quitado son pocos, y con un menú encima (texto, botones) son muchos. Con `menu_dibujos=12` o más → pantalla flotante. Log: `pantalla: dibujos_ultimo= max= modo= cine=` cada segundo, para ajustar el número.
- **Cierre al cambiar gráficos (2.º intento)**: log de la E20c: cada cambio crea una swapchain nueva, y en el segundo cambio el overlay de Steam (`gameoverlayrenderer64.dll`, cargado aunque esté desactivado) leía la **primera swapchain, ya borrada**, dentro del Present. La tabla propia de la swapchain era una foto del momento; si luego alguien cambia la tabla original, no se enteraba. Ahora se sincroniza con la original en cada Present (log `SWAPCHAIN: casilla N … la copio`).

## E20c (rama `manos`) — Cierre al cambiar las opciones gráficas: freno y chivato
- Log del usuario (con la E19), al aplicar opciones gráficas desde el menú: `ERROR 0xC0000005 en d3d11.dll+0x151A5A`, lectura de `0xEC`. Con la `d3d11.dll` de su Windows: es **UpdateSubresource** ("rellena esta textura") de la tabla de funciones "versión 1" del contexto (la misma que alterna con la 2, como los Draw), y la textura que le pasan es **nula**: el juego intenta rellenar una textura cuya creación falló antes.
- El contador `UpdSub` no subió: esa llamada no pasaba por nuestro gancho, porque Windows también cambia esa casilla de la tabla.
- Ahora **UpdateSubresource va por MinHook** (todas sus versiones, como los Draw) y, si llega sin textura, **se salta** (log `FRENO: …`) en vez de petar.
- **Chivato** (MinHook en `CreateBuffer` y `CreateTexture2D` del dispositivo): si el juego no consigue crear algo, el log dice qué, el error y si la gráfica se ha "caído" (`CHIVATO: … FALLO hr=… motivo=…`). También `PRESENT FALLO` si falla el Present. Esto dirá la causa de verdad.
- Ojo: además del menú de antes, la nota vieja (ESTADO.md, problema 4) decía que pasaba también con `visor_imagen=0`.

## E20b (rama `manos`) — Menús, intro y vídeos en una pantalla flotante; VR solo en la partida
- Lo pidió el usuario: fuera de la partida (menús, elegir partida, intro, vídeos, pantallas de carga, el menú de opciones…) la imagen del juego se ve en una **pantalla flotante** delante de ti, con todo lo demás en **negro**. Al cargar la partida del todo, se pone sola en VR.
- **Automático**: el mod mira si el juego está montando la cámara del jugador (el gancho de la cámara nativa, `+0x69B900`). Si la monta durante 1,5 s seguidos → modo VR (lo que antes se hacía con F9). Si deja de montarla → pantalla flotante. Pulsar F9 pasa a manual hasta reiniciar el juego.
- La pantalla es un *overlay* de SteamVR (`IVROverlay_028`): 2,6 m de ancho a 2 m delante de donde miras, recta. Se coloca cada vez que se entra en un menú. Los ojos reciben una imagen negra.
- Config: `pantalla_menus=1`, `pantalla_distancia=2.0`, `pantalla_ancho=2.6`, `vr_automatico=1`. Log: `CINE: …` y `>>> AUTO: …`.
- **Por probar**: si en el menú de pausa el juego sigue montando la cámara, el automático no se enterará (se vería como antes); el log lo dirá.

## E20 (rama `manos`) — Girar sobre ti mismo con el stick
- Usuario: al girar con el mando estando quieto, "giras dando vueltas alrededor de un eje" en vez de sobre ti mismo.
- Causa: la posición de la cabeza va respecto a un punto fijo de la habitación (donde empezó SteamVR), y al girar con el stick el juego gira el cuerpo sobre ese punto. Si no estás justo encima, la cabeza da vueltas a su alrededor, como en un tiovivo.
- Ahora, cada vez que cambia el giro del cuerpo se suma justo lo necesario (`g_piv`) para que la cabeza se quede quieta: giras sobre ti. Ese desplazamiento se va gastando mientras andas con el stick (no se nota) y nunca pasa de 1 m.
- Config `girar_sobre_ti=1` (0 = como antes). Log: `giro: correcciones= desplazado= cabeza=` (cabeza = cuánto estás separado del punto de la habitación).

## E19 (rama `manos`) — Las balas salen del arma, hacia donde apunta
- Antes la bala salía de la cabeza hacia donde mirabas, aunque el arma estuviera en otro sitio.
- Gancho (con MinHook) en `metro.exe+0x3D7FF0`, la función que crea cada bala (RDX = origen, R8 = dirección; idea de D-Rey86 en 2033). Antes de engancharla se comprueba que dentro llame al constructor de la bala (`+0x3D6970`).
- A la bala del jugador se le aplica **el mismo movimiento que al arma que ves** (mano ahora × mano calibrada⁻¹, en los ejes de la cámara): sale del arma y va hacia donde apunta su cañón. Se conserva la dispersión del juego. Si la mano está donde se calibró, la bala no cambia.
- Bala del jugador = sale a menos de 1,5 m de tu cabeza y va a menos de 20° de donde mira la cámara. Las de los enemigos no se tocan.
- **Prueba (usuario, 11/11): funciona, las balas salen del cañón.** Log: gancho `MH_OK`, 144 balas del jugador desviadas (hasta ~30°). Falta ver con enemigos disparando (`otras=0` en la prueba). Se guardó una pieza más como brazo (`252/855750`, un trocito del reloj): 14 en total.
- Config `balas_mano=1` (0 = como antes). Log: `BALAS: gancho en metro.exe+0x3D7FF0 … -> MH_OK`, las primeras `BALA: del jugador. origen … -> … | direccion … -> … (N grados)` y cada segundo `balas: jugador= otras= ultima_desviada=`.

## E18 (rama `manos`) — MinHook: el mod por fin ve los dibujos (para quitar los brazos con F6)
- Prueba de la E17d (vídeo + log): **el arma y los brazos siguen al mando derecho** (`mano: ok=1 calibrada=1`). Los brazos van rígidos pegados al arma y se ven las mangas flotando: hay que quitarlos.
- El log de la E17d enseña por qué no se veían los dibujos: Windows **alterna dos versiones** de cada función de dibujo (p. ej. Draw `…3540` y `…3610`) y va cambiando la casilla de la tabla entre ellas en cada fotograma.
- Ahora se usa **MinHook** (licencia BSD, en `src/minhook`): se engancha **por dentro** cada versión de Draw/DrawIndexed/DrawInstanced/DrawIndexedInstanced que aparezca (hasta 6 de cada). Ya no se toca la tabla para dibujar.
- Con esto deberían funcionar **F6** (ocultar la siguiente pieza del arma), **Shift+F6** (esa pieza es un brazo: se guarda) y **Ctrl+F6** (borrar las guardadas), que estaban hechas desde la E17 pero no veían nada.
- **Prueba (usuario, 11/11): funciona.** Las 8 versiones enganchadas (`MH_OK`), `dibujos≈85000/s`, y con F6/Shift+F6 se quitaron los brazos enteros (13 piezas): **solo queda el arma en la mano**. Las piezas de brazo son las mismas con otras armas.
- Log: `MINHOOK: arrancado`, `MINHOOK <función> version N = … -> MH_OK`, y cada segundo `dibujos=` (debe ser > 0) y `minhook: versiones=… llamadas=…`. Al cambiar de arma: `PIEZAS del arma (n): …`.

## E17d (rama `manos`) — La mano, en la cámara con la que de verdad se pinta el arma
- Log de la E17c: los desvíos de "pinta esto" los vuelve a quitar Windows en **cada** fotograma (288 veces/s), así que el mod sigue sin ver dibujos. Por esa vía no se puede. Se apuntan los primeros bytes de las funciones reales de dibujo para engancharlas por dentro en otra versión (lo que se necesita para quitar brazos).
- Descubierto en el log: `arma: piezas_corregidas` era justo el doble que `mano: piezas`. La mitad eran cajones del cajón 0 (que la E15 movía, pero no son el arma que ves) y la otra mitad la **cámara del arma**, que es con la que se pinta el arma visible (la que arregló el 3D en la E10c). Ahora la mano se aplica también a esa cámara. Log: `mano: camara_arma=…`.

## E17c (rama `manos`) — El mod no veía ningún dibujo
- Log de la E17b: `dibujos=0`. El mod no ve ni una orden de "pinta esto" del juego (sí ve los datos, por eso la cámara y el 3D funcionan). Por eso F6 no encuentra piezas y el HUD de la E11 no hizo nada.
- Sospecha: Windows cambia por dentro la "centralita" de esas órdenes después de que pusimos el desvío, o el juego pinta con otro contexto. Ahora, en cada fotograma, se comprueba y se vuelve a poner, y se usa el contexto que de verdad usa el juego. Log: `VTABLE: …` y `desvios_repuestos=`.

## E17b (rama `manos`) — Arreglos del arma en la mano y de F6
- Log de la E17: el mando derecho "desaparecía" la mitad del tiempo (`porque=3`: SteamVR decía que no sabía cuál era el derecho), así que el arma iba a la mano y volvía a la cara muchas veces por segundo. Ahora se busca el mando derecho entre los mandos y se recuerda el último bueno. Lo mismo para los botones.
- F6 no encontraba ninguna pieza (`pintadas=0`): el juego pinta también desde otros hilos (listas "diferidas") y solo se miraba uno. Ahora se miran todos. Esto explica también por qué el HUD de la E11 no encontraba nada.
- Log: `dibujos=` (cuántos dibujos ve el mod por segundo).

## E17 (rama `manos`) — Solo el arma en el mando: quitar los brazos
- Objetivo: solo el arma, sin brazos, en el mando derecho, y poder verla por todos lados.
- **Quitar brazos** (idea de D-Rey86 en 2033, que no pinta las "mangas"): el mod apunta qué piezas se pintan con la lente del arma (cada una por su número de triángulos y dónde empieza). Las que se guardan como "brazos" no se pintan.
  - **F6**: oculta la siguiente pieza, para ver cuál es (pitido).
  - **Shift+F6**: "la que acabo de ocultar es un brazo": se guarda en `%TEMP%\metro_vr_brazos.txt` y ya no se pinta nunca.
  - **Ctrl+F6**: borra todas las guardadas. (F6 antes era "luces", una prueba antigua.)
  - Config `ocultar_brazos=1`. Log: `PIEZAS del arma (n): 1:triangulos/inicio …` cuando cambian, y `brazos: ocultas= pintadas= guardadas=`.
- **Arma en la mano:** la E15 no hizo nada visible. Ahora el log dice por qué: `mano: … ok= porque= pos=` (1 apagado, 2 falta funcion de SteamVR, 3 sin mando derecho, 4 visor no valido, 5 mando no valido) y dónde está la mano.

## E16 — Optimización: tirones al apuntar y trabajo de sobra
- **Al apuntar** el juego hace zoom y la lente principal se estrecha deprisa. El mod solo aceptaba cambios del 5 %: durante el zoom tomaba la cámara principal por "la del arma" (0,7 s cada vez) y todo se descolocaba a tirones; al soltar se arreglaba. Ahora sigue el zoom (hasta 15 % por paso, y se recupera en 0,15 s). El arma se reconoce comparando con la lente principal del momento.
- **Trabajo de sobra quitado:** el "detective de lentes" (una herramienta de las pruebas antiguas) seguía leyendo entero cada cajón que el juego escribía, en cada dibujo, para siempre. Y la búsqueda de la cámara miraba también los cajones de huesos (3856 bytes) de cada personaje. Apagado lo primero (`detective=1` lo enciende) y lo segundo solo mira el cajón de la cámara.
- La cámara principal se reconoce con la forma real de la pantalla (ya no solo 16:9): se puede probar otra resolución.
- Log nuevo cada segundo: `TIEMPOS: en_grafica(Present)=…ms/s en_el_mod=…ms/s fotograma_mas_lento=…ms lente_principal=…` para saber si lo que frena es la gráfica, el procesador o el mod.
- E15 (arma en la mano): el usuario no notó nada; queda pendiente de revisar.

## E15 — El arma en la mano derecha (paso 2 del plan de los mandos)
- Prueba de la E14: andar hacia donde miras, **bien**.
- Lo que se dibuja con la lente del arma (arma y brazos) ahora sigue al **mando derecho**: se calcula dónde está el mando respecto a la cabeza y se mueve el arma eso mismo (y también su posición para la luz, `m_WV`, para que no se ilumine como si siguiera en la cara).
- **Calibrar:** pon la mano donde ves el arma, apuntando adelante, y **mantén 1,5 s el clic del stick derecho** (pitido doble). Se guarda en `%TEMP%\metro_vr_mano.txt` y se carga al arrancar. Un toque corto sigue siendo el cuchillo.
- Todavía las balas van hacia donde miras (paso 3). Los brazos giran rígidos con el arma.
- Config: `arma_en_mano=1`. Log: `mano: piezas=… calibrada=…`.

## E14 — Andar hacia donde miras
- Prueba de la E13: botones bien, pero el stick izquierdo iba "al revés". No eran los ejes: el juego mueve según el giro del **cuerpo**, y la cabeza solo gira la cámara. Si te giras en la habitación, "adelante" iba hacia donde mira el cuerpo (a tu espalda).
- Gancho en `metro.exe+0x28D110` (movimiento del jugador, llamada en `+0x1F32C5`; comprobado en nuestro exe, idéntico a 2033): el resultado se gira con lo que tu cabeza se ha girado. Ahora "adelante" es hacia donde miras, con el stick y con el teclado. Idea de D-Rey86.
- Log: `ANDAR: gancho puesto…` y `andar=` (veces por segundo).

## E13b — Correr con el mando ya no activa atajos del mod
- En el log de la E13 salía muchas veces `Shift+D: busco la camara del fotograma ANTERIOR`. Al correr (clic del stick = Shift) e ir a la derecha (D), el mod creía que habías pulsado su atajo Shift+D, que pone el juego muy lento 10 s. Igual con Shift+R (recargar). Ahora, mientras el mando tiene pulsado Shift, los atajos con Shift del mod no cuentan.
- Medido en la E13: con la oclusión del juego puesta va a ~72 fps (36 parejas) con margen (espera 300-470 ms/s); quitándola (Shift+F10) baja a 50-65 fps.

## E13 — Mandos como teclado/ratón, y la oclusión del juego otra vez puesta
- Prueba de la E12: en VR el juego hace unas 25-35 parejas de ojos por segundo; cada pareja tarda ~25 ms y no llega a los 13,9 ms de los 72 Hz. En pantalla normal va bien.
- Ahora que la cámara es la del juego, ya no hace falta quitar su "oclusión" (la E9 la quitaba): con ella puesta el juego no dibuja lo que está tapado, y debería ir bastante más rápido. **Shift+F10** la quita/pone para comparar (`sin_oclusion=1` en la config la deja quitada).
- **Mandos** (paso 1 del plan de los mandos):
  - Izquierdo: stick = andar · clic del stick = correr · gatillo = lanzar (C) · agarre = botiquín (Q) · X toque = linterna (F), X mantener = máscara (G) · Y toque = arma siguiente, Y mantener 1 s = menú (Esc).
  - Derecho: gatillo = disparar · agarre = apuntar · A = usar (E) · B = recargar (R) · stick a los lados = girar · stick arriba = saltar · stick abajo = agacharse (Z) · clic del stick = cuchillo (V).
  - Config: `mandos=1`, `giro_velocidad=120` (grados/s), `giro_saltos=0` (p. ej. 30 = giro a saltos de 30°), `raton_por_grado=11`.

## E12 — Medir por qué va a tirones
- El log decía `fps=72` y `espera_steamvr=600ms`: el juego dibuja ~36 parejas de ojos por segundo (la mitad de los 72 Hz de las gafas) y pasa el 60% del tiempo esperando. Eso es SteamVR bajando a la mitad porque la pareja no llega a tiempo, así que cada ojo se refresca a 36 Hz.
- Ahora se apunta cada segundo lo que mide el propio SteamVR: `STEAMVR: pares=… grafica_juego=…ms grafica_total=… entre_pares=… repetidos=… con_reproyeccion=…`. Con esto se ve si falta gráfica (bajar resolución/calidad) o si es otra cosa.

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
