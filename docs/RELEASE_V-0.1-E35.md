# Metro Last Light Redux VR — V-0.1 (E35)

Mod de realidad virtual **gratuito** para Metro Last Light Redux (Steam, DirectX 11). Es una versión temprana, pero ya se puede jugar de principio a fin con el visor y los mandos.

## Qué puedes hacer
- **Ver el juego en 3D de verdad** en el visor, con el campo de visión del visor y cada ojo desde su sitio.
- **Mirar con la cabeza** con los 6 grados de libertad: girar, inclinarte, ladear la cabeza y moverte por la habitación (agacharte, asomarte por una esquina…).
- **Andar hacia donde miras** con el stick izquierdo (analógico), y **girar** con el stick derecho sobre ti mismo.
- **El arma en tu mano derecha**: sigue al mando y apuntas con la mano, no con la cabeza. **Las balas salen del cañón** y van hacia donde apunta el arma.
- **Solo se ven tus manos** (sin los brazos ni las mangas flotando). La **mano izquierda va con el mando izquierdo** y lleva el **reloj** en la muñeca.
- **Agarrar el arma con las dos manos** (manteniendo el botón de agarre izquierdo), como en Pavlov: el cañón apunta de tu mano derecha hacia la izquierda.
- **Menús, pausa, intro y vídeos en una pantalla flotante** con fondo negro. El VR se pone y se quita solo al entrar y salir de la partida (también al cambiar de arma).
- **Dos ojos en cada fotograma**: el juego "piensa" una vez por pareja de imágenes. Con un PC modesto (i5-7500 + RX 580) llega a 72 imágenes por segundo a 1280x720.
- **Sin parpadeos de objetos**: el descarte de objetos del juego viene apagado por defecto (Shift+F10 lo vuelve a poner si te falta rendimiento).
- Sin HUD ni mirilla en la cara (en VR apuntas con el arma).

## Mandos (Quest u otros mandos de SteamVR)
| Mando | Botón | Qué hace |
|---|---|---|
| Izquierdo | Stick | Andar (clic = correr) |
| Izquierdo | Gatillo | Lanzar (granadas, cuchillos) |
| Izquierdo | Agarre (mantener) | Coger el arma con las dos manos |
| Izquierdo | X | Toque = pausa · mantener = máscara de gas |
| Izquierdo | Y | Toque = arma siguiente · mantener = linterna |
| Izquierdo | Clic del stick (mantener 1,5 s) | Calibrar la mano izquierda |
| Derecho | Gatillo | Disparar |
| Derecho | Agarre | Apuntar |
| Derecho | A | Usar / coger |
| Derecho | B | Recargar |
| Derecho | Stick | Girar (lados), saltar (arriba), agacharse (abajo) |
| Derecho | Clic del stick | Toque = cuchillo · mantener 1,5 s = calibrar la mano del arma |

En los menús los mandos funcionan como un mando de consola (X = Start).

**Calibrar la mano:** pon la mano donde ves la empuñadura del arma y mantén 1,5 s el clic del stick derecho (con la izquierda, el izquierdo). Se guarda sola para la próxima vez.

## Instalación
1. Necesitas el juego de Steam en la **versión anterior a la actualización "next-gen" (22/10/2026)**. Con la next-gen el mod no funciona.
2. Descarga `d3d11.dll` (abajo) y `openvr_api.dll` de 64 bits de Valve: https://github.com/ValveSoftware/openvr/blob/master/bin/win64/openvr_api.dll
3. Copia las dos junto a `metro.exe`, en `C:\Program Files (x86)\Steam\steamapps\common\Metro Last Light Redux\`.
4. En el `user.cfg` (`%LOCALAPPDATA%\4A Games\Metro Last Light\<número>\user.cfg`) pon: `r_base_fov 90`, `r_blur_level 0`, `r_fullscreen off`, `show_game_ui 0`, `g_show_crosshair off` y `g_quick_hints 0` (si la línea no está, añádela).
5. Recomendado: gráficos en **Medio**, sin SSAA y la imagen a **1280x720** (cámbiala con el juego cerrado, ver abajo).
6. Abre **SteamVR** con el visor y luego el juego. Al entrar en la partida el VR se pone solo.

Para quitar el mod, borra `d3d11.dll`. El mod no cambia ningún archivo del juego.

## Problemas conocidos
- **No cambies la resolución ni las opciones gráficas desde el menú del juego con el mod puesto**: puede cerrarse. Cámbialas en el `user.cfg` con el juego cerrado.
- En escenas con muchísimos objetos puede haber algún tirón al girar la cabeza (el procesador va al límite).
- Algunos objetos pueden verse raros en algún sitio concreto.
- La recarga todavía es la del juego (botón B). La recarga manual con las manos está en pruebas para la siguiente versión.

## Si algo falla
El mod apunta todo en `%TEMP%\metro_vr_log.txt`. Pásalo al abrir un aviso (Issue) en GitHub.

Gracias a **D-Rey86** (Metro 2033 Redux VR) por compartir lo que descubrió del motor 4A.
