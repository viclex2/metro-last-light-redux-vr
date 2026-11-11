# Plan: manos (mano derecha en el arma, mano izquierda en el mando izquierdo)

Investigado el 11/11/2026 en el mod de Metro 2033 Redux de D-Rey86 (GPL-3): `docs/history/15-current-architecture.md`,
`22-shooting-current-state.md`, `31-separated-left-hand-and-watch.md`, `32-prologue-left-hand-and-watch.md` y su código
(`HackerContext.cpp`: `kWeaponCalibrations`, `IdentifyWeapon`, `SubstituteWeaponInstanceBuffer`, `BeginLeftHandBonePalette`;
`VRPose.cpp`: `kEmbeddedWeaponCalibrationLines`). Aquí solo se usan sus ideas y datos; el código es nuestro.

## 1. El arma "en la mano": agarre por arma (HECHO en la E28)
- D-Rey: cada arma tiene su **punto de agarre** (la empuñadura, en la vista del juego). El arma gira alrededor de ese punto
  y ese punto se pone en el mando. Probó a calcularlo midiendo la malla y falló (70 cm de error: la malla es "con huesos");
  lo que funcionó fue **calibrarlo a mano en las gafas, una vez por arma**, y guardarlo (perfiles `v4 <arma> … x y z giro…`).
- El arma se reconoce por su **pieza cuerpo** (la más grande del arma, sin contar manos/reloj).
- Nuestro equivalente: `G = mano_ahora · mano_calibrada⁻¹` ya pega el arma al mando como un objeto rígido, con el
  "agarre" donde estaba el mando al calibrar. Faltaba que cada arma tuviera su propia calibración → E28.

## 2. Mano derecha visible
- En 2033: brazos (6576) ocultos siempre (giran rígidos con el arma y queda raro: "la mayoría de mods VR acaban con manos
  flotando en el arma"). Las **dos manos son UNA sola pieza** (12132 índices, con huesos en el hueco 8 = 3856 B, igual
  que aquí), que va con el arma.
- En Last Light no sabemos aún qué pieza es la mano: tenemos 14 guardadas como brazo (`metro_vr_brazos.txt`, la grande
  `16734/177912`, 12 pequeñas 849921–855738 y `252/855750` del reloj). **E28: tecla Inicio** para enseñarlas una a una.
  - Si la mano derecha es una pieza suelta: basta con quitarla de la lista (ya va con la cámara del arma, o sea, al mando).
  - Si las manos vienen juntas con los antebrazos o con la otra mano: hay que **partir la pieza** (paso 3).

## 3. Mano izquierda en el mando izquierdo (lo que hizo D-Rey, doc 31)
- La pieza de las manos tiene **islas** (trozos sin unir) en tramos **seguidos** de índices. En 2033:
  `+0` 1443 (derecha), `+1443` 6066 (izquierda entera), `+7509` 4623 (derecha). Se dibuja la pieza en 3 trozos
  (tres DrawIndexed) y el de la izquierda con su propia colocación. No separar por shader ni dibujarla entera dos veces.
- Para nosotros: en el gancho MinHook de DrawIndexed, si es la pieza de las manos, partirla; el trozo izquierdo se pinta
  con una **cámara del arma propia** (copia del cajón de cámara) con `G_izq = mando_izq_ahora · mando_izq_calibrado⁻¹`
  (calibrar igual: mano izquierda donde se ve la mano del juego, mantener un botón). Ojo con `dos_ojos=2` (`TW_DRAW`).
- Hay que averiguar los tramos en Last Light: leer el búfer de índices de esa pieza una vez y buscar islas (vértices
  conectados), apuntarlas en el log y probarlas ocultando una a una.
- Mejoras de D-Rey que vendrán después (no hacen falta para la 1.ª versión):
  - **Postura fija** de la mano izquierda: el juego la anima (agarrando el arma). D-Rey guarda una vez los huesos de esa
    mano (18 huesos, filas 0,3,…,51 del hueco 8) y los usa siempre, en una copia privada del cajón de huesos (nunca
    escribir el del juego).
  - Colocar por la **muñeca** (no por el centro de la malla): si no, la mano "barre" en arco al girar.
  - Base del mando **ortonormal** (ejes perpendiculares y de largo 1): si no, la mano se aplasta/deforma.
  - Ocultar las manos fuera de la partida (en menús e intro el juego las esconde moviéndolas, y al pegarlas al mando
    aparecerían).
  - Reloj (doc 31/32): pieza aparte con varios dibujos; muy largo. Para el final.
