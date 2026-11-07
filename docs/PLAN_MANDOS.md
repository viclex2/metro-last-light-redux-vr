# Plan: el arma en los mandos (paso 1)

Investigado el 7/11/2026. Todo sale del mod de Metro 2033 Redux de D-Rey86 (GPL-3), comprobado en **nuestro** `metro.exe`
(Last Light Redux, desempaquetado con Steamless): las funciones están en las mismas direcciones y hacen lo mismo.

## Lo que ya tenemos
- Los dibujos del arma y de los brazos se reconocen por su lente (más estrecha que la del mundo): la E10c ya los mueve
  (`piezas_corregidas` ≈ 1400/s). Mover el arma a la mano es lo mismo, pero con el giro y la posición del mando.

## Direcciones comprobadas en nuestro exe
| Qué | Dirección | Comprobación |
|---|---|---|
| Crear la bala (origen en RDX, dirección en R8) | `+0x3D7FF0` | empieza una función y llama a `+0x3D6970` (constructor de la bala), igual que en 2033 |
| Constructor de la bala | `+0x3D6970` | empieza una función |
| Productor común del disparo | `+0x3D9360` | llama a `+0x4F2770`, igual que en 2033 |
| Envoltorio del disparo (visual) | `+0x4F3E60` | `sub rsp,0x78; ... [rdx+0x13c] [rdx+0x138] ... call +0x4F3EE0`, idéntico |

## Etapas
1. **Mandos como teclado/ratón.** Leer los mandos (OpenVR) y mandar teclas: gatillo = disparar, agarre = apuntar,
   stick izq = andar, stick dcho = girar (por pasos o suave), botones = recargar, linterna, máscara, etc.
2. **El arma en la mano derecha.** A los dibujos con la lente del arma se les cambia la posición (m_WV y m_WVP, los dos,
   para que la luz cuadre) con: mano respecto a la cabeza × "posición de reposo" del arma. Calibrar el agarre por arma.
   D-Rey86 oculta los brazos (giran rígidos con el arma y queda raro) y deja las manos.
3. **Las balas salen hacia donde apunta el arma.** Gancho en `+0x3D7FF0`: solo para el jugador (D-Rey86 comprueba el
   dueño y que se esté disparando); se cambia la dirección por la del cañón, manteniendo la dispersión del juego.

## Cosas a tener en cuenta (lecciones de D-Rey86)
- El juego no traza por nuestro rayo en `+0x4F3E60` (es solo lo visual); por eso usan `+0x3D7FF0`.
- La bala de las NPC usa la misma función: hay que filtrar por dueño, si no, los enemigos disparan hacia donde apuntas.
- Al apuntar (ADS), el juego mueve el arma hacia la cara: hay que desactivarlo o compensarlo.
