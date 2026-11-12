# Plan: recarga manual (investigado el 12/11/2026, sin programar todavía)

## Lo que hay hecho por otros
- **D-Rey86 (Metro 2033 Redux VR, v0.1.0-test.4): NO tiene recarga manual.** X = usar/recargar con la animación del juego.
  Solo busca el contador de balas escaneando la memoria (para su marcador). Nada que copiar aquí.

## Cómo se ha mirado el metro.exe
- El `metro.exe` de Steam viene cifrado (SteamStub 3.0, sección `.bind`). Se descifra sin Steamless: cabecera de 0xD0 bytes
  justo antes del punto de entrada (XOR encadenado, firma 0xC0DEC0DE), con la clave AES-256 y el IV dentro; el IV se descifra
  en ECB y el código (16 bytes "robados" + `.text`) en CBC. Comprobado: los bytes de `+0x826B50` y `+0x3D7FF0` salen iguales
  que los del log del mod.
- Herramientas: `llvm-objdump` + la tabla `.pdata` (límites de función) + buscar referencias RIP a las cadenas del juego.

## Lo encontrado (RVA del metro.exe pre "next-gen")
- Clase `weapon_magazined`. Punteros: `arma+0x118` = dueño; `[arma+0x1b8]-0xa8` = objeto con los datos fijos del arma.
- Datos fijos: `+0x124a` = `auto_reload` (bool), `+0x124c` = `need_clip_out`.
- **`+0x2BAA90` = "¿toca recarga automática?"** (rcx = arma). Devuelve 1 si: balas cargadas == 0 (virtual `+0xEC8`), el dueño es
  el jugador (global `+0xD01EA8` → `+0x38` → `+0xA0`), `auto_reload` y hay reserva (virtual `+0x1338`). Es una pregunta sin
  efectos: **engancharla con MinHook y devolver 0 quita la recarga automática sin romper nada**, y de paso nos da el puntero
  del arma que lleva el jugador. Variantes de otras clases de arma que leen el mismo `auto_reload`: `+0x2B88C0`, y las
  funciones que contienen `+0x2F3FAE` y `+0x300B9F` (escopeta/tubo).
- Virtuales del arma: `+0xEC8` = balas cargadas (leer), `+0x1350` = poner balas cargadas (la usa la carga de partida, cadena
  `_ammo_loaded`), `+0x1338` = reserva.
- `+0x2B66A0` = empezar la recarga (pone la animación `magazined::reload`); `+0x2F5E90` = la de las armas de tubo.
- El jugador tiene un bool `auto_reload_disabled` en `jugador+0xC45` (lo usan los guiones); alternativa al gancho.
- Huesos del cargador en el modelo: `hud_magazine_bone`, `bone_clip`, eventos `clip_in` / `clip_out`.

## Plan por fases
1. **Fase 1 (poco riesgo):** gancho en `+0x2BAA90` (y variantes) → sin recarga automática. Gesto: mano izq a la cadera izq +
   agarre = "coger cargador" (vibración); llevarla al arma = se lanza la recarga DEL JUEGO (tecla R) con su animación; durante
   la animación la mano izq va con el arma. El agarre izq fuera de la cadera sigue siendo el agarre a dos manos.
2. **Fase 2 (E37):** B toque. Expulsar el cargador con un botón (balas a 0 con la virtual `+0x1350`; el arma no dispara hasta meter otro).
3. **Fase 3 (riesgo alto, arma por arma):** cargador visible en la mano izq (pieza del cargador pintada con la cámara de la mano
   izq, como el reloj) y meter las balas sin la animación del juego.
- Escopeta/revólver (cartucho a cartucho): después, con sus funciones propias.
