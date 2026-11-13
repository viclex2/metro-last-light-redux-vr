/*
 * HITO E10 - d3d11.dll (camara nativa completa: cabeza y ojos dentro de la camara del juego) (el cuerpo solo con la camara principal) (reflejos con el H del fotograma anterior) (objetos con su camara + camara del arma tambien) (base E7d otra vez, para RenderDoc con F9) (un H por fotograma; hueco 7 con el del anterior) (base E7d; preparada para RenderDoc) (luces con su camara) (cada dibujo con su camara) (rescate de dibujos que no cuadran) (F10: objetos de los huecos 8 y 13) (fantasma: H de los dibujos fijado en Present) (detective del fotograma anterior: Shift+D) (la cabeza mueve el raton: el juego gira de verdad con tu cabeza) (la linterna: con F9 se agranda el FOV del juego para que no desaparezcan cosas al girar la cabeza; la camara del juego ya no se toca) (cabeza falsa para RenderDoc) (compatible con RenderDoc) (F5-F8 encienden/apagan piezas en directo) (luces y sombras con la lente del visor) (detective: que otros cajones llevan la lente del juego) (buscador de la camara del juego en memoria: Shift+H) (estereo sincronizado: los dos ojos ven el mismo instante, con el reloj del juego parado en el 2o ojo) (lente del juego = lente del visor; cada ojo recibe su trozo) (imagen al visor: la misma para los dos ojos; visor via SteamVR + teclado) (vista con el teclado: flechas, o cualquier Shift / Bloq Mayus + letras, o numerico) "adaptador" (proxy) para Metro Last Light Redux (64 bits)
 *
 * Que hace, ademas de pasar todo a la d3d11.dll real de Windows:
 *   1) Vigila cuando el juego escribe datos en los "cajones" (constant buffers).
 *   2) Si el cajon tiene pinta de ser la ficha de la camara (cb_main_matrices1),
 *      la LEE y apunta posicion, hacia donde mira y angulo de vision.
 *   3) Cuenta fotogramas (Present) y resume cada segundo.
 *   4) "Caja negra": si el juego se cae por un error de memoria, apunta DONDE.
 *   5) Interruptores en %TEMP%\metro_vr_config.txt para apagar piezas sueltas.
 *   6) PRUEBA C1: gira la camara del juego unos grados (solo la que se elija) para
 *      comprobar que podemos CAMBIAR la ficha de la camara antes de que llegue a la gpu.
 *   Con prueba_camara=0 no cambia nada de lo que el juego manda: solo mira.
 *
 *   NOTA: hook_present viene apagado a proposito. La Steam Overlay tambien engancha
 *   Present y las dos se llamaban la una a la otra en bucle (desbordamiento de pila).
 *
 * Todo se apunta en %TEMP%\metro_vr_log.txt
 *
 * Distribucion de la ficha (lo que vimos en RenderDoc), en floats:
 *   [ 0..11] m_V   (3x4)   camara: filas = derecha, arriba, adelante (en el mundo)
 *   [12..23] m_iV  (3x4)   inversa: ultima columna = POSICION de la camara
 *   [24..39] m_P   (4x4)   lente: [24]=P00, [29]=P11
 *   (luego m_iP, m_VP, m_iVP hasta 352 bytes)
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <xmmintrin.h>
#include "MinHook.h"   /* (E18) MinHook (BSD-2, src/minhook): ganchos "por dentro" de las funciones */

#define RAD2DEG 57.29577951f
#define CAM_BYTES 352

/* ------------------------------------------------------------------ log */
static CRITICAL_SECTION g_cs;
static char g_logpath[MAX_PATH] = {0};
static char g_cfgpath[MAX_PATH] = {0};

static CRITICAL_SECTION g_logcs;
static volatile LONG g_log_init = 0;
static char g_logbuf[256 * 1024];
static int g_loglen = 0;

static void log_flush_locked(void)          /* con g_logcs cogido */
{
    if (!g_loglen) return;
    HANDLE h = CreateFileA(g_logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(h, g_logbuf, (DWORD)g_loglen, &w, NULL);
        CloseHandle(h);
    }
    g_loglen = 0;
}

static DWORD WINAPI log_thread(LPVOID u)
{
    (void)u;
    static char tmp[256 * 1024];
    for (;;) {
        Sleep(250);
        int n = 0;
        EnterCriticalSection(&g_logcs);       /* solo copiar: el candado dura microsegundos */
        if (g_loglen) { memcpy(tmp, g_logbuf, g_loglen); n = g_loglen; g_loglen = 0; }
        LeaveCriticalSection(&g_logcs);
        if (n) {
            HANDLE h = CreateFileA(g_logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, tmp, (DWORD)n, &w, NULL); CloseHandle(h); }
        }
    }
    return 0;
}

static void log_init_once(void)
{
    if (g_log_init == 2) return;
    if (InterlockedCompareExchange(&g_log_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_logcs);
        HANDLE th = CreateThread(NULL, 0, log_thread, NULL, 0, NULL);
        if (th) CloseHandle(th);
        InterlockedExchange(&g_log_init, 2);
    }
    while (g_log_init != 2) Sleep(0);
}

/* para la caja negra: escribir YA lo que haya en la bandeja */
static void log_flush_now(void)
{
    if (g_log_init != 2) return;
    EnterCriticalSection(&g_logcs);
    log_flush_locked();
    LeaveCriticalSection(&g_logcs);
}

static void vr_log(const char *fmt, ...)
{
    if (!g_logpath[0]) return;
    char line[1200];
    SYSTEMTIME t; GetLocalTime(&t);
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] ",
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    int room = (int)sizeof(line) - n - 3;
    va_list ap; va_start(ap, fmt);
    int m = vsnprintf(line + n, room, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    if (m > room) m = room;
    n += m;
    line[n++] = '\r'; line[n++] = '\n';

    /* Antes se abria y escribia el archivo AQUI, con el mismo candado que usa el juego en cada Map:
     * cada vez que escribiamos (varias por segundo) el juego se quedaba esperando -> micro-cortes.
     * Ahora solo se copia a una bandeja en memoria; un hilo aparte la vuelca al archivo. */
    log_init_once();
    EnterCriticalSection(&g_logcs);
    if (g_loglen + n > (int)sizeof(g_logbuf)) log_flush_locked();
    memcpy(g_logbuf + g_loglen, line, n);
    g_loglen += n;
    LeaveCriticalSection(&g_logcs);
}

static volatile LONG g_mode = 0;     /* modo de prueba actual (se cambia con F9 o Insert) */
#define NSLOT 16
static void * volatile g_vs_bound[NSLOT];           /* que cajon hay ahora en cada hueco del shader de vertices */
static volatile LONG g_slot_binds[NSLOT], g_slot_writes[NSLOT], g_slot_bw[NSLOT], g_samples_left[NSLOT];
static volatile LONG g_zeroed = 0, g_nomatch7 = 0, g_cb0_mod = 0, g_cb0_skip = 0;
/* vista controlada por teclado: H (espacio de vista) y su inversa, con doble buffer para leer sin tirones */
static float g_Hbuf[2][2][4][4];       /* [buffer][0=H,1=Hinv][4][4] */
static volatile LONG g_Hidx = 0;
static volatile float g_yaw = 0, g_pitch = 0, g_roll = 0, g_sx = 0, g_sy = 0, g_sz = 0;
/* Modos: 0 normal | 1 silencia los huecos de huecos_a_cero | 2 silencia todos menos el 1 | 3 giro de vista */
#define NMODES 2   /* 0 normal | 1 vista controlada con el teclado (hueco 1 + hueco 0), sin tocar el arma */
static volatile LONG g_slot_upd[NSLOT], g_slot_copy[NSLOT], g_uslot_samples[NSLOT];
static volatile LONG g_slot_desc_bw[NSLOT], g_slot_desc_usage[NSLOT], g_ndesclog = 0;
static unsigned g_mute_mask = (1u << 9);     /* huecos que se "silencian" en el modo 1 */

/* ------------------------------------------------------- interruptores */
static int cfg_map = 1;       /* vigilar Map/Unmap                         */
static int cfg_updsub = 1;    /* vigilar UpdateSubresource                 */
static int cfg_present = 0;   /* contar fotogramas (Present) - choca con Steam Overlay */
static int cfg_thread = 1;    /* hilo que resume cada segundo              */
static int cfg_read = 1;      /* leer de verdad el contenido del cajon     */
static int cfg_test = 0;      /* prueba: 0 nada, 1 camara ancha (mundo), 2 camara estrecha, 3 ambas */
static volatile LONG g_arma = 1;  /* 1 = la lente estrecha (arma, 57 grados) tambien gira con el mundo */
static float cfg_deg = 20.0f; /* grados de giro de la prueba               */
static int cfg_direct = 1;    /* 1 = escribir por la misma 'puerta' que el juego; 0 = WriteProcessMemory */
static int cfg_vscb = 1;      /* vigilar que cajon usa el shader de vertices en el hueco 1 */
static int cfg_head = 1;      /* la vista sigue al visor (OpenVR) */
static int cfg_headpos = 1;   /* tambien la posicion de la cabeza */
static float cfg_headscale = 1.0f;
static int cfg_fov = 1;
static int cfg_novsync = 1;
static int cfg_stereo = 2;
static int cfg_clock_thread = 1;
static int cfg_linterna = 0;            /* con F9: agrandar el FOV del juego (lo que decide que se dibuja) */
static float cfg_linterna_deg = 140.0f; /* FOV vertical del juego con la linterna */
static int cfg_nativa = 1;              /* (E9) la cabeza entra en la camara del juego por dentro (metro.exe+0x69B900) */
static int cfg_oclusion = 1;   /* (E35) por defecto QUITADA: se dibuja todo (si no, desaparecen cajas al mirar) */            /* (E9) con F9, el juego no descarta objetos por su "oclusion" de CPU (metro.exe+0x826B50) */
static volatile LONG g_nat_installed = 0, g_nat_hits = 0;
static int cfg_natarma = 1;             /* (E10b) a la camara del arma (que no sale de nuestra posicion) se le pone el ojo */
static void *g_bb_ptr;
static volatile LONG g_hud_n;
static volatile LONG g_mov_hits;
static void pieces_frame(void);
static void pieces_key(int shift, int ctrl);
static void mag_key(int shift, int ctrl);
static void pieces_show_key(void);
static void draw_hooks_check(void);
static ID3D11DeviceContext *g_hud_ctx;
static volatile LONG g_vt_fix;
/* (E18) MinHook */
#define MHVAR 6
#define MHK 12                                       /* (E27) 5 de antes + 7 para los dos ojos */
static void *g_mh_fn[MHK][MHVAR], *g_mh_tr[MHK][MHVAR];  /* funcion real enganchada / puente para llamar a la original */
static int g_mh_n[MHK];                                /* versiones enganchadas de Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced, (E20c) UpdateSubresource */
static volatile LONG g_mh_calls[4];                  /* llamadas por segundo (log) */
static int g_hud_draws = 0;             /* dibujos en la imagen final en este fotograma (el 1o es la escena) */
static int cfg_mandos = 1;
static float cfg_giro = 120.0f;       /* grados por segundo con el stick derecho a tope */
static float cfg_giro_paso = 0.0f;    /* >0: giro a saltos de estos grados (0 = suave) */
static float cfg_raton_grado = 11.0f; /* pasos de raton por grado de giro del juego (se ajusta con la calibracion E7 si la hay) */
/* (E15) el arma en la mano derecha */
static int cfg_mano = 1;
static volatile LONG g_hand_ok = 0;          /* hay postura del mando derecho */
static float g_hand_rel[4][4];               /* mando derecho respecto al centro de la cabeza, en ejes del juego */
static float g_hand_G[4][4];                 /* lo que se mueve el arma: mano_ahora * mano_calibrada^-1 */
static int g_hand_cal = 0;                   /* 1 = ya hay calibracion */
static float g_hand_C0[4][4];                /* postura de la mano "con el arma en reposo" */
static volatile LONG g_hand_req = 0, g_hand_n = 0, g_hand_cam = 0;
/* (E28) agarre POR ARMA (la solucion de D-Rey86: cada arma tiene su propio "punto de agarre"). Cada arma recuerda
 * su propia calibracion de la mano; si no tiene, se usa la general. El arma se reconoce por su pieza mas grande. */
typedef struct { UINT ic, si; float C0[4][4]; } WpnCal;
#define MAXW 32
static WpnCal g_wcal[MAXW]; static int g_nwcal = 0;
static volatile UINT g_wpn_ic = 0, g_wpn_si = 0;   /* arma que se ve ahora (0 = sin reconocer) */
static int g_show = -1;                            /* (E28) tecla Inicio: pieza guardada como brazo que se ensena (-1 = ninguna) */
/* (E29) ISLAS de la pieza grande de los brazos (manos + antebrazos van en una sola pieza). Una "isla" es un trozo de
 * la malla que no esta unido al resto (una mano, un antebrazo...). Como D-Rey86 en Metro 2033 (sus manos: 3 tramos
 * seguidos), se lee la pieza una vez, se buscan sus islas y la pieza se puede pintar por trozos, quitando alguno. */
#define MAXISR 256
typedef struct { UINT start, count; int isl; } IslRange;
static IslRange g_isr[MAXISR]; static int g_nisr = 0, g_nisl = 0;
static UINT g_isl_size[MAXISR]; static int g_isl_order[MAXISR];   /* tamano de cada isla; islas de mayor a menor */
static volatile LONG g_isl_state = 0;             /* 0 nada, 1 buffer apuntado, 2 islas listas, 3 fallo */
static ID3D11Buffer *g_isl_ib = NULL; static DXGI_FORMAT g_isl_fmt; static UINT g_isl_off = 0, g_isl_ic = 0, g_isl_si = 0;
static volatile int g_isl_on = 0;                 /* (tecla Fin) 1 = la pieza grande se pinta por trozos, sin la isla g_isl_hide */
static volatile int g_isl_hide = -1;
/* (E30) solo las MANOS: de la pieza grande se pintan las islas de las manos y no las de los antebrazos.
 * Numeros = orden de la tecla Fin (de mayor a menor): en la prueba de la E29, n.1 mano izq, n.2 mano der, n.3 y n.4 antebrazos */
static int cfg_manos = 1, cfg_mano_izq = 1, cfg_isla_der = 2, cfg_isla_izq = 1;
static int cfg_mano_izq_mando = 1;   /* (E31) mano izq en el mando izq */
static int cfg_reloj = 1;            /* (E33) 1 = con "solo las manos" se sigue viendo el reloj (las piezas pequenas guardadas) */
static volatile LONG g_watch_n = 0;  /* (E33) piezas del reloj pintadas (para el chivato) */
static int cfg_dos_manos = 1;        /* (E34) 0 = nunca, 1 = MANTENER el boton de agarre izq, 2 = siempre, 3 = automatico por cercania (el de la E32) */
static int cfg_huesos_hueco = 8;     /* en que hueco (cajon de vertices) van los huesos */
static int cfg_huesos_desfase = 0;   /* filas antes del hueso 0 (por si el cajon empieza con una cabecera) */
static int cfg_dos_manos_ejes = 1;
static int cfg_recarga_manual = 1;   /* (E36) 1 = sin recarga automatica; cargador desde la cadera izq (agarre) y llevarlo al arma */
static int cfg_recarga_ms = 2200;    /* (E36) tiempo que la mano izq va con el arma tras meter el cargador (animacion del juego) */
static float cfg_cadera_cm = 22.0f;  /* (E36) radio de la "cartuchera" en la cadera izq */
static volatile LONG g_mag_state = 0;   /* (E36) 0 nada, 1 cargador en la mano, 2 metiendolo (mano izq con el arma) */
static volatile LONG g_reload_req = 0;
static volatile LONG g_expel_req = 0, g_expel_n = 0;   /* (E37) B toque = expulsar cargador (se hace en el hilo del juego) */
static void *volatile g_pw = NULL;
static void expel_tick(void);   /* (E37) arma del jugador (la ve el gancho de la recarga automatica) */   /* (E36) el gesto pide pulsar R (lo hace mandos_step) */
static volatile LONG g_ar_blocked = 0, g_ar2_blocked = 0, g_mag_ins = 0;   /* (E34) 1 = con las dos manos el arma apunta de la mano der hacia la izq (como Pavlov). 0 = no gira */
static volatile LONG g_lgrip = 0;    /* (E34) boton de agarre izq pulsado (solo en partida) */
static volatile LONG g_tw_deg10 = 0; /* (E34) cuanto gira ahora el arma la mano izq, en decimas de grado (chivato) */
static float cfg_dos_manos_cm = 12.0f;   /* (E32) a cuantos cm se engancha (se suelta a 1,8 veces eso) */
static volatile LONG g_dos_manos = 0;    /* (E32) 1 = ahora mismo la mano izq va con el arma */
static unsigned char g_isl_show[MAXISR];          /* 1 = esta isla se pinta */
static void isl_default_mask(void)
{
    memset(g_isl_show, 0, sizeof(g_isl_show));
    if (!cfg_manos || g_isl_state != 2) { g_isl_on = 0; return; }
    if (cfg_isla_der >= 1 && cfg_isla_der <= g_nisl) g_isl_show[g_isl_order[cfg_isla_der - 1]] = 1;
    if (cfg_mano_izq && cfg_isla_izq >= 1 && cfg_isla_izq <= g_nisl) g_isl_show[g_isl_order[cfg_isla_izq - 1]] = 1;
    g_isl_on = 1;
}
static int wcal_find(UINT ic, UINT si) { for (int k = 0; k < g_nwcal; k++) if (g_wcal[k].ic == ic && g_wcal[k].si == si) return k; return -1; }
static int g_hand_why = 0;                   /* (E17) por que no hay mano: 1 apagado, 2 sin funcion, 3 indice, 4 visor no valido, 5 mando no valido */
static int cfg_detective = 0;           /* (E16) el "detective" de lentes leia entero cada cajon que escribe el juego: apagado */
static volatile LONG g_prof_present_us = 0, g_prof_mod_us = 0, g_prof_maxframe_us = 0;
static volatile LONG g_prof_cb0_us = 0, g_prof_cb0_n = 0, g_prof_frames = 0, g_prof_draw_us = 0;   /* (E23) */
/* (E17) piezas del arma/brazos: censo y ocultar */
static int cfg_pivote = 1;                   /* (E20) al girar con el stick, girar sobre la cabeza (no alrededor del centro de la habitacion) */
static int cfg_cine = 1;                         /* 1 = fuera de la partida, pantalla flotante */
static float cfg_cine_dist = 2.0f, cfg_cine_ancho = 2.6f;
static int cfg_auto = 1;                         /* 1 = el modo VR (F9) se pone y quita solo segun haya partida */
static int cfg_menu_draws = 20;                  /* (E20d) con tantos dibujos en la imagen final, hay un menu encima (E20f: 20) */
static int cfg_menu_stay = 10;                   /* (E20f) y se sigue en el menu mientras haya al menos estos */
static volatile ULONGLONG g_arm_seen = 0;        /* (E20f) ultima vez que se oculto un brazo guardado (= se pintan los brazos de Artyom) */
static volatile ULONGLONG g_mov_seen = 0;
static volatile ULONGLONG g_mov_run = 0;         /* (E20h) ultima vez que el jugador se actualizo con el tiempo en marcha (dt > 0) */
static volatile LONG g_mov_calls = 0, g_mov_dt0 = 0;        /* (E20e) ultima vez que el juego movio al jugador (solo existe en la partida) */
static volatile ULONGLONG g_cam_seen = 0;        /* ultima vez que el juego monto la camara del jugador */
static int g_auto_manual = 0;                    /* se ha pulsado F9: el automatico se aparta */
static volatile LONG g_bb_draws = 0; static LONG g_bb_last = 0, g_bb_max = 0;    /* (E20d) dibujos en la imagen final por fotograma */
static void sc_vt_sync(void);
static volatile ULONGLONG g_wpn_switch_t = 0;    /* (E21) ultima vez que el mando cambio de arma */
static int g_menu_pad = 0;
static int cfg_xinput = 1;                   /* (E21) los mandos VR tambien como mando de Xbox */
static void xinput_hook_try(void);
static int cfg_pose_cam = 1;                 /* (E22) mandar cada imagen con la postura con la que se monto su camara */
static int g_cine_on; static void cine_hide(void);
static int cfg_fastread = 1;
static int cfg_shadow = 1;                   /* (E24) borrador en memoria normal para los cajones de objeto */
/* (E27) DOS OJOS EN EL MISMO FOTOGRAMA (idea de D-Rey86, Metro 2033 Redux VR; codigo propio).
 *   0 = como antes (un ojo por fotograma, reloj parado en el 2.o)
 *   1 = PRUEBA: todo lo que depende del ojo se pinta en "copias" de las texturas en vez de en las del juego, y a las
 *       gafas se manda la copia. Mismo coste que 0 y la imagen deberia ser IGUAL: si se ve bien, las copias funcionan.
 *   2 = DE VERDAD: el juego piensa una vez y cada dibujo que depende del ojo se repite para el ojo derecho en las copias. */
static int cfg_dosojos = 2;   /* (E35) por defecto los dos ojos en cada fotograma (probado y bueno desde la E27) */
static volatile LONG g_twin_on = 0;          /* modo de los dos ojos activo AHORA (se cambia entre fotogramas) */
static __thread int t_eye = -1;              /* ojo forzado en este hilo (para calcular lo del ojo derecho) */
static __thread int t_quiet = 0;             /* calculo "de copia": sin apuntar nada ni tocar el estado global */
static volatile LONG g_shadow_n = 0;                 /* (E23) leer los cajones directamente (sin ReadProcessMemory) */
static int cfg_balas = 1;                    /* (E19) las balas del jugador salen del arma, hacia donde apunta */
static int cfg_brazos = 1;                   /* 1 = no pintar las piezas guardadas como "brazos" */
static __thread int g_cb0_weapon = 0;        /* el ultimo cajon 0 (en ESTE hilo) era de una pieza con la lente del arma */
static volatile LONG g_pc_lock = 0, g_draws_seen = 0;
typedef struct { UINT ic, si; } PieceSig;
#define MAXP 64
static PieceSig g_pc_cur[MAXP], g_pc_last[MAXP]; static int g_pc_ncur = 0, g_pc_nlast = 0;
static PieceSig g_hide[MAXP]; static int g_nhide = 0;
static int g_try = -1;                       /* F6: pieza que se esta probando a ocultar (-1 = ninguna) */
static PieceSig g_try_sig;
/* (E39) cargador de verdad: las piezas que son el cargador de cada arma (arma = su pieza grande g_wpn_ic/si) */
typedef struct { UINT wic, wsi, ic, si; } MagSig;
#define MAXMAG 48
static MagSig g_mag[MAXMAG]; static int g_nmag = 0;
static volatile UINT g_out_ic = 0, g_out_si = 0;   /* arma con el cargador FUERA (0 = ninguna) */
static volatile LONG g_mag_hid_n = 0, g_mag_hand_n = 0;
static int mag_is(UINT wic, UINT wsi, UINT ic, UINT si)
{
    for (int i = 0; i < g_nmag; i++) if (g_mag[i].wic == wic && g_mag[i].wsi == wsi && g_mag[i].ic == ic && g_mag[i].si == si) return 1;
    return 0;
}
static int mag_count(UINT wic, UINT wsi) { int n = 0; for (int i = 0; i < g_nmag; i++) if (g_mag[i].wic == wic && g_mag[i].wsi == wsi) n++; return n; }
static volatile LONG g_pc_hidden = 0, g_pc_drawn = 0;
static int cfg_hud = 0;                 /* (E11) HUD como un cartel a una distancia fija, igual para los dos ojos */
static float cfg_hud_size = 0.75f;      /* mitad del ancho del HUD, en "tangente" (0.75 = unos 74 grados de ancho) */
static float cfg_hud_dist = 1.5f;       /* metros a los que se ve el HUD */
static int cfg_natfull = 1;             /* (E10) TODA la cabeza (ladeo, posicion y cada ojo) dentro de la camara del juego; H = nada */
static int nat_full(void);
static void nat_eye_check(const float *f);
static int nat_pos_match(const float *f, LONG *eye);
static volatile LONG g_nat_armaojo = 0, g_nat_otras = 0;
static volatile LONG g_nat_flip = 1, g_nat_eok = 0, g_nat_ebad = 0, g_nat_enone = 0;
static void nat_install(void);
static void mh_start(void);
static void bul_install(void);
static void ocl_update(void);
static void mandos_step(float dt);
static int nat_active(void);
static int cfg_previo = 1;              /* (E8g) corregir reflejos/huecos 3 y 7 con el H de antes y el de ahora */
static int cfg_modoh = 0;               /* 0 = como la E7d (sin fantasma); 1 = como la E8c */
static int cfg_porobjeto = 1;           /* (E7g) cada dibujo con su propia camara */
static int cfg_rescate = 1;             /* (E7f) corregir tambien los dibujos con WV/WVP de fotogramas distintos */
static int cfg_raton = 1;               /* (E7) la cabeza mueve el raton: el juego gira con tu cabeza */
static int cfg_raton_pitch = 1;         /* tambien arriba/abajo */
static int cfg_raton_lag = 0;           /* cuantos envios tarda el juego en notar el raton */
static float cfg_raton_pmax = 70.0f;    /* hasta cuantos grados arriba/abajo se lleva al juego (el resto lo pone H) */
static int cfg_fakehead = 0;
static int cfg_fakeauto = 0;            /* (E8f) la cabeza falsa se mueve sola */
static int cfg_lights = 1;
static volatile LONG g_l7 = 0, g_l3 = 0;
static float cfg_ipdscale = 1.0f;      /* usar el campo de vision del visor */
static int cfg_vrimg = 1;    /* mandar la imagen al visor (OpenVR como aplicacion "escena") */

static const char CFG_DEFAULT[] =
    "# Interruptores del adaptador VR (1 = activado, 0 = desactivado)\r\n"
    "# Si el juego se cierra solo, pon alguno a 0 y vuelve a abrirlo.\r\n"
    "hook_map=1\r\n"
    "hook_updatesub=1\r\n"
    "# hook_present: pilla cada fotograma (D7c: con tabla propia, ya no choca con la Steam Overlay). 0 = apagado\r\n"
    "hook_present=1\r\n"
    "hilo_resumen=1\r\n"
    "leer_camara=1\r\n"
    "# Modo con el que arranca el juego. Dentro del juego, F9 o Insert cambian de modo (con pitidos):\r\n"
    "# modo 0 = no toca nada | modo 1 = vista controlada con el teclado. F9 o Insert (o Shift + P) = activar/desactivar\r\n"
    "# FLECHAS = girar izq/der y mirar arriba/abajo (no hace falta modificador)\r\n"
    "# Con cualquier SHIFT pulsado (o Bloq Mayus activado): J/L = girar | I/K = arriba/abajo | U/O = inclinar | N/M = mover | B = reset\r\n"
    "# Con numerico (Bloq Num): 4/6 giro | 8/2 arriba/abajo | 7/9 inclinar | 1/3 mover | 5 = reset\r\n"
    "# (marca de version: E7)\r\n"
    "# cabeza_falsa=1: SIN visor (p.ej. para capturar con RenderDoc): las flechas hacen de cabeza\r\n"
    "cabeza_falsa=0\r\n"
    "# En el juego (para probar sin reiniciar): F5 = estereo | F6 = luces_visor | F7 = fov_visor | F8 = linterna | Shift+F7 = buscar otra vez el FOV del juego\r\n"
    "# luces_visor=1: corrige tambien los cajones de luces y sombras (hueco 7 = lente, hueco 3 = rayos de reconstruccion)\r\n"
    "luces_visor=1\r\n"
    "# linterna=1: con F9, el juego 'mira' mas ancho (su FOV sube a linterna_grados) solo para decidir QUE dibuja.\r\n"
    "#   La imagen se sigue haciendo con la lente del visor. Asi, al girar la cabeza, lo que miras ya esta dibujado. 0 = no tocar el FOV\r\n"
    "linterna=0\r\n"
    "# linterna_grados: FOV vertical del juego con F9 (el normal es 50.6). Mas = mas margen para girar la cabeza, pero la grafica dibuja mas (60 a 160)\r\n"
    "linterna_grados=140\r\n"
    "# raton_cabeza=1: (E7) con F9, la cabeza MUEVE EL RATON: el juego gira de verdad hacia donde miras (puedes mirar\r\n"
    "#   detras de ti). Al pulsar F9 la primera vez se calibra solo: deja el raton quieto 2 s (dos pitidos subiendo = listo).\r\n"
    "#   Shift+R = calibrar otra vez. 0 = como la E6 (el juego mira donde el raton y la cabeza solo mueve la imagen)\r\n"
    "raton_cabeza=1\r\n"
    "# raton_inclinacion=1: tambien arriba/abajo (hasta raton_max_grados; lo que pase lo pone la DLL)\r\n"
    "raton_inclinacion=1\r\n"
    "raton_max_grados=70\r\n"
    "# raton_retraso: cuantos envios tarda el juego en notar el raton (0 o 1). Si al girar la cabeza el mundo 'patina', prueba el otro\r\n"
    "raton_retraso=0\r\n"
    "# reloj_hilos=1: solo se para el reloj al hilo que dibuja (el resto, sonido etc., sigue normal). 0 = a todos\r\n"
    "reloj_hilos=1\r\n"
    "# estereo: con F9. 2 = sincronizado (el juego dibuja izq y der del MISMO instante: se para su reloj en el 2o ojo)\r\n"
    "#          1 = alternos (un ojo por fotograma, el mundo sigue moviendose) | 0 = mono\r\n"
    "estereo=2\r\n"
    "# dos_ojos=2: (E27) los dos ojos en el mismo fotograma (el juego piensa una vez por pareja: va mas fluido). 0 = un ojo por fotograma\r\n"
    "dos_ojos=2\r\n"
    "# sin_oclusion=1: (E35) el juego dibuja todo lo que esta delante (si no, en VR desaparecen cajas y paredes). 0 = mas rapido pero parpadean\r\n"
    "#   Shift+F10 lo cambia en el juego y se guarda aqui solo\r\n"
    "sin_oclusion=1\r\n"
    "# recarga_manual=1: (E36) el arma ya no recarga sola. Mano izq a la cadera izq + agarre = coger cargador; llevarlo al arma = recargar.\r\n"
    "#   B (derecho) sigue recargando de golpe. 0 = como el juego (recarga automatica)\r\n"
    "recarga_manual=1\r\n"
    "# ipd_escala: separacion de los ojos (1.0 = la real del visor). Mas = el mundo parece mas pequeno\r\n"
    "ipd_escala=1.0\r\n"
    "# quitar_vsync=1: con la imagen en el visor, quita la sincronizacion vertical del juego (SteamVR ya marca el ritmo)\r\n"
    "quitar_vsync=1\r\n"
    "# fov_visor=1: con F9 activo, el juego dibuja con el campo de vision del visor (asi la imagen encaja con tus ojos)\r\n"
    "fov_visor=1\r\n"
    "# visor_imagen=1: manda la imagen del juego al visor (la misma a los dos ojos). 0 = solo leer el visor\r\n"
    "visor_imagen=1\r\n"
    "# cabeza=1: la vista sigue al visor (hace falta SteamVR abierto y openvr_api.dll junto a metro.exe). 0 = solo teclado\r\n"
    "cabeza=1\r\n"
    "# cabeza_posicion=1: tambien mueve la vista al desplazar la cabeza. cabeza_escala = metros del juego por metro real\r\n"
    "cabeza_posicion=1\r\n"
    "cabeza_escala=1.0\r\n"
    "# arma_con_mundo=1: el arma y las manos giran con el mundo (como en VR). 0 = pegadas a la pantalla. En el juego: Shift+Y\r\n"
    "arma_con_mundo=1\r\n"
    "prueba_camara=0\r\n"
    "prueba_grados=20\r\n"
    "# Lista de huecos (0 a 15) separados por comas para el modo 1. Puedes cambiarla sin pedirme otra DLL.\r\n"
    "huecos_a_cero=9\r\n"
    "# escritura_directa=1: se escribe en la memoria por la misma via que usa el juego (lo normal).\r\n"
    "escritura_directa=1\r\n"
    "# hook_vscb: vigila los cajones del shader de vertices (hace falta para los modos 1 y 2).\r\n"
    "hook_vscb=1\r\n";

static void cfg_apply_line(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == 0) return;
    char *eq = strchr(s, '=');
    if (!eq) return;
    *eq = 0;
    int v = atoi(eq + 1) ? 1 : 0;
    int iv = atoi(eq + 1);
    float fv = (float)atof(eq + 1);
    char *e = eq - 1;
    while (e >= s && (*e == ' ' || *e == '\t')) { *e = 0; e--; }
    if      (!strcmp(s, "hook_map"))       cfg_map = v;
    else if (!strcmp(s, "hook_updatesub")) cfg_updsub = v;
    else if (!strcmp(s, "hook_present"))   cfg_present = v;
    else if (!strcmp(s, "hilo_resumen"))   cfg_thread = v;
    else if (!strcmp(s, "leer_camara"))    cfg_read = v;
    else if (!strcmp(s, "prueba_camara"))  { cfg_test = (iv < 0 || iv >= NMODES) ? 0 : iv; g_mode = cfg_test; }
    else if (!strcmp(s, "prueba_grados"))  cfg_deg = fv;
    else if (!strcmp(s, "huecos_a_cero")) {
        unsigned m = 0; const char *q = eq + 1;
        while (*q) {
            if (*q >= '0' && *q <= '9') {
                int n = atoi(q);
                if (n >= 0 && n < 32) m |= (1u << n);
                while (*q >= '0' && *q <= '9') q++;
            } else q++;
        }
        g_mute_mask = m;
    }
    else if (!strcmp(s, "escritura_directa")) cfg_direct = v;
    else if (!strcmp(s, "hook_vscb"))      cfg_vscb = v;
    else if (!strcmp(s, "arma_con_mundo")) g_arma = v;
    else if (!strcmp(s, "cabeza"))          cfg_head = v;
    else if (!strcmp(s, "cabeza_posicion")) cfg_headpos = v;
    else if (!strcmp(s, "cabeza_escala"))   cfg_headscale = fv;
    else if (!strcmp(s, "visor_imagen"))    cfg_vrimg = v;
    else if (!strcmp(s, "fov_visor"))       cfg_fov = v;
    else if (!strcmp(s, "quitar_vsync"))    cfg_novsync = v;
    else if (!strcmp(s, "estereo"))         cfg_stereo = (iv < 0 || iv > 2) ? 2 : iv;
    else if (!strcmp(s, "ipd_escala"))      cfg_ipdscale = fv;
    else if (!strcmp(s, "reloj_hilos"))     cfg_clock_thread = v;
    else if (!strcmp(s, "linterna"))        cfg_linterna = v;
    else if (!strcmp(s, "raton_cabeza"))     cfg_raton = v;
    else if (!strcmp(s, "rescate"))          cfg_rescate = v;
    else if (!strcmp(s, "por_objeto"))       cfg_porobjeto = v;
    else if (!strcmp(s, "modo_h"))           cfg_modoh = v;
    else if (!strcmp(s, "previo"))           cfg_previo = v;
    else if (!strcmp(s, "camara_nativa"))    cfg_nativa = v;
    else if (!strcmp(s, "camara_nativa_completa")) cfg_natfull = v;
    else if (!strcmp(s, "ojo_arma"))         cfg_natarma = v;
    else if (!strcmp(s, "hud_vr"))           cfg_hud = v;
    else if (!strcmp(s, "ocultar_brazos"))   cfg_brazos = v;
    else if (!strcmp(s, "manos"))            cfg_manos = v;      /* (E30) */
    else if (!strcmp(s, "mano_izq"))         cfg_mano_izq = v;
    else if (!strcmp(s, "isla_mano_der"))    cfg_isla_der = v;
    else if (!strcmp(s, "isla_mano_izq"))    cfg_isla_izq = v;
    else if (!strcmp(s, "mano_izq_mando"))   cfg_mano_izq_mando = v;   /* (E31) */
    else if (!strcmp(s, "reloj"))            cfg_reloj = v;            /* (E33) */
    else if (!strcmp(s, "dos_manos"))        cfg_dos_manos = v;        /* (E32) */
    else if (!strcmp(s, "dos_manos_cm"))     cfg_dos_manos_cm = fv;    /* (E32) */
    else if (!strcmp(s, "dos_manos_ejes"))   cfg_dos_manos_ejes = v;   /* (E34) */
    else if (!strcmp(s, "huesos_hueco"))     cfg_huesos_hueco = v;     /* (E43) */
    else if (!strcmp(s, "huesos_desfase"))   cfg_huesos_desfase = v;   /* (E43) */
    else if (!strcmp(s, "recarga_manual"))   cfg_recarga_manual = v;   /* (E36) */
    else if (!strcmp(s, "recarga_ms"))       cfg_recarga_ms = v;       /* (E36) */
    else if (!strcmp(s, "cadera_cm"))        cfg_cadera_cm = fv;       /* (E36) */
    else if (!strcmp(s, "balas_mano"))       cfg_balas = v;
    else if (!strcmp(s, "girar_sobre_ti"))   cfg_pivote = v;
    else if (!strcmp(s, "mando_xbox"))       cfg_xinput = v;
    else if (!strcmp(s, "postura_camara"))   cfg_pose_cam = v;
    else if (!strcmp(s, "lectura_rapida"))   cfg_fastread = v;
    else if (!strcmp(s, "borrador"))         cfg_shadow = v;
    else if (!strcmp(s, "dos_ojos"))         cfg_dosojos = (iv < 0 || iv > 2) ? 0 : iv;
    else if (!strcmp(s, "pantalla_menus"))   cfg_cine = v;
    else if (!strcmp(s, "pantalla_distancia")) cfg_cine_dist = fv;
    else if (!strcmp(s, "pantalla_ancho"))   cfg_cine_ancho = fv;
    else if (!strcmp(s, "vr_automatico"))    cfg_auto = v;
    else if (!strcmp(s, "menu_dibujos"))     cfg_menu_draws = iv;
    else if (!strcmp(s, "menu_dibujos_seguir")) cfg_menu_stay = iv;
    else if (!strcmp(s, "detective"))        cfg_detective = v;
    else if (!strcmp(s, "mandos"))           cfg_mandos = v;
    else if (!strcmp(s, "arma_en_mano"))     cfg_mano = v;
    else if (!strcmp(s, "giro_velocidad"))   { if (fv > 1.0f && fv < 1000.0f) cfg_giro = fv; }
    else if (!strcmp(s, "giro_saltos"))      { if (fv >= 0.0f && fv < 180.0f) cfg_giro_paso = fv; }
    else if (!strcmp(s, "raton_por_grado"))  { if (fv > 0.1f && fv < 1000.0f) cfg_raton_grado = fv; }
    else if (!strcmp(s, "hud_tamano"))       { if (fv > 0.1f && fv < 3.0f) cfg_hud_size = fv; }
    else if (!strcmp(s, "hud_distancia"))    { if (fv > 0.2f && fv < 50.0f) cfg_hud_dist = fv; }
    else if (!strcmp(s, "sin_oclusion"))     cfg_oclusion = v;
    else if (!strcmp(s, "raton_inclinacion")) cfg_raton_pitch = v;
    else if (!strcmp(s, "raton_retraso"))    cfg_raton_lag = (iv < 0) ? 0 : (iv > 4 ? 4 : iv);
    else if (!strcmp(s, "raton_max_grados")) cfg_raton_pmax = (fv < 0.0f) ? 0.0f : (fv > 85.0f ? 85.0f : fv);
    else if (!strcmp(s, "linterna_grados")) cfg_linterna_deg = (fv < 60.0f) ? 60.0f : (fv > 160.0f ? 160.0f : fv);
    else if (!strcmp(s, "cabeza_falsa"))    cfg_fakehead = v;
    else if (!strcmp(s, "cabeza_falsa_auto")) cfg_fakeauto = v;
    else if (!strcmp(s, "luces_visor"))     cfg_lights = v;
}

static void parse_cfg_text(char *buf)
{
    char *p = buf;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n' && *nl != '\r') nl++;
        char save = *nl; *nl = 0;
        cfg_apply_line(p);
        if (!save) break;
        p = nl + 1;
    }
}

/* (E35) cambia (o anade) una linea "clave=valor" en metro_vr_config.txt, sin tocar el resto */
static void cfg_store(const char *key, int val)
{
    if (!g_cfgpath[0]) return;
    FILE *fp = fopen(g_cfgpath, "rb"); if (!fp) return;
    static char in[16384], out[16600]; size_t n = fread(in, 1, sizeof(in) - 1, fp); fclose(fp); in[n] = 0;
    size_t kl = strlen(key), o = 0; char *q = in; int done = 0;
    while (*q) {
        char *e = strchr(q, '\n'); size_t ll = e ? (size_t)(e - q + 1) : strlen(q);
        if (!strncmp(q, key, kl) && q[kl] == '=') { if (!done) { o += snprintf(out + o, sizeof(out) - o, "%s=%d\r\n", key, val); done = 1; } }
        else if (o + ll < sizeof(out) - 64) { memcpy(out + o, q, ll); o += ll; }
        q += ll;
    }
    if (!done) { if (o && out[o - 1] != '\n') o += snprintf(out + o, sizeof(out) - o, "\r\n"); o += snprintf(out + o, sizeof(out) - o, "%s=%d\r\n", key, val); }
    fp = fopen(g_cfgpath, "wb"); if (!fp) return; fwrite(out, 1, o, fp); fclose(fp);
}
/* (E35) ARCHIVOS DE SERIE: si falta alguno, se crea con lo calibrado por el autor (Alex, 12/11/2026, rama Steam pre "next-gen").
 * Si ya existe NO se toca (la calibracion de cada uno manda). Asi, quien solo copia las DLL ve lo mismo: solo las manos, el
 * reloj, la mano izq en el mando, el agarre a dos manos y el arma mas o menos en la mano (luego cada uno recalibra). */
static const char SEED_BRAZOS[] =
    "16734 177912\n2538 849921\n288 855195\n1782 852885\n252 854925\n174 856020\n54 855498\n"
    "6 855606\n24 855690\n24 855630\n6 855618\n6 855678\n6 855738\n252 855750\n";
static const char SEED_MANO[] =
    "0.997146 -0.011328 0.074647 0.104040\n"
    "-0.058139 0.515567 0.854875 -0.039188\n"
    "-0.048169 -0.856774 0.513436 0.442438\n"
    "arma 6840 591642 0.995716 0.019216 -0.090447 0.114524 0.083253 0.239331 0.967362 0.005541 0.040236 -0.970748 0.236706 0.702303\n";
static const char SEED_MANO_IZQ[] =
    "0.559864 -0.827566 -0.041077 0.091846\n"
    "0.774478 0.505040 0.380945 -0.167313\n"
    "-0.294512 -0.245091 0.923685 0.719847\n";
static void seed_files(const char *tmp)
{
    static const struct { const char *name; const char *txt; } f[] = {
        { "metro_vr_brazos.txt", SEED_BRAZOS }, { "metro_vr_mano.txt", SEED_MANO }, { "metro_vr_mano_izq.txt", SEED_MANO_IZQ },
        { "metro_vr_huesos.txt", "16251 862638 1 15\n" } };   /* (E43) metralleta: cargador = hueso 15 */
    for (int i = 0; i < 4; i++) {
        char path[MAX_PATH]; snprintf(path, sizeof(path), "%s%s", tmp, f[i].name);
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);   /* solo si no existe */
        if (h == INVALID_HANDLE_VALUE) continue;
        DWORD w; WriteFile(h, f[i].txt, (DWORD)strlen(f[i].txt), &w, NULL); CloseHandle(h);
        vr_log("ARCHIVOS: %s no existia -> creado con los datos de serie", f[i].name);
    }
}
static void load_config(const char *tmp)
{
    snprintf(g_cfgpath, sizeof(g_cfgpath), "%smetro_vr_config.txt", tmp);
    HANDLE h = CreateFileA(g_cfgpath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);          /* solo si no existe */
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(h, CFG_DEFAULT, (DWORD)(sizeof(CFG_DEFAULT) - 1), &w, NULL);
        CloseHandle(h);
    }
    h = CreateFileA(g_cfgpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    static char buf[8192]; DWORD got = 0;
    if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL)) {
        buf[got] = 0;
        if (!strstr(buf, "marca de version: E7)")) {
            /* archivo de una version anterior: lo renovamos con los valores nuevos */
            CloseHandle(h);
            h = CreateFileA(g_cfgpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD w; WriteFile(h, CFG_DEFAULT, (DWORD)(sizeof(CFG_DEFAULT) - 1), &w, NULL);
                CloseHandle(h);
            }
            strcpy(buf, CFG_DEFAULT);
            parse_cfg_text(buf);
            return;
        }
        parse_cfg_text(buf);
    }
    CloseHandle(h);
}

/* ----------------------------------------------------------- contadores */
static volatile LONG g_maps = 0, g_cbmaps = 0, g_cams = 0;
static volatile LONG g_frames = 0, g_updsub = 0, g_budget = 0, g_readfail = 0;
static volatile LONG g_modded = 0, g_nomatch = 0, g_vs1_cam = 0, g_vs1_other = 0;
static void * volatile g_cam_buf = NULL;     /* el cajon donde hemos visto la camara */
static volatile LONG g_present_hooked = 0, g_present_tried = 0, g_thread_started = 0, g_keys_started = 0;

/* ------------------------------------------------- d3d11 real de Windows */
static HMODULE g_real = NULL;

static HMODULE real_d3d11(void)
{
    if (!g_real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\d3d11.dll");
        g_real = LoadLibraryA(path);
        vr_log("d3d11 REAL: %s -> %s", path, g_real ? "cargada OK" : "FALLO");
    }
    return g_real;
}

/* ------------------------------------------------- caja negra (errores) */
static const char *module_of(const void *addr, unsigned long long *off, char *name)
{
    /* "name" lo pone quien llama (sin memoria especial por hilo: mas seguro dentro de un error) */
    HMODULE hm = NULL;
    *off = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &hm) && hm) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(hm, full, MAX_PATH)) {
            const char *b = strrchr(full, '\\');
            HMODULE me = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)module_of, &me);
            snprintf(name, MAX_PATH, "%s%s", b ? b + 1 : full, hm == me ? "(NUESTRA)" : "");
            *off = (unsigned long long)((const char *)addr - (const char *)hm);
            return name;
        }
    }
    snprintf(name, MAX_PATH, "(desconocido)");
    return name;
}

static int is_exec_addr(const void *a)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(a, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static LONG CALLBACK crash_handler(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000005 && code != 0xC000001D && code != 0xC0000094 &&
        code != 0xC00000FD && code != 0xC0000374 && code != 0xC0000096 &&
        code != 0xC000008C && code != 0xC0000409)
        return EXCEPTION_CONTINUE_SEARCH;

    static volatile LONG count = 0;
    if (InterlockedIncrement(&count) > 5) return EXCEPTION_CONTINUE_SEARCH;

    unsigned long long off;
    char modbuf[MAX_PATH], modbuf2[MAX_PATH];
    const void *addr = ep->ExceptionRecord->ExceptionAddress;
    const char *mod = module_of(addr, &off, modbuf);
    vr_log("!!! ERROR 0x%08lX en %s+0x%llX (direccion %p) hilo=%lu",
           (unsigned long)code, mod, off, addr, (unsigned long)GetCurrentThreadId());
    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR k = ep->ExceptionRecord->ExceptionInformation[0];
        vr_log("    tipo=%s  direccion_accedida=%p",
               k == 0 ? "lectura" : (k == 1 ? "escritura" : "ejecucion"),
               (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    vr_log("    contadores: Map=%ld Map_cb=%ld UpdSub=%ld camaras=%ld fotogramas=%ld",
           (long)g_maps, (long)g_cbmaps, (long)g_updsub, (long)g_cams, (long)g_frames);

    /* Buscamos en la pila direcciones que apunten a codigo: es el "rastro" de quien llamo a quien. */
    const ULONG_PTR *sp = (const ULONG_PTR *)ep->ContextRecord->Rsp;
    int shown = 0;
    for (int i = 0; i < 160 && shown < 14; i++) {
        ULONG_PTR v = 0; SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, sizeof(v), &got) || got != sizeof(v)) break;
        if (v > 0x10000 && is_exec_addr((const void *)v)) {
            unsigned long long o2;
            const char *m2 = module_of((const void *)v, &o2, modbuf2);
            vr_log("    pila[%d]: %s+0x%llX", i, m2, o2);
            shown++;
        }
    }
    log_flush_now();
    return EXCEPTION_CONTINUE_SEARCH;   /* no tapamos nada: el juego sigue su camino */
}

/* ------------------------------------------------------ analisis camara */
static int is_camera(const float *f)
{
    for (int i = 0; i < 30; i++)
        if (!isfinite(f[i])) return 0;
    /* las 3 filas de m_V tienen longitud ~1 (son ejes de giro) */
    for (int r = 0; r < 3; r++) {
        float l = f[r*4]*f[r*4] + f[r*4+1]*f[r*4+1] + f[r*4+2]*f[r*4+2];
        if (fabsf(l - 1.0f) > 0.02f) return 0;
    }
    /* y m_iV es la inversa: su parte de giro es la traspuesta de m_V */
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (fabsf(f[12 + r*4 + c] - f[c*4 + r]) > 0.001f) return 0;
    return 1;
}

/* Una camara "de verdad" del juego: ademas de girar bien, tiene una lente (m_P) normal.
 * Asi descartamos otros datos que el juego mete en el mismo cajon (efectos, etc.). */
static int is_real_cam(const float *f)
{
    if (!is_camera(f)) return 0;
    for (int i = 30; i < 56; i++)
        if (!isfinite(f[i])) return 0;
    if (f[24] < 0.05f || f[29] < 0.05f || f[24] > 40.0f || f[29] > 40.0f) return 0;   /* P00, P11 (0.05: con la linterna la lente es muy ancha) */
    if (fabsf(f[36] - 0.0f) > 1e-4f || fabsf(f[37] - 0.0f) > 1e-4f) return 0;       /* P30, P31 */
    if (fabsf(f[38] - 1.0f) > 1e-4f || fabsf(f[39]) > 1e-4f) return 0;              /* P32=1, P33=0 */
    return 1;
}

/* Rehace las 6 matrices del cajon con la camara girada "deg" grados alrededor del eje vertical (Y). */
static void mul4(const float A[4][4], const float B[4][4], float C[4][4])
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float a = 0;
            for (int k = 0; k < 4; k++) a += A[i][k] * B[k][j];
            C[i][j] = a;
        }
}

__attribute__((unused)) static int apply_yaw(float *g, float deg)
{
    const float PI_F = 3.14159265f;
    float a = deg * PI_F / 180.0f, c = cosf(a), sn = sinf(a);
    float pos[3] = { g[15], g[19], g[23] };

    float P[4][4], iP[4][4], V4[4][4], iV4[4][4], VP[4][4], iVP[4][4], chk[4][4], R[3][3];
    memcpy(P,  g + 24, sizeof(P));
    memcpy(iP, g + 40, sizeof(iP));

    /* comprobacion de seguridad: la ficha original tiene que cumplir VP = P * V */
    memset(V4, 0, sizeof(V4)); V4[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) V4[i][j] = g[i*4 + j];
    mul4(P, V4, chk);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float want = g[56 + i*4 + j];
            if (fabsf(chk[i][j] - want) > 1e-3f * (1.0f + fabsf(want))) return 0;
        }

    /* giramos los 3 ejes de la camara (derecha, arriba, adelante) alrededor de Y */
    for (int i = 0; i < 3; i++) {
        float x = g[i*4], y = g[i*4 + 1], z = g[i*4 + 2];
        R[i][0] = x * c + z * sn;
        R[i][1] = y;
        R[i][2] = -x * sn + z * c;
    }
    memset(V4, 0, sizeof(V4)); memset(iV4, 0, sizeof(iV4));
    V4[3][3] = 1.0f; iV4[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) { V4[i][j] = R[i][j]; iV4[i][j] = R[j][i]; }
        V4[i][3]  = -(R[i][0]*pos[0] + R[i][1]*pos[1] + R[i][2]*pos[2]);
        iV4[i][3] = pos[i];
    }
    mul4(P, V4, VP);       /* VP  = P * V        */
    mul4(iV4, iP, iVP);    /* iVP = iV * iP      */

    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) { g[i*4 + j] = V4[i][j]; g[12 + i*4 + j] = iV4[i][j]; }
    memcpy(g + 56, VP,  sizeof(VP));
    memcpy(g + 72, iVP, sizeof(iVP));
    return 1;
}

/* Matriz de giro (alrededor del eje vertical) en el espacio de vista y su traspuesta (= inversa). */
static void make_H(float deg, float H[4][4], float Ht[4][4])
{
    const float PI_F = 3.14159265f;
    float a = deg * PI_F / 180.0f, c = cosf(a), sn = sinf(a);
    memset(H, 0, 16 * sizeof(float));
    H[0][0] = c;  H[0][2] = sn;
    H[1][1] = 1.0f;
    H[2][0] = -sn; H[2][2] = c;
    H[3][3] = 1.0f;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) Ht[i][j] = H[j][i];
}

/* H = giro(roll, pitch, yaw) * traslacion(-s) en el espacio de vista. Convencion: yaw>0 = mirar a la derecha,
 * pitch>0 = mirar arriba, roll>0 = inclinar a la derecha, sx>0 = mover la camara a la derecha. */
static void build_H(float yaw, float pitch, float roll, float sx, float sy, float sz, float H[4][4], float Hi[4][4])
{
    const float K = 3.14159265f / 180.0f;
    float cy = cosf(yaw*K), sy_ = sinf(yaw*K), cp = cosf(pitch*K), sp = sinf(pitch*K), cr = cosf(roll*K), sr = sinf(roll*K);
    float Ry[4][4] = {{cy,0,-sy_,0},{0,1,0,0},{sy_,0,cy,0},{0,0,0,1}};
    float Rx[4][4] = {{1,0,0,0},{0,cp,-sp,0},{0,sp,cp,0},{0,0,0,1}};
    float Rz[4][4] = {{cr,-sr,0,0},{sr,cr,0,0},{0,0,1,0},{0,0,0,1}};
    float A[4][4], R[4][4], T[4][4] = {{1,0,0,-sx},{0,1,0,-sy},{0,0,1,-sz},{0,0,0,1}};
    float Ti[4][4] = {{1,0,0,sx},{0,1,0,sy},{0,0,1,sz},{0,0,0,1}}, Rt[4][4];
    mul4(Rx, Ry, A);  mul4(Rz, A, R);          /* R = Rz*Rx*Ry */
    mul4(R, T, H);                              /* H = R*T      */
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) Rt[i][j] = R[j][i];
    mul4(Ti, Rt, Hi);                           /* H^-1 = T^-1 * R^T */
}

/* Giro de la cabeza (visor) ya pasado al espacio de vista del juego. Identidad si no hay visor. */
static float g_headH[4][4]  = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
static float g_headHi[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

/* H final = cabeza * teclado  (el teclado es como "girar el cuerpo", la cabeza va encima) */
static CRITICAL_SECTION g_hcs;
static volatile LONG g_hcs_init = 0;
static void publish_H(void)
{
    if (InterlockedCompareExchange(&g_hcs_init, 1, 0) == 0) { InitializeCriticalSection(&g_hcs); InterlockedExchange(&g_hcs_init, 2); }
    while (g_hcs_init != 2) Sleep(0);
    EnterCriticalSection(&g_hcs);
    LONG other = 1 - g_Hidx;
    float Hk[4][4], Hki[4][4];
    if (cfg_fakehead) build_H(0, 0, 0, 0, 0, 0, Hk, Hki);     /* con cabeza falsa, las teclas son la cabeza */
    else build_H(g_yaw, g_pitch, g_roll, g_sx, g_sy, g_sz, Hk, Hki);
    mul4(g_headH, Hk, g_Hbuf[other][0]);
    mul4(Hki, g_headHi, g_Hbuf[other][1]);
    InterlockedExchange(&g_Hidx, other);
    LeaveCriticalSection(&g_hcs);
}

/* ---- estereo por fotogramas alternos ---- */
static volatile LONG g_eye = 0;            /* ojo que se esta dibujando en este fotograma: 0 izq, 1 der */
static volatile LONG g_stereo_on = 0;      /* estereo activo ahora mismo (F9 + estereo=1 + visor listo) */
static float g_eyepos[2][3];               /* posicion de cada ojo respecto a la cabeza, en ejes del juego (metros) */
static float g_eyerow[2][2][4];            /* filas x/y de la lente de cada ojo */
static inline LONG cur_eye(void) { return t_eye >= 0 ? t_eye : g_eye; }   /* (E27) */
/* (E27) se le pone el ojo a H? Antes solo sin camara nativa. Con los dos ojos de verdad (modo 2) el juego dibuja
 * desde el CENTRO de la cabeza y cada ojo se aparta en H (igual que hacia el arma). */
static int nat_full(void);
static inline int eye_in_H(void) { return g_stereo_on && (!nat_full() || g_twin_on == 2); }

static void get_H(float H[4][4], float Hi[4][4])
{
    LONG i = g_Hidx;
    memcpy(H,  g_Hbuf[i][0], 16 * sizeof(float));
    memcpy(Hi, g_Hbuf[i][1], 16 * sizeof(float));
    if (eye_in_H()) {
        /* el ojo esta desplazado t respecto al centro de la cabeza: vista_ojo = T(-t) * vista */
        LONG e = cur_eye();
        float t[3] = { g_eyepos[e][0] * cfg_ipdscale, g_eyepos[e][1] * cfg_ipdscale, g_eyepos[e][2] * cfg_ipdscale };
        for (int r = 0; r < 3; r++) {
            H[r][3] -= t[r];
            Hi[r][3] += Hi[r][0] * t[0] + Hi[r][1] * t[1] + Hi[r][2] * t[2];
        }
    }
}

/* (E7) la cabeza mueve el raton */
#define RT_HIST 8
static volatile LONG g_rt_cal = 0;          /* 0 sin calibrar, 1 calibrando, 2 listo, -1 fallo */
static ULONGLONG g_rt_cal_retry = 0;
static float g_rt_kx = 0, g_rt_ky = 0;      /* radianes que gira el juego por cada paso de raton (x = giro, y = inclinacion) */
static LONG g_rt_cx = 0, g_rt_cy = 0;       /* pasos mandados por la cabeza (en total, desde el ultimo reinicio) */
static LONG g_rt_hx[RT_HIST], g_rt_hy[RT_HIST];
static volatile LONG g_rt_hi = 0;
static volatile LONG g_rt_reset = 1;
static float g_rt_Rg[3][3] = {{1,0,0},{0,1,0},{0,0,1}}, g_rt_dg[3] = {0,0,0};   /* cabeza respecto al cuerpo (ultima) */
static float g_rt_lastcam[12];              /* filas de giro de la ultima camara principal (original) */
static volatile LONG g_rt_havecam = 0;
static float g_rt_unwrap = 0, g_rt_last = 0;
static volatile LONG g_rt_resid = 0;        /* mayor desajuste del ultimo segundo, en decimas de grado */
static volatile LONG g_rt_sent = 0;         /* pasos mandados en el ultimo segundo */
static int raton_active(void);
static void raton_make_H(const float *f, float Ho[4][4], float Hio[4][4]);
static volatile LONG g_cb0_resc = 0;
static volatile float g_orig_P00 = 0, g_orig_P11 = 0, g_orig_P22 = 0, g_orig_P23 = 0;   /* lente ORIGINAL del juego */
static volatile LONG g_inst_fix = 0, g_inst_mod = 0, g_inst_dump = 0;   /* (E7e) huecos 8 y 13 */
static volatile LONG g_ord_flag = 0, g_ord_before = 0, g_ord_after = 0, g_ord_cb1 = 0;   /* (E7d) orden de los cajones */
/* (E7d) H para el cajon de la camara, hecho con ESA camara. El juego prepara la camara del siguiente
 * fotograma antes de terminar el actual, asi que los dibujos (hueco 0, luces) usan el H fijado en Present
 * con la camara de entonces, y la camara nueva el suyo propio: asi cuadran entre si aunque te muevas */
static float g_rt_Rb[3][3]; static volatile LONG g_rt_haveRb = 0;
static int g_rt_bodyfrom_any = 0;   /* (E8h) 0: el "cuerpo" solo se calcula con la camara PRINCIPAL */
/* (E8b) UN solo H por fotograma, hecho con la camara de ESE fotograma (la primera que llega tras Present).
 * Y el del fotograma anterior, para el hueco 7, que guarda la camara del fotograma ANTERIOR (para
 * reaprovechar su imagen): hay que darle lo que de verdad se dibujo entonces, no lo de ahora. */
static float g_fs_curH[4][4], g_fs_prevH[4][4], g_fs_curL[2][4], g_fs_prevL[2][4];
/* (E8g) H de la camara principal en ESTE fotograma y en el ANTERIOR (con el ojo), y la lente de cada uno.
 * Los efectos que reaprovechan la imagen del fotograma anterior (reflejos, hueco 7) tienen que usar el de antes. */
static float g_oc_H[4][4], g_oc_Hi[4][4], g_oc_L[2][4], g_op_H[4][4], g_op_Hi[4][4], g_op_L[2][4];
static volatile LONG g_oc_have = 0, g_op_have = 0;
static volatile LONG g_s7vp = 0, g_s7T = 0, g_s3rays = 0, g_s3P = 0, g_s3refl = 0;
static volatile LONG g_fs_have = 0, g_fs_need = 1, g_fs_frames = 0;   /* (E7g) el "cuerpo" (camara sin nuestro raton) */
static volatile LONG g_cb0_own = 0, g_iw_bad = 0, g_own_fail = 0, g_l7own = 0, g_l7prev = 0;
static void finish_H(float A[4][4], float Ai[4][4], float H[4][4], float Hi[4][4]);
static void cam_own_H(const float *f, float H[4][4], float Hi[4][4])
{
    float A[4][4], Ai[4][4];
    raton_make_H(f, A, Ai);
    finish_H(A, Ai, H, Hi);
}
/* (E7g) H para un dibujo, hecho con LA camara que uso ese dibujo (V, filas de 3x4): asi todos los dibujos
 * acaban mirando a donde mira tu cabeza, aunque unos lleven la camara de este fotograma y otros la del anterior */
static int view_H(const float *V, float H[4][4], float Hi[4][4])
{
    if (nat_full()) {
        float A[4][4], Ai[4][4];
        memset(A, 0, sizeof(A)); memset(Ai, 0, sizeof(Ai));
        for (int i = 0; i < 4; i++) A[i][i] = Ai[i][i] = 1.0f;
        finish_H(A, Ai, H, Hi);
        return 1;
    }
    if (!g_rt_haveRb) return 0;
    float Rc[3][3], Rd[3][3], A[4][4], Ai[4][4], t[3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Rc[i][j] = V[j * 4 + i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        float a = 0; for (int k = 0; k < 3; k++) a += g_rt_Rb[i][k] * g_rt_Rg[k][j]; Rd[i][j] = a;
    }
    memset(A, 0, sizeof(A)); memset(Ai, 0, sizeof(Ai)); A[3][3] = Ai[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        float a = 0; for (int k = 0; k < 3; k++) a += Rd[k][i] * Rc[k][j]; A[i][j] = a;
    }
    for (int i = 0; i < 3; i++) t[i] = -(g_rt_Rg[0][i] * g_rt_dg[0] + g_rt_Rg[1][i] * g_rt_dg[1] + g_rt_Rg[2][i] * g_rt_dg[2]);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) Ai[i][j] = A[j][i];
        A[i][3] = t[i];
        Ai[i][3] = -(A[0][i] * t[0] + A[1][i] * t[1] + A[2][i] * t[2]);
    }
    finish_H(A, Ai, H, Hi);
    return 1;
}
static void finish_H(float A[4][4], float Ai[4][4], float H[4][4], float Hi[4][4])
{
    float Hk[4][4], Hki[4][4];
    build_H(g_yaw, g_pitch, g_roll, g_sx, g_sy, g_sz, Hk, Hki);
    mul4(A, Hk, H); mul4(Hki, Ai, Hi);
    if (eye_in_H()) {
        LONG e = cur_eye();
        float t[3] = { g_eyepos[e][0] * cfg_ipdscale, g_eyepos[e][1] * cfg_ipdscale, g_eyepos[e][2] * cfg_ipdscale };
        for (int r = 0; r < 3; r++) {
            H[r][3] -= t[r];
            Hi[r][3] += Hi[r][0] * t[0] + Hi[r][1] * t[1] + Hi[r][2] * t[2];
        }
    }
}

/* ---- lente del visor ---- filas 0 y 1 de la proyeccion (x e y); la profundidad (filas 2 y 3) es la del juego */
static float g_fovrow[2][4];
static float g_vis_L = 0, g_vis_R = 0, g_vis_UP = 0, g_vis_DN = 0;   /* bordes del visor (tangentes): para el margen de la linterna */
static volatile LONG g_fov_ok = 0;
static float g_eyeb[2][4];   /* trozo de la imagen para cada ojo: uMin vMin uMax vMax */

static void fov_xy(float P[4][4])
{
    if (!cfg_fov || !g_fov_ok) return;
    if (g_stereo_on) {      /* lente del ojo de este fotograma */
        LONG e = cur_eye();
        for (int j = 0; j < 4; j++) { P[0][j] = g_eyerow[e][0][j]; P[1][j] = g_eyerow[e][1][j]; }
        return;
    }
    for (int j = 0; j < 4; j++) { P[0][j] = g_fovrow[0][j]; P[1][j] = g_fovrow[1][j]; }
}

/* inversa general 4x4 (Gauss-Jordan). Devuelve 0 si no tiene. */
static int inv4(const float A[4][4], float R[4][4])
{
    double m[4][8];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) m[i][j] = j < 4 ? A[i][j] : (j - 4 == i);
    for (int c = 0; c < 4; c++) {
        int pv = c;
        for (int i = c + 1; i < 4; i++) if (fabs(m[i][c]) > fabs(m[pv][c])) pv = i;
        if (fabs(m[pv][c]) < 1e-12) return 0;
        if (pv != c) for (int j = 0; j < 8; j++) { double t = m[c][j]; m[c][j] = m[pv][j]; m[pv][j] = t; }
        double d = m[c][c];
        for (int j = 0; j < 8; j++) m[c][j] /= d;
        for (int i = 0; i < 4; i++) if (i != c) { double f = m[i][c]; for (int j = 0; j < 8; j++) m[i][j] -= f * m[c][j]; }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) R[i][j] = (float)m[i][j + 4];
    return 1;
}

/* Ficha de la camara (352 bytes): la vista (m_V, m_iV) NO se toca; el giro se mete en la lente:
 *   P' = P*H     iP' = H^-1 * iP     VP' = P'*V     iVP' = iV * iP'      */
static int apply_view_yaw_cam(float *g, const float H[4][4], const float Hi[4][4])
{
    float P[4][4], iP[4][4], V4[4][4], iV4[4][4], chk[4][4], P2[4][4], iP2[4][4], VP[4][4], iVP[4][4];
    memcpy(P, g + 24, sizeof(P));
    memcpy(iP, g + 40, sizeof(iP));
    memset(V4, 0, sizeof(V4));  V4[3][3] = 1.0f;
    memset(iV4, 0, sizeof(iV4)); iV4[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) { V4[i][j] = g[i*4 + j]; iV4[i][j] = g[12 + i*4 + j]; }
    mul4(P, V4, chk);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float want = g[56 + i*4 + j];
            if (fabsf(chk[i][j] - want) > 1e-3f * (1.0f + fabsf(want))) return 0;
        }
    if (cfg_fov && g_fov_ok) {         /* lente del visor en vez de la del juego */
        fov_xy(P);
        if (!inv4(P, iP)) return 0;
    }
    mul4(P, H, P2);
    mul4(Hi, iP, iP2);
    mul4(P2, V4, VP);
    mul4(iV4, iP2, iVP);
    memcpy(g + 24, P2,  sizeof(P2));
    memcpy(g + 40, iP2, sizeof(iP2));
    memcpy(g + 56, VP,  sizeof(VP));
    memcpy(g + 72, iVP, sizeof(iVP));
    return 1;
}

/* Cajon del hueco 0 (208 bytes): m_W (3x4) | m_iW (3x4) | m_WV (3x4) | m_WVP (4x4), en floats: 0, 12, 24, 36.
 * Solo tocamos m_WVP:  WVP' = P * H * WV  = WVP * WV^-1 * H * WV   (P sale de WVP * WV^-1). */
static int apply_cb0_yaw(float *f, const float H[4][4])
{
    float WV4[4][4], WVP[4][4], iWV4[4][4], Pe[4][4], M1[4][4], M2[4][4], NW[4][4];
    for (int i = 24; i < 52; i++) if (!isfinite(f[i])) return 0;
    memset(WV4, 0, sizeof(WV4)); WV4[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) WV4[i][j] = f[24 + i*4 + j];
    memcpy(WVP, f + 36, sizeof(WVP));

    /* inversa de una transformacion afin: [A t]^-1 = [A^-1, -A^-1 t] */
    double a = WV4[0][0], b = WV4[0][1], c = WV4[0][2];
    double d = WV4[1][0], e = WV4[1][1], g = WV4[1][2];
    double h = WV4[2][0], i2 = WV4[2][1], j2 = WV4[2][2];
    double det = a*(e*j2 - g*i2) - b*(d*j2 - g*h) + c*(d*i2 - e*h);
    if (fabs(det) < 1e-12) return 0;
    double inv[3][3] = {
        {  (e*j2 - g*i2) / det, -(b*j2 - c*i2) / det,  (b*g - c*e) / det },
        { -(d*j2 - g*h)  / det,  (a*j2 - c*h)  / det, -(a*g - c*d) / det },
        {  (d*i2 - e*h)  / det, -(a*i2 - b*h)  / det,  (a*e - b*d) / det } };
    memset(iWV4, 0, sizeof(iWV4)); iWV4[3][3] = 1.0f;
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) iWV4[r][k] = (float)inv[r][k];
        iWV4[r][3] = (float)-(inv[r][0]*WV4[0][3] + inv[r][1]*WV4[1][3] + inv[r][2]*WV4[2][3]);
    }
    float Hown[4][4], Hiown[4][4];
    if (cfg_porobjeto && raton_active()) {
        /* (E7h) la camara que uso este dibujo: V = WV * W^-1. La inversa la calculamos nosotros con W
         * (en la E7g se usaba el iW del cajon, que en algunos dibujos no es la inversa de W) */
        float W4[4][4], iW4[4][4], Vu[4][4]; int ok = 1;
        memset(W4, 0, sizeof(W4)); W4[3][3] = 1.0f;
        for (int i = 0; i < 12; i++) if (!isfinite(f[i])) ok = 0;
        if (ok) {
            for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) W4[i][j] = f[i * 4 + j];
            if (!inv4(W4, iW4)) ok = 0;
        }
        if (ok) {
            {   /* diagnostico: el iW del cajon, es la inversa de W? */
                float dmax = 0;
                for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) { float d = fabsf(iW4[i][j] - f[12 + i * 4 + j]); if (d > dmax) dmax = d; }
                if (dmax > 1e-3f) InterlockedIncrement(&g_iw_bad);
            }
            mul4(WV4, iW4, Vu);
            for (int r = 0; r < 3 && ok; r++) {
                float l = Vu[r][0] * Vu[r][0] + Vu[r][1] * Vu[r][1] + Vu[r][2] * Vu[r][2];
                if (fabsf(l - 1.0f) > 0.02f) ok = 0;
            }
            float Vr[12];
            for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) Vr[i * 4 + j] = Vu[i][j];
            if (ok && view_H(Vr, Hown, Hiown)) { H = (const float (*)[4])Hown; InterlockedIncrement(&g_cb0_own); }
            else InterlockedIncrement(&g_own_fail);
        }
    }
    mul4(WVP, iWV4, Pe);                                   /* la lente de este dibujo */
    if (fabsf(Pe[3][2] - 1.0f) > 1e-3f || fabsf(Pe[3][3]) > 1e-3f ||
        fabsf(Pe[3][0]) > 1e-3f || fabsf(Pe[3][1]) > 1e-3f) {
        /* (E7f) RESCATE: el WV y el WVP de este dibujo no cuadran entre si (uno con la camara de este fotograma
         * y otro con la del anterior). Pasa con la camara moviendose: antes se saltaba y el objeto se quedaba sin
         * corregir (se movia un poco al mover la cabeza). Si se parece a la lente principal, se usa esa lente. */
        if (!cfg_rescate || g_orig_P00 <= 0 || !(cfg_fov && g_fov_ok)) return 0;
        if (fabsf(Pe[3][2] - 1.0f) > 0.15f || fabsf(Pe[3][3]) > 0.15f || fabsf(Pe[3][0]) > 0.15f || fabsf(Pe[3][1]) > 0.15f) return 0;
        if (fabsf(Pe[0][0] - g_orig_P00) > 0.1f * g_orig_P00 || fabsf(Pe[1][1] - g_orig_P11) > 0.1f * g_orig_P11) return 0;
        float Pm[4][4] = {{g_orig_P00,0,0,0},{0,g_orig_P11,0,0},{0,0,g_orig_P22,g_orig_P23},{0,0,1,0}}, iPm[4][4], Pv[4][4], VW[4][4];
        if (!inv4(Pm, iPm)) return 0;
        memcpy(Pv, Pm, sizeof(Pv)); fov_xy(Pv);
        mul4(iPm, WVP, VW);                                /* el WV que de verdad usa su WVP */
        mul4(H, VW, M1);
        mul4(Pv, M1, NW);
        memcpy(f + 36, NW, sizeof(NW));
        InterlockedIncrement(&g_cb0_resc);
        return 1;
    }
    if (Pe[0][0] < 0.05f || Pe[1][1] < 0.05f) return 0;     /* no parece una lente en perspectiva (0.05: linterna) */
    g_cb0_weapon = 0;
    if (Pe[0][0] >= 1.428f && !g_arma) return 0;             /* lente estrecha (~57 grados): es el arma */
    float Hw[4][4];
    int is_wpn = nat_full() && g_stereo_on && cfg_natarma && g_orig_P00 > 0 && Pe[0][0] > g_orig_P00 * 1.10f;
    g_cb0_weapon = is_wpn;
    if (is_wpn) {
        /* (E10c) ARMA y BRAZOS (su lente es mas estrecha): el juego los pega a la camara, y la camara ya va en
         * cada ojo -> el arma se iba con cada ojo y los dos la veian igual (sin 3D, enorme). Se la devuelve al
         * centro de la cabeza: en la vista del ojo, todo lo del arma se corre -ojo. */
        LONG e = cur_eye();
        float t[3] = { g_eyepos[e][0] * cfg_ipdscale, g_eyepos[e][1] * cfg_ipdscale, g_eyepos[e][2] * cfg_ipdscale };
        memcpy(Hw, H, sizeof(Hw));
        if (eye_in_H() && nat_full()) for (int r = 0; r < 3; r++) Hw[r][3] += t[r];   /* (E27) H ya traia el ojo: se quita (va abajo) */
        int mano = cfg_mano && g_hand_ok;
        if (mano) {   /* (E15) el arma va a la mano: primero se mueve con la mano, luego al ojo */
            float Hg[4][4]; mul4(g_hand_G, Hw, Hg); memcpy(Hw, Hg, sizeof(Hw));
        }
        for (int r = 0; r < 3; r++) Hw[r][3] -= t[r];
        H = (const float (*)[4])Hw;
        if (mano) {   /* y su posicion para la luz (m_WV) tambien, si no se ilumina como si siguiera en la cara */
            float NWV[4][4]; mul4(H, WV4, NWV);
            for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) f[24 + i * 4 + j] = NWV[i][j];
            InterlockedIncrement(&g_hand_n);
        }
        InterlockedIncrement(&g_nat_armaojo);
    }
    if (cfg_fov && g_fov_ok) {
        fov_xy(Pe);                    /* WVP' = Plente_visor * H * WV */
        mul4(H, WV4, M1);
        mul4(Pe, M1, NW);
    } else {
        mul4(iWV4, H, M1);
        mul4(M1, WV4, M2);
        mul4(WVP, M2, NW);
    }
    memcpy(f + 36, NW, sizeof(NW));
    return 1;
}

/* Cajon del hueco 7 (144 bytes): [ P (16 floats) | VP o copia de P (16 floats) | 4 floats ]. */
__attribute__((unused)) static int apply_view_yaw_slot7(float *f, float deg)
{
    float P[4][4], H[4][4], Ht[4][4], PH[4][4], iPa[4][4], T[4][4], VP[4][4], VP2[4][4];
    memcpy(P, f, sizeof(P));
    for (int i = 0; i < 16; i++) if (!isfinite(f[i]) || !isfinite(f[16 + i])) return 0;
    float a = P[0][0], b = P[1][1], c = P[2][2], d = P[2][3], e = P[3][2];
    if (fabsf(a) < 1e-4f || fabsf(b) < 1e-4f || fabsf(d) < 1e-6f || fabsf(e) < 1e-4f) return 0;
    if (fabsf(P[0][1]) + fabsf(P[0][2]) + fabsf(P[0][3]) + fabsf(P[1][0]) + fabsf(P[1][2]) + fabsf(P[1][3]) +
        fabsf(P[2][0]) + fabsf(P[2][1]) + fabsf(P[3][0]) + fabsf(P[3][1]) + fabsf(P[3][3]) > 1e-5f) return 0;
    memset(iPa, 0, sizeof(iPa));
    iPa[0][0] = 1.0f / a;  iPa[1][1] = 1.0f / b;  iPa[2][3] = 1.0f / e;
    iPa[3][2] = 1.0f / d;  iPa[3][3] = -c / (d * e);
    make_H(deg, H, Ht);
    mul4(P, H, PH);
    memcpy(VP, f + 16, sizeof(VP));
    int same = 1;
    for (int i = 0; i < 16; i++) if (fabsf(f[16 + i] - f[i]) > 1e-5f) same = 0;
    memcpy(f, PH, sizeof(PH));
    if (same) {
        memcpy(f + 16, PH, sizeof(PH));
    } else {
        mul4(PH, iPa, T);          /* T = P*H*P^-1  (transformacion en el espacio de recorte) */
        mul4(T, VP, VP2);          /* VP' = T * VP = P*H*V */
        memcpy(f + 16, VP2, sizeof(VP2));
    }
    return 1;
}

/* "dst" = memoria del cajon donde podemos escribir (solo cuando viene de Map); NULL si no. */
/* ---- hacia donde mira la camara del juego (ratón), sacado de la ficha ORIGINAL (antes de tocarla) ---- */
static volatile float g_cam_yaw = 0, g_cam_pitch = 0;
static volatile float g_cam_fx = 0, g_cam_fy = 0, g_cam_fz = 0;
static float g_aspect = 16.0f / 9.0f;      /* se actualiza con el tamano real de la imagen */
static volatile LONG g_cam_seq = 0;
static float wrap_pi(float a);

/* (E6) La camara del juego ya NO se toca: el raton la mueve como siempre y la cabeza solo mueve lo que se ve
 * en las gafas (H). Lo de E4-E5h (camara_juego) daba el "mundo marron" y se ha quitado. Ver docs/ESTADO.md */
static void publish_H(void);
/* (E7c) detective del fotograma ANTERIOR: busca en los cajones las matrices de la camara de hace un fotograma */
static float g_pv_cur[88], g_pv_prev[88];   /* camara principal ORIGINAL: actual y la del fotograma anterior */
static volatile LONG g_pv_have = 0;         /* 2 = hay actual y anterior distintas */
static volatile ULONGLONG g_pv_until = 0;   /* buscar hasta esta hora (Shift+D) */
static int rd_float(const float *p, float *v) { SIZE_T g = 0; return ReadProcessMemory(GetCurrentProcess(), p, v, 4, &g) && g == 4; }
static int wr_float(float *p, float v) { SIZE_T g = 0; return WriteProcessMemory(GetCurrentProcess(), p, &v, 4, &g) && g == 4; }

/* (E8g) La camara PRINCIPAL: forma de pantalla apaisada (en ventana no es 16:9 exacto) y la lente MAS ANCHA.
 * Ojo: con r_base_fov 90, la camara del ARMA tambien es 16:9 y P00 < 1.43; antes se colaba como principal y
 * cambiaba a ratos la lente "original" (por eso a ratos no se corregian luces y reflejos). */
static float g_main_p00 = 0; static ULONGLONG g_main_seen = 0;
static int is_main_cam(const float *f)
{
    float p00 = f[24], p11 = f[29];
    if (!(p00 > 0.05f && p00 < 1.428f && p11 > 0.05f)) return 0;
    float r = p11 / p00;
    /* (E16) la forma de la pantalla del juego, sea la que sea (antes solo 16:9) */
    if (g_aspect > 0.5f) { if (fabsf(r / g_aspect - 1.0f) > 0.08f) return 0; }
    else if (r < 1.70f || r > 1.95f) return 0;
    ULONGLONG now = GetTickCount64();
    if (g_main_p00 <= 0 || now - g_main_seen > 150 || p00 < g_main_p00 * 0.95f) { g_main_p00 = p00; g_main_seen = now; return 1; }
    /* (E16) al APUNTAR el juego hace zoom: la lente principal se estrecha poco a poco (hasta ~10% por fotograma).
     * Antes solo se aceptaba un 5%: durante el zoom la camara principal se tomaba por "el arma" 0,7 s cada vez
     * y todo se descolocaba a tirones. La del arma esta mucho mas lejos (0.96 frente a 0.55). */
    if (fabsf(p00 - g_main_p00) <= g_main_p00 * 0.15f) { g_main_p00 = p00; g_main_seen = now; return 1; }
    return 0;    /* lente mucho mas estrecha de golpe: el arma */
}
/* (E31) MANO IZQUIERDA EN EL MANDO IZQUIERDO. La mano izq. es una isla de la pieza de los brazos (E29/E30). Se pinta
 * con su PROPIA camara del arma: la misma que la del arma, pero movida con el mando izquierdo en vez del derecho
 * (G_izq = mando_izq_ahora * mando_izq_calibrado^-1). Calibrar: mantener el clic del stick IZQUIERDO; mientras se
 * mantiene, la mano izq. vuelve a donde la pone el juego; pon el mando ahi y a los 1,5 s se guarda. */
static volatile LONG g_lhand_ok = 0, g_lhand_req = 0, g_lhand_hold = 0, g_lh_n = 0, g_lh_draw = 0, g_lh_nocam = 0;
static float g_lhand_G[4][4], g_lhand_C0[4][4]; static int g_lhand_cal = 0;
#define LH_N 8
typedef struct { void *r; int have[2]; float d[2][CAM_BYTES / 4]; } LhCam;
static LhCam g_lh[LH_N]; static int g_lh_next = 0;
static SRWLOCK g_lh_lock = SRWLOCK_INIT;
static void lh_store(void *r, int twin, const float *d)
{
    AcquireSRWLockExclusive(&g_lh_lock);
    int k = -1;
    for (int i = 0; i < LH_N; i++) if (g_lh[i].r == r) { k = i; break; }
    if (k < 0) { k = g_lh_next; g_lh_next = (g_lh_next + 1) % LH_N; g_lh[k].r = r; g_lh[k].have[0] = g_lh[k].have[1] = 0; }
    memcpy(g_lh[k].d[twin], d, CAM_BYTES); g_lh[k].have[twin] = 1;
    ReleaseSRWLockExclusive(&g_lh_lock);
}
static int lh_get(void *r, int twin, float *out)
{
    int ok = 0;
    AcquireSRWLockShared(&g_lh_lock);
    for (int i = 0; i < LH_N; i++) if (g_lh[i].r == r && g_lh[i].have[twin]) { memcpy(out, g_lh[i].d[twin], CAM_BYTES); ok = 1; break; }
    ReleaseSRWLockShared(&g_lh_lock);
    return ok;
}
static void check_camera(const void *src, void *dst, void *res, UINT bw, UINT mapType, const char *via)
{
    float f[CAM_BYTES / 4];
    SIZE_T got = 0;
    /* ReadProcessMemory = lectura "con red": si la memoria no es legible, falla sin tirar el juego */
    if (!ReadProcessMemory(GetCurrentProcess(), src, f, CAM_BYTES, &got) || got != CAM_BYTES) {
        InterlockedIncrement(&g_readfail);
        return;
    }
    if (!is_real_cam(f)) return;
    float p00 = f[24], p11 = f[29];
    if (t_quiet) goto modo;   /* (E27) calculo del ojo derecho: sin apuntar nada */
    /* solo la camara PRINCIPAL: forma de pantalla 16:9 (las camaras de reflejos/cubos son cuadradas) */
    {   static int said = 0;
        if (f[24] < 1.428f && f[24] > 0.05f && said < 6 && GetTickCount64() % 997 < 3) { said++; vr_log("CAMARA: P00=%.4f P11=%.4f forma=%.4f (busco %.4f)", f[24], f[29], f[29] / f[24], g_aspect); }
    }
    if (is_main_cam(f)) {
        float fy = f[9]; if (fy > 1) fy = 1; if (fy < -1) fy = -1;
        g_cam_yaw = atan2f(f[8], f[10]);
        g_cam_fx = f[8]; g_cam_fy = f[9]; g_cam_fz = f[10];
        g_cam_pitch = asinf(fy);
        g_orig_P00 = f[24]; g_orig_P11 = f[29]; g_orig_P22 = f[34]; g_orig_P23 = f[35];
        memcpy(g_rt_lastcam, f, sizeof(g_rt_lastcam)); g_rt_havecam = 1;
        if (raton_active()) {   /* (E8h) el cuerpo, SOLO con la camara principal */
            float A[4][4], Ai[4][4];
            g_rt_bodyfrom_any = 1; raton_make_H(f, A, Ai); g_rt_bodyfrom_any = 0;
        }
        memcpy(g_pv_cur, f, sizeof(g_pv_cur));   /* (E7c) la camara principal de este fotograma */
        g_ord_flag = 1; InterlockedIncrement(&g_ord_cb1);
        InterlockedIncrement(&g_cam_seq);
    }

    InterlockedIncrement(&g_cams);
    if (res && !g_cam_buf) {
        g_cam_buf = res;
        vr_log("El cajon de la camara es %p (ancho=%u)", res, bw);
    }
    if (g_budget > 0) {
        InterlockedDecrement(&g_budget);
        float fovH = 2.0f * atanf(1.0f / p00) * RAD2DEG;
        float fovV = 2.0f * atanf(1.0f / p11) * RAD2DEG;
        vr_log("  [f=%ld] cam via %s buf=%p ancho=%u mapa=%u pos=(%.2f, %.2f, %.2f) "
               "mira=(%.3f, %.3f, %.3f) P00=%.5f P11=%.5f fovH=%.1f fovV=%.1f",
               (long)g_frames, via, res, bw, mapType,
               f[15], f[19], f[23],
               f[8], f[9], f[10],
               p00, p11, fovH, fovV);
    }

modo:;
    /* --- PRUEBA: giro "en el espacio de vista" (modos 7 y 8) --- */
    LONG mode = g_mode;
    if (mode == 1 && dst && (p00 < 1.428f || g_arma)) {      /* solo la camara ancha: el arma (57 grados) se queda quieta */
        float g[CAM_BYTES / 4], Hc[4][4], Hic[4][4];
        memcpy(g, f, sizeof(g));
        int main_cam = raton_active() && is_main_cam(f);
        if (main_cam && cfg_modoh == 1 && g_fs_need && !t_quiet) {
            g_fs_need = 0;
            if (g_fs_have) { memcpy(g_fs_prevH, g_fs_curH, sizeof(g_fs_curH)); memcpy(g_fs_prevL, g_fs_curL, sizeof(g_fs_curL)); }
            raton_make_H(f, g_headH, g_headHi);
            publish_H();
            float Hx[4][4], Pt[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,1,0}};
            get_H(g_fs_curH, Hx);
            fov_xy(Pt);
            for (int j = 0; j < 4; j++) { g_fs_curL[0][j] = Pt[0][j]; g_fs_curL[1][j] = Pt[1][j]; }
            if (!g_fs_have) { memcpy(g_fs_prevH, g_fs_curH, sizeof(g_fs_curH)); memcpy(g_fs_prevL, g_fs_curL, sizeof(g_fs_curL)); g_fs_have = 1; }
            InterlockedIncrement(&g_fs_frames);
        }
        get_H(Hc, Hic);
        /* como la E7d; y (E8e) con por_objeto, TODAS las camaras (tambien la del arma) con su propio H, igual que
         * los dibujos del hueco 0: el arma se dibuja con piezas de los dos caminos y tienen que coincidir */
        if (raton_active() && cfg_modoh == 0 && (main_cam || cfg_porobjeto)) {
            float Hv[4][4], Hiv[4][4];
            if (view_H(f, Hv, Hiv)) { memcpy(Hc, Hv, sizeof(Hv)); memcpy(Hic, Hiv, sizeof(Hiv)); }
            else cam_own_H(f, Hc, Hic);
        }
        if (main_cam) { if (!t_quiet) nat_eye_check(f); }
        /* (E10b) camaras que NO salen de nuestra posicion (la del ARMA y los brazos: el juego la monta aparte, pegada
         * a la cara): sin esto los dos ojos ven el arma igual -> sin profundidad, enorme. Se les pone el ojo en H. */
        else if (nat_full() && g_stereo_on && cfg_natarma) {
            if (g_orig_P00 > 0 && p00 > g_orig_P00 * 1.3f) {   /* (E10c) camara del arma (lente estrecha) */
                LONG e = cur_eye();
                float t[3] = { g_eyepos[e][0] * cfg_ipdscale, g_eyepos[e][1] * cfg_ipdscale, g_eyepos[e][2] * cfg_ipdscale };
                if (eye_in_H()) for (int r = 0; r < 3; r++) Hc[r][3] += t[r];   /* (E27) H ya traia el ojo: se quita (va abajo) */
                /* (E17d) ESTA es la camara con la que se pinta el arma que ves (la E15 solo movia las piezas del
                 * cajon 0, que eran otras). Se le aplica la mano: vista_arma = T(-ojo) * G_mano * vista */
                if (cfg_mano_izq_mando && g_lhand_ok && res) {   /* (E31) la misma camara, movida con el mando izquierdo */
                    float HL[4][4], HLi[4][4], gl[CAM_BYTES / 4];
                    mul4(g_lhand_G, Hc, HL);
                    for (int r = 0; r < 3; r++) HL[r][3] -= t[r];
                    if (inv4(HL, HLi)) {
                        memcpy(gl, f, CAM_BYTES);
                        if (apply_view_yaw_cam(gl, HL, HLi)) { lh_store(res, t_eye == 1, gl); InterlockedIncrement(&g_lh_n); }
                    }
                }
                if (cfg_mano && g_hand_ok) {
                    float Hg[4][4]; mul4(g_hand_G, Hc, Hg); memcpy(Hc, Hg, sizeof(Hg));
                    InterlockedIncrement(&g_hand_cam);
                }
                for (int r = 0; r < 3; r++) Hc[r][3] -= t[r];
                if (!inv4(Hc, Hic)) { for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) Hic[r][k] = (r == k); }
                InterlockedIncrement(&g_nat_armaojo);
            } else InterlockedIncrement(&g_nat_otras);
        }
        if (main_cam && cfg_modoh == 0 && !t_quiet) {
            float Pt[4][4];
            memcpy(Pt, f + 24, sizeof(Pt));
            fov_xy(Pt);
            memcpy(g_oc_H, Hc, sizeof(Hc)); memcpy(g_oc_Hi, Hic, sizeof(Hic));
            for (int j = 0; j < 4; j++) { g_oc_L[0][j] = Pt[0][j]; g_oc_L[1][j] = Pt[1][j]; }
            if (!g_op_have) { memcpy(g_op_H, g_oc_H, sizeof(g_oc_H)); memcpy(g_op_Hi, g_oc_Hi, sizeof(g_oc_Hi)); memcpy(g_op_L, g_oc_L, sizeof(g_oc_L)); g_op_have = 1; }
            g_oc_have = 1;
        }
        if (apply_view_yaw_cam(g, Hc, Hic)) {
            memcpy(dst, g, CAM_BYTES);
            _mm_sfence();
            InterlockedIncrement(&g_modded);
        } else if (InterlockedIncrement(&g_nomatch) == 1) {
            vr_log("AVISO: la ficha no cumple VP = P*V; no la toco (primera vez)");
        }
    }
}

/* -------------------------------------------------------------- cajones */
static int cb_buffer_info(ID3D11Resource *res, UINT *bw)
{
    D3D11_RESOURCE_DIMENSION dim;
    ID3D11Resource_GetType(res, &dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return 0;
    D3D11_BUFFER_DESC d;
    ID3D11Buffer_GetDesc((ID3D11Buffer *)res, &d);
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) return 0;
    if (d.ByteWidth < 16) return 0;
    *bw = d.ByteWidth;
    return 1;
}

static LONGLONG g_qpf = 10000000;
typedef struct { void *res; const void *p; UINT bw; UINT type; void *real; } Pending;   /* (E24) real = memoria de la grafica si p es un borrador */
static Pending g_pend[16];

static int pend_add(void *res, const void *p, UINT bw, UINT type)
{
    int ok = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < 16; i++) {
        if (!g_pend[i].res || g_pend[i].res == res) {
            g_pend[i].res = res; g_pend[i].p = p; g_pend[i].bw = bw; g_pend[i].type = type; g_pend[i].real = NULL;
            ok = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}

static void pend_set_real(void *res, void *real)
{
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < 16; i++) if (g_pend[i].res == res) { g_pend[i].real = real; break; }
    LeaveCriticalSection(&g_cs);
}
static int pend_take(void *res, Pending *out)
{
    int found = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < 16; i++) {
        if (g_pend[i].res == res) { *out = g_pend[i]; g_pend[i].res = NULL; found = 1; break; }
    }
    LeaveCriticalSection(&g_cs);
    return found;
}

/* ------------------------------------------------ reloj del juego (para el estereo sincronizado) */
/* Mientras se dibuja el 2o ojo, el juego ve el reloj PARADO en el instante en que empezo el 1o:
 * asi el mundo no avanza entre los dos ojos. Luego el reloj sigue normal (no hay camara lenta). */
static volatile LONG g_freeze = 0;
static volatile LONG g_capture = 0;
static LARGE_INTEGER g_t0_qpc;
static DWORD g_t0_tick = 0, g_t0_tgt = 0;
static ULONGLONG g_t0_tick64 = 0;
static volatile LONG g_cap_tick = 0, g_cap_tgt = 0, g_cap_tick64 = 0;
static volatile LONG g_frozen_reads = 0;
static volatile LONG g_freeze_epoch = 0;      /* cuenta cada vez que se para el reloj */
static __thread LONG t_epoch = -1, t_reads = 0, t_spinner = 0;
static __thread LONGLONG t_first = 0;          /* hora real de la primera lectura parada de este hilo */
static volatile DWORD g_draw_tid = 0;          /* hilo que dibuja (el que llama a Present) */
static volatile LONG g_valve = 0;              /* veces que un hilo se ha soltado por esperar demasiado */
static volatile LONG g_spinners = 0;
typedef BOOL (WINAPI *PFN_QPC)(LARGE_INTEGER *);
typedef DWORD (WINAPI *PFN_TICK)(void);
typedef ULONGLONG (WINAPI *PFN_TICK64)(void);
static PFN_QPC    o_QPC = NULL;
static PFN_TICK   o_Tick = NULL, o_TGT = NULL;
static PFN_TICK64 o_Tick64 = NULL;

static BOOL WINAPI hk_QPC(LARGE_INTEGER *p)
{
    if (g_freeze && p && (!cfg_clock_thread || GetCurrentThreadId() == g_draw_tid)) {
        /* un hilo que mira el reloj sin parar esta ESPERANDO a que pase el tiempo (audio, limitador...):
         * a ese hilo le damos la hora real, para que no se quede atascado */
        LONG ep = g_freeze_epoch;
        if (t_epoch != ep) { t_epoch = ep; t_reads = 0; LARGE_INTEGER n0; o_QPC(&n0); t_first = n0.QuadPart; }
        else if ((t_reads & 15) == 0) {
            /* si este hilo lleva mas de 25 ms viendo el reloj parado, en esta pareja ya ve la hora real */
            LARGE_INTEGER n1, fq; o_QPC(&n1); QueryPerformanceFrequency(&fq);
            if ((n1.QuadPart - t_first) * 1000 / fq.QuadPart > 25) { InterlockedIncrement(&g_valve); t_reads = 1 << 30; }
        }
        if (t_reads >= (1 << 30)) return o_QPC(p);
        if (t_spinner || ++t_reads > 3000) {
            if (!t_spinner) {
                t_spinner = 1;
                if (InterlockedIncrement(&g_spinners) <= 8)
                    vr_log("RELOJ: el hilo %lu espera al reloj; a partir de ahora ve la hora real", (unsigned long)GetCurrentThreadId());
            }
            return o_QPC(p);
        }
        LONG n = InterlockedIncrement(&g_frozen_reads);
        /* valvula de seguridad: si el juego se queda esperando a que pase el tiempo, soltamos el reloj */
        if ((n & 255) == 0) {
            LARGE_INTEGER now, f; o_QPC(&now); QueryPerformanceFrequency(&f);
            if ((now.QuadPart - g_t0_qpc.QuadPart) * 1000 / f.QuadPart > 100) {
                g_freeze = 0;
                static int warned = 0;
                if (!warned++) vr_log("AVISO RELOJ: el juego esperaba a que pasara el tiempo con el reloj parado; lo suelto (pasara cada vez que ocurra)");
                return o_QPC(p);
            }
        }
        *p = g_t0_qpc;
        return TRUE;
    }
    BOOL r = o_QPC(p);
    /* apuntamos siempre la hora mas alta que le hemos dado al juego: al parar el reloj, se para AHI
     * (si lo parabamos antes, el juego veia el tiempo ir hacia atras y se aceleraba) */
    if (r && p && p->QuadPart > g_t0_qpc.QuadPart) g_t0_qpc = *p;
    return r;
}
static DWORD WINAPI hk_Tick(void)
{
    if (g_freeze && (!cfg_clock_thread || GetCurrentThreadId() == g_draw_tid) && t_reads < (1 << 30)) return g_t0_tick;
    DWORD v = o_Tick();
    g_t0_tick = v;
    return v;
}
static DWORD WINAPI hk_TGT(void)
{
    if (g_freeze && (!cfg_clock_thread || GetCurrentThreadId() == g_draw_tid) && t_reads < (1 << 30)) return g_t0_tgt;
    DWORD v = o_TGT();
    g_t0_tgt = v;
    return v;
}
static ULONGLONG WINAPI hk_Tick64(void)
{
    if (g_freeze && (!cfg_clock_thread || GetCurrentThreadId() == g_draw_tid) && t_reads < (1 << 30)) return g_t0_tick64;
    ULONGLONG v = o_Tick64();
    if (v > g_t0_tick64) g_t0_tick64 = v;
    return v;
}
static void pair_begin(void)
{
    InterlockedExchange(&g_capture, 1);
    InterlockedExchange(&g_cap_tick, 1); InterlockedExchange(&g_cap_tgt, 1); InterlockedExchange(&g_cap_tick64, 1);
}

/* Cambia en la "agenda de importaciones" de metro.exe la funcion 'fn' por la nuestra */
static int patch_iat(HMODULE mod, const char *fn, void *hook, void **orig)
{
    unsigned char *base = (unsigned char *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return 0;
    int done = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd.VirtualAddress); d->Name; d++) {
        if (!d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *on = (IMAGE_THUNK_DATA64 *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *ft = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL64(on->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + on->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, fn) != 0) continue;
            DWORD old;
            if (VirtualProtect(&ft->u1.Function, sizeof(ULONGLONG), PAGE_READWRITE, &old)) {
                if (!*orig) *orig = (void *)(ULONG_PTR)ft->u1.Function;
                ft->u1.Function = (ULONGLONG)(ULONG_PTR)hook;
                VirtualProtect(&ft->u1.Function, sizeof(ULONGLONG), old, &old);
                done++;
            }
        }
    }
    return done;
}

static void hook_clocks(void)
{
    static int once = 0;
    if (once++) return;
    HMODULE exe = GetModuleHandleA(NULL);
    int a = patch_iat(exe, "QueryPerformanceCounter", (void *)hk_QPC, (void **)&o_QPC);
    int b = patch_iat(exe, "GetTickCount", (void *)hk_Tick, (void **)&o_Tick);
    int c = patch_iat(exe, "timeGetTime", (void *)hk_TGT, (void **)&o_TGT);
    int d = patch_iat(exe, "GetTickCount64", (void *)hk_Tick64, (void **)&o_Tick64);
    vr_log("RELOJ: enganchados QueryPerformanceCounter=%d GetTickCount=%d timeGetTime=%d GetTickCount64=%d", a, b, c, d);
}

/* ---------------------------------------------------------------- hooks */
typedef HRESULT (STDMETHODCALLTYPE *PFN_Map)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
                                             D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
typedef void    (STDMETHODCALLTYPE *PFN_Unmap)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
typedef void    (STDMETHODCALLTYPE *PFN_UpdateSub)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
                                                   const D3D11_BOX*, const void*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present)(IDXGISwapChain*, UINT, UINT);

typedef void    (STDMETHODCALLTYPE *PFN_VSSetCB)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer *const *);
static PFN_VSSetCB   g_orig_VSSetCB = NULL;
static PFN_VSSetCB   g_orig_PSSetCB = NULL, g_orig_CSSetCB = NULL;
static void * volatile g_ps_bound[NSLOT], * volatile g_cs_bound[NSLOT];
static void STDMETHODCALLTYPE hk_PSSetCB(ID3D11DeviceContext *c, UINT start, UINT num, ID3D11Buffer *const *bufs)
{
    if (bufs) for (UINT i = 0; i < num && start + i < NSLOT; i++) g_ps_bound[start + i] = (void *)bufs[i];
    g_orig_PSSetCB(c, start, num, bufs);
}
static void STDMETHODCALLTYPE hk_CSSetCB(ID3D11DeviceContext *c, UINT start, UINT num, ID3D11Buffer *const *bufs)
{
    if (bufs) for (UINT i = 0; i < num && start + i < NSLOT; i++) g_cs_bound[start + i] = (void *)bufs[i];
    g_orig_CSSetCB(c, start, num, bufs);
}
static int slot_of(void *res);
/* (E23) LECTURA RAPIDA. Entre Map y Unmap el puntero del juego es valido seguro: se copia directamente.
 * ReadProcessMemory (lectura "con red") es una llamada a Windows por cada objeto: ~25.000 por segundo, y era
 * la mayor parte de los ~250 ms/s que gastaba el mod. */
static int cb_read(const void *src, void *dst, SIZE_T n)
{
    if (cfg_fastread) { memcpy(dst, src, n); return 1; }
    SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), src, dst, n, &got) && got == n;
}
static int slot_in(void * volatile *arr, void *res) { for (int i = 0; i < NSLOT; i++) if (arr[i] == res) return i; return -1; }

/* DETECTIVE: busca en cada cajon que escribe el juego los numeros de su lente (P00, P11, sus inversas...).
 * Asi sabremos que cajones usan las luces/sombras y hay que corregir tambien. */
static void lens_detective(void *res, const void *data, UINT bw)
{
    static struct { UINT bw; int idx; int what; } seen[64]; static int nseen = 0;
    if (nseen >= 60 || bw > 8192 || g_orig_P00 == 0) return;
    float p00 = g_orig_P00, p11 = g_orig_P11;
    static int said = 0;
    if (!said++) vr_log("DETECTIVE: busco la lente del juego: P00=%.5f P11=%.5f P22=%.5f P23=%.5f", p00, p11, g_orig_P22, g_orig_P23);
    /* numeros "redondos" (1, 0.5...) salen en cualquier cajon: no sirven de pista */
    if (fabsf(p00 - p11) < 0.05f || fabsf(p00 - 1.0f) < 0.02f || fabsf(p11 - 1.0f) < 0.02f) return;
    float t[6] = { p00, p11, 1.0f / p00, 1.0f / p11, g_orig_P22, g_orig_P23 };
    const char *tn[6] = { "P00", "P11", "1/P00", "1/P11", "P22", "P23" };
    static float buf[2048]; SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), data, buf, bw, &got) || got != bw) return;
    for (UINT i = 0; i < bw / 4; i++) {
        for (int w = 0; w < 4; w++) {
            /* ojo: un "NaN" (numero roto) hace que cualquier comparacion de "no se parece" salga falsa */
            if (!isfinite(buf[i]) || !(fabsf(buf[i] - t[w]) <= 1e-4f * fabsf(t[w])) || fabsf(buf[i]) < 0.05f) continue;
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (seen[k].bw == bw && seen[k].idx == (int)i && seen[k].what == w) dup = 1;
            if (dup || nseen >= 60) continue;
            seen[nseen].bw = bw; seen[nseen].idx = i; seen[nseen].what = w; nseen++;
            vr_log("DETECTIVE: cajon ancho=%u (VS hueco %d, PS hueco %d, CS hueco %d) tiene %s en la casilla %u",
                   bw, slot_of(res), slot_in(g_ps_bound, res), slot_in(g_cs_bound, res), tn[w], i);
            static UINT dumped[16]; static int nd = 0; int was = 0;
            for (int k = 0; k < nd; k++) if (dumped[k] == bw) was = 1;
            if (!was && nd < 16) {
                dumped[nd++] = bw;
                char line[1200]; int L = 0;
                for (UINT q = 0; q < bw / 4 && q < 96; q++) {
                    L += snprintf(line + L, sizeof(line) - L, "%s%.4g", (q % 16) ? " " : "", (double)buf[q]);
                    if (q % 16 == 15 || q + 1 == bw / 4 || q == 95) { vr_log("   [%u..] %s", q - (q % 16), line); L = 0; }
                }
            }
        }
    }
}
static PFN_Map       g_orig_Map = NULL;
static PFN_Unmap     g_orig_Unmap = NULL;
static PFN_UpdateSub g_orig_UpdateSub = NULL;
static PFN_Present   g_orig_Present = NULL;


/* (E7c) Con la camara moviendose, busca en el cajon trozos (4 numeros seguidos) de la camara del fotograma
 * ANTERIOR que ya no coinciden con la actual. Si el juego guarda eso, lo usa para mezclar con el fotograma
 * anterior (la "estela"/fantasma), y habra que corregirlo tambien. */
static void prev_detective(void *res, const void *data, UINT bw)
{
    static struct { UINT bw; int idx; int what; } seen[48]; static int nseen = 0;
    if (g_pv_have < 2 || bw < 16 || bw > 8192 || GetTickCount64() > g_pv_until || nseen >= 46 || res == g_cam_buf) return;
    /* objetivos: filas y columnas de V (0..11), iV (12..23), VP (56..71) e iVP (72..87) del fotograma anterior */
    static const int base[4] = { 0, 12, 56, 72 }; static const char *bn[4] = { "V", "iV", "VP", "iVP" };
    float tg[24][4]; int tw[24], nt = 0;
    for (int m = 0; m < 4; m++) {
        int rows = (m < 2) ? 3 : 4;
        for (int r = 0; r < 4; r++) {
            float row[4], col[4], crow[4], ccol[4]; int ok = 1;
            for (int c = 0; c < 4; c++) {
                row[c]  = g_pv_prev[base[m] + r * 4 + c];   crow[c] = g_pv_cur[base[m] + r * 4 + c];
                col[c]  = (c < rows) ? g_pv_prev[base[m] + c * 4 + r] : 0; ccol[c] = (c < rows) ? g_pv_cur[base[m] + c * 4 + r] : 0;
            }
            if (r < rows) {
                memcpy(tg[nt], row, sizeof(row)); tw[nt++] = m * 16 + r; (void)crow;
            }
            if (rows == 4) {
                memcpy(tg[nt], col, sizeof(col)); tw[nt++] = m * 16 + 8 + r; (void)ccol;
            }
            (void)ok;
        }
    }
    if (!nt) return;
    static float buf[2048]; SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), data, buf, bw, &got) || got != bw) return;
    for (UINT i = 0; i + 4 <= bw / 4; i++) {
        if (!isfinite(buf[i]) || fabsf(buf[i]) < 1e-3f) continue;
        for (int t = 0; t < nt; t++) {
            int hit = 1;
            for (int c = 0; c < 4 && hit; c++) if (!(fabsf(buf[i + c] - tg[t][c]) <= 1e-4f + 1e-4f * fabsf(tg[t][c]))) hit = 0;
            if (!hit) continue;
            int dup = 0;
            for (int k = 0; k < nseen; k++) if (seen[k].bw == bw && seen[k].idx == (int)i) dup = 1;
            if (dup) continue;
            seen[nseen].bw = bw; seen[nseen].idx = i; seen[nseen].what = tw[t]; nseen++;
            int m = tw[t] / 16, rr = tw[t] % 16;
            vr_log("ANTERIOR: cajon ancho=%u (VS hueco %d, PS hueco %d, CS hueco %d) casilla %u = %s %s %d del fotograma ANTERIOR",
                   bw, slot_of(res), slot_in(g_ps_bound, res), slot_in(g_cs_bound, res), i, bn[m], rr < 8 ? "fila" : "columna", rr % 8);
            static UINT dumped[12]; static int nd = 0; int was = 0;
            for (int k = 0; k < nd; k++) if (dumped[k] == bw) was = 1;
            if (!was && nd < 12) {
                dumped[nd++] = bw;
                char line[1200]; int L = 0;
                for (UINT q = 0; q < bw / 4 && q < 96; q++) {
                    L += snprintf(line + L, sizeof(line) - L, "%s%.5g", (q % 16) ? " " : "", (double)buf[q]);
                    if (q % 16 == 15 || q + 1 == bw / 4 || q == 95) { vr_log("   [%u..] %s", q - (q % 16), line); L = 0; }
                }
                char l2[1200]; L = 0;
                for (int q = 0; q < 88; q++) {
                    L += snprintf(l2 + L, sizeof(l2) - L, "%s%.5g", (q % 16) ? " " : "", (double)g_pv_prev[q]);
                    if (q % 16 == 15 || q == 87) { vr_log("   camara ANTERIOR [%d..] %s", q - (q % 16), l2); L = 0; }
                }
            }
            break;
        }
    }
}

static HRESULT STDMETHODCALLTYPE hk_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *m)
{
    HRESULT hr = g_orig_Map(c, r, sub, type, flags, m);
    InterlockedIncrement(&g_maps);
    if (cfg_read && SUCCEEDED(hr) && m && m->pData &&
        (type == D3D11_MAP_WRITE || type == D3D11_MAP_WRITE_DISCARD ||
         type == D3D11_MAP_WRITE_NO_OVERWRITE)) {
        UINT bw;
        if (cb_buffer_info(r, &bw)) {
            InterlockedIncrement(&g_cbmaps);
            /* (E24) BORRADOR. Los cajones de los objetos estan en memoria de la grafica, lentisima de LEER (~9 us por
             * objeto). Al juego se le da un borrador en memoria normal; en el Unmap se corrige ahi y se copia de golpe
             * (escribir alli si es rapido). Solo cajones de objeto (208), luces (144, 272), con DISCARD y en modo VR. */
            if (cfg_shadow && g_mode == 1 && type == D3D11_MAP_WRITE_DISCARD && (bw == 208 || bw == 144 || bw == 272 || bw == CAM_BYTES)) {   /* (E26) + la camara (352) */
                static __thread __attribute__((aligned(64))) float shadow[16][96];
                static __thread int si = 0;
                float *sh = shadow[si]; si = (si + 1) & 15;
                if (pend_add(r, sh, bw, (UINT)type)) {   /* solo si queda apuntado: si no, nadie copiaria el borrador */
                    pend_set_real(r, m->pData);
                    m->pData = sh;
                    InterlockedIncrement(&g_shadow_n);
                }
            } else
                pend_add(r, m->pData, bw, (UINT)type);
        }
    }
    return hr;
}

/* Que hueco del shader de vertices tiene este cajon? Contamos escrituras, tomamos muestras de
 * contenido y, en los modos 6-12, lo ponemos a cero para ver que desaparece en pantalla. */
static int slot_of(void *res)
{
    for (int i = 0; i < NSLOT; i++) if (g_vs_bound[i] == res) return i;
    return -1;
}

static void dump_floats(int slot, const char *tag, const void *src, UINT bw)
{
    float f[32]; SIZE_T got = 0;
    UINT n = bw < sizeof(f) ? bw : (UINT)sizeof(f);
    if (ReadProcessMemory(GetCurrentProcess(), src, f, n, &got) && got == n) {
        char line[1100];
        int k = snprintf(line, sizeof(line), "  [hueco %d %s ancho=%u]", slot, tag, bw);
        for (UINT i = 0; i < n / 4 && k < (int)sizeof(line) - 24; i++)
            k += snprintf(line + k, sizeof(line) - k, " %.3g", (double)f[i]);
        vr_log("%s", line);
    }
}

/* Que hueco del shader de vertices tiene este cajon? Contamos escrituras y tomamos muestras. */
/* Hueco 7 (144 bytes): [ P | P*algo (reproyeccion) | 0.5 -0.5 0 0 ]. Se cambia la lente por la del visor
 * (y el giro H), igual que en la ficha de la camara:  P' = Pv*H ;  M2' = P' * P^-1 * M2 */
static int fix_slot7(float *f)
{
    float P[4][4], M2[4][4], iP[4][4], Pv[4][4], H[4][4], Hi[4][4], P2[4][4], T[4][4], M2n[4][4];
    memcpy(P, f, sizeof(P)); memcpy(M2, f + 16, sizeof(M2));
    for (int i = 0; i < 32; i++) if (!isfinite(f[i])) return 0;
    if (fabsf(P[0][0] - g_orig_P00) > 1e-3f || fabsf(P[1][1] - g_orig_P11) > 1e-3f) return 0;   /* solo la camara principal */
    if (fabsf(P[3][2] - 1.0f) > 1e-4f || fabsf(P[3][3]) > 1e-4f) return 0;
    if (!inv4(P, iP)) return 0;
    memcpy(Pv, P, sizeof(Pv));
    fov_xy(Pv);
    get_H(H, Hi);
    mul4(iP, M2, T);
    if (0 && cfg_porobjeto && raton_active()) {
        /* (E7i, apagado en la E8e) las luces tambien con SU camara (T = P^-1 * VP = V), igual que cada dibujo */
        float Vr[12]; int ok = 1;
        for (int i = 0; i < 3; i++) {
            float l = T[i][0] * T[i][0] + T[i][1] * T[i][1] + T[i][2] * T[i][2];
            if (!(fabsf(l - 1.0f) < 0.02f)) ok = 0;
            for (int j = 0; j < 4; j++) Vr[i * 4 + j] = T[i][j];
        }
        if (ok && view_H(Vr, H, Hi)) InterlockedIncrement(&g_l7own);
    }
    mul4(Pv, H, P2);
    if (cfg_modoh == 1 && !cfg_porobjeto && raton_active() && g_fs_have) {
        float Pp[4][4], P3[4][4];
        memcpy(Pp, P, sizeof(Pp));
        for (int j = 0; j < 4; j++) { Pp[0][j] = g_fs_prevL[0][j]; Pp[1][j] = g_fs_prevL[1][j]; }
        /* T = V_ant * V_act^-1 (de "vista de ahora" a "vista de antes"). Nuestra vista de ahora es H_act*V_act y la
         * imagen de antes se hizo con Plente_ant*H_ant*V_ant, asi que: M2' = Plente_ant * H_ant * T * H_act^-1 */
        float P4[4][4];
        mul4(Pp, g_fs_prevH, P3);
        mul4(P3, T, P4);
        mul4(P4, Hi, M2n);
        InterlockedIncrement(&g_l7prev);
    } else
    mul4(P2, T, M2n);
    memcpy(f, P2, sizeof(P2)); memcpy(f + 16, M2n, sizeof(M2n));
    return 1;
}

/* Hueco 3 (272 bytes): en las casillas 36..43 va la "receta" para sacar la direccion de cada pixel
 * (u,v de 0 a 1) en el espacio de la camara:  x/z = (2/P00) u - 1/P00 ;  y/z = -(2/P11) v + 1/P11.
 * Con la lente del visor (descentrada):        x/z = (2/P00') u - (1+P02')/P00' ;  y/z = -(2/P11') v + (1-P12')/P11' */
static int fix_slot3(float *f)
{
    if (!isfinite(f[36]) || !isfinite(f[41])) return 0;
    if (fabsf(f[36] - 2.0f / g_orig_P00) > 1e-3f || fabsf(f[41] + 2.0f / g_orig_P11) > 1e-3f) return 0;
    if (fabsf(f[39] + 1.0f / g_orig_P00) > 1e-3f || fabsf(f[43] - 1.0f / g_orig_P11) > 1e-3f) return 0;
    float Pv[4][4] = {{g_orig_P00,0,0,0},{0,g_orig_P11,0,0},{0,0,1,0},{0,0,1,0}};
    fov_xy(Pv);
    float a = Pv[0][0], b = Pv[0][2], c = Pv[1][1], d = Pv[1][2];
    if (fabsf(a) < 1e-3f || fabsf(c) < 1e-3f) return 0;
    f[36] = 2.0f / a;  f[37] = 0; f[38] = 0; f[39] = -(1.0f + b) / a;
    f[40] = 0; f[41] = -2.0f / c; f[42] = 0; f[43] = (1.0f - d) / c;
    return 1;
}


/* (E7e) Huecos 8 (3856 bytes) y 13 (3888): una cabecera y luego 80 matrices de 3x4. Si son "objeto -> vista"
 * (barandillas, carteles, objetos sueltos dibujados en grupo), el juego las calcula con SU camara y nosotros
 * no las corregiamos: por eso esos objetos se movian un poco al mover la cabeza. Aqui se les aplica H. */
static void inst_stuff(int slot, const Pending *p)
{
    if (!g_inst_fix && g_inst_dump <= 0) return;
    UINT head = (p->bw == 3856) ? 4 : 8;              /* floats de cabecera */
    UINT n = (p->bw / 4 - head) / 12;
    static float f[1024]; SIZE_T got = 0;
    if (p->bw > sizeof(f) || !ReadProcessMemory(GetCurrentProcess(), p->p, f, p->bw, &got) || got != p->bw) return;
    if (g_inst_dump > 0 && InterlockedDecrement(&g_inst_dump) >= 0) {
        char line[1200]; int L = 0;
        for (UINT q = 0; q < head + 36 && q < p->bw / 4; q++) {
            L += snprintf(line + L, sizeof(line) - L, "%s%.5g", (q % 12) ? " " : "", (double)f[q]);
            if (q % 12 == 11 || q + 1 == head + 36) { vr_log("   hueco %d [%u..] %s", slot, q - (q % 12), line); L = 0; }
        }
        vr_log("   camara V: %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g",
               g_pv_cur[0], g_pv_cur[1], g_pv_cur[2], g_pv_cur[3], g_pv_cur[4], g_pv_cur[5], g_pv_cur[6], g_pv_cur[7],
               g_pv_cur[8], g_pv_cur[9], g_pv_cur[10], g_pv_cur[11]);
    }
    if (!g_inst_fix || g_mode != 1) return;
    float H[4][4], Hi[4][4];
    get_H(H, Hi);
    int any = 0;
    for (UINT k = 0; k < n; k++) {
        float *m = f + head + k * 12, o[12]; int ok = 1, nz = 0;
        for (int q = 0; q < 12; q++) { if (!isfinite(m[q])) ok = 0; if (m[q] != 0.0f) nz = 1; }
        if (!ok || !nz) continue;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++)
            o[r * 4 + c] = H[r][0] * m[c] + H[r][1] * m[4 + c] + H[r][2] * m[8 + c] + (c == 3 ? H[r][3] : 0.0f);
        memcpy(m, o, sizeof(o)); any = 1;
    }
    if (any) { memcpy((void *)p->p, f, p->bw); _mm_sfence(); InterlockedIncrement(&g_inst_mod); }
}


/* (E8g) Hueco 7 = [P | M2]. M2 puede ser P*V (la camara de AHORA, para cosas en coordenadas del mundo)
 * o P*T con T = V_antes * V_ahora^-1 (para reaprovechar la imagen del fotograma ANTERIOR). Las posiciones que
 * llegan son las del juego (vista del juego, sin H), asi que:  P' = Lente_ahora*H_ahora ;  P*V -> Lente_ahora*H_ahora*V ;
 * P*T -> Lente_antes*H_antes*T   (H_antes = el que se uso para dibujar la imagen anterior) */
/* (E27) H y lente de "ahora" y "de antes" para el ojo que se esta calculando. Lo guardado es del ojo izquierdo;
 * para el derecho (copia) se aparta la diferencia entre ojos y se pone su lente. */
static void oc_eye(float H[4][4], float Hi[4][4], float L[2][4], float sH[4][4], float sHi[4][4], float sL[2][4])
{
    memcpy(H, sH, 64); memcpy(Hi, sHi, 64); memcpy(L, sL, 32);
    if (t_eye != 1 || g_twin_on != 2) return;
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = (g_eyepos[1][i] - g_eyepos[0][i]) * cfg_ipdscale;
    for (int r = 0; r < 3; r++) { H[r][3] -= d[r]; Hi[r][3] += Hi[r][0] * d[0] + Hi[r][1] * d[1] + Hi[r][2] * d[2]; }
    for (int j = 0; j < 4; j++) { L[0][j] = g_eyerow[1][0][j]; L[1][j] = g_eyerow[1][1][j]; }
}
static int fix_prev_s7(float *f)
{
    float ocH[4][4], ocHi[4][4], ocL[2][4], opH[4][4], opHi[4][4], opL[2][4];
    oc_eye(ocH, ocHi, ocL, g_oc_H, g_oc_Hi, g_oc_L); oc_eye(opH, opHi, opL, g_op_H, g_op_Hi, g_op_L);
    float P[4][4], M2[4][4], iP[4][4], T[4][4], Pc[4][4], Pp[4][4], A[4][4], B[4][4], C[4][4];
    for (int i = 0; i < 32; i++) if (!isfinite(f[i])) return 0;
    memcpy(P, f, sizeof(P)); memcpy(M2, f + 16, sizeof(M2));
    if (fabsf(P[0][0] - g_orig_P00) > 1e-3f || fabsf(P[1][1] - g_orig_P11) > 1e-3f) return 0;
    if (fabsf(P[3][2] - 1.0f) > 1e-4f || fabsf(P[3][3]) > 1e-4f) return 0;
    if (!inv4(P, iP)) return 0;
    mul4(iP, M2, T);
    memcpy(Pc, P, sizeof(P)); memcpy(Pp, P, sizeof(P));
    for (int j = 0; j < 4; j++) { Pc[0][j] = ocL[0][j]; Pc[1][j] = ocL[1][j]; Pp[0][j] = opL[0][j]; Pp[1][j] = opL[1][j]; }
    mul4(Pc, ocH, A);
    int vp = 1;
    for (int i = 0; i < 3 && vp; i++) for (int j = 0; j < 4; j++)
        if (fabsf(T[i][j] - g_rt_lastcam[i * 4 + j]) > 2e-3f * (1.0f + fabsf(g_rt_lastcam[i * 4 + j]))) { vp = 0; break; }
    if (vp) { mul4(A, T, B); InterlockedIncrement(&g_s7vp); }
    else    { mul4(Pp, opH, C); mul4(C, T, B); InterlockedIncrement(&g_s7T); }
    memcpy(f, A, sizeof(A)); memcpy(f + 16, B, sizeof(B));
    return 1;
}
/* (E8g) Hueco 3, casillas 36..51 (una matriz 4x4 que cambia segun la pasada):
 *  - "rayos" (de la pantalla a la vista): con la lente de ahora y pasados a la vista del juego (H_ahora^-1)
 *  - P del juego: -> Lente_ahora*H_ahora
 *  - REFLEJOS (S*T, sin traslacion): miran la imagen borrosa del fotograma ANTERIOR. Su lente y su H son los de antes:
 *       S*T -> S*Lj^-1 * Lente_antes * H_antes * T          (este era el "fantasma") */
static int fix_prev_s3(float *f)
{
    float ocH[4][4], ocHi[4][4], ocL[2][4], opH[4][4], opHi[4][4], opL[2][4];
    oc_eye(ocH, ocHi, ocL, g_oc_H, g_oc_Hi, g_oc_L); oc_eye(opH, opHi, opL, g_op_H, g_op_Hi, g_op_L);
    float *m = f + 36;
    for (int i = 0; i < 16; i++) if (!isfinite(m[i])) return 0;
    float p00 = g_orig_P00, p11 = g_orig_P11;
    if (p00 <= 0 || p11 <= 0) return 0;
    if (fabsf(m[0] - 2.0f / p00) < 1e-3f && fabsf(m[5] + 2.0f / p11) < 1e-3f &&
        fabsf(m[3] + 1.0f / p00) < 1e-3f && fabsf(m[7] - 1.0f / p11) < 1e-3f) {
        float a = ocL[0][0], b = ocL[0][2], c = ocL[1][1], d = ocL[1][2], R[4][4], O[4][4];
        if (fabsf(a) < 1e-3f || fabsf(c) < 1e-3f) return 0;
        memcpy(R, m, sizeof(R));
        R[0][0] = 2.0f / a; R[0][1] = 0; R[0][2] = 0; R[0][3] = -(1.0f + b) / a;
        R[1][0] = 0; R[1][1] = -2.0f / c; R[1][2] = 0; R[1][3] = (1.0f - d) / c;
        mul4(ocHi, R, O);
        memcpy(m, O, sizeof(O));
        InterlockedIncrement(&g_s3rays);
        return 1;
    }
    if (fabsf(m[0] - p00) < 1e-4f && fabsf(m[5] - p11) < 1e-4f && fabsf(m[10] - g_orig_P22) < 1e-3f &&
        fabsf(m[11] - g_orig_P23) < 1e-3f && fabsf(m[14] - 1.0f) < 1e-4f && fabsf(m[15]) < 1e-6f &&
        fabsf(m[1]) < 1e-6f && fabsf(m[2]) < 1e-6f && fabsf(m[4]) < 1e-6f && fabsf(m[6]) < 1e-6f) {
        float Pc[4][4], O[4][4];
        memcpy(Pc, m, sizeof(Pc));
        for (int j = 0; j < 4; j++) { Pc[0][j] = ocL[0][j]; Pc[1][j] = ocL[1][j]; }
        mul4(Pc, ocH, O);
        memcpy(m, O, sizeof(O));
        InterlockedIncrement(&g_s3P);
        return 1;
    }
    if (fabsf(m[3]) < 1e-6f && fabsf(m[7]) < 1e-6f && fabsf(m[11]) < 1e-6f && fabsf(m[12]) < 1e-6f &&
        fabsf(m[13]) < 1e-6f && fabsf(m[14]) < 1e-6f && fabsf(m[15] - 1.0f) < 1e-6f) {
        float s0 = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        float s1 = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        float s2 = sqrtf(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
        if (fabsf(s2 - 1.0f) > 0.01f || s0 < 0.1f || s1 < 0.1f) return 0;
        float X[3][3], L3[3][3], Hr[3][3], K0 = s0 / p00, K1 = s1 / p11, Q[3][3], Q2[3][3];
        for (int j = 0; j < 3; j++) { X[0][j] = m[j] / s0; X[1][j] = m[4 + j] / s1; X[2][j] = m[8 + j]; }
        for (int j = 0; j < 3; j++) { L3[0][j] = opL[0][j]; L3[1][j] = opL[1][j]; L3[2][j] = (j == 2) ? 1.0f : 0.0f; }
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Hr[i][j] = opH[i][j];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float a = 0; for (int k = 0; k < 3; k++) a += Hr[i][k] * X[k][j]; Q[i][j] = a; }
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float a = 0; for (int k = 0; k < 3; k++) a += L3[i][k] * Q[k][j]; Q2[i][j] = a; }
        for (int j = 0; j < 3; j++) { m[j] = K0 * Q2[0][j]; m[4 + j] = K1 * Q2[1][j]; m[8 + j] = Q2[2][j]; }
        InterlockedIncrement(&g_s3refl);
        return 1;
    }
    return 0;
}

static void slot_stuff(void *res, const Pending *p)
{
    int slot = slot_of(res);
    if (slot < 0) return;
    if (g_mode == 1 && cfg_previo && cfg_porobjeto && cfg_modoh == 0 && raton_active() && g_oc_have && g_orig_P00 > 0) {
        if (slot == 7 && p->bw == 144) {
            float f[36]; SIZE_T got = 0;
            (void)got; if (cb_read(p->p, f, 144) && fix_prev_s7(f)) {
                memcpy((void *)p->p, f, 128); _mm_sfence(); InterlockedIncrement(&g_l7);
            }
        } else if (slot == 3 && p->bw == 272) {
            float f[68]; SIZE_T got = 0;
            (void)got; if (cb_read(p->p, f, 272) && fix_prev_s3(f)) {
                memcpy((char *)p->p + 36 * 4, f + 36, 16 * 4); _mm_sfence(); InterlockedIncrement(&g_l3);
            }
        }
    } else
    if (g_mode == 1 && cfg_lights && cfg_fov && g_fov_ok && g_orig_P00 > 0) {
        if (slot == 7 && p->bw == 144) {
            float f[36]; SIZE_T got = 0;
            (void)got; if (cb_read(p->p, f, 144) && fix_slot7(f)) {
                memcpy((void *)p->p, f, 128); _mm_sfence(); InterlockedIncrement(&g_l7);
            }
        } else if (slot == 3 && p->bw == 272) {
            float f[68]; SIZE_T got = 0;
            (void)got; if (cb_read(p->p, f, 272) && fix_slot3(f)) {
                memcpy((char *)p->p + 36 * 4, f + 36, 8 * 4); _mm_sfence(); InterlockedIncrement(&g_l3);
            }
        }
    }
    InterlockedIncrement(&g_slot_writes[slot]);
    g_slot_bw[slot] = (LONG)p->bw;
    if ((slot == 8 && p->bw == 3856) || (slot == 13 && p->bw == 3888)) inst_stuff(slot, p);
    {
        LONG mode = g_mode;
        if (slot == 0 && p->bw == 208) InterlockedIncrement(g_ord_flag ? &g_ord_after : &g_ord_before);
        if (mode == 1 && slot == 0 && p->bw == 208) {
            float f[52], Hc[4][4], Hic[4][4];
            LARGE_INTEGER c0, c1; QueryPerformanceCounter(&c0);
            get_H(Hc, Hic);
            if (cb_read(p->p, f, 208)) {
                if (apply_cb0_yaw(f, Hc)) {
                    memcpy((char *)p->p + 144, f + 36, 64);   /* solo m_WVP */
                    _mm_sfence();
                    InterlockedIncrement(&g_cb0_mod);
                } else {
                    InterlockedIncrement(&g_cb0_skip);
                }
            }
            QueryPerformanceCounter(&c1);
            InterlockedExchangeAdd(&g_prof_cb0_us, (LONG)((c1.QuadPart - c0.QuadPart) * 1000000 / g_qpf));
            InterlockedIncrement(&g_prof_cb0_n);
        }
    }
    if (slot != 1 && !t_quiet && g_samples_left[slot] > 0) {
        InterlockedDecrement(&g_samples_left[slot]);
        dump_floats(slot, "map", p->p, p->bw);
    }
}

static void tw_cb_upload(ID3D11DeviceContext *c, ID3D11Resource *r, const void *data, UINT bw);
static void tw_cb_invalidate(ID3D11Resource *r);
static void STDMETHODCALLTYPE hk_Unmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub)
{
    Pending p;
    if (cfg_read && pend_take(r, &p)) {
        LARGE_INTEGER pq0, pq1; QueryPerformanceCounter(&pq0);
        /* (E27) dos ojos: se guarda lo que escribio el juego (antes de tocarlo) para hacer luego la version del ojo derecho */
        __attribute__((aligned(16))) float tw_raw[96];
        int tw_do = g_twin_on == 2 && p.real && p.bw <= sizeof(tw_raw);
        if (tw_do) memcpy(tw_raw, p.p, p.bw);
        if (p.bw == CAM_BYTES)   /* (E16) la camara va en su cajon de 352; antes se miraban tambien los de huesos (3856) en cada dibujo */
            check_camera(p.p, (void *)p.p, r, p.bw, p.type, "Map");   /* leer/cambiar ANTES de soltar */
        slot_stuff(r, &p);
        if (cfg_detective && g_mode == 1) lens_detective(r, p.p, p.bw);
        prev_detective(r, p.p, p.bw);
        if (p.real) memcpy(p.real, p.p, p.bw);   /* (E24) el borrador ya corregido, de golpe a la grafica */
        if (tw_do) {   /* (E27) lo mismo para el ojo derecho, en su copia del cajon */
            Pending q = p; q.p = tw_raw; q.real = NULL;
            t_eye = 1; t_quiet = 1;
            if (q.bw == CAM_BYTES) check_camera(tw_raw, tw_raw, r, q.bw, q.type, "Map");
            slot_stuff(r, &q);
            t_quiet = 0; t_eye = -1;
            tw_cb_upload(c, r, tw_raw, q.bw);
        } else if (g_twin_on == 2 && (p.bw == 208 || p.bw == 144 || p.bw == 272 || p.bw == CAM_BYTES)) tw_cb_invalidate(r);
        QueryPerformanceCounter(&pq1);
        InterlockedExchangeAdd(&g_prof_mod_us, (LONG)((pq1.QuadPart - pq0.QuadPart) * 1000000 / g_qpf));
    }
    g_orig_Unmap(c, r, sub);
}

static volatile LONG g_upd_null = 0;
static void upd_core(PFN_UpdateSub orig, ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                     const D3D11_BOX *box, const void *src, UINT rp, UINT dp)
{
    InterlockedIncrement(&g_updsub);
    if (!r) {   /* (E20c) el juego manda rellenar una textura que no existe (su creacion fallo): Windows petaba aqui */
        if (InterlockedIncrement(&g_upd_null) <= 10) vr_log("FRENO: UpdateSubresource sin textura (la creacion fallo antes): me la salto para que no pete");
        return;
    }
    UINT bw;
    if (cfg_read && src && cb_buffer_info(r, &bw)) {
        if (g_twin_on == 2) tw_cb_invalidate(r);   /* (E27) su copia del ojo derecho ya no vale: se usa el del juego */
        if (!box && bw == CAM_BYTES)
            check_camera(src, NULL, r, bw, 0, "UpdateSubresource");
        if (!box && cfg_detective && g_mode == 1) lens_detective(r, src, bw);
        if (!box) prev_detective(r, src, bw);
        int slot = slot_of(r);
        if (slot >= 0) {
            InterlockedIncrement(&g_slot_upd[slot]);
            if (!box && g_uslot_samples[slot] > 0) {
                InterlockedDecrement(&g_uslot_samples[slot]);
                dump_floats(slot, "upd", src, bw);
            }
        }
    }
    orig(c, r, sub, box, src, rp, dp);
}
static void STDMETHODCALLTYPE hk_UpdateSub(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                           const D3D11_BOX *box, const void *src, UINT rp, UINT dp)
{ upd_core(g_orig_UpdateSub, c, r, sub, box, src, rp, dp); }

typedef void (STDMETHODCALLTYPE *PFN_CopyRes)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
typedef void (STDMETHODCALLTYPE *PFN_CopySub)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT,
                                              ID3D11Resource*, UINT, const D3D11_BOX*);
static PFN_CopyRes g_orig_CopyRes = NULL;
static PFN_CopySub g_orig_CopySub = NULL;

static void STDMETHODCALLTYPE hk_CopyRes(ID3D11DeviceContext *c, ID3D11Resource *dst, ID3D11Resource *src)
{
    int slot = slot_of(dst);
    if (slot >= 0) InterlockedIncrement(&g_slot_copy[slot]);
    g_orig_CopyRes(c, dst, src);
}

static void STDMETHODCALLTYPE hk_CopySub(ID3D11DeviceContext *c, ID3D11Resource *dst, UINT ds, UINT dx, UINT dy,
                                         UINT dz, ID3D11Resource *src, UINT ss, const D3D11_BOX *box)
{
    int slot = slot_of(dst);
    if (slot >= 0) InterlockedIncrement(&g_slot_copy[slot]);
    g_orig_CopySub(c, dst, ds, dx, dy, dz, src, ss, box);
}

/* Cajon "de mentira" lleno de ceros: UNO por cada tamano (sirve para cualquier cajon de ese tamano). */
typedef struct { UINT bw; ID3D11Buffer *zero; } ZeroEntry;
static ZeroEntry g_zero[32];
static int g_nzero = 0;
static unsigned char g_zeros[65536];

static int is_zero_buf(ID3D11Buffer *b)
{
    int r = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_nzero; i++) if (g_zero[i].zero == b) { r = 1; break; }
    LeaveCriticalSection(&g_cs);
    return r;
}

static ID3D11Buffer *get_zero_buffer(ID3D11DeviceContext *c, ID3D11Buffer *orig)
{
    D3D11_BUFFER_DESC d;
    ID3D11Buffer_GetDesc(orig, &d);
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER) || d.ByteWidth == 0 || d.ByteWidth > 65536) return NULL;

    ID3D11Buffer *res = NULL;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_nzero; i++)
        if (g_zero[i].bw == d.ByteWidth) { res = g_zero[i].zero; LeaveCriticalSection(&g_cs); return res; }
    if (g_nzero < 32) {
        D3D11_BUFFER_DESC nd;
        memset(&nd, 0, sizeof(nd));
        nd.ByteWidth = d.ByteWidth;
        nd.Usage = D3D11_USAGE_IMMUTABLE;
        nd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd;
        sd.pSysMem = g_zeros; sd.SysMemPitch = 0; sd.SysMemSlicePitch = 0;
        ID3D11Device *dev = NULL;
        ID3D11DeviceContext_GetDevice(c, &dev);
        if (dev) {
            HRESULT hr = ID3D11Device_CreateBuffer(dev, &nd, &sd, &res);
            ID3D11Device_Release(dev);
            if (FAILED(hr)) res = NULL;
            vr_log("cajon de ceros de %u bytes: %s", d.ByteWidth, res ? "creado" : "FALLO");
        }
        g_zero[g_nzero].bw = d.ByteWidth; g_zero[g_nzero].zero = res; g_nzero++;
    }
    LeaveCriticalSection(&g_cs);
    return res;
}

static void STDMETHODCALLTYPE hk_VSSetCB(ID3D11DeviceContext *c, UINT start, UINT num,
                                         ID3D11Buffer *const *bufs)
{
    if (bufs) {
        for (UINT i = 0; i < num && start + i < NSLOT; i++) {
            UINT sl = start + i;
            ID3D11Buffer *b = bufs[i];
            if (g_vs_bound[sl] != (void *)b && b && g_ndesclog < 60 && !is_zero_buf(b)) {
                InterlockedIncrement(&g_ndesclog);
                D3D11_BUFFER_DESC d;
                ID3D11Buffer_GetDesc(b, &d);
                g_slot_desc_bw[sl] = (LONG)d.ByteWidth; g_slot_desc_usage[sl] = (LONG)d.Usage;
                vr_log("hueco %u: nuevo cajon %p (ancho=%u uso=%u bind=0x%X cpu=0x%X)",
                       sl, (void *)b, d.ByteWidth, (unsigned)d.Usage, d.BindFlags, d.CPUAccessFlags);
            }
            g_vs_bound[sl] = (void *)b;
            InterlockedIncrement(&g_slot_binds[sl]);
            if (sl == 1 && b) {
                if ((void *)b == g_cam_buf) InterlockedIncrement(&g_vs1_cam);
                else InterlockedIncrement(&g_vs1_other);
            }
        }
    }

    /* Modos 1 y 2: en vez del cajon real, le damos al shader uno lleno de ceros. */
    ID3D11Buffer *tmp[NSLOT];
    ID3D11Buffer *const *use = bufs;
    LONG mode = 0;   /* el silenciado de cajones esta desactivado en esta version */
    if (bufs && (mode == 1 || mode == 2) && num <= NSLOT) {
        int changed = 0;
        for (UINT i = 0; i < num; i++) {
            UINT sl = start + i;
            tmp[i] = bufs[i];
            int mute = (mode == 1) ? (sl < 32 && ((g_mute_mask >> sl) & 1u)) : (sl != 1);
            if (mute && bufs[i] && !is_zero_buf(bufs[i])) {
                ID3D11Buffer *z = get_zero_buffer(c, bufs[i]);
                if (z) { tmp[i] = z; changed = 1; }
            }
        }
        if (changed) { use = tmp; InterlockedIncrement(&g_zeroed); }
    }
    g_orig_VSSetCB(c, start, num, use);
}

/* El Present "de fabrica" de dxgi.dll, sacado del archivo en disco (asi no dependemos de quien
 * mas se haya enganchado: la Steam Overlay y nosotros nos pasabamos la pelota en bucle). */
static PFN_Present g_real_Present = NULL;
static __thread int t_in_present = 0;

__attribute__((unused)) static void find_real_present(void **slot)
{
    HMODULE dx = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)slot, &dx) || !dx) { vr_log("Present: la tabla no esta en ninguna dll"); return; }
    char path[MAX_PATH];
    if (!GetModuleFileNameA(dx, path, MAX_PATH)) return;
    const char *b = strrchr(path, '\\');
    if (_stricmp(b ? b + 1 : path, "dxgi.dll") != 0) { vr_log("Present: la tabla esta en %s, no en dxgi.dll", b ? b + 1 : path); return; }
    /* copia limpia de dxgi.dll, solo como datos (no se ejecuta nada) */
    HMODULE img = LoadLibraryExA(path, NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (!img) { vr_log("Present: no puedo abrir una copia limpia de dxgi.dll"); return; }
    const unsigned char *clean = (const unsigned char *)((ULONG_PTR)img & ~(ULONG_PTR)3);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)clean;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(clean + dos->e_lfanew);
    ULONG_PTR rva = (ULONG_PTR)slot - (ULONG_PTR)dx;
    if (nt->Signature != IMAGE_NT_SIGNATURE || rva >= nt->OptionalHeader.SizeOfImage) { FreeLibrary(img); return; }
    ULONGLONG v = *(const ULONGLONG *)(clean + rva);                  /* direccion "de fabrica" sin recolocar */
    ULONGLONG real = (ULONGLONG)(ULONG_PTR)dx + (v - nt->OptionalHeader.ImageBase);
    ULONG_PTR size = nt->OptionalHeader.SizeOfImage;
    FreeLibrary(img);
    if (real < (ULONGLONG)(ULONG_PTR)dx || real >= (ULONGLONG)(ULONG_PTR)dx + size) { vr_log("Present: direccion de fabrica rara"); return; }
    g_real_Present = (PFN_Present)(ULONG_PTR)real;
    unsigned long long off; char mb[MAX_PATH];
    module_of((const void *)g_orig_Present, &off, mb);
    vr_log("Present: el de fabrica es dxgi.dll+0x%llX; antes de nosotros estaba %s+0x%llX%s",
           (unsigned long long)(real - (ULONGLONG)(ULONG_PTR)dx), mb, off,
           (void *)g_orig_Present == (void *)g_real_Present ? " (nadie mas enganchado)" : " (otro programa enganchado: lo saltamos)");
}

static void vr_submit_frame(IDXGISwapChain *sc);
static void tw_update_mode(IDXGISwapChain *sc);         /* (E27) */
static ID3D11Texture2D *tw_twin_tex(ID3D11Texture2D *t);
static volatile LONG g_tw_nobb;
static __thread int t_tw_raw;
static volatile LONG g_real_frames = 0;   /* fotogramas de verdad (sin los de prueba) */
static volatile LONG g_wait_us = 0;       /* tiempo esperando a SteamVR en el ultimo segundo (microsegundos) */
static void vr_after_present(void);
static int vr_active(void);

/* (E25) CHIVATO DE SALTITOS: tiempos de cada fotograma; si uno tarda demasiado se apunta en que se fue */
static volatile LONG g_fr_sub_us = 0, g_fr_wait_us = 0, g_fr_pres_us = 0;
static LONG g_fr_mod0 = 0;
static volatile LONG g_saltos = 0;
static HRESULT STDMETHODCALLTYPE hk_Present_in(IDXGISwapChain *sc, UINT sync, UINT flags);
static HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    if (t_in_present) return hk_Present_in(sc, sync, flags);
    static LARGE_INTEGER last_exit; static ULONGLONG said_t = 0; static int said_n = 0;
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    LONG mod_now = g_prof_mod_us, mod_frame = mod_now - g_fr_mod0; g_fr_mod0 = mod_now;
    LONG eye = g_eye;
    g_fr_sub_us = g_fr_wait_us = g_fr_pres_us = 0;
    HRESULT hr = hk_Present_in(sc, sync, flags);
    QueryPerformanceCounter(&t1);
    if (last_exit.QuadPart && g_qpf) {
        double game = (double)(t0.QuadPart - last_exit.QuadPart) * 1000.0 / g_qpf;
        double inside = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / g_qpf;
        double wait = g_fr_wait_us / 1000.0, work = game + inside - wait;
        double lim = g_twin_on == 2 ? 13.9 : 9.0;   /* (E35) con los dos ojos a la vez cada fotograma es una PAREJA: 13,9 ms */
        if (g_mode == 1 && vr_active() && work > lim) {
            InterlockedIncrement(&g_saltos);
            ULONGLONG now = GetTickCount64();
            if (now - said_t > 1000) { said_t = now; said_n = 0; }
            if (said_n++ < 3)
                vr_log("SALTITO: fotograma de %.1f ms (limite %.1f) sin contar la espera (ojo %ld): juego=%.1f (dentro, el mod con los objetos=%.1f) | mod_envio=%.1f present=%.1f espera_steamvr=%.1f resto=%.1f -> culpa: %s",
                       work, lim, (long)eye, game, mod_frame / 1000.0, g_fr_sub_us / 1000.0, g_fr_pres_us / 1000.0, wait,
                       inside - g_fr_sub_us / 1000.0 - g_fr_pres_us / 1000.0 - wait,
                       (game - mod_frame / 1000.0) >= lim ? "JUEGO" : (mod_frame / 1000.0 > 2.0 ? "juego + MOD" : "juego (justo al limite)"));
        }
    }
    last_exit = t1;
    return hr;
}
/* ===================== (E40) EXPORTAR EL ARMA A BLENDER =====================
 * Re Pag: durante unos fotogramas se apuntan las piezas que se pintan con la camara del arma (arma, cargador, manos) y
 * despues se leen sus vertices y se escribe %TEMP%\metro_vr_arma_<ic>_<si>.obj, con un objeto por pieza y por hueso.
 * Para leer los vertices hace falta saber como estan guardados: por eso se apunta como se crea cada "plantilla de
 * vertices" (CreateInputLayout, enganchado desde el principio). */
typedef struct { char sem[16]; UINT si, fmt, slot, off; } ExpElem;
typedef struct { void *lay; UINT n; ExpElem e[14]; } ExpLayout;
#define MAXLAY 1536
static ExpLayout g_lay[MAXLAY]; static int g_nlay = 0; static volatile LONG g_lay_lock = 0;
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateIL)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT, const void *, SIZE_T, ID3D11InputLayout **);
static PFN_CreateIL g_tr_CreateIL = NULL;
static UINT fmt_bytes(UINT f)
{
    if (f >= 1 && f <= 4) return 16;
    if (f >= 5 && f <= 8) return 12;
    if (f >= 9 && f <= 18) return 8;
    if (f >= 19 && f <= 47) return 4;
    if (f >= 48 && f <= 59) return 2;
    if (f >= 60 && f <= 65) return 1;
    if (f == 87 || f == 88) return 4;
    return 4;
}
static HRESULT STDMETHODCALLTYPE hk_CreateIL(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *el, UINT n, const void *bc, SIZE_T bl, ID3D11InputLayout **o)
{
    HRESULT hr = g_tr_CreateIL(d, el, n, bc, bl, o);
    if (SUCCEEDED(hr) && o && *o && el) {
        while (InterlockedCompareExchange(&g_lay_lock, 1, 0) != 0) YieldProcessor();
        int k = -1;
        for (int i = 0; i < g_nlay; i++) if (g_lay[i].lay == (void *)*o) { k = i; break; }
        if (k < 0 && g_nlay < MAXLAY) k = g_nlay++;
        if (k >= 0) {
            ExpLayout *L = &g_lay[k]; L->lay = (void *)*o; L->n = 0;
            UINT acc[16]; memset(acc, 0, sizeof(acc));
            for (UINT i = 0; i < n && L->n < 14; i++) {
                ExpElem *e = &L->e[L->n++];
                snprintf(e->sem, sizeof(e->sem), "%s", el[i].SemanticName ? el[i].SemanticName : "?");
                e->si = el[i].SemanticIndex; e->fmt = (UINT)el[i].Format; e->slot = el[i].InputSlot & 15;
                e->off = el[i].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? acc[e->slot] : el[i].AlignedByteOffset;
                acc[e->slot] = e->off + fmt_bytes(e->fmt);
            }
        }
        InterlockedExchange(&g_lay_lock, 0);
    }
    return hr;
}
static int lay_get(void *lay, ExpLayout *out)
{
    int ok = 0;
    while (InterlockedCompareExchange(&g_lay_lock, 1, 0) != 0) YieldProcessor();
    for (int i = 0; i < g_nlay; i++) if (g_lay[i].lay == lay) { *out = g_lay[i]; ok = 1; break; }
    InterlockedExchange(&g_lay_lock, 0);
    return ok;
}
typedef struct { UINT ic, si; INT bv; ID3D11Buffer *ib; DXGI_FORMAT ifmt; UINT ioff; ID3D11Buffer *vb[4]; UINT st[4], vo[4]; ID3D11InputLayout *il; int arm; UINT sinst; } ExpPiece;
#define MAXEXP 48
static ExpPiece g_exp[MAXEXP]; static int g_nexp = 0; static volatile LONG g_exp_lock = 0;
static volatile LONG g_exp_state = 0; static ULONGLONG g_exp_t = 0;
static void exp_capture(ID3D11DeviceContext *c, UINT ic, UINT si, INT bv, UINT sinst)
{
    if (!g_cb0_weapon || g_mode != 1 || ic < 3) return;
    while (InterlockedCompareExchange(&g_exp_lock, 1, 0) != 0) YieldProcessor();
    int dup = 0;
    for (int i = 0; i < g_nexp; i++) if (g_exp[i].ic == ic && g_exp[i].si == si) { dup = 1; break; }
    if (!dup && g_nexp < MAXEXP) {
        ExpPiece *p = &g_exp[g_nexp]; memset(p, 0, sizeof(*p));
        p->ic = ic; p->si = si; p->bv = bv; p->sinst = sinst;
        ID3D11DeviceContext_IAGetIndexBuffer(c, &p->ib, &p->ifmt, &p->ioff);
        ID3D11DeviceContext_IAGetVertexBuffers(c, 0, 4, p->vb, p->st, p->vo);
        ID3D11DeviceContext_IAGetInputLayout(c, &p->il);
        for (int k = 0; k < g_nhide; k++) if (g_hide[k].ic == ic && g_hide[k].si == si) p->arm = 1;
        g_nexp++;
    }
    InterlockedExchange(&g_exp_lock, 0);
}
static void exp_release(void)
{
    for (int i = 0; i < g_nexp; i++) {
        ExpPiece *p = &g_exp[i];
        if (p->ib) ID3D11Buffer_Release(p->ib);
        for (int s = 0; s < 4; s++) if (p->vb[s]) ID3D11Buffer_Release(p->vb[s]);
        if (p->il) ID3D11InputLayout_Release(p->il);
    }
    g_nexp = 0;
}
static void exp_key(void)
{
    if (g_mode != 1) { vr_log(">>> Re Pag: exportar el arma solo funciona en partida, con el VR puesto"); Beep(400, 150); return; }
    if (g_exp_state != 0) return;
    while (InterlockedCompareExchange(&g_exp_lock, 1, 0) != 0) YieldProcessor();
    exp_release();
    InterlockedExchange(&g_exp_lock, 0);
    g_exp_t = GetTickCount64();
    g_exp_state = 1;
    vr_log(">>> Re Pag: exportando el arma (no la muevas medio segundo)...");
    Beep(800, 60);
}
static float half2f(USHORT h)
{
    UINT s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = ldexpf((float)m, -24); else if (e == 31) v = 65504.0f; else v = ldexpf((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
/* lee hasta 4 componentes; norm = 0 deja los enteros tal cual */
static void read_comp(const BYTE *p, UINT f, float o[4], int norm)
{
    o[0] = o[1] = o[2] = 0; o[3] = 1;
    switch (f) {
    case 2: case 6: case 16: { int n = f == 2 ? 4 : f == 6 ? 3 : 2; for (int i = 0; i < n; i++) memcpy(&o[i], p + 4 * i, 4); break; }
    case 10: case 34: { int n = f == 10 ? 4 : 2; for (int i = 0; i < n; i++) o[i] = half2f(((const USHORT *)p)[i]); break; }
    case 11: case 12: case 35: case 36: { int n = (f == 11 || f == 12) ? 4 : 2; for (int i = 0; i < n; i++) o[i] = ((const USHORT *)p)[i] / ((norm && (f == 11 || f == 35)) ? 65535.0f : 1.0f); break; }
    case 13: case 14: case 37: case 38: { int n = (f == 13 || f == 14) ? 4 : 2; for (int i = 0; i < n; i++) o[i] = ((const SHORT *)p)[i] / ((norm && (f == 13 || f == 37)) ? 32767.0f : 1.0f); break; }
    case 28: case 30: case 87: case 88: for (int i = 0; i < 4; i++) o[i] = p[i] / ((norm && f != 30) ? 255.0f : 1.0f);
        if (f >= 87) { float t = o[0]; o[0] = o[2]; o[2] = t; } break;
    case 31: case 32: for (int i = 0; i < 4; i++) o[i] = ((const signed char *)p)[i] / ((norm && f == 31) ? 127.0f : 1.0f); break;
    case 24: case 25: { UINT v = *(const UINT *)p; o[0] = (v & 1023); o[1] = ((v >> 10) & 1023); o[2] = ((v >> 20) & 1023); o[3] = (v >> 30);
        if (norm && f == 24) { o[0] /= 1023; o[1] /= 1023; o[2] /= 1023; o[3] /= 3; } break; }
    case 41: memcpy(&o[0], p, 4); break;
    default: { UINT v; memcpy(&v, p, 4); o[0] = (float)v; break; }
    }
}
static BYTE *exp_read(ID3D11Device *dv, ID3D11DeviceContext *ctx, ID3D11Buffer *src, UINT off, UINT len)
{
    if (!src || !len) return NULL;
    D3D11_BUFFER_DESC sd; ID3D11Buffer_GetDesc(src, &sd);
    if (off >= sd.ByteWidth) return NULL;
    if (off + len > sd.ByteWidth) len = sd.ByteWidth - off;
    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (len + 15) & ~15u; bd.Usage = D3D11_USAGE_STAGING; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer *st = NULL;
    if (FAILED(ID3D11Device_CreateBuffer(dv, &bd, NULL, &st)) || !st) return NULL;
    D3D11_BOX box = { off, 0, 0, off + len, 1, 1 };
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)st, 0, 0, 0, 0, (ID3D11Resource *)src, 0, &box);
    BYTE *out = NULL;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms)) && ms.pData) {
        out = (BYTE *)malloc(bd.ByteWidth);
        if (out) { memset(out, 0, bd.ByteWidth); memcpy(out, ms.pData, len); }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    }
    ID3D11Buffer_Release(st);
    return out;
}
static const ExpElem *lay_find(const ExpLayout *L, const char *sem, UINT si)
{
    for (UINT i = 0; i < L->n; i++) if (!_stricmp(L->e[i].sem, sem) && L->e[i].si == si) return &L->e[i];
    return NULL;
}
static void exp_run(IDXGISwapChain *sc)
{
    g_exp_state = 2;
    while (InterlockedCompareExchange(&g_exp_lock, 1, 0) != 0) YieldProcessor();
    ID3D11Device *dv = NULL; ID3D11DeviceContext *ctx = NULL; FILE *fp = NULL;
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path);
    size_t pl = strlen(path);
    snprintf(path + pl, sizeof(path) - pl, "metro_vr_arma_%u_%u.obj", g_wpn_ic, g_wpn_si);
    int npie = 0; UINT vbase = 1, ntri_all = 0;
    if (!g_nexp) { vr_log("EXPORTAR: no se ha visto ninguna pieza del arma (mira el arma y vuelve a pulsar Re Pag)"); goto out; }
    if (FAILED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dv)) || !dv) goto out;
    ID3D11Device_GetImmediateContext(dv, &ctx);
    if (!ctx) goto out;
    fp = fopen(path, "w");
    if (!fp) { vr_log("EXPORTAR: no puedo crear %s", path); goto out; }
    fprintf(fp, "# Metro Last Light Redux VR - arma %u/%u exportada por el mod (E40)\n# un objeto por pieza y por hueso: pieza_<indices>_<inicio>_hueso_<n>\n", g_wpn_ic, g_wpn_si);
    for (int i = 0; i < g_nexp; i++) {
        ExpPiece *p = &g_exp[i];
        ExpLayout L; memset(&L, 0, sizeof(L));
        char desc[700]; int dl = 0; desc[0] = 0;
        if (!p->il || !lay_get(p->il, &L)) { vr_log("EXPORTAR: pieza %u/%u sin plantilla de vertices conocida (se salta)", p->ic, p->si); continue; }
        for (UINT k = 0; k < L.n && dl < 600; k++) dl += snprintf(desc + dl, sizeof(desc) - dl, " %s%u(fmt%u,hueco%u,+%u)", L.e[k].sem, L.e[k].si, L.e[k].fmt, L.e[k].slot, L.e[k].off);
        const ExpElem *ep = lay_find(&L, "POSITION", 0), *eb = lay_find(&L, "BLENDINDICES", 0), *ew = lay_find(&L, "BLENDWEIGHT", 0);
        if (!eb) eb = lay_find(&L, "BONES", 0);     /* (E41) el motor 4A los llama BONES / WEIGHTS */
        if (!ew) ew = lay_find(&L, "WEIGHTS", 0);
        vr_log("EXPORTAR: pieza %u/%u%s bv=%d pasos=%u/%u/%u/%u plantilla:%s", p->ic, p->si, p->arm ? " (brazo)" : "", p->bv, p->st[0], p->st[1], p->st[2], p->st[3], desc);
        if (!ep || !p->vb[ep->slot] || !p->st[ep->slot]) { vr_log("EXPORTAR:   sin posiciones, se salta"); continue; }
        UINT isz = p->ifmt == DXGI_FORMAT_R16_UINT ? 2 : (p->ifmt == DXGI_FORMAT_R32_UINT ? 4 : 0);
        if (!isz || p->ic % 3) continue;
        BYTE *ib = exp_read(dv, ctx, p->ib, p->ioff + p->si * isz, p->ic * isz);
        if (!ib) { vr_log("EXPORTAR:   no se pudieron leer los indices"); continue; }
        UINT *idx = (UINT *)malloc(p->ic * sizeof(UINT));
        if (!idx) { free(ib); continue; }
        long long mn = 0x7fffffff, mx = -1;
        for (UINT k = 0; k < p->ic; k++) {
            long long v = (long long)(isz == 2 ? ((USHORT *)ib)[k] : ((UINT *)ib)[k]) + p->bv;
            idx[k] = (UINT)(v < 0 ? 0 : v);
            if ((long long)idx[k] < mn) mn = idx[k];
            if ((long long)idx[k] > mx) mx = idx[k];
        }
        free(ib);
        UINT nv = (UINT)(mx - mn + 1);
        if (mx < 0 || nv > 2000000) { free(idx); continue; }
        BYTE *vd[4] = { 0 };
        int need[4] = { 0 }; need[ep->slot] = 1; if (eb) need[eb->slot] = 1; if (ew) need[ew->slot] = 1;
        for (int s = 0; s < 4; s++) if (need[s] && p->vb[s] && p->st[s]) vd[s] = exp_read(dv, ctx, p->vb[s], p->vo[s] + (UINT)mn * p->st[s], nv * p->st[s]);
        if (!vd[ep->slot]) { vr_log("EXPORTAR:   no se pudieron leer los vertices"); free(idx); for (int s = 0; s < 4; s++) free(vd[s]); continue; }
        /* (E42) cada pieza trae su propia colocacion (matriz por instancia xform0-3, hueco 1): sin ella salen todas apinadas */
        float xf[16]; int hxf = 0;
        const ExpElem *ex = lay_find(&L, "xform", 0);
        if (ex && p->vb[ex->slot] && p->st[ex->slot]) {
            BYTE *xb = exp_read(dv, ctx, p->vb[ex->slot], p->vo[ex->slot] + p->sinst * p->st[ex->slot] + ex->off, 64);
            if (xb) { memcpy(xf, xb, 64); hxf = 1; free(xb); }
        }
        /* forma A: filas con la traslacion en la 4.a columna (xform3 = 0 0 0 1); forma B: traslacion en xform3 */
        int xrow = hxf && fabsf(xf[12]) < 1e-4f && fabsf(xf[13]) < 1e-4f && fabsf(xf[14]) < 1e-4f;
        float rl = hxf ? sqrtf(xf[0] * xf[0] + xf[1] * xf[1] + xf[2] * xf[2]) : 1.0f;
        if (hxf) vr_log("EXPORTAR:   colocacion (%s, largo fila %.5f): %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f",
                        xrow ? "filas" : "columnas", rl, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5], xf[6], xf[7], xf[8], xf[9], xf[10], xf[11], xf[12], xf[13], xf[14], xf[15]);
        /* vertices */
        fprintf(fp, "# pieza %u/%u%s: %u vertices, %u triangulos, plantilla:%s\n", p->ic, p->si, p->arm ? " (brazo)" : "", nv, p->ic / 3, desc);
        int *vbone = (int *)malloc(nv * sizeof(int));
        float b0[3] = { 0 }; int bfirst = 1;
        for (UINT v = 0; v < nv; v++) {
            float q[4]; read_comp(vd[ep->slot] + v * p->st[ep->slot] + ep->off, ep->fmt, q, 1);
            if (bfirst) { b0[0] = q[0]; b0[1] = q[1]; b0[2] = q[2]; bfirst = 0; }
            float sc = ((ep->fmt == 14 || ep->fmt == 12) && rl > 0.05f) ? 1.0f / 2048.0f : 1.0f;   /* (E41) enteros del 4A: a un tamano manejable */
            float x = q[0] * sc, y = q[1] * sc, z = q[2] * sc, o[3] = { x, y, z };
            if (hxf) {
                if (xrow) for (int r = 0; r < 3; r++) o[r] = xf[4 * r] * x + xf[4 * r + 1] * y + xf[4 * r + 2] * z + xf[4 * r + 3];
                else for (int r = 0; r < 3; r++) o[r] = xf[r] * x + xf[4 + r] * y + xf[8 + r] * z + xf[12 + r];
            }
            fprintf(fp, "v %.6f %.6f %.6f\n", o[0], o[1], o[2]);
            int bone = -1;
            if (eb && vd[eb->slot]) {
                float bi[4], bw[4] = { 1, 0, 0, 0 };
                read_comp(vd[eb->slot] + v * p->st[eb->slot] + eb->off, eb->fmt, bi, 0);
                if (eb->fmt == 28 || eb->fmt == 87) for (int k = 0; k < 4; k++) bi[k] *= 255.0f;   /* UNORM: el byte tal cual */
                if (ew && vd[ew->slot]) read_comp(vd[ew->slot] + v * p->st[ew->slot] + ew->off, ew->fmt, bw, 1);
                int best = 0; for (int k = 1; k < 4; k++) if (bw[k] > bw[best]) best = k;
                bone = (int)(bi[best] + 0.5f);
            }
            if (vbone) vbone[v] = bone;
        }
        (void)b0;
        /* triangulos agrupados por el hueso de su primer vertice */
        int bl[512]; int nb = 0;
        UINT nt = p->ic / 3;
        for (UINT t = 0; t < nt; t++) {
            int b = vbone ? vbone[idx[3 * t] - (UINT)mn] : -1, f = 0;
            for (int k = 0; k < nb; k++) if (bl[k] == b) { f = 1; break; }
            if (!f && nb < 512) bl[nb++] = b;
        }
        char bonelist[700]; int bll = 0; bonelist[0] = 0;
        for (int k = 0; k < nb; k++) {
            int b = bl[k], cnt = 0;
            if (b >= 0) fprintf(fp, "o pieza_%u_%u%s_hueso_%d\n", p->ic, p->si, p->arm ? "_brazo" : "", b);
            else fprintf(fp, "o pieza_%u_%u%s\n", p->ic, p->si, p->arm ? "_brazo" : "");
            for (UINT t = 0; t < nt; t++) {
                int tb = vbone ? vbone[idx[3 * t] - (UINT)mn] : -1;
                if (tb != b) continue;
                fprintf(fp, "f %u %u %u\n", vbase + idx[3 * t] - (UINT)mn, vbase + idx[3 * t + 1] - (UINT)mn, vbase + idx[3 * t + 2] - (UINT)mn);
                cnt++;
            }
            if (bll < 620) bll += snprintf(bonelist + bll, sizeof(bonelist) - bll, " %d:%d", b, cnt);
        }
        vr_log("EXPORTAR:   %u vertices, %u triangulos; huesos (hueso:triangulos):%s", nv, nt, bonelist);
        vbase += nv; ntri_all += nt; npie++;
        free(vbone); free(idx); for (int s = 0; s < 4; s++) free(vd[s]);
    }
out:
    if (fp) fclose(fp);
    if (npie) { vr_log("EXPORTAR: LISTO -> %s (%d piezas, %u triangulos)", path, npie, ntri_all); Beep(900, 80); Beep(1300, 80); Beep(1700, 150); }
    else { vr_log("EXPORTAR: no se exporto nada"); Beep(400, 300); }
    exp_release();
    InterlockedExchange(&g_exp_lock, 0);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dv) ID3D11Device_Release(dv);
    g_exp_state = 0;
}
/* ===================== (E43) HUESOS DEL ARMA =====================
 * El arma va "con esqueleto": cada parte (cargador, cerrojo, cada bala...) tiene su hueso (BONES del vertice = fila del
 * cajon de huesos, de 3 en 3). Para quitar o mover una parte se pinta la pieza con una COPIA de ese cajon (en la grafica,
 * sin leerlo) con las filas de ese hueso a cero (= encogido a nada) o con SOLO ese hueso (para pintarlo en la mano izq).
 * metro_vr_huesos.txt: "arma_ic arma_si parte hueso" (parte 1 = cargador, 2 = cerrojo). */
typedef struct { UINT wic, wsi; int part, bone; } BoneSig;
#define MAXBONE 64
static BoneSig g_bone[MAXBONE]; static int g_nbone = 0;
static volatile int g_bone_try = -1;   /* Shift+Re Pag: hueso que se esta probando a encoger (-1 = ninguno) */
static ID3D11Buffer *g_bz = NULL, *g_bcopy[2] = { NULL, NULL }; static UINT g_bcopy_w[2] = { 0, 0 };
static volatile LONG g_bone_draws = 0, g_bone_fail = 0, g_bone_lh = 0;
static ID3D11Buffer *volatile g_bdiag_buf = NULL; static volatile LONG g_bdiag_req = 1; static UINT g_bdiag_sz[14];
static __thread int t_bh[8], t_bhn = 0, t_bk[8], t_bkn = 0, t_blh = 0;
static __thread ID3D11Buffer *t_borig = NULL;
static void bone_path(char *p) { GetTempPathA(MAX_PATH, p); strcat(p, "metro_vr_huesos.txt"); }
static void bone_load(void)
{
    static int done = 0; if (done) return; done = 1;
    char path[MAX_PATH]; bone_path(path);
    FILE *fp = fopen(path, "r"); if (!fp) return;
    UINT a, b; int c, d;
    while (g_nbone < MAXBONE && fscanf(fp, "%u %u %d %d", &a, &b, &c, &d) == 4) { g_bone[g_nbone].wic = a; g_bone[g_nbone].wsi = b; g_bone[g_nbone].part = c; g_bone[g_nbone].bone = d; g_nbone++; }
    fclose(fp);
    vr_log("HUESOS: %d partes de armas con hueso (%s), cajon de huesos en el hueco %d, desfase %d", g_nbone, path, cfg_huesos_hueco, cfg_huesos_desfase);
}
static int bone_list(UINT wic, UINT wsi, int part, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < g_nbone && n < max; i++) if (g_bone[i].wic == wic && g_bone[i].wsi == wsi && g_bone[i].part == part) out[n++] = g_bone[i].bone;
    return n;
}
/* (E43) en el Present: lee una vez el cajon de huesos del arma y apunta sus filas (para saber si el desfase es bueno) */
static void bone_diag_run(IDXGISwapChain *sc)
{
    ID3D11Buffer *b = g_bdiag_buf; if (!b) return;
    g_bdiag_buf = NULL;
    ID3D11Device *dv = NULL; ID3D11DeviceContext *ctx = NULL;
    if (SUCCEEDED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dv)) && dv) {
        ID3D11Device_GetImmediateContext(dv, &ctx);
        D3D11_BUFFER_DESC d; ID3D11Buffer_GetDesc(b, &d);
        BYTE *m = ctx ? exp_read(dv, ctx, b, 0, d.ByteWidth) : NULL;
        char sz[200]; int L = 0; for (int i = 0; i < 14; i++) L += snprintf(sz + L, sizeof(sz) - L, " %u", g_bdiag_sz[i]);
        vr_log("HUESOS: tamano de los 14 cajones de vertices de la pieza grande del arma:%s (se usa el hueco %d, %u bytes)", sz, cfg_huesos_hueco, d.ByteWidth);
        if (m) {
            const float *f = (const float *)m; UINT nr = d.ByteWidth / 16;
            int show[] = { 0, 1, 2, 3, 15, 16, 17, 18, 21, 22, 23, (int)nr - 1 };
            for (int k = 0; k < 12; k++) { int r = show[k]; if (r < 0 || (UINT)r >= nr) continue;
                vr_log("HUESOS:   fila %3d: %9.4f %9.4f %9.4f %9.4f", r, f[4 * r], f[4 * r + 1], f[4 * r + 2], f[4 * r + 3]); }
            free(m);
        }
    }
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dv) ID3D11Device_Release(dv);
    ID3D11Buffer_Release(b);
}
/* (E29) lee una vez los indices de la pieza grande (copia a una caja "de lectura") y busca sus islas */
static void isl_scan(IDXGISwapChain *sc)
{
    if (g_isl_state != 1 || !g_isl_ib) return;
    LONG fail = 3;
    ID3D11Device *dv = NULL; ID3D11DeviceContext *ctx = NULL; ID3D11Buffer *st = NULL; UINT *par = NULL, *tri = NULL;
    UINT isz = g_isl_fmt == DXGI_FORMAT_R16_UINT ? 2 : (g_isl_fmt == DXGI_FORMAT_R32_UINT ? 4 : 0);
    UINT ic = g_isl_ic, si = g_isl_si;
    if (!isz || ic < 3 || ic % 3) { vr_log("ISLAS: formato raro (fmt=%d ic=%u)", (int)g_isl_fmt, ic); goto out; }
    if (FAILED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dv)) || !dv) goto out;
    ID3D11Device_GetImmediateContext(dv, &ctx);
    if (!ctx) goto out;
    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = ic * isz; bd.Usage = D3D11_USAGE_STAGING; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateBuffer(dv, &bd, NULL, &st)) || !st) { vr_log("ISLAS: no se pudo crear la caja de lectura"); goto out; }
    D3D11_BOX box = { g_isl_off + si * isz, 0, 0, g_isl_off + (si + ic) * isz, 1, 1 };
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)st, 0, 0, 0, 0, (ID3D11Resource *)g_isl_ib, 0, &box);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms)) || !ms.pData) { vr_log("ISLAS: no se pudo leer"); goto out; }
    UINT maxv = 0;
    tri = (UINT *)malloc(ic * sizeof(UINT));
    if (!tri) { ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0); goto out; }
    for (UINT k = 0; k < ic; k++) { tri[k] = isz == 2 ? ((const USHORT *)ms.pData)[k] : ((const UINT *)ms.pData)[k]; if (tri[k] > maxv) maxv = tri[k]; }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    if (maxv > 4000000) { vr_log("ISLAS: indices demasiado grandes (%u)", maxv); goto out; }
    par = (UINT *)malloc((maxv + 1) * sizeof(UINT));
    if (!par) goto out;
    for (UINT v = 0; v <= maxv; v++) par[v] = v;
#define ISL_ROOT(x) ({ UINT r__ = (x); while (par[r__] != r__) { par[r__] = par[par[r__]]; r__ = par[r__]; } r__; })
    for (UINT t = 0; t < ic; t += 3) {   /* los 3 vertices de cada triangulo estan unidos */
        UINT a = ISL_ROOT(tri[t]), b = ISL_ROOT(tri[t + 1]), d = ISL_ROOT(tri[t + 2]);
        par[b] = a; par[d] = a;
    }
    /* isla de cada triangulo (numeradas por orden de aparicion) y tramos seguidos de la misma isla */
    UINT roots[MAXISR]; int nisl = 0, nr = 0, cur = -1;
    for (UINT t = 0; t < ic; t += 3) {
        UINT rt = ISL_ROOT(tri[t]); int id = -1;
        for (int k = 0; k < nisl; k++) if (roots[k] == rt) { id = k; break; }
        if (id < 0) { if (nisl >= MAXISR) { vr_log("ISLAS: mas de %d islas", MAXISR); goto out; } id = nisl; roots[nisl] = rt; g_isl_size[nisl] = 0; nisl++; }
        g_isl_size[id] += 3;
        if (id != cur) {
            if (nr >= MAXISR) { vr_log("ISLAS: mas de %d tramos", MAXISR); goto out; }
            g_isr[nr].start = t; g_isr[nr].count = 0; g_isr[nr].isl = id; nr++; cur = id;
        }
        g_isr[nr - 1].count += 3;
    }
#undef ISL_ROOT
    for (int k = 0; k < nisl; k++) g_isl_order[k] = k;
    for (int a = 0; a < nisl; a++) for (int b = a + 1; b < nisl; b++)
        if (g_isl_size[g_isl_order[b]] > g_isl_size[g_isl_order[a]]) { int x = g_isl_order[a]; g_isl_order[a] = g_isl_order[b]; g_isl_order[b] = x; }
    g_nisl = nisl; g_nisr = nr;
    vr_log("ISLAS: pieza %u/%u (indices de %u bytes, vertices hasta %u): %d islas en %d tramos", ic, si, isz, maxv, nisl, nr);
    for (int o = 0; o < nisl; o++) {
        int k = g_isl_order[o];
        char line[600]; int L = 0;
        for (int r = 0; r < nr && L < 520; r++) if (g_isr[r].isl == k) L += snprintf(line + L, sizeof(line) - L, " +%u:%u", g_isr[r].start, g_isr[r].count);
        vr_log("ISLAS:   isla %d (n.%d con la tecla Fin): %u indices, tramos%s", k, o + 1, g_isl_size[k], line);
    }
    fail = 2;
    vr_log("ISLAS: manos=%d mano_izq=%d -> se ven la isla n.%d (mano der)%s", cfg_manos, cfg_mano_izq, cfg_isla_der, cfg_mano_izq ? " y la de la mano izq" : "");
out:
    if (par) free(par);
    if (tri) free(tri);
    if (st) ID3D11Buffer_Release(st);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dv) ID3D11Device_Release(dv);
    if (g_isl_ib) { ID3D11Buffer_Release(g_isl_ib); g_isl_ib = NULL; }
    MemoryBarrier();
    g_isl_state = fail;
    if (fail == 2) isl_default_mask();   /* (E30) a partir de aqui se ven las manos */
}
/* (E29) tecla Fin: ensena los brazos quitando UNA isla cada vez (de la mas grande a la mas pequena); tras la ultima, se
 * vuelven a ocultar. Lo que desaparece en cada pulsacion es esa isla. */
static void isl_key(void)
{
    if (g_isl_state != 2) { vr_log(">>> Fin: aun no hay islas (estado %ld: tiene que verse el arma en VR un momento)", (long)g_isl_state); Beep(400, 150); return; }
    static int o = -1;
    o++;
    if (o > g_nisl) { o = -1; g_isl_hide = -1; isl_default_mask(); vr_log(">>> Fin: vuelta a lo normal (%s)", g_isl_on ? "solo las manos" : "brazos ocultos"); Beep(600, 100); return; }
    g_isl_on = 1;
    memset(g_isl_show, 1, sizeof(g_isl_show));
    if (o == 0) { g_isl_hide = -1; vr_log(">>> Fin: brazos ENTEROS (pintados por trozos: deben verse igual que siempre)"); }
    else { g_isl_hide = g_isl_order[o - 1]; g_isl_show[g_isl_hide] = 0; vr_log(">>> Fin: brazos SIN la isla n.%d de %d (isla %d, %u indices): lo que desaparece es esa isla",
                                                  o, g_nisl, g_isl_hide, g_isl_size[g_isl_hide]); }
    Beep(1000, 60);
}
static HRESULT STDMETHODCALLTYPE hk_Present_in(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    /* si nos vuelven a llamar desde dentro (bucle con la overlay), vamos directos al de fabrica */
    if (!t_in_present) sc_vt_sync();   /* (E20d) */
    PFN_Present next = g_orig_Present;
    if (t_in_present) return next(sc, sync, flags);
    t_in_present = 1;
    if (g_isl_state == 1) isl_scan(sc);   /* (E29) */
    if (g_exp_state == 1 && GetTickCount64() - g_exp_t > 400) exp_run(sc);   /* (E40) */
    if (g_bdiag_buf) bone_diag_run(sc);   /* (E43) */
    LONG n = InterlockedIncrement(&g_frames);
    if (n <= 10)
        vr_log("Present #%ld (sync=%u flags=0x%X) Map=%ld Map_cb=%ld UpdSub=%ld",
               (long)n, sync, flags, (long)g_maps, (long)g_cbmaps, (long)g_updsub);
    int real_frame = !(flags & DXGI_PRESENT_TEST);
    if (cfg_novsync && vr_active()) sync = 0;   /* SteamVR marca el ritmo */
    if (real_frame) {
        LARGE_INTEGER s0, s1; QueryPerformanceCounter(&s0);
        InterlockedIncrement(&g_real_frames); vr_submit_frame(sc);
        QueryPerformanceCounter(&s1); g_fr_sub_us = (LONG)((s1.QuadPart - s0.QuadPart) * 1000000 / g_qpf);
    }
    LARGE_INTEGER pp0, pp1; QueryPerformanceCounter(&pp0);
    HRESULT hr = next(sc, sync, flags);
    if (FAILED(hr)) {   /* (E20c) */
        static int said = 0;
        if (said++ < 5) {
            ID3D11Device *dv = NULL; HRESULT rr = 0;
            if (SUCCEEDED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dv)) && dv) { rr = ID3D11Device_GetDeviceRemovedReason(dv); ID3D11Device_Release(dv); }
            vr_log("PRESENT FALLO hr=0x%08lX (grafica caida? motivo=0x%08lX)", (unsigned long)hr, (unsigned long)rr);
        }
    }
    QueryPerformanceCounter(&pp1);
    {
        static LARGE_INTEGER lastp; LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_qpf = fq.QuadPart;
        InterlockedExchangeAdd(&g_prof_present_us, (LONG)((pp1.QuadPart - pp0.QuadPart) * 1000000 / fq.QuadPart));
        g_fr_pres_us = (LONG)((pp1.QuadPart - pp0.QuadPart) * 1000000 / fq.QuadPart);
        if (lastp.QuadPart) {
            LONG ft = (LONG)((pp1.QuadPart - lastp.QuadPart) * 1000000 / fq.QuadPart);
            if (ft > g_prof_maxframe_us) g_prof_maxframe_us = ft;
        }
        lastp = pp1;
    }
    if (real_frame) {
        /* (E7c) foto de la camara de este fotograma: en el siguiente, es "la anterior" */
        static float last[88]; float dd = 0;
        for (int q = 56; q < 72; q++) dd += fabsf(g_pv_cur[q] - last[q]);
        memcpy(g_pv_prev, g_pv_cur, sizeof(g_pv_prev)); memcpy(last, g_pv_cur, sizeof(last));
        g_pv_have = (dd > 1e-3f) ? 2 : 0;      /* solo si la camara se ha movido de un fotograma a otro */
        g_ord_flag = 0;
        g_fs_need = 1;
        g_hud_draws = 0;
        draw_hooks_check();
        pieces_frame();
        {   /* (E20d) dibujos en la imagen final en este fotograma: en los menus (texto, botones) son muchos */
            g_bb_last = InterlockedExchange(&g_bb_draws, 0);
            if (g_bb_last > g_bb_max) g_bb_max = g_bb_last;
        }
        if (cfg_auto && !g_auto_manual && g_nat_installed == 1) {   /* (E20) modo VR solo con partida */
            static ULONGLONG since = 0, gone = 0, menu_t = 0;
            ULONGLONG now = GetTickCount64();
            /* (E20f) entrar en "menu" con muchos dibujos (pausa: 25+); seguir en el mientras haya bastantes (opciones: 13-14).
             * Jugando son 1-4, y al mirar a ciertos sitios suben a ~14: con 20 para entrar ya no salta. */
            /* (E20h) mirando hacia abajo tambien salen 24-31 dibujos (como la pausa). Para contar como menu hace falta
             * ademas que el juego este parado (el jugador no se actualiza con el tiempo en marcha) o no mirar abajo. */
            int paused = !g_mov_run || now - g_mov_run > 200;
            int looking_down = g_rt_Rg[1][2] < -0.42f;          /* mas de 25 grados hacia abajo */
            /* (E22) log de la E21: en la pausa de verdad el jugador deja de actualizarse (0/s); al cambiar de arma y en
             * otros "falsos menus" sigue a ~70/s. Asi que solo cuenta como menu con el juego parado. */
            int menu_ok = paused; (void)looking_down;
            if ((g_bb_last >= cfg_menu_draws && menu_ok) || (menu_t && now - menu_t < 400 && g_bb_last >= cfg_menu_stay)) menu_t = now;
            int menu = menu_t && now - menu_t < 400;
            /* (E20e) el menu principal tiene un escenario 3D con la camara del jugador: no basta con la camara.
             * La funcion que mueve al jugador solo se llama con una partida cargada (y se para en la pausa). */
            int cam = g_cam_seen && now - g_cam_seen < 250 && g_mov_seen && now - g_mov_seen < 300 && !menu;
            if (cam) { if (!since) since = now; gone = 0; } else { since = 0; if (!gone) gone = now; }
            LONG want = g_mode;
            /* (E20f) para ENTRAR en VR hace falta ver los brazos de Artyom (los guardados con F6): la intro tiene
             * escenario 3D, camara y hasta movimiento (el tren), pero no brazos. Para seguir en VR no hace falta. */
            int arms = g_nhide == 0 || (g_arm_seen && now - g_arm_seen < 1000);
            if (cam && now - since > 1000 && (g_mode == 1 || arms)) want = 1;
            else if (!cam && gone && now - gone > 150) want = 0;
            if (want == 1 && g_cine_on) cine_hide();   /* (E22) */
            if (want != g_mode) { g_mode = want; vr_log(">>> AUTO: %s (dibujos en pantalla=%ld)", want ? "PARTIDA -> modo VR" : "MENU / VIDEO / CARGA / PAUSA -> pantalla flotante", (long)g_bb_last); }
        }
        if (g_nat_installed == 0 && (n % 30) == 0) nat_install();
        if ((n % 60) == 0) xinput_hook_try();   /* (E21) */
        {   /* (E13) mandos */
            static LARGE_INTEGER lt; LARGE_INTEGER now, fq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
            float dt = lt.QuadPart ? (float)(now.QuadPart - lt.QuadPart) / (float)fq.QuadPart : 0.0f; lt = now;
            if (dt > 0.1f) dt = 0.1f;
            mandos_step(dt);
        }
        ocl_update();
        if (g_oc_have) { memcpy(g_op_H, g_oc_H, sizeof(g_oc_H)); memcpy(g_op_Hi, g_oc_Hi, sizeof(g_oc_Hi)); memcpy(g_op_L, g_oc_L, sizeof(g_oc_L)); g_op_have = 1; }
        if (!vr_active() && (n % 60) == 1) {     /* sin visor: la forma de pantalla real (en ventana no es 16:9 exacto) */
            DXGI_SWAP_CHAIN_DESC sd;
            if (SUCCEEDED(IDXGISwapChain_GetDesc(sc, &sd)) && sd.BufferDesc.Height)
                g_aspect = (float)sd.BufferDesc.Width / (float)sd.BufferDesc.Height;
        }
        tw_update_mode(sc);   /* (E27) */
        vr_after_present();
    }
    t_in_present = 0;
    return hr;
}

/* Cambia una casilla de la tabla de funciones de un objeto COM por la nuestra. */
static int patch_slot(void **slot, void *hook, void **orig)
{
    if (*slot == hook) return 0;                    /* ya estaba */
    if (*orig && *slot != *orig) return -2;         /* otra tabla distinta */
    DWORD old;
    if (!VirtualProtect((LPVOID)slot, sizeof(void *), PAGE_READWRITE, &old)) return -1;
    if (!*orig) *orig = *slot;
    *slot = hook;
    VirtualProtect((LPVOID)slot, sizeof(void *), old, &old);
    return 1;
}

#define PATCH(slot, hook, orig, name) do {                                   \
        int r_ = patch_slot((void **)(slot), (void *)(hook), (void **)&(orig)); \
        if (r_ != 0) vr_log("hook %s: %s", name,                              \
            r_ == 1 ? "instalado" : (r_ == -1 ? "ERROR VirtualProtect" : "tabla distinta, NO instalado")); \
    } while (0)

/*
 * Para localizar la funcion Present creamos NUESTRO PROPIO dispositivo y swapchain
 * de mentira (con una ventana oculta), sin tocar el del juego. Luego lo tiramos.
 */
/* ---- Present con "tabla propia" ----
 * En vez de cambiar la tabla de funciones COMPARTIDA (donde tambien se mete la Steam Overlay y nos
 * pasabamos la pelota en bucle), le damos a la swapchain DEL JUEGO su propia copia de la tabla.
 * Asi el juego nos llama a nosotros, y nosotros llamamos a lo que habia antes (overlay o Windows). */
#define SC_VT_N 40
static void *g_sc_vt[SC_VT_N];
static void vr_drop_copy(void);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Resize)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetFS)(IDXGISwapChain*, BOOL, IDXGIOutput*);
static PFN_Resize g_orig_Resize = NULL;
static PFN_SetFS  g_orig_SetFS = NULL;
static HRESULT STDMETHODCALLTYPE hk_Resize(IDXGISwapChain *sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
    vr_log("SWAPCHAIN: ResizeBuffers(%u, %ux%u, formato=%d, flags=0x%X)", n, w, h, (int)f, fl);
    vr_drop_copy();
    HRESULT hr = g_orig_Resize(sc, n, w, h, f, fl);
    vr_log("SWAPCHAIN: ResizeBuffers -> 0x%08lX", (unsigned long)hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_SetFS(IDXGISwapChain *sc, BOOL fs, IDXGIOutput *o)
{
    vr_log("SWAPCHAIN: SetFullscreenState(%d)", (int)fs);
    vr_drop_copy();
    HRESULT hr = g_orig_SetFS(sc, fs, o);
    vr_log("SWAPCHAIN: SetFullscreenState -> 0x%08lX", (unsigned long)hr);
    return hr;
}
static IDXGISwapChain * volatile g_game_sc = NULL;

/* (E20d) la tabla propia se copio en un momento; si despues alguien (el overlay de Steam) cambia la tabla original,
 * hay que copiarlo, o ese alguien no se entera de cosas (p. ej. que el juego tira la swapchain) y peta. */
static void **g_sc_src = NULL; static int g_sc_nv = 0;
static void sc_vt_sync(void)
{
    void **src = g_sc_src; if (!src) return;
    static int said = 0;
    for (int i = 0; i < g_sc_nv; i++) {
        void *v = src[i];
        if (i == 8)  { if (v != (void *)hk_Present && v != (void *)g_orig_Present) { if (said++ < 5) vr_log("SWAPCHAIN: el Present original ha cambiado (%p)", v); g_orig_Present = (PFN_Present)v; } continue; }
        if (i == 10) { if (v != (void *)hk_SetFS) g_orig_SetFS = (PFN_SetFS)v; continue; }
        if (i == 13) { if (v != (void *)hk_Resize) g_orig_Resize = (PFN_Resize)v; continue; }
        if (g_sc_vt[i] != v) { if (said++ < 5) vr_log("SWAPCHAIN: casilla %d de la tabla original ha cambiado: la copio", i); g_sc_vt[i] = v; }
    }
}
static void give_private_vtable(IDXGISwapChain *sc)
{
    void **cur = *(void ***)sc;
    if (cur == g_sc_vt) return;                              /* ya es la nuestra */
    EnterCriticalSection(&g_cs);
    if (*(void ***)sc != g_sc_vt) {
        int n = 0;
        for (; n < SC_VT_N; n++) {
            void *v = NULL; SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), cur + n, &v, sizeof(v), &got) || got != sizeof(v) || !v) break;
            g_sc_vt[n] = v;
        }
        if (n > 8) {
            g_orig_Present = (PFN_Present)g_sc_vt[8];        /* lo que el juego iba a llamar */
            g_sc_src = cur; g_sc_nv = n;
            g_sc_vt[8] = (void *)hk_Present;
            g_orig_SetFS  = (PFN_SetFS)g_sc_vt[10];  g_sc_vt[10] = (void *)hk_SetFS;
            g_orig_Resize = (PFN_Resize)g_sc_vt[13]; g_sc_vt[13] = (void *)hk_Resize;
            *(void ***)sc = g_sc_vt;                          /* el objeto es memoria normal: se puede escribir */
            g_game_sc = sc;
            InterlockedExchange(&g_present_hooked, 1);
            unsigned long long off; char mb[MAX_PATH];
            module_of((const void *)g_orig_Present, &off, mb);
            vr_log("Present: swapchain del juego %p con tabla propia (%d casillas). Antes iba a %s+0x%llX", (void *)sc, n, mb, off);
        } else {
            vr_log("Present: tabla de la swapchain demasiado corta (%d), no la toco", n);
        }
    }
    LeaveCriticalSection(&g_cs);
}

/* Cuando el juego crea la "vista" de la imagen final, sacamos de ahi su swapchain. */
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateRTV)(ID3D11Device*, ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*, ID3D11RenderTargetView**);
static PFN_CreateRTV g_orig_CreateRTV = NULL;
static HRESULT STDMETHODCALLTYPE hk_CreateRTV(ID3D11Device *d, ID3D11Resource *res, const D3D11_RENDER_TARGET_VIEW_DESC *desc, ID3D11RenderTargetView **out)
{
    HRESULT hr = g_orig_CreateRTV(d, res, desc, out);
    if (SUCCEEDED(hr) && res && cfg_present) {
        IDXGISurface *surf = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)res, &IID_IDXGISurface, (void **)&surf)) && surf) {
            IDXGISwapChain *sc = NULL;
            if (SUCCEEDED(IDXGISurface_GetParent(surf, &IID_IDXGISwapChain, (void **)&sc)) && sc) {
                give_private_vtable(sc);
                IDXGISwapChain_Release(sc);
            }
            IDXGISurface_Release(surf);
        }
    }
    return hr;
}

__attribute__((unused)) static void hook_present_via_dummy(void)
{
    vr_log("Present: buscando con dispositivo de prueba propio...");
    HMODULE m = real_d3d11();
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN f =
        m ? (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(m, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (!f) { vr_log("Present: no encuentro la funcion real"); return; }

    HWND w = CreateWindowExA(0, "STATIC", "vrdummy", WS_OVERLAPPED, 0, 0, 8, 8, NULL, NULL,
                             GetModuleHandleA(NULL), NULL);
    if (!w) { vr_log("Present: sin ventana de prueba"); return; }

    DXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.BufferDesc.Width = 8; sd.BufferDesc.Height = 8;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = w;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device *d = NULL; ID3D11DeviceContext *c = NULL; IDXGISwapChain *sc = NULL;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = f(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                   &sd, &sc, &d, &fl, &c);
    if (FAILED(hr) || !sc) {
        vr_log("Present: dispositivo de prueba fallo hr=0x%08lX", (unsigned long)hr);
    } else {
        PATCH(&sc->lpVtbl->Present, hk_Present, g_orig_Present, "Present");
        if (g_orig_Present) { find_real_present((void **)&sc->lpVtbl->Present); InterlockedExchange(&g_present_hooked, 1); }
    }
    if (sc) IDXGISwapChain_Release(sc);
    if (c)  ID3D11DeviceContext_Release(c);
    if (d)  ID3D11Device_Release(d);
    DestroyWindow(w);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------ visor (OpenVR / SteamVR) */
/* Solo lo minimo de openvr_capi.h. La DLL de Valve se carga "a mano": si no esta, el resto sigue igual. */
typedef struct { float m[3][4]; } OvrMat34;
/* (E22) */
static OvrMat34 g_eye_rpose[2]; static volatile LONG g_eye_rpose_ok[2] = { 0, 0 };
static volatile LONG g_pose_same = 0, g_pose_diff = 0;
typedef struct { OvrMat34 mat; float vel[3]; float avel[3]; int result; unsigned char valid; unsigned char connected; } OvrPose;
typedef intptr_t (*PFN_VR_InitInternal)(int *err, int type);
typedef intptr_t (*PFN_VR_GetGenericInterface)(const char *ver, int *err);
typedef void     (*PFN_GetPose)(int origin, float predict, OvrPose *poses, unsigned count);

static PFN_VR_InitInternal        p_VR_Init = NULL;
static PFN_VR_GetGenericInterface p_VR_GetIf = NULL;
static PFN_GetPose                p_GetPose = NULL;
/* (E13) mandos */
typedef struct { float x, y; } OvrAxis;
typedef struct { uint32_t packet; uint64_t pressed, touched; OvrAxis axis[5]; } OvrCtrlState;
typedef uint32_t (*PFN_RoleIdx)(int role);
typedef int (*PFN_CtrlState)(uint32_t idx, OvrCtrlState *st, uint32_t size);
static PFN_RoleIdx p_RoleIdx = NULL;
typedef int (*PFN_RoleOf)(uint32_t idx);
static PFN_RoleOf p_RoleOf = NULL;
static PFN_CtrlState p_CtrlState = NULL;
typedef struct { void *handle; int type; int colorspace; } OvrTexture;
typedef struct { float uMin, vMin, uMax, vMax; } OvrBounds;
typedef int (*PFN_WaitGetPoses)(OvrPose *render, unsigned nr, OvrPose *game, unsigned ng);
typedef int (*PFN_Submit)(int eye, OvrTexture *tex, OvrBounds *bounds, int flags);
static PFN_WaitGetPoses p_WaitGetPoses = NULL;
static PFN_Submit       p_Submit = NULL;
/* (E12) tiempos que mide SteamVR de cada fotograma (IVRCompositor::GetFrameTiming) */
typedef struct {
    uint32_t size, frameIndex, numPresents, numMisPresented, numDropped, reprojFlags;
    double sysTime;
    float preSubmitGpu, postSubmitGpu, totalRenderGpu, compGpu, compCpu, compIdleCpu;
    float clientInterval, presentCallCpu, waitForPresentCpu, submitFrame;
    float waitCalled, posesReady, frameReady, compUpdStart, compUpdEnd, compRenderStart;
    unsigned char pose[80];
    uint32_t vsyncsReady, vsyncsToFirstView;
    float transferLatency;
} OvrFrameTiming;
typedef int (*PFN_GetFrameTiming)(OvrFrameTiming *t, uint32_t framesAgo);
static PFN_GetFrameTiming p_GetFrameTiming = NULL;
/* (E20) "cine": menus, intro y videos en una pantalla flotante (overlay de SteamVR) con el fondo negro */
typedef int (*PFN_OvlH)(uint64_t h);
typedef int (*PFN_GetTS)(void);
static PFN_GetTS p_GetTS = NULL;                 /* IVRCompositor::GetTrackingSpace */
static void **g_ovl = NULL;                      /* tabla de IVROverlay_028 */
static uint64_t g_ovl_h = 0;
static int g_ovl_state = 0;                      /* 0 sin crear, 1 listo, -1 no se puede */
static int g_cine_on = 0;                        /* la pantalla flotante se esta viendo ahora */
static float g_eraw[2][4]; static OvrMat34 g_e2h[2]; static int g_eraw_ok = 0;   /* (E20d) lente cruda y ojo->cabeza de cada ojo */
static ID3D11Texture2D *g_black = NULL;          /* imagen negra para los ojos (en nuestro dispositivo) */
static double g_ft_gpu = 0, g_ft_tot = 0, g_ft_int = 0, g_ft_idle = 0, g_ft_comp = 0;
static long g_ft_n = 0, g_ft_drop = 0, g_ft_mis = 0, g_ft_reproj = 0, g_ft_pres = 0;
static uint32_t g_ft_last = 0;
typedef unsigned char (*PFN_PollEvent)(void *ev, uint32_t size);
static PFN_PollEvent    p_PollEvent = NULL;
static int head_apply(const OvrPose *pz);
static void hand_update(const OvrPose *rp);
static void lhand_update(const OvrPose *rp);
static void reload_update(const OvrPose *rp);   /* (E36) */
static void raton_step(void);
static OvrMat34 g_frame_pose;              /* postura con la que se esta dibujando este fotograma */
static OvrMat34 g_eye_pose[2];             /* postura con la que se dibujo la ultima imagen de cada ojo */
static volatile LONG g_have_frame_pose = 0;
typedef void (*PFN_GetDXGIOutputInfo)(int32_t *idx);
static int g_vr_adapter = -1;
static HMODULE g_ovr = NULL;
static int g_head_state = 0;          /* 0 = sin probar, 1 = funcionando, -1 = no hay openvr_api.dll */
static ULONGLONG g_head_retry = 0;
static volatile LONG g_head_recenter = 1;
static float g_refR[3][3], g_refP[3];
static float g_head_yaw = 0, g_head_pitch = 0, g_head_roll = 0, g_head_dx = 0, g_head_dy = 0, g_head_dz = 0;
static volatile LONG g_head_ok = 0;

static void head_try_init(void)
{
    if (g_head_state != 0) return;
    ULONGLONG now = GetTickCount64();
    if (now < g_head_retry) return;
    g_head_retry = now + 3000;           /* si SteamVR no esta abierto, reintenta cada 3 s */
    if (!g_ovr) {
        char path[MAX_PATH]; HMODULE me = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)head_try_init, &me);
        if (me && GetModuleFileNameA(me, path, MAX_PATH)) {
            char *b = strrchr(path, '\\');
            if (b) { strcpy(b + 1, "openvr_api.dll"); g_ovr = LoadLibraryA(path); }
        }
        if (!g_ovr) g_ovr = LoadLibraryA("openvr_api.dll");
        if (!g_ovr) { vr_log("VISOR: no encuentro openvr_api.dll junto a metro.exe (la cabeza queda apagada)"); g_head_state = -1; return; }
        p_VR_Init  = (PFN_VR_InitInternal)(void *)GetProcAddress(g_ovr, "VR_InitInternal");
        p_VR_GetIf = (PFN_VR_GetGenericInterface)(void *)GetProcAddress(g_ovr, "VR_GetGenericInterface");
        if (!p_VR_Init || !p_VR_GetIf) { vr_log("VISOR: openvr_api.dll no tiene las funciones esperadas (es de 32 bits?)"); g_head_state = -1; return; }
        vr_log("VISOR: openvr_api.dll cargada");
    }
    int err = 0;
    /* 1 = Scene (somos "el juego VR" y mandamos imagen) | 3 = Background (solo leer el visor) */
    p_VR_Init(&err, cfg_vrimg ? 1 : 3);
    if (err != 0) {
        static int logged = 0;
        if (logged++ < 3) vr_log("VISOR: SteamVR no responde (error %d). Abre SteamVR y el visor; reintento cada 3 s", err);
        return;
    }
    const char *vers[] = { "FnTable:IVRSystem_026", "FnTable:IVRSystem_022", "FnTable:IVRSystem_019" };
    for (int i = 0; i < 3 && !p_GetPose; i++) {
        err = 0;
        void **tbl = (void **)p_VR_GetIf(vers[i], &err);
        if (tbl && err == 0) {
            p_GetPose = (PFN_GetPose)tbl[12];      /* GetDeviceToAbsoluteTrackingPose */
            if (i == 0) {   /* (E13) mandos: solo con la version de la tabla que conocemos */
                p_RoleIdx = (PFN_RoleIdx)tbl[18];       /* GetTrackedDeviceIndexForControllerRole */
                p_RoleOf = (PFN_RoleOf)tbl[19];         /* GetControllerRoleForTrackedDeviceIndex */
                p_CtrlState = (PFN_CtrlState)tbl[37];   /* GetControllerState */
            }
            p_PollEvent = (PFN_PollEvent)tbl[30];  /* PollNextEvent: SteamVR espera que la escena "lea su correo" */
            int32_t ai = -1; ((PFN_GetDXGIOutputInfo)tbl[8])(&ai); g_vr_adapter = ai;
            {
                typedef OvrMat34 (*PFN_EyeToHead)(int eye);
                for (int k = 0; k < 2; k++) {
                    OvrMat34 m = ((PFN_EyeToHead)tbl[5])(k);
                    g_e2h[k] = m;   /* (E20d) para la pantalla flotante propia */
                    g_eyepos[k][0] = m.m[0][3]; g_eyepos[k][1] = m.m[1][3]; g_eyepos[k][2] = -m.m[2][3];  /* OpenVR -> juego: Z al reves */
                }
                vr_log("VISOR: ojos en (%.4f, %.4f, %.4f) y (%.4f, %.4f, %.4f) -> separacion %.1f mm",
                       g_eyepos[0][0], g_eyepos[0][1], g_eyepos[0][2], g_eyepos[1][0], g_eyepos[1][1], g_eyepos[1][2],
                       (g_eyepos[1][0] - g_eyepos[0][0]) * 1000.0f);
            }
            {
                typedef void (*PFN_GetProjRaw)(int eye, float *l, float *r, float *t, float *b);
                float e[2][4];
                for (int k = 0; k < 2; k++) ((PFN_GetProjRaw)tbl[2])(k, &e[k][0], &e[k][1], &e[k][2], &e[k][3]);
                memcpy(g_eraw, e, sizeof(e)); g_eraw_ok = 1;   /* (E20d) */
                /* union de los dos ojos. En OpenVR "bottom" es el borde de ARRIBA y "top" el de abajo (van al reves) */
                float L = fminf(e[0][0], e[1][0]), R = fmaxf(e[0][1], e[1][1]);
                float UP = fmaxf(e[0][3], e[1][3]), DN = fminf(e[0][2], e[1][2]);
                vr_log("VISOR: lente ojo izq: %.3f %.3f %.3f %.3f | der: %.3f %.3f %.3f %.3f",
                       e[0][0], e[0][1], e[0][2], e[0][3], e[1][0], e[1][1], e[1][2], e[1][3]);
                g_vis_L = L; g_vis_R = R; g_vis_UP = UP; g_vis_DN = DN;
                if (R - L > 0.1f && UP - DN > 0.1f) {
                    float cx = (L + R) * 0.5f, hw = (R - L) * 0.5f, cy = (UP + DN) * 0.5f, hh = (UP - DN) * 0.5f;
                    float r0[4] = { 1.0f / hw, 0, -cx / hw, 0 }, r1[4] = { 0, 1.0f / hh, -cy / hh, 0 };
                    memcpy(g_fovrow[0], r0, sizeof(r0)); memcpy(g_fovrow[1], r1, sizeof(r1));
                    for (int k = 0; k < 2; k++) {
                        g_eyeb[k][0] = (e[k][0] - L) / (R - L);
                        g_eyeb[k][2] = (e[k][1] - L) / (R - L);
                        g_eyeb[k][1] = (UP - e[k][3]) / (UP - DN);
                        g_eyeb[k][3] = (UP - e[k][2]) / (UP - DN);
                    }
                    for (int k = 0; k < 2; k++) {
                        float l = e[k][0], rr = e[k][1], up = e[k][3], dn = e[k][2];
                        float ecx = (l + rr) * 0.5f, ehw = (rr - l) * 0.5f, ecy = (up + dn) * 0.5f, ehh = (up - dn) * 0.5f;
                        float q0[4] = { 1.0f / ehw, 0, -ecx / ehw, 0 }, q1[4] = { 0, 1.0f / ehh, -ecy / ehh, 0 };
                        memcpy(g_eyerow[k][0], q0, sizeof(q0)); memcpy(g_eyerow[k][1], q1, sizeof(q1));
                    }
                    g_fov_ok = 1;
                    vr_log("VISOR: campo de vision total %.1f x %.1f grados. Trozo izq u=%.3f-%.3f v=%.3f-%.3f, der u=%.3f-%.3f",
                           (atanf(R) - atanf(L)) * RAD2DEG, (atanf(UP) - atanf(DN)) * RAD2DEG,
                           g_eyeb[0][0], g_eyeb[0][2], g_eyeb[0][1], g_eyeb[0][3], g_eyeb[1][0], g_eyeb[1][2]);
                }
            }
            vr_log("VISOR: SteamVR usa la grafica numero %d", (int)ai);
            vr_log("VISOR: conectado a SteamVR (%s)", vers[i] + 8);
        }
    }
    if (!p_GetPose) { vr_log("VISOR: SteamVR abierto pero no da el interfaz IVRSystem"); g_head_state = -1; return; }
    if (cfg_vrimg) {
        const char *cv[] = { "FnTable:IVRCompositor_029", "FnTable:IVRCompositor_028", "FnTable:IVRCompositor_027" };
        for (int i = 0; i < 3 && !p_Submit; i++) {
            err = 0;
            void **tbl = (void **)p_VR_GetIf(cv[i], &err);
            if (tbl && err == 0) {
                p_WaitGetPoses = (PFN_WaitGetPoses)tbl[2];
                p_Submit       = (PFN_Submit)tbl[6];
                p_GetFrameTiming = (PFN_GetFrameTiming)tbl[10];
                p_GetTS = (PFN_GetTS)tbl[1];
                vr_log("VISOR: compositor listo (%s): la imagen ira al visor", cv[i] + 8);
            }
        }
        if (!p_Submit) vr_log("VISOR: no hay compositor; solo se leera el visor");
        if (0) {   /* (E20) overlay de SteamVR: quitado en la E20d (pantalla propia) */
            int err = 0;
            void **tbl = (void **)p_VR_GetIf("FnTable:IVROverlay_028", &err);
            if (tbl && err == 0) { g_ovl = tbl; vr_log("CINE: overlay listo (IVROverlay_028)"); }
            else vr_log("CINE: no hay overlay (err=%d); los menus se veran como antes", err);
        }
    }
    g_head_state = 1;
}

/* ---- imagen al visor: copiamos la imagen final del juego y se la damos a SteamVR ---- */

/* (E10d) F10 = FOTO: guarda lo que ve cada ojo en %TEMP%\metro_vr_ojo_izq.bmp y _der.bmp (a mitad de tamano) */
static volatile LONG g_photo = 0;    /* bits: 1 = falta el ojo izq, 2 = falta el der */
static void photo_save(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *src, const D3D11_TEXTURE2D_DESC *d, int eye)
{
    int bgra;
    switch ((int)d->Format) {
        case 27: case 28: case 29: bgra = 0; break;          /* R8G8B8A8 */
        case 87: case 88: case 90: case 91: bgra = 1; break; /* B8G8R8A8 */
        default: vr_log("FOTO: formato de imagen %d no soportado", (int)d->Format); return;
    }
    D3D11_TEXTURE2D_DESC t = *d;
    t.MipLevels = 1; t.ArraySize = 1; t.SampleDesc.Count = 1; t.SampleDesc.Quality = 0;
    t.Usage = D3D11_USAGE_STAGING; t.BindFlags = 0; t.CPUAccessFlags = D3D11_CPU_ACCESS_READ; t.MiscFlags = 0;
    ID3D11Texture2D *st = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &t, NULL, &st)) || !st) { vr_log("FOTO: no pude crear la copia"); return; }
    if (d->SampleDesc.Count > 1) { vr_log("FOTO: imagen con antialiasing (MSAA), no se puede"); ID3D11Texture2D_Release(st); return; }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)src);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) { ID3D11Texture2D_Release(st); return; }
    UINT W = d->Width / 2, H = d->Height / 2, row = (W * 3 + 3) & ~3u;
    BYTE *out = (BYTE *)malloc((size_t)row * H);
    if (out) {
        for (UINT y = 0; y < H; y++) {
            const BYTE *sp = (const BYTE *)m.pData + (size_t)(y * 2) * m.RowPitch;
            BYTE *dp = out + (size_t)(H - 1 - y) * row;        /* BMP va de abajo a arriba */
            for (UINT x = 0; x < W; x++) {
                const BYTE *px = sp + x * 8;
                dp[x * 3 + 0] = bgra ? px[0] : px[2];
                dp[x * 3 + 1] = px[1];
                dp[x * 3 + 2] = bgra ? px[2] : px[0];
            }
        }
        char path[MAX_PATH]; GetTempPathA(MAX_PATH, path);
        strcat(path, eye ? "metro_vr_ojo_der.bmp" : "metro_vr_ojo_izq.bmp");
        FILE *fp = fopen(path, "wb");
        if (fp) {
            BYTE hd[54]; memset(hd, 0, sizeof(hd));
            UINT fsz = 54 + row * H;
            hd[0] = 'B'; hd[1] = 'M'; *(UINT *)(hd + 2) = fsz; *(UINT *)(hd + 10) = 54;
            *(UINT *)(hd + 14) = 40; *(INT32 *)(hd + 18) = (INT32)W; *(INT32 *)(hd + 22) = (INT32)H;
            *(WORD *)(hd + 26) = 1; *(WORD *)(hd + 28) = 24; *(UINT *)(hd + 34) = row * H;
            fwrite(hd, 1, 54, fp); fwrite(out, 1, (size_t)row * H, fp); fclose(fp);
            vr_log("FOTO: guardado %s (%ux%u)", path, W, H);
        }
        free(out);
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    ID3D11Texture2D_Release(st);
}
static ID3D11Texture2D *g_vr_tex = NULL;      /* (ojo 0) la copia, vista desde el dispositivo del JUEGO */
static ID3D11Texture2D *g_vr_own = NULL;      /* (ojo 0) la misma copia, vista desde NUESTRO dispositivo (la que va a SteamVR) */
static ID3D11Texture2D *g_vr_tex2 = NULL, *g_vr_own2 = NULL;   /* ojo 1 (para el estereo) */
static ID3D11Device *g_vr_dev = NULL;
static int g_vr_dev_tried = 0;

static void log_adapter(const char *who, IDXGIAdapter *a)
{
    DXGI_ADAPTER_DESC ad;
    if (a && SUCCEEDED(IDXGIAdapter_GetDesc(a, &ad))) {
        char name[128]; WideCharToMultiByte(CP_ACP, 0, ad.Description, -1, name, sizeof(name), NULL, NULL);
        vr_log("VISOR: grafica de %s: %s (LUID %08lX:%08lX)", who, name, (unsigned long)ad.AdapterLuid.HighPart, (unsigned long)ad.AdapterLuid.LowPart);
    }
}

/* crea una textura en NUESTRO dispositivo y la abre en el del juego */
static int make_shared_pair(ID3D11Device *gamedev, const D3D11_TEXTURE2D_DESC *t, ID3D11Texture2D **own, ID3D11Texture2D **game)
{
    HANDLE sh = NULL;
    *own = NULL; *game = NULL;
    if (FAILED(ID3D11Device_CreateTexture2D(g_vr_dev, t, NULL, own)) || !*own) return 0;
    IDXGIResource *dr = NULL;
    if (SUCCEEDED(ID3D11Texture2D_QueryInterface(*own, &IID_IDXGIResource, (void **)&dr)) && dr) {
        IDXGIResource_GetSharedHandle(dr, &sh);
        IDXGIResource_Release(dr);
    }
    if (!sh || FAILED(ID3D11Device_OpenSharedResource(gamedev, sh, &IID_ID3D11Texture2D, (void **)game)) || !*game) {
        ID3D11Texture2D_Release(*own); *own = NULL; *game = NULL;
        return 0;
    }
    return 1;
}

/* Nuestro propio dispositivo D3D11 en la grafica que diga SteamVR: con el, SteamVR si acepta la imagen. */
static void vr_make_device(ID3D11Device *game_dev)
{
    if (g_vr_dev_tried) return;
    g_vr_dev_tried = 1;
    IDXGIDevice *gd = NULL; IDXGIAdapter *ga = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(game_dev, &IID_IDXGIDevice, (void **)&gd)) && gd) {
        if (SUCCEEDED(IDXGIDevice_GetAdapter(gd, &ga)) && ga) { log_adapter("el juego", ga); IDXGIAdapter_Release(ga); }
        IDXGIDevice_Release(gd);
    }
    IDXGIFactory1 *fac = NULL; IDXGIAdapter1 *va = NULL;
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&fac)) || !fac) { vr_log("VISOR: sin fabrica DXGI"); return; }
    if (FAILED(IDXGIFactory1_EnumAdapters1(fac, g_vr_adapter >= 0 ? (UINT)g_vr_adapter : 0, &va)) || !va) {
        vr_log("VISOR: no encuentro la grafica %d", g_vr_adapter); IDXGIFactory1_Release(fac); return;
    }
    log_adapter("SteamVR", (IDXGIAdapter *)va);
    HMODULE m = real_d3d11();
    PFN_D3D11_CREATE_DEVICE f = m ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice") : NULL;
    HRESULT hr = f ? f((IDXGIAdapter *)va, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &g_vr_dev, NULL, NULL) : E_FAIL;
    vr_log("VISOR: dispositivo propio para SteamVR: %s (hr=0x%08lX)", SUCCEEDED(hr) ? "OK" : "FALLO", (unsigned long)hr);
    IDXGIAdapter1_Release(va);
    IDXGIFactory1_Release(fac);
}
static UINT g_vr_w = 0, g_vr_h = 0;
static ID3D11Device *g_vr_gamedev = NULL;   /* dispositivo del juego con el que se abrio la copia */
static DXGI_FORMAT g_vr_fmt = DXGI_FORMAT_UNKNOWN;
static volatile LONG g_vr_submits = 0, g_vr_errs = 0;

/* suelta la copia de la imagen (antes de que el juego cambie el tamano de su pantalla) */
static void vr_drop_copy(void)
{
    if (g_vr_tex) { ID3D11Texture2D_Release(g_vr_tex); g_vr_tex = NULL; }
    if (g_vr_own) { ID3D11Texture2D_Release(g_vr_own); g_vr_own = NULL; }
    if (g_vr_tex2) { ID3D11Texture2D_Release(g_vr_tex2); g_vr_tex2 = NULL; }
    if (g_vr_own2) { ID3D11Texture2D_Release(g_vr_own2); g_vr_own2 = NULL; }
    g_vr_w = g_vr_h = 0;
}

static int vr_active(void) { return cfg_vrimg && g_head_state == 1 && p_Submit != NULL; }

/* (E20) CINE. Fuera de la partida (menus, intro, videos, opciones) la imagen del juego se pone en una pantalla
 * flotante delante de ti y los ojos ven negro. Indices de la tabla de IVROverlay_028 (openvr_capi.h de Valve). */
typedef int (*PFN_OvlCreate)(const char *key, const char *name, uint64_t *h);
typedef int (*PFN_OvlFlag)(uint64_t h, int flag, unsigned char on);
typedef int (*PFN_OvlF)(uint64_t h, float v);
typedef int (*PFN_OvlXform)(uint64_t h, int origin, const OvrMat34 *m);
typedef int (*PFN_OvlTex)(uint64_t h, const OvrTexture *t);
static OvrMat34 g_cine_M;
static void cine_hide(void)
{
    if (g_cine_on) vr_log("CINE: fuera (vuelve el VR)");
    g_cine_on = 0;
}
/* coloca la pantalla a cfg_cine_dist delante de donde miras (solo el giro, recta) */
static void cine_place(void)
{
    if (!g_have_frame_pose) return;
    const OvrMat34 *H = &g_frame_pose;
    float fx = -H->m[0][2], fz = -H->m[2][2], l = sqrtf(fx * fx + fz * fz);
    if (l < 1e-3f) { fx = 0; fz = -1; } else { fx /= l; fz /= l; }
    OvrMat34 M; memset(&M, 0, sizeof(M));
    /* columnas: X = derecha, Y = arriba, Z = hacia ti */
    M.m[0][0] = -fz; M.m[1][0] = 0; M.m[2][0] = fx;
    M.m[0][1] = 0;   M.m[1][1] = 1; M.m[2][1] = 0;
    M.m[0][2] = -fx; M.m[1][2] = 0; M.m[2][2] = -fz;
    M.m[0][3] = H->m[0][3] + fx * cfg_cine_dist;
    M.m[1][3] = H->m[1][3];
    M.m[2][3] = H->m[2][3] + fz * cfg_cine_dist;
    g_cine_M = M;
    vr_log("CINE: pantalla colocada a %.1f m delante", cfg_cine_dist);
}
/* (E20d) PANTALLA FLOTANTE PROPIA (sin el overlay de SteamVR): en cada ojo se pinta, sobre negro, un rectangulo
 * con la imagen del juego, colocado en la habitacion a cfg_cine_dist delante. Se le da a SteamVR como cualquier
 * imagen de ojo (con la postura de la cabeza), asi la pantalla se queda quieta en el sitio aunque muevas la cabeza. */
#define CINE_RES 1440
static ID3D11DeviceContext *g_cine_ctx = NULL;
static ID3D11Texture2D *g_cine_tex[2]; static ID3D11RenderTargetView *g_cine_rtv[2];
static ID3D11VertexShader *g_cine_vs = NULL; static ID3D11PixelShader *g_cine_ps = NULL;
static ID3D11Buffer *g_cine_cb = NULL; static ID3D11SamplerState *g_cine_smp = NULL; static ID3D11RasterizerState *g_cine_rs = NULL;
static ID3D11ShaderResourceView *g_cine_srv = NULL; static void *g_cine_srv_of = NULL;
static int g_cine_ready = 0;   /* 0 sin preparar, 1 listo, -1 no se puede */
static const char g_cine_hlsl[] =
    "cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
    "Texture2D T : register(t0); SamplerState S : register(s0);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint i : SV_VertexID) { V o; float2 c = float2(i & 1, i >> 1); o.uv = float2(c.x, 1 - c.y);\n"
    "  o.p = mul(mvp, float4(c.x - 0.5, c.y - 0.5, 0, 1)); return o; }\n"
    "float4 ps(V i) : SV_Target { return float4(T.Sample(S, i.uv).rgb, 1); }\n";
typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
static ID3DBlob *cine_compile(PFN_D3DCompile f, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = f(g_cine_hlsl, sizeof(g_cine_hlsl) - 1, "cine", NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) vr_log("CINE: no compila %s: %s", entry, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
    if (err) ID3D10Blob_Release(err);
    return SUCCEEDED(hr) ? code : NULL;
}
static int cine_init(void)
{
    if (g_cine_ready) return g_cine_ready == 1;
    g_cine_ready = -1;
    if (!g_vr_dev || !g_eraw_ok) return 0;
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN_D3DCompile f = m ? (PFN_D3DCompile)(void *)GetProcAddress(m, "D3DCompile") : NULL;
    if (!f) { vr_log("CINE: falta d3dcompiler_47.dll"); return 0; }
    ID3DBlob *bv = cine_compile(f, "vs", "vs_5_0"), *bp = cine_compile(f, "ps", "ps_5_0");
    if (!bv || !bp) return 0;
    ID3D11Device *d = g_vr_dev;
    ID3D11Device_CreateVertexShader(d, ID3D10Blob_GetBufferPointer(bv), ID3D10Blob_GetBufferSize(bv), NULL, &g_cine_vs);
    ID3D11Device_CreatePixelShader(d, ID3D10Blob_GetBufferPointer(bp), ID3D10Blob_GetBufferSize(bp), NULL, &g_cine_ps);
    ID3D10Blob_Release(bv); ID3D10Blob_Release(bp);
    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(d, &bd, NULL, &g_cine_cb);
    D3D11_SAMPLER_DESC sd; memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ID3D11Device_CreateSamplerState(d, &sd, &g_cine_smp);
    D3D11_RASTERIZER_DESC rd; memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(d, &rd, &g_cine_rs);
    for (int e = 0; e < 2; e++) {
        D3D11_TEXTURE2D_DESC t; memset(&t, 0, sizeof(t));
        t.Width = t.Height = CINE_RES; t.MipLevels = t.ArraySize = 1; t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        t.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(ID3D11Device_CreateTexture2D(d, &t, NULL, &g_cine_tex[e])) || !g_cine_tex[e]) return 0;
        if (FAILED(ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource *)g_cine_tex[e], NULL, &g_cine_rtv[e]))) return 0;
    }
    ID3D11Device_GetImmediateContext(d, &g_cine_ctx);
    if (!g_cine_vs || !g_cine_ps || !g_cine_cb || !g_cine_smp || !g_cine_rs || !g_cine_ctx) return 0;
    g_cine_ready = 1;
    vr_log("CINE: pantalla flotante propia lista (%dx%d por ojo)", CINE_RES, CINE_RES);
    return 1;
}
/* 4x4 (fila mayor) = A * B */
static void m4mul(const float A[4][4], const float B[4][4], float C[4][4])
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float t = 0; for (int k = 0; k < 4; k++) t += A[i][k] * B[k][j]; C[i][j] = t; }
}
static void m34to44(const OvrMat34 *a, float o[4][4])
{
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) o[i][j] = a->m[i][j];
    o[3][0] = o[3][1] = o[3][2] = 0; o[3][3] = 1;
}
/* 1 = esta imagen va a la pantalla flotante (y los ojos ya se han mandado) */
static int cine_frame(void *own_tex)
{
    if (!cfg_cine || g_mode == 1 || !g_have_frame_pose) { cine_hide(); return 0; }
    if (!cine_init()) return 0;
    if (g_cine_srv_of != own_tex) {
        if (g_cine_srv) { ID3D11ShaderResourceView_Release(g_cine_srv); g_cine_srv = NULL; }
        ID3D11Device_CreateShaderResourceView(g_vr_dev, (ID3D11Resource *)own_tex, NULL, &g_cine_srv);
        g_cine_srv_of = own_tex;
    }
    if (!g_cine_srv) return 0;
    if (!g_cine_on) { cine_place(); g_cine_on = 1; vr_log("CINE: dentro (menu/video/pausa en pantalla flotante)"); }
    float W = cfg_cine_ancho, Hh = (g_vr_w > 0) ? cfg_cine_ancho * (float)g_vr_h / (float)g_vr_w : cfg_cine_ancho * 0.5625f;
    float Mq[4][4], S[4][4] = { { W, 0, 0, 0 }, { 0, Hh, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } }, MS[4][4];
    m34to44(&g_cine_M, Mq); m4mul(Mq, S, MS);
    OvrMat34 pose = g_frame_pose;
    float Hd[4][4]; m34to44(&pose, Hd);
    ID3D11DeviceContext *c = g_cine_ctx;
    float black[4] = { 0, 0, 0, 1 };
    for (int e = 0; e < 2; e++) {
        float E[4][4], HE[4][4], V[4][4];
        m34to44(&g_e2h[e], E); m4mul(Hd, E, HE);
        /* inversa de un giro + posicion */
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) V[i][j] = HE[j][i];
        for (int i = 0; i < 3; i++) V[i][3] = -(V[i][0] * HE[0][3] + V[i][1] * HE[1][3] + V[i][2] * HE[2][3]);
        V[3][0] = V[3][1] = V[3][2] = 0; V[3][3] = 1;
        /* lente del ojo (como ComposeProjection de OpenVR), cerca 0.05 lejos 100 */
        float l = g_eraw[e][0], r = g_eraw[e][1], t = g_eraw[e][2], b = g_eraw[e][3], zn = 0.05f, zf = 100.0f;
        float idx = 1.0f / (r - l), idy = 1.0f / (b - t);
        float P[4][4] = { { 2 * idx, 0, (r + l) * idx, 0 }, { 0, 2 * idy, (b + t) * idy, 0 },
                          { 0, 0, zf / (zn - zf), zf * zn / (zn - zf) }, { 0, 0, -1, 0 } };
        float PV[4][4], MVP[4][4];
        m4mul(P, V, PV); m4mul(PV, MS, MVP);
        ID3D11DeviceContext_UpdateSubresource(c, (ID3D11Resource *)g_cine_cb, 0, NULL, MVP, 0, 0);
        ID3D11DeviceContext_OMSetRenderTargets(c, 1, &g_cine_rtv[e], NULL);
        ID3D11DeviceContext_ClearRenderTargetView(c, g_cine_rtv[e], black);
        D3D11_VIEWPORT vp = { 0, 0, (float)CINE_RES, (float)CINE_RES, 0, 1 };
        ID3D11DeviceContext_RSSetViewports(c, 1, &vp);
        ID3D11DeviceContext_RSSetState(c, g_cine_rs);
        ID3D11DeviceContext_IASetInputLayout(c, NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(c, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_VSSetShader(c, g_cine_vs, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(c, 0, 1, &g_cine_cb);
        ID3D11DeviceContext_PSSetShader(c, g_cine_ps, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &g_cine_srv);
        ID3D11DeviceContext_PSSetSamplers(c, 0, 1, &g_cine_smp);
        ID3D11DeviceContext_Draw(c, 4, 0);
    }
    ID3D11RenderTargetView *nullrt = NULL; ID3D11ShaderResourceView *nullsrv = NULL;
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &nullrt, NULL);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 1, &nullsrv);
    ID3D11DeviceContext_Flush(c);
    typedef struct { void *handle; int type; int colorspace; OvrMat34 pose; } OvrTexPose2;
    OvrTexPose2 t0 = { g_cine_tex[0], 0, 0, pose }, t1 = { g_cine_tex[1], 0, 0, pose };
    OvrBounds bb = { 0, 0, 1, 1 };
    int e1 = p_Submit(0, (OvrTexture *)&t0, &bb, 8), e2 = p_Submit(1, (OvrTexture *)&t1, &bb, 8);
    static int se = 0;
    if ((e1 || e2) && se++ < 5) vr_log("CINE: Submit fallo (%d %d)", e1, e2);
    return 1;
}

static void vr_submit_frame(IDXGISwapChain *sc)
{
    if (!cfg_vrimg || g_head_state != 1 || !p_Submit) return;
    ID3D11Texture2D *bb = NULL;
    if (FAILED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) || !bb) return;
    g_bb_ptr = (void *)bb;   /* (E11) para reconocer la imagen final al dibujar el HUD */
    D3D11_TEXTURE2D_DESC d;
    ID3D11Texture2D_GetDesc(bb, &d);
    ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL;
    ID3D11Texture2D_GetDevice(bb, &dev);
    if (dev) ID3D11Device_GetImmediateContext(dev, &ctx);
    if (ctx && ctx != g_hud_ctx && (cfg_hud || cfg_brazos || cfg_dosojos)) {   /* (E17c) el contexto que de verdad usa el juego */
        vr_log("VTABLE: el juego pinta con el contexto %p (los desvios estaban en %p): me cambio", (void *)ctx, (void *)g_hud_ctx);
        g_hud_ctx = ctx;
    }
    if (dev && ctx) {
        vr_make_device(dev);
        static int vr_fail = 0;
        if (g_vr_dev && vr_fail < 3 && (!g_vr_tex || g_vr_gamedev != dev || g_vr_w != d.Width || g_vr_h != d.Height || g_vr_fmt != d.Format)) {
            if (g_vr_tex && g_vr_gamedev != dev) vr_log("VISOR: el juego ha cambiado de dispositivo grafico (p.ej. al cambiar opciones): rehago la copia");
            vr_drop_copy();
            D3D11_TEXTURE2D_DESC t = d;
            t.MipLevels = 1; t.ArraySize = 1;
            t.SampleDesc.Count = 1; t.SampleDesc.Quality = 0;
            t.Usage = D3D11_USAGE_DEFAULT;
            t.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            t.CPUAccessFlags = 0; t.MiscFlags = D3D11_RESOURCE_MISC_SHARED;   /* SteamVR la abre desde su proceso: tiene que ser "compartible" */
            /* se crean en NUESTRO dispositivo y el juego las abre por su "llave" compartida (una por ojo) */
            if (make_shared_pair(dev, &t, &g_vr_own, &g_vr_tex) && !make_shared_pair(dev, &t, &g_vr_own2, &g_vr_tex2))
                vr_log("VISOR: no pude crear la copia del segundo ojo (el estereo no ira)");
            if (g_vr_tex) {
                g_vr_w = d.Width; g_vr_h = d.Height; g_vr_fmt = d.Format; g_vr_gamedev = dev;
                if (d.Height) g_aspect = (float)d.Width / (float)d.Height;
                vr_log("VISOR: copia de la imagen %ux%u formato=%d muestras=%u", d.Width, d.Height, (int)d.Format, d.SampleDesc.Count);
            } else {
                vr_log("VISOR: no pude crear la copia de la imagen");
                vr_fail++;
            }
        }
        if (g_vr_tex) {
            int stereo = g_stereo_on && g_vr_tex2;
            /* (E27) dos ojos: la imagen final del ojo derecho esta en la COPIA de la imagen final */
            LONG twm = g_twin_on;
            ID3D11Texture2D *src = bb, *srcR = NULL;
            if (twm) {
                ID3D11Texture2D *tb = tw_twin_tex(bb);
                if (!tb && InterlockedIncrement(&g_tw_nobb) <= 3) vr_log("DOS OJOS: la imagen final aun no tiene copia (mando la del juego)");
                if (twm == 1 && tb) src = tb;
                if (twm == 2) srcR = tb ? tb : bb;
            }
            int both = stereo && twm == 2;
            LONG eye = (stereo && !both) ? g_eye : 0;
            ID3D11Texture2D *dst = eye ? g_vr_tex2 : g_vr_tex;
            t_tw_raw++;
            if (d.SampleDesc.Count > 1)
                ID3D11DeviceContext_ResolveSubresource(ctx, (ID3D11Resource *)dst, 0, (ID3D11Resource *)src, 0, d.Format);
            else
                ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)dst, (ID3D11Resource *)src);
            if (both) {
                if (d.SampleDesc.Count > 1)
                    ID3D11DeviceContext_ResolveSubresource(ctx, (ID3D11Resource *)g_vr_tex2, 0, (ID3D11Resource *)srcR, 0, d.Format);
                else
                    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)g_vr_tex2, (ID3D11Resource *)srcR);
            }
            t_tw_raw--;
            ID3D11DeviceContext_Flush(ctx);     /* que la copia llegue antes de que SteamVR la lea */
            if (!stereo && cine_frame(g_vr_own)) goto done_submit;   /* (E20) menus/videos: pantalla flotante */
            if (g_photo) {
                int pe = stereo ? (int)eye : 0;
                if (g_photo & (1 << pe)) { photo_save(dev, ctx, bb, &d, pe); InterlockedAnd(&g_photo, ~(1 << pe)); }
                if (!stereo) g_photo = 0;
            }
            if (g_have_frame_pose) {
                if (cfg_pose_cam && g_mode == 1 && InterlockedExchange(&g_eye_rpose_ok[eye], 0)) {   /* (E22) */
                    g_eye_pose[eye] = g_eye_rpose[eye];
                    if (memcmp(&g_eye_rpose[eye], &g_frame_pose, sizeof(OvrMat34))) InterlockedIncrement(&g_pose_diff); else InterlockedIncrement(&g_pose_same);
                } else g_eye_pose[eye] = g_frame_pose;
            }
            if (!stereo || both) g_eye_pose[1] = g_eye_pose[0];
            /* textura + postura con la que se dibujo: asi SteamVR recoloca bien cada ojo aunque su imagen sea de antes */
            typedef struct { void *handle; int type; int colorspace; OvrMat34 pose; } OvrTexPose;
            OvrTexPose t0 = { g_vr_own, 0, 0, g_eye_pose[0] };
            OvrTexPose t1 = { stereo ? g_vr_own2 : g_vr_own, 0, 0, g_eye_pose[1] };
            int fl = g_have_frame_pose ? 8 /* Submit_TextureWithPose */ : 0;
            /* mono con lente del visor: cada ojo coge su trozo de la union. Estereo: cada imagen ya es de su ojo */
            int fov_on = cfg_fov && g_fov_ok && g_mode == 1;
            OvrBounds b0 = { 0.0f, 0.0f, 1.0f, 1.0f }, b1 = b0;
            if (fov_on && !stereo) {
                b0.uMin = g_eyeb[0][0]; b0.vMin = g_eyeb[0][1]; b0.uMax = g_eyeb[0][2]; b0.vMax = g_eyeb[0][3];
                b1.uMin = g_eyeb[1][0]; b1.vMin = g_eyeb[1][1]; b1.uMax = g_eyeb[1][2]; b1.vMax = g_eyeb[1][3];
            }
            /* sincronizado: tras el ojo izquierdo aun no se manda nada; tras el derecho, la pareja entera */
            if (stereo && cfg_stereo == 2 && eye == 0 && !both) goto done_submit;
            int e1 = p_Submit(0, (OvrTexture *)&t0, &b0, fl);    /* ojo izquierdo */
            int e2 = p_Submit(1, (OvrTexture *)&t1, &b1, fl);    /* ojo derecho */
            if (e1 || e2) {
                if (InterlockedIncrement(&g_vr_errs) <= 5) vr_log("VISOR: Submit fallo (izq=%d der=%d)", e1, e2);
            } else if (InterlockedIncrement(&g_vr_submits) == 1) {
                vr_log("VISOR: primera imagen enviada al visor");
            }
        }
    }
done_submit:
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dev) ID3D11Device_Release(dev);
    ID3D11Texture2D_Release(bb);
}

/* SteamVR quiere que la "escena" le pida las posturas una vez por fotograma (tambien marca el ritmo) */
static void vr_after_present(void)
{
    /* SIN visor: cabeza falsa con las teclas, para poder probar (y capturar con RenderDoc) sin el visor */
    if (cfg_fakehead && !(cfg_vrimg && g_head_state == 1 && p_WaitGetPoses)) {
        OvrPose fp; memset(&fp, 0, sizeof(fp));
        float fy = g_yaw, fx = g_pitch;
        if (cfg_fakeauto && g_mode == 1 && g_rt_cal == 2) {
            /* (E8f) cabeza falsa que se mueve sola (para ver y capturar el fallo sin tocar teclas) */
            double t = (double)GetTickCount64() / 1000.0;
            fy = (float)(35.0 * sin(6.2831853 * t / 3.0));
            fx = (float)(8.0 * sin(6.2831853 * t / 2.3));
        }
        float y = -fy * 0.01745329f, x = fx * 0.01745329f;     /* OpenVR: girar a la derecha = -Y */
        float cy = cosf(y), sy = sinf(y), cx = cosf(x), sx = sinf(x);
        float R[3][3] = { { cy, sy * sx, sy * cx }, { 0, cx, -sx }, { -sy, cy * sx, cy * cx } };   /* Ry(y) * Rx(x) */
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) fp.mat.m[i][j] = R[i][j];
        fp.valid = 1;
        if (head_apply(&fp)) publish_H();
        raton_step();
        return;
    }
    if (!cfg_vrimg || g_head_state != 1 || !p_WaitGetPoses) return;
    /* el siguiente fotograma es para el otro ojo */
    {
        LONG want = cfg_stereo && cfg_fov && g_fov_ok && g_mode == 1 && g_vr_tex2;
        if (want != g_stereo_on) {
            g_stereo_on = want; g_eye = 0; g_freeze = 0;
            vr_log(">>> ESTEREO %s", !want ? "apagado" : (cfg_stereo == 2 ? "SINCRONIZADO (reloj parado en el 2o ojo)" : "alternos (un ojo por fotograma)"));
            if (want && cfg_stereo == 2 && !o_QPC) vr_log("AVISO: el reloj del juego no esta enganchado; los ojos no iran sincronizados");
            pair_begin();
        } else if (want && g_twin_on == 2) {
            /* (E27) los dos ojos en cada fotograma: cada fotograma es una pareja entera, sin parar el reloj */
            g_eye = 0; g_freeze = 0;
            pair_begin();
        } else if (want && cfg_stereo == 2) {
            if (g_eye == 0) {
                /* acaba de salir el ojo izquierdo: ahora el derecho, con el reloj parado y SIN esperar a SteamVR */
                g_eye = 1;
                g_draw_tid = GetCurrentThreadId();
                InterlockedIncrement(&g_freeze_epoch);
                g_freeze = 1;
                {
                    static int logged = 0;
                    if (!logged++) vr_log("RELOJ: el hilo que dibuja (Present) es el %lu", (unsigned long)GetCurrentThreadId());
                }
                return;
            }
            /* acaba de salir el derecho: pareja completa. Reloj en marcha y a por la siguiente */
            g_freeze = 0;
            g_eye = 0;
            pair_begin();
        } else {
            g_eye = want ? 1 - g_eye : 0;
        }
        if (!want) g_freeze = 0;
    }
    OvrPose rp[16];
    memset(rp, 0, sizeof(rp));
    LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0);
    p_WaitGetPoses(rp, 16, NULL, 0);
    reload_update(rp);  /* (E36) antes que hand_update: decide si el agarre izq es cargador o dos manos */
    hand_update(rp);
    lhand_update(rp);   /* (E31) */
    QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
    InterlockedExchangeAdd(&g_wait_us, (LONG)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart));
    g_fr_wait_us = (LONG)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart);   /* (E25) */
    if (p_GetFrameTiming) {   /* (E12) lo que ha tardado la grafica en el ultimo fotograma que vio SteamVR */
        OvrFrameTiming ft; memset(&ft, 0, sizeof(ft)); ft.size = sizeof(ft);
        if (p_GetFrameTiming(&ft, 1) && ft.frameIndex != g_ft_last) {
            g_ft_last = ft.frameIndex;
            g_ft_gpu += ft.preSubmitGpu; g_ft_tot += ft.totalRenderGpu; g_ft_int += ft.clientInterval;
            g_ft_idle += ft.compIdleCpu; g_ft_comp += ft.compGpu;
            g_ft_drop += ft.numDropped; g_ft_mis += ft.numMisPresented; g_ft_pres += ft.numPresents;
            if (ft.reprojFlags) g_ft_reproj++;
            g_ft_n++;
        }
    }
    /* esta postura es la que SteamVR espera para el proximo fotograma: la usamos para dibujarlo */
    if (rp[0].valid) { g_frame_pose = rp[0].mat; g_have_frame_pose = 1; }
    if (cfg_head && head_apply(&rp[0])) publish_H();
    raton_step();
}


/* ------------------------------------------------ (E15) el arma en la mano derecha */
/* Mano respecto a la cabeza (SteamVR: x derecha, y arriba, z hacia atras) -> ejes del juego (z hacia delante).
 * Calibrar: con la mano donde ves el arma y apuntando adelante, mantener 1,5 s el clic del stick derecho.
 * Desde ahi, el arma (en la vista, respecto al centro de la cabeza) se mueve: G = mano_ahora * mano_calibrada^-1 */
static void hand_save(void)
{
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path); strcat(path, "metro_vr_mano.txt");
    FILE *fp = fopen(path, "w"); if (!fp) return;
    for (int i = 0; i < 3; i++) fprintf(fp, "%.6f %.6f %.6f %.6f\n", g_hand_C0[i][0], g_hand_C0[i][1], g_hand_C0[i][2], g_hand_C0[i][3]);
    /* (E28) y una linea por arma: "arma <pieza> <inicio> <12 numeros>" */
    for (int k = 0; k < g_nwcal; k++) {
        fprintf(fp, "arma %u %u", g_wcal[k].ic, g_wcal[k].si);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) fprintf(fp, " %.6f", g_wcal[k].C0[i][j]);
        fprintf(fp, "\n");
    }
    fclose(fp);
}
static void hand_load(void)
{
    static int done = 0; if (done) return; done = 1;
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path); strcat(path, "metro_vr_mano.txt");
    /* sin calibrar: mano 15 cm a la derecha, 25 cm abajo, 30 cm delante, mando apuntando adelante */
    memset(g_hand_C0, 0, sizeof(g_hand_C0));
    for (int i = 0; i < 4; i++) g_hand_C0[i][i] = 1.0f;
    g_hand_C0[0][3] = 0.15f; g_hand_C0[1][3] = -0.25f; g_hand_C0[2][3] = 0.30f;
    g_hand_cal = 0;
    FILE *fp = fopen(path, "r");
    if (!fp) { vr_log("MANO: sin calibrar (%s no existe)", path); return; }
    float Cg[4][4]; memset(Cg, 0, sizeof(Cg)); Cg[3][3] = 1.0f;
    char ln[512]; int row = 0;
    while (fgets(ln, sizeof(ln), fp)) {
        if (!strncmp(ln, "arma", 4)) {     /* (E28) calibracion propia de un arma */
            UINT ic, si; float m[12];
            if (g_nwcal < MAXW && sscanf(ln + 4, "%u %u %f %f %f %f %f %f %f %f %f %f %f %f", &ic, &si,
                       &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6], &m[7], &m[8], &m[9], &m[10], &m[11]) == 14) {
                WpnCal *w = &g_wcal[g_nwcal++];
                w->ic = ic; w->si = si;
                memset(w->C0, 0, sizeof(w->C0)); w->C0[3][3] = 1.0f;
                for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) w->C0[i][j] = m[i * 4 + j];
            }
        } else if (row < 3 && sscanf(ln, "%f %f %f %f", &Cg[row][0], &Cg[row][1], &Cg[row][2], &Cg[row][3]) == 4) row++;
    }
    fclose(fp);
    if (row == 3) memcpy(g_hand_C0, Cg, sizeof(Cg));
    g_hand_cal = (row == 3 || g_nwcal > 0);
    vr_log("MANO: calibracion general %s, %d arma(s) con calibracion propia (%s)", row == 3 ? "cargada" : "NO hay", g_nwcal, path);
    for (int k = 0; k < g_nwcal; k++) vr_log("MANO:   arma %u/%u: mano a %.2f %.2f %.2f m de la cabeza", g_wcal[k].ic, g_wcal[k].si,
                                             g_wcal[k].C0[0][3], g_wcal[k].C0[1][3], g_wcal[k].C0[2][3]);
}
/* (E34) postura de un mando (role 1 = izq, 2 = der) respecto al centro de la cabeza, en ejes del juego.
 * Es la misma cuenta que hacen hand_update y lhand_update. Devuelve 0 si no hay mando. */
/* (E36) RECARGA MANUAL (fase 1). Sin recarga automatica (ganchos en metro.exe+0x2BAA90 y +0x2B88C0). Gesto:
 * mano izq a la cadera izq (bajo la cabeza, a la izquierda segun hacia donde mira el cuerpo) + apretar el agarre = coger un
 * cargador (vibra). Con el agarre apretado, llevar la mano izq al mando der (el arma) = se pulsa R: recarga del juego con su
 * animacion; la mano izq va con el arma cfg_recarga_ms. Soltar el agarre antes = se tira el cargador. Todo en el espacio de
 * SteamVR (metros), sin el juego. */
static void haptic(uint32_t role, int us)
{
    typedef void (*PFN_Haptic)(uint32_t dev, uint32_t axis, unsigned short us);
    static PFN_Haptic p = NULL; static int tried = 0;
    if (!tried) {
        tried = 1; int err = 0;
        void **tbl = p_VR_GetIf ? (void **)p_VR_GetIf("FnTable:IVRSystem_026", &err) : NULL;
        if (tbl && !err) p = (PFN_Haptic)tbl[39];   /* TriggerHapticPulse (justo despues de GetControllerStateWithPose) */
    }
    if (!p || !p_RoleIdx) return;
    uint32_t i = p_RoleIdx(role); if (i >= 16) return;
    p(i, 0, (unsigned short)(us > 3999 ? 3999 : us));
}
static void reload_update(const OvrPose *rp)
{
    static int prev_grip = 0, buzz = 0; static ULONGLONG t_ins = 0;
    if (g_mode != 1) g_pw = NULL;   /* (E38) menus / cargas: el arma puede dejar de existir */
    if (!cfg_recarga_manual || g_mode != 1 || !p_RoleIdx || !rp[0].valid) { g_mag_state = 0; prev_grip = 0; return; }
    uint32_t li = p_RoleIdx(1), ri = p_RoleIdx(2);
    int grip = g_lgrip != 0;
    if (li >= 16 || !rp[li].valid) { prev_grip = grip; return; }
    const OvrMat34 *H = &rp[0].mat, *L = &rp[li].mat;
    /* cadera izq: 55 cm bajo la cabeza, 18 cm a la izquierda (solo el giro horizontal de la cabeza) */
    float fx = -H->m[0][2], fz = -H->m[2][2], fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1e-3f) { fx = 0; fz = -1; fl = 1; }
    fx /= fl; fz /= fl;
    float rx = -fz, rz = fx;   /* derecha = adelante girado 90 grados */
    float hip[3] = { H->m[0][3] - rx * 0.18f, H->m[1][3] - 0.55f, H->m[2][3] - rz * 0.18f };
    float dx = L->m[0][3] - hip[0], dy = L->m[1][3] - hip[1], dz = L->m[2][3] - hip[2];
    float dhip = sqrtf(dx * dx + dy * dy + dz * dz);
    float dgun = 9.0f;
    if (ri < 16 && rp[ri].valid) {
        const OvrMat34 *R = &rp[ri].mat;
        float ex = L->m[0][3] - R->m[0][3], ey = L->m[1][3] - R->m[1][3], ez = L->m[2][3] - R->m[2][3];
        dgun = sqrtf(ex * ex + ey * ey + ez * ez);
    }
    if (buzz > 0) { haptic(1, 3000); buzz--; }
    switch (g_mag_state) {
    case 0:
        if (grip && !prev_grip && dhip < cfg_cadera_cm * 0.01f) {
            g_mag_state = 1; buzz = 6; vr_log("RECARGA: cargador cogido de la cadera (mano a %.0f cm)", dhip * 100.0f);
        }
        break;
    case 1:
        if (!grip) { g_mag_state = 0; vr_log("RECARGA: cargador soltado sin meterlo"); break; }
        if (dgun < 0.20f && dhip > cfg_cadera_cm * 0.01f) {
            InterlockedExchange(&g_reload_req, 1); g_mag_state = 2; t_ins = GetTickCount64(); buzz = 4; haptic(2, 3000);
            g_out_ic = 0;   /* (E39) el cargador vuelve a verse en el arma */
            InterlockedIncrement(&g_mag_ins); vr_log("RECARGA: cargador al arma (manos a %.0f cm) -> recarga", dgun * 100.0f);
        }
        break;
    case 2:
        if (GetTickCount64() - t_ins > (ULONGLONG)cfg_recarga_ms) g_mag_state = 0;
        break;
    }
    prev_grip = grip;
}
/* (E36) ganchos: "toca recarga automatica?" (devuelve 0/1) y su variante de otras armas (hace la recarga ella misma) */
static int (*g_ar_tramp)(void *) = NULL;
static void (*g_ar2_tramp)(void *) = NULL;
/* (E37) es el arma del jugador? Misma cuenta que hace el juego en +0x2BAA90: dueno (arma+0x118) == jugador (global +0xD01EA8 -> +0x38, +0xA0) */
static int is_player_wpn(void *w)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    void **own = (void **)((BYTE *)w + 0x118);
    if (!*own || !*(void **)*own) return 0;
    BYTE *g = *(BYTE **)(base + 0xD01EA8); if (!g) return 0;
    BYTE *pl = *(BYTE **)(g + 0x38); if (!pl) return 0;
    typedef char (*PFN_Same)(void *, void *);
    return ((PFN_Same)(base + 0xDEB0))(own, pl + 0xA0) != 0;
}
/* (E37) expulsar: balas del cargador a 0 (virtual +0x1350 = poner balas cargadas, la que usa cargar partida) */
static volatile UINT g_pw_ic = 0, g_pw_si = 0;   /* (E38) a que arma visible corresponde g_pw */
static void expel_do(void *w)
{
    if (!InterlockedExchange(&g_expel_req, 0)) return;
    void **vt = *(void ***)w;
    typedef int (*PFN_Get)(void *); typedef void (*PFN_Set)(void *, int);
    int before = ((PFN_Get)vt[0xEC8 / 8])(w);
    ((PFN_Set)vt[0x1350 / 8])(w, 0);
    InterlockedIncrement(&g_expel_n);
    vr_log("RECARGA: cargador expulsado (tenia %d balas)", before);
}
/* (E37/E38) el gancho de la recarga solo se llama al DISPARAR: aqui se apunta cual es el arma del jugador (y que arma se ve),
 * y si B estaba pedido y aun no se habia hecho, se hace ya (caso: arma recien cambiada) */
static void expel_check(void *w)
{
    if (!is_player_wpn(w)) return;
    g_pw = w; g_pw_ic = g_wpn_ic; g_pw_si = g_wpn_si;
    if (g_expel_req) expel_do(w);
}
/* (E38) cada paso del jugador (hilo del juego, ~100/s): si B esta pedido y sabemos cual es el arma que se ve, al momento */
static void expel_tick(void)
{
    void *w = g_pw;
    if (!g_expel_req || !w || g_mode != 1) return;
    if (g_pw_ic != g_wpn_ic || g_pw_si != g_wpn_si) return;   /* se cambio de arma: esperar a que dispare una vez */
    if (!is_player_wpn(w)) { g_pw = NULL; return; }
    expel_do(w);
}
static int ar_hook(void *w)
{
    if (cfg_recarga_manual && g_mode == 1) expel_check(w);   /* (E37) */
    int r = g_ar_tramp(w);
    if (r && cfg_recarga_manual && g_mode == 1) { InterlockedIncrement(&g_ar_blocked); return 0; }
    return r;
}
static void ar2_hook(void *w)
{
    if (cfg_recarga_manual && g_mode == 1) { expel_check(w); InterlockedIncrement(&g_ar2_blocked); return; }   /* todas sus salidas tempranas = no hacer nada */
    g_ar2_tramp(w);
}
static void ar_install(void)
{
    static int done = 0; if (done) return; done = 1;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    static const BYTE sig[12] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x8B, 0xD9 };
    struct { UINT32 rva; void *hk; void **tr; } h[2] = { { 0x2BAA90, (void *)ar_hook, (void **)&g_ar_tramp }, { 0x2B88C0, (void *)ar2_hook, (void **)&g_ar2_tramp } };
    mh_start();
    for (int k = 0; k < 2; k++) {
        BYTE *fn = base + h[k].rva;
        if (memcmp(fn, sig, sizeof(sig)) || *(UINT32 *)(fn + 14) != 0xEC8) {
            vr_log("RECARGA: metro.exe+0x%X no es la esperada (otra version del juego?): recarga automatica sin tocar", h[k].rva); continue;
        }
        MH_STATUS st = MH_CreateHook(fn, h[k].hk, h[k].tr);
        if (st == MH_OK) st = MH_EnableHook(fn);
        vr_log("RECARGA: gancho en metro.exe+0x%X -> %s", h[k].rva, MH_StatusToString(st));
    }
}
static int ctrl_rel(const OvrPose *rp, uint32_t role, uint32_t *last, float A[4][4])
{
    if (!p_RoleIdx || !rp[0].valid) return 0;
    uint32_t i = p_RoleIdx(role);
    if (i >= 16 && p_RoleOf) for (uint32_t k = 1; k < 16; k++) if (rp[k].valid && p_RoleOf(k) == role) { i = k; break; }
    if (i >= 16 && *last < 16 && rp[*last].valid) i = *last;
    if (i >= 16 || !rp[i].valid) return 0;
    *last = i;
    const OvrMat34 *Hm = &rp[0].mat, *Mm = &rp[i].mat;
    float d[3] = { Mm->m[0][3] - Hm->m[0][3], Mm->m[1][3] - Hm->m[1][3], Mm->m[2][3] - Hm->m[2][3] };
    float R[3][3], t[3];
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) R[a][b] = Hm->m[0][a] * Mm->m[0][b] + Hm->m[1][a] * Mm->m[1][b] + Hm->m[2][a] * Mm->m[2][b];
        t[a] = Hm->m[0][a] * d[0] + Hm->m[1][a] * d[1] + Hm->m[2][a] * d[2];
    }
    memset(A, 0, sizeof(float) * 16); A[3][3] = 1.0f;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) A[a][b] = R[a][b] * ((a == 2) != (b == 2) ? -1.0f : 1.0f);
    A[0][3] = t[0]; A[1][3] = t[1]; A[2][3] = -t[2];
    return 1;
}
/* (E34) AGARRE A DOS MANOS (como en Pavlov). Al agarrar se apunta DONDE esta tu mando izq visto desde la mano der
 * (d0, en ejes de la mano der). Mientras agarras, el arma gira alrededor de la mano der lo justo para que esa
 * direccion vuelva a apuntar a tu mando izq: mueves la izq y el canon la sigue; el giro de la muneca der (ladeo)
 * sigue siendo de la der. Al soltar, el giro se deshace en ~0,1 s (sin tiron). Cambia A (postura de la mano der). */
static void twohand_apply(const OvrPose *rp, float A[4][4], const float C0i[4][4])
{
    static int grab = 0; static float d0[3];
    static float ax[3] = { 0, 0, 1 }, ang = 0;            /* giro que se le da al arma (eje en ejes de la mano der, angulo) */
    static uint32_t last_li = 0xFFFFFFFFu;
    float L[4][4]; int lok = ctrl_rel(rp, 1, &last_li, L);
    int want = 0; float dist = -1.0f;
    if (lok && !g_lhand_hold) {
        if (cfg_dos_manos == 1) want = g_lgrip != 0 && g_mag_state != 1;   /* (E36) con el cargador en la mano no se agarra el arma */
        else if (cfg_dos_manos == 2) want = 1;
        else if (cfg_dos_manos == 3) {   /* (E32) automatico: mando izq cerca del sitio de agarre (G * C0_izq) */
            float G0[4][4], T[4][4]; mul4(A, C0i, G0); mul4(G0, g_lhand_C0, T);
            float dx = L[0][3] - T[0][3], dy = L[1][3] - T[1][3], dz = L[2][3] - T[2][3];
            float lim = cfg_dos_manos_cm * 0.01f;
            dist = sqrtf(dx * dx + dy * dy + dz * dz);
            want = grab ? (dist < lim * 1.8f) : (dist < lim);
        }
    }
    float d[3] = { 0, 0, 0 };   /* mando izq visto desde la mano der, en ejes de la mano der: R^T (pL - pR) */
    if (lok) for (int i = 0; i < 3; i++)
        d[i] = A[0][i] * (L[0][3] - A[0][3]) + A[1][i] * (L[1][3] - A[1][3]) + A[2][i] * (L[2][3] - A[2][3]);
    float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (want && !grab) {
        if (dl < 0.05f) want = 0;   /* manos casi juntas: no hay direccion */
        else {
            /* si aun se estaba deshaciendo un giro, se parte de el (sin salto): d0 = Q^-1 d */
            memcpy(d0, d, sizeof(d0));
            if (ang > 0.0f) {
                float c = cosf(-ang), sn = sinf(-ang), k[3] = { ax[0], ax[1], ax[2] };
                float kd = k[0] * d[0] + k[1] * d[1] + k[2] * d[2];
                float kx[3] = { k[1] * d[2] - k[2] * d[1], k[2] * d[0] - k[0] * d[2], k[0] * d[1] - k[1] * d[0] };
                for (int i = 0; i < 3; i++) d0[i] = d[i] * c + kx[i] * sn + k[i] * kd * (1.0f - c);
            }
        }
    }
    if (want != grab) {
        grab = want;
        if (cfg_dos_manos == 3) vr_log("MANO IZQ: DOS MANOS %s (mando a %.1f cm del sitio de agarre)", grab ? "SI" : "NO", dist * 100.0f);
        else vr_log("MANO IZQ: DOS MANOS %s (boton de agarre izq%s)", grab ? "SI" : "NO", cfg_dos_manos_ejes ? ", el arma apunta a la mano izq" : "");
    }
    g_dos_manos = grab;
    if (grab && cfg_dos_manos_ejes && dl > 0.05f) {
        float l0 = sqrtf(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]);
        float a[3] = { d0[0] / l0, d0[1] / l0, d0[2] / l0 }, b[3] = { d[0] / dl, d[1] / dl, d[2] / dl };
        float cx[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        float sl = sqrtf(cx[0] * cx[0] + cx[1] * cx[1] + cx[2] * cx[2]), co = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        if (sl > 1e-5f) {
            float an = atan2f(sl, co);
            if (an > 100.0f / RAD2DEG) an = 100.0f / RAD2DEG;   /* tope: mas alla no tiene sentido (manos cruzadas) */
            ax[0] = cx[0] / sl; ax[1] = cx[1] / sl; ax[2] = cx[2] / sl; ang = an;
        } else ang = 0.0f;
    } else if (ang > 0.0f) {
        ang *= 0.7f; if (ang < 0.002f) ang = 0.0f;   /* al soltar: vuelve a la mano der en ~0,1 s */
    }
    g_tw_deg10 = (LONG)(ang * RAD2DEG * 10.0f);
    if (ang <= 0.0f) return;
    /* Q = giro (Rodrigues) en ejes de la mano der; la mano der "nueva" = R * Q (misma posicion) */
    float c = cosf(ang), sn = sinf(ang), t = 1.0f - c, x = ax[0], y = ax[1], z = ax[2];
    float Q[3][3] = { { t * x * x + c,     t * x * y - sn * z, t * x * z + sn * y },
                      { t * x * y + sn * z, t * y * y + c,     t * y * z - sn * x },
                      { t * x * z - sn * y, t * y * z + sn * x, t * z * z + c     } };
    float R2[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R2[i][j] = A[i][0] * Q[0][j] + A[i][1] * Q[1][j] + A[i][2] * Q[2][j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) A[i][j] = R2[i][j];
}
static void hand_update(const OvrPose *rp)
{
    hand_load();
    if (!cfg_mano || !p_RoleIdx) { g_hand_ok = 0; g_hand_why = cfg_mano ? 2 : 1; return; }
    /* (E17b) SteamVR a veces dice "no se cual es el derecho" un instante: se busca entre los mandos y se recuerda */
    static uint32_t last_ri = 0xFFFFFFFFu;
    uint32_t ri = p_RoleIdx(2);
    if (ri >= 16 && p_RoleOf) for (uint32_t k = 1; k < 16; k++) if (rp[k].valid && p_RoleOf(k) == 2) { ri = k; break; }
    if (ri >= 16 && last_ri < 16 && rp[last_ri].valid) ri = last_ri;
    if (ri >= 16) { g_hand_ok = 0; g_hand_why = 3; return; }
    last_ri = ri;
    if (!rp[0].valid) { g_hand_ok = 0; g_hand_why = 4; return; }
    if (!rp[ri].valid) { g_hand_ok = 0; g_hand_why = 5; return; }
    g_hand_why = 0;
    /* rel = cabeza^-1 * mano (SteamVR) */
    const OvrMat34 *Hm = &rp[0].mat, *Mm = &rp[ri].mat;
    float d[3] = { Mm->m[0][3] - Hm->m[0][3], Mm->m[1][3] - Hm->m[1][3], Mm->m[2][3] - Hm->m[2][3] };
    float R[3][3], t[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) R[i][j] = Hm->m[0][i] * Mm->m[0][j] + Hm->m[1][i] * Mm->m[1][j] + Hm->m[2][i] * Mm->m[2][j];
        t[i] = Hm->m[0][i] * d[0] + Hm->m[1][i] * d[1] + Hm->m[2][i] * d[2];
    }
    /* a ejes del juego: S R S, S t, con S = diag(1,1,-1) */
    float A[4][4]; memset(A, 0, sizeof(A)); A[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) A[i][j] = R[i][j] * ((i == 2) != (j == 2) ? -1.0f : 1.0f);
    A[0][3] = t[0]; A[1][3] = t[1]; A[2][3] = -t[2];
    memcpy(g_hand_rel, A, sizeof(A));
    if (g_hand_req) {
        g_hand_req = 0;
        UINT wic = g_wpn_ic, wsi = g_wpn_si;
        if (wic) {   /* (E28) se guarda para el arma que se ve ahora */
            int k = wcal_find(wic, wsi);
            if (k < 0 && g_nwcal < MAXW) { k = g_nwcal++; g_wcal[k].ic = wic; g_wcal[k].si = wsi; }
            if (k >= 0) memcpy(g_wcal[k].C0, A, sizeof(A));
        }
        if (!wic || !g_hand_cal) memcpy(g_hand_C0, A, sizeof(A));   /* la general: sin arma reconocida, o la primera vez */
        g_hand_cal = 1; hand_save();
        if (wic) vr_log("MANO: calibrada para el arma %u/%u (mano a %.2f %.2f %.2f m de la cabeza)", wic, wsi, A[0][3], A[1][3], A[2][3]);
        else     vr_log("MANO: calibrada la general, arma sin reconocer (mano a %.2f %.2f %.2f m de la cabeza)", A[0][3], A[1][3], A[2][3]);
        Beep(900, 80); Beep(1300, 120);
    }
    float C0i[4][4], G[4][4];
    const float (*C0)[4] = (const float (*)[4])g_hand_C0;
    {   /* (E28) si el arma de ahora tiene calibracion propia, se usa esa */
        int k = g_wpn_ic ? wcal_find(g_wpn_ic, g_wpn_si) : -1;
        if (k >= 0) C0 = (const float (*)[4])g_wcal[k].C0;
    }
    if (!inv4(C0, C0i)) { g_hand_ok = 0; return; }
    if (cfg_dos_manos) twohand_apply(rp, A, (const float (*)[4])C0i);   /* (E34) agarre a dos manos */
    else g_dos_manos = 0;
    mul4(A, C0i, G);
    memcpy(g_hand_G, G, sizeof(G));
    g_hand_ok = 1;
}
/* (E31) mando izquierdo: igual que la mano derecha, con su propio archivo metro_vr_mano_izq.txt */
static void lhand_path(char *path) { GetTempPathA(MAX_PATH, path); strcat(path, "metro_vr_mano_izq.txt"); }
static void lhand_update(const OvrPose *rp)
{
    static int loaded = 0;
    if (!loaded) {
        loaded = 1;
        memset(g_lhand_C0, 0, sizeof(g_lhand_C0));
        for (int i = 0; i < 4; i++) g_lhand_C0[i][i] = 1.0f;
        g_lhand_C0[0][3] = -0.10f; g_lhand_C0[1][3] = -0.25f; g_lhand_C0[2][3] = 0.40f;   /* sin calibrar */
        char path[MAX_PATH]; lhand_path(path);
        FILE *fp = fopen(path, "r"); int ok = 0;
        if (fp) { ok = 1; for (int i = 0; i < 3 && ok; i++) if (fscanf(fp, "%f %f %f %f", &g_lhand_C0[i][0], &g_lhand_C0[i][1], &g_lhand_C0[i][2], &g_lhand_C0[i][3]) != 4) ok = 0; fclose(fp); }
        if (!ok) { memset(g_lhand_C0, 0, sizeof(g_lhand_C0)); for (int i = 0; i < 4; i++) g_lhand_C0[i][i] = 1.0f;
                   g_lhand_C0[0][3] = -0.10f; g_lhand_C0[1][3] = -0.25f; g_lhand_C0[2][3] = 0.40f; }
        g_lhand_cal = ok;
        vr_log("MANO IZQ: %s (%s)", ok ? "calibracion cargada" : "sin calibrar (mantener el clic del stick izquierdo 1,5 s con el mando donde ves la mano)", path);
    }
    if (!cfg_mano_izq_mando || !p_RoleIdx || !rp[0].valid) { g_lhand_ok = 0; return; }
    static uint32_t last_li = 0xFFFFFFFFu;
    uint32_t li = p_RoleIdx(1);
    if (li >= 16 && p_RoleOf) for (uint32_t k = 1; k < 16; k++) if (rp[k].valid && p_RoleOf(k) == 1) { li = k; break; }
    if (li >= 16 && last_li < 16 && rp[last_li].valid) li = last_li;
    if (li >= 16 || !rp[li].valid) { g_lhand_ok = 0; return; }
    last_li = li;
    const OvrMat34 *Hm = &rp[0].mat, *Mm = &rp[li].mat;
    float d[3] = { Mm->m[0][3] - Hm->m[0][3], Mm->m[1][3] - Hm->m[1][3], Mm->m[2][3] - Hm->m[2][3] };
    float R[3][3], t[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) R[i][j] = Hm->m[0][i] * Mm->m[0][j] + Hm->m[1][i] * Mm->m[1][j] + Hm->m[2][i] * Mm->m[2][j];
        t[i] = Hm->m[0][i] * d[0] + Hm->m[1][i] * d[1] + Hm->m[2][i] * d[2];
    }
    float A[4][4]; memset(A, 0, sizeof(A)); A[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) A[i][j] = R[i][j] * ((i == 2) != (j == 2) ? -1.0f : 1.0f);
    A[0][3] = t[0]; A[1][3] = t[1]; A[2][3] = -t[2];
    if (g_lhand_req) {
        g_lhand_req = 0;
        memcpy(g_lhand_C0, A, sizeof(A)); g_lhand_cal = 1;
        char path[MAX_PATH]; lhand_path(path);
        FILE *fp = fopen(path, "w");
        if (fp) { for (int i = 0; i < 3; i++) fprintf(fp, "%.6f %.6f %.6f %.6f\n", A[i][0], A[i][1], A[i][2], A[i][3]); fclose(fp); }
        vr_log("MANO IZQ: calibrada (mando a %.2f %.2f %.2f m de la cabeza)", A[0][3], A[1][3], A[2][3]);
        Beep(900, 80); Beep(1300, 120);
    }
    float C0i[4][4], G[4][4];
    if (g_lhand_hold) {   /* calibrando: la mano vuelve a donde la pone el juego */
        memset(G, 0, sizeof(G)); for (int i = 0; i < 4; i++) G[i][i] = 1.0f;
    } else {
        if (!inv4(g_lhand_C0, C0i)) { g_lhand_ok = 0; return; }
        mul4(A, C0i, G);
    }
    /* (E32/E34) AGARRE A DOS MANOS: la mano izq pasa de su mando al arma. El juego ya la pone en el guardamanos con la
     * animacion, asi que se le da la MISMA G que al arma. Se decide en hand_update (twohand_apply), que va antes. */
    if ((g_dos_manos || g_mag_state == 2) && g_hand_ok && !g_lhand_hold) memcpy(G, g_hand_G, sizeof(G));   /* (E36) metiendo el cargador: con el arma */
    memcpy(g_lhand_G, G, sizeof(G));
    g_lhand_ok = 1;
}
/* Lee el visor y deja preparada g_headH. Devuelve 1 si ha cambiado algo. */
static int head_update(void)
{
    if (!cfg_head) return 0;
    head_try_init();
    if (g_head_state != 1) return 0;
    /* con imagen en el visor, la postura la da WaitGetPoses (la MISMA que usa SteamVR para recolocar la imagen) */
    if (cfg_vrimg && p_WaitGetPoses) return 0;
    OvrPose pose[1];
    memset(pose, 0, sizeof(pose));
    p_GetPose(1 /* Standing */, 0.0f, pose, 1);   /* dispositivo 0 = el visor */
    return head_apply(&pose[0]);
}

static int head_apply(const OvrPose *pz)
{
    if (!pz->valid) { g_head_ok = 0; return 0; }
    g_head_ok = 1;
    float R[3][3], P[3];
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) R[i][j] = pz->mat.m[i][j]; P[i] = pz->mat.m[i][3]; }

    if (g_head_recenter) {
        /* referencia = solo hacia donde miras en horizontal (el suelo sigue siendo el suelo) */
        float fx = -R[0][2], fz = -R[2][2], l = sqrtf(fx*fx + fz*fz);
        if (l < 1e-3f) { fx = 0; fz = -1; l = 1; }
        float s = -fx / l, c = -fz / l;
        float Ry[3][3] = {{c,0,s},{0,1,0},{-s,0,c}};
        memcpy(g_refR, Ry, sizeof(Ry));
        memcpy(g_refP, P, sizeof(P));
        g_head_recenter = 0;
        g_rt_reset = 1;
        vr_log("VISOR: centrado");
    }
    /* giro y desplazamiento de la cabeza respecto a la referencia (coordenadas de OpenVR) */
    float Rr[3][3], d[3], dp[3] = { P[0] - g_refP[0], P[1] - g_refP[1], P[2] - g_refP[2] };
    for (int i = 0; i < 3; i++) {
        d[i] = 0;
        for (int j = 0; j < 3; j++) {
            float a = 0;
            for (int k = 0; k < 3; k++) a += g_refR[k][i] * R[k][j];
            Rr[i][j] = a;
            d[i] += g_refR[j][i] * dp[j];
        }
    }
    /* OpenVR mira hacia -Z; el juego hacia +Z: se le da la vuelta al eje Z */
    float Rg[3][3], dg[3] = { d[0], d[1], -d[2] };
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Rg[i][j] = Rr[i][j] * ((i == 2) != (j == 2) ? -1.0f : 1.0f);
    if (!cfg_headpos) dg[0] = dg[1] = dg[2] = 0;
    for (int i = 0; i < 3; i++) dg[i] *= cfg_headscale;

    /* vista nueva = Rg^T * (vista - dg)  ->  H = [Rg^T | -Rg^T*dg],  H^-1 = [Rg | dg] */
    float H[4][4], Hi[4][4];
    memset(H, 0, sizeof(H)); memset(Hi, 0, sizeof(Hi)); H[3][3] = Hi[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) { H[i][j] = Rg[j][i]; Hi[i][j] = Rg[i][j]; }
        H[i][3] = -(Rg[0][i]*dg[0] + Rg[1][i]*dg[1] + Rg[2][i]*dg[2]);
        Hi[i][3] = dg[i];
    }
    memcpy(g_rt_Rg, Rg, sizeof(Rg)); memcpy(g_rt_dg, dg, sizeof(dg));
    if (raton_active() && g_rt_havecam) { if (cfg_modoh == 0) raton_make_H(g_rt_lastcam, g_headH, g_headHi); /* modo 1: al llegar la camara */ }
    else { memcpy(g_headH, H, sizeof(H)); memcpy(g_headHi, Hi, sizeof(Hi)); }
    /* para el log: hacia donde mira (columna "adelante") e inclinacion */
    g_head_yaw   = atan2f(Rg[0][2], Rg[2][2]) * RAD2DEG;
    g_head_pitch = asinf(clampf(Rg[1][2], -1.0f, 1.0f)) * RAD2DEG;
    g_head_roll  = atan2f(-Rg[1][0], Rg[1][1]) * RAD2DEG;
    g_head_dx = dg[0]; g_head_dy = dg[1]; g_head_dz = dg[2];
    return 1;
}

/* ------------------------------------------------ (E7) la cabeza mueve el raton */
/* En vez de escribir en la memoria de la camara (E4-E5h, daba el "mundo marron"), la DLL mueve el raton por ti,
 * como si lo movieras tu. Asi es el PROPIO juego el que gira hacia donde miras: decide bien que dibujar (tambien
 * detras de ti), las luces cuadran y el arma apunta donde miras. Lo que falte (el juego va un pelin por detras,
 * inclinar la cabeza de lado, moverla, cada ojo) lo pone H, calculado con la camara REAL de cada fotograma:
 *   cuerpo  = camara del juego menos lo que ha girado por nuestro raton  (Rb)
 *   deseado = cuerpo * cabeza                                            (Rd = Rb * Rg)
 *   H       = [ Rd^T * Rcam | -Rg^T * dg ]                                                                       */
/* (E9) camara nativa: estado */
static volatile ULONGLONG g_nat_last = 0;
static float g_nat_yh = 0, g_nat_ref = 0; static int g_nat_was = 0;
static int nat_active(void)
{
    return cfg_nativa && g_nat_installed == 1 && g_mode == 1 && (GetTickCount64() - g_nat_last) < 500;
}
static int raton_active(void)
{
    if (g_mode != 1 || !(g_head_ok || cfg_fakehead)) return 0;
    if (nat_active()) return 1;
    return cfg_raton && g_rt_cal == 2;
}

/* camara (a mundo) a partir de giro e inclinacion, como la del juego: columnas derecha, arriba, adelante */
static void euler_cam(float yaw, float pitch, float M[3][3])
{
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    M[0][0] = cy;  M[0][1] = -sy * sp; M[0][2] = sy * cp;
    M[1][0] = 0;   M[1][1] = cp;       M[1][2] = sp;
    M[2][0] = -sy; M[2][1] = -cy * sp; M[2][2] = cy * cp;
}

static LONG rt_applied(const LONG *h)
{
    int lag = cfg_raton_lag; if (lag >= RT_HIST) lag = RT_HIST - 1;
    return h[(g_rt_hi - lag + RT_HIST * 4) % RT_HIST];
}

static void raton_make_H(const float *f, float Ho[4][4], float Hio[4][4])
{
    float Rc[3][3], Rb[3][3], Rd[3][3], A[3][3];
    if (nat_full()) {   /* (E10) la camara del juego YA es la del ojo: no hay nada que corregir */
        memset(Ho, 0, 16 * sizeof(float)); memset(Hio, 0, 16 * sizeof(float));
        for (int i = 0; i < 4; i++) Ho[i][i] = Hio[i][i] = 1.0f;
        return;
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Rc[i][j] = f[j * 4 + i];   /* camara del juego -> mundo */
    float G = atan2f(f[8], f[10]), fy = f[9]; if (fy > 1) fy = 1; if (fy < -1) fy = -1;
    float Pg = asinf(fy);
    float yb = G - g_rt_kx * (float)rt_applied(g_rt_hx);
    float pb = Pg - g_rt_ky * (float)rt_applied(g_rt_hy);
    if (nat_active()) { yb = G - g_nat_yh; pb = 0; }   /* (E9) el juego ya lleva el giro y la inclinacion de la cabeza */
    euler_cam(yb, pb, Rb);
    if (g_rt_bodyfrom_any) { memcpy(g_rt_Rb, Rb, sizeof(Rb)); g_rt_haveRb = 1; }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        float a = 0; for (int k = 0; k < 3; k++) a += Rb[i][k] * g_rt_Rg[k][j]; Rd[i][j] = a;
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        float a = 0; for (int k = 0; k < 3; k++) a += Rd[k][i] * Rc[k][j]; A[i][j] = a;
    }
    float H[4][4], Hi[4][4], t[3];
    memset(H, 0, sizeof(H)); memset(Hi, 0, sizeof(Hi)); H[3][3] = Hi[3][3] = 1.0f;
    for (int i = 0; i < 3; i++) t[i] = -(g_rt_Rg[0][i] * g_rt_dg[0] + g_rt_Rg[1][i] * g_rt_dg[1] + g_rt_Rg[2][i] * g_rt_dg[2]);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) { H[i][j] = A[i][j]; Hi[i][j] = A[j][i]; }
        H[i][3] = t[i];
        Hi[i][3] = -(A[0][i] * t[0] + A[1][i] * t[1] + A[2][i] * t[2]);
    }
    memcpy(Ho, H, sizeof(H));
    memcpy(Hio, Hi, sizeof(Hi));
    /* desajuste = lo que H tiene que girar (sin contar inclinar la cabeza de lado): adelante de A */
    float c = clampf(A[2][2], -1.0f, 1.0f);
    LONG r = (LONG)(acosf(c) * RAD2DEG * 10.0f);
    if (r > g_rt_resid) g_rt_resid = r;
}

static void rt_send(LONG dx, LONG dy)
{
    if (!dx && !dy) return;
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE; in.mi.dx = dx; in.mi.dy = dy; in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

static int game_has_focus(void)
{
    HWND w = GetForegroundWindow(); DWORD pid = 0;
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

/* espera a que la camara del juego deje de moverse; devuelve 0 si no llegan camaras */
static int rt_wait_still(int ms)
{
    float py = g_cam_yaw, pp = g_cam_pitch; int still = 0; LONG s0 = g_cam_seq;
    for (int t = 0; t < ms; t += 40) {
        Sleep(40);
        if (fabsf(wrap_pi(g_cam_yaw - py)) < 0.0005f && fabsf(g_cam_pitch - pp) < 0.0005f) still++; else still = 0;
        py = g_cam_yaw; pp = g_cam_pitch;
        if (still >= 5 && g_cam_seq != s0) return 1;
    }
    return g_cam_seq != s0;
}

static DWORD WINAPI raton_cal_thread(LPVOID u)
{
    (void)u;
    vr_log("RATON: calibrando (no muevas el raton 2 s)...");
    const LONG N = 400, NP = 200;
    if (!rt_wait_still(1500)) { vr_log("RATON: no veo la camara del juego; entra en partida"); goto fail; }
    float y0 = g_cam_yaw, p0 = g_cam_pitch;
    for (int k = 0; k < 10; k++) { rt_send(N / 10, 0); Sleep(15); }
    rt_wait_still(2500);
    float y1 = g_cam_yaw;
    for (int k = 0; k < 10; k++) { rt_send(-N / 10, 0); Sleep(15); }
    rt_wait_still(2500);
    float y2 = g_cam_yaw;
    LONG dir = (p0 > 0) ? 1 : -1;                       /* inclinacion: hacia el centro, para no chocar con el tope */
    for (int k = 0; k < 10; k++) { rt_send(0, dir * NP / 10); Sleep(15); }
    rt_wait_still(2500);
    float p1 = g_cam_pitch;
    for (int k = 0; k < 10; k++) { rt_send(0, -dir * NP / 10); Sleep(15); }
    rt_wait_still(2500);
    float kx = wrap_pi(y1 - y0) / (float)N, ky = (p1 - p0) / (float)(dir * NP);
    vr_log("RATON: giro %.4f -> %.4f -> %.4f rad | inclinacion %.4f -> %.4f rad (vuelve a %.4f)",
           (double)y0, (double)y1, (double)y2, (double)p0, (double)p1, (double)g_cam_pitch);
    if (fabsf(kx) < 1e-5f || fabsf(kx) > 0.05f || fabsf(wrap_pi(y2 - y0)) > 0.25f * fabsf(kx * N)) {
        vr_log("RATON: el giro no responde bien al raton (%.6f rad por paso); sin raton_cabeza", (double)kx); goto fail;
    }
    if (fabsf(ky) < 1e-5f || fabsf(ky) > 0.05f) {
        vr_log("RATON: la inclinacion no responde (%.6f); solo giro", (double)ky); ky = 0;
    }
    g_rt_kx = kx; g_rt_ky = ky; g_rt_reset = 1;
    InterlockedExchange(&g_rt_cal, 2);
    vr_log("RATON: LISTO. %.5f grados por paso (giro), %.5f (inclinacion). La cabeza ya mueve el juego",
           (double)(kx * RAD2DEG), (double)(ky * RAD2DEG));
    Beep(1000, 90); Beep(1400, 120);
    return 0;
fail:
    g_rt_cal_retry = GetTickCount64() + 15000;
    InterlockedExchange(&g_rt_cal, -1);
    Beep(400, 300);
    return 0;
}

static void raton_calibrate(void)
{
    LONG c = g_rt_cal;
    if (c == 1) return;
    if (InterlockedCompareExchange(&g_rt_cal, 1, c) != c) return;
    HANDLE th = CreateThread(NULL, 0, raton_cal_thread, NULL, 0, NULL);
    if (th) CloseHandle(th); else g_rt_cal = -1;
}

/* despues de cada Present (en estereo sincronizado, una vez por pareja): mandar el raton que toca */
static void raton_step(void)
{
    if (!cfg_raton || g_mode != 1) { g_rt_reset = 1; return; }
    if (!(g_head_ok || cfg_fakehead)) return;            /* (E7b) visor perdido un instante: esperar, NO reiniciar */
    if (cfg_nativa && g_nat_installed >= 0) { g_rt_reset = 1; return; }   /* (E9) camara nativa (o buscandola): no se mueve el raton */
    LONG c = g_rt_cal;
    if (c == 0 || (c == -1 && GetTickCount64() > g_rt_cal_retry && g_rt_cal_retry)) {
        if (g_rt_havecam && game_has_focus()) { g_rt_cal_retry = 0; raton_calibrate(); }
        return;
    }
    if (c != 2) return;
    float yh = atan2f(g_rt_Rg[0][2], g_rt_Rg[2][2]);
    float ph = asinf(clampf(g_rt_Rg[1][2], -1.0f, 1.0f));
    if (g_rt_reset) {
        g_rt_reset = 0; g_rt_cx = g_rt_cy = 0;
        for (int i = 0; i < RT_HIST; i++) g_rt_hx[i] = g_rt_hy[i] = 0;
        g_rt_unwrap = yh; g_rt_last = yh;
        vr_log("RATON: empiezo (cuerpo = donde mira ahora el juego)");
    }
    {
        /* (E7b) un salto enorme de golpe (SteamVR recentrado, el visor que se pierde y vuelve) no es tu cabeza:
         * nadie gira 45 grados en un fotograma. Se ignora, para que el juego no pegue un giro loco */
        float d = wrap_pi(yh - g_rt_last);
        if (fabsf(d) > 0.785f) { vr_log("RATON: salto de %.0f grados de golpe en el visor: lo ignoro", (double)(d * RAD2DEG)); d = 0; }
        g_rt_unwrap += d; g_rt_last = yh;
    }
    if (!game_has_focus()) return;                       /* sin el juego delante no se mueve el raton de Windows */
    LONG nx = (LONG)lroundf(g_rt_unwrap / g_rt_kx) - g_rt_cx, ny = 0;
    if (cfg_raton_pitch && g_rt_ky != 0) {
        float pm = g_cam_pitch - g_rt_ky * (float)rt_applied(g_rt_hy);   /* lo que has inclinado tu con el raton */
        float lim = cfg_raton_pmax / RAD2DEG;
        float tp = clampf(pm + ph, -lim, lim);
        ny = (LONG)lroundf((tp - pm) / g_rt_ky) - g_rt_cy;
    }
    if (nx > 3000) nx = 3000; if (nx < -3000) nx = -3000;
    if (ny > 3000) ny = 3000; if (ny < -3000) ny = -3000;
    rt_send(nx, ny);
    g_rt_cx += nx; g_rt_cy += ny;
    LONG hi = (g_rt_hi + 1) % RT_HIST;
    g_rt_hx[hi] = g_rt_cx; g_rt_hy[hi] = g_rt_cy;
    g_rt_hi = hi;
    InterlockedExchangeAdd(&g_rt_sent, labs(nx) + labs(ny));
}

/* ------------------------------------------------ (E21) mando de Xbox "de mentira" (XInput) */
/* El juego lee los mandos de Xbox con XInputGetState. Se engancha (MinHook) en las xinput*.dll que tenga cargadas y,
 * si hay mandos VR, se le contesta con el estado que montamos aqui: andar analogico con el stick, y en los menus todo
 * (navegar, aceptar, volver) como un mando de consola. Si hay un mando de verdad, se suma. */
typedef struct { WORD wButtons; BYTE bLT, bRT; SHORT sLX, sLY, sRX, sRY; } XPad;
typedef struct { DWORD dwPacket; XPad pad; } XState;
typedef struct { BYTE Type, SubType; WORD Flags; XPad pad; WORD wLeftMotor, wRightMotor; } XCaps;
typedef DWORD (WINAPI *PFN_XGetState)(DWORD, XState *);
typedef DWORD (WINAPI *PFN_XGetCaps)(DWORD, DWORD, XCaps *);
#define XMOD_N 4
static PFN_XGetState g_x_tr_state[XMOD_N]; static PFN_XGetCaps g_x_tr_caps[XMOD_N];
static XPad g_xpad; static volatile LONG g_xpad_on = 0, g_xpad_pkt = 0, g_x_hooked = 0, g_x_reads = 0;
static DWORD x_state_common(PFN_XGetState tr, DWORD i, XState *st)
{
    DWORD r = tr ? tr(i, st) : 1167;
    if (i != 0 || !g_xpad_on || !st) return r;
    InterlockedIncrement(&g_x_reads);
    XPad v = g_xpad;
    if (r != 0) { memset(st, 0, sizeof(*st)); }
    else {   /* mando de verdad: se suma */
        v.wButtons |= st->pad.wButtons;
        if (st->pad.bLT > v.bLT) v.bLT = st->pad.bLT;
        if (st->pad.bRT > v.bRT) v.bRT = st->pad.bRT;
        if (abs(st->pad.sLX) > abs(v.sLX)) v.sLX = st->pad.sLX;
        if (abs(st->pad.sLY) > abs(v.sLY)) v.sLY = st->pad.sLY;
        if (abs(st->pad.sRX) > abs(v.sRX)) v.sRX = st->pad.sRX;
        if (abs(st->pad.sRY) > abs(v.sRY)) v.sRY = st->pad.sRY;
    }
    st->pad = v;
    st->dwPacket = (DWORD)g_xpad_pkt;
    return 0;
}
static DWORD x_caps_common(PFN_XGetCaps tr, DWORD i, DWORD f, XCaps *c)
{
    DWORD r = tr ? tr(i, f, c) : 1167;
    if (r == 0 || i != 0 || !g_xpad_on || !c) return r;
    memset(c, 0, sizeof(*c)); c->Type = 1; c->SubType = 1;
    c->pad.wButtons = 0xF3FF; c->pad.bLT = c->pad.bRT = 0xFF;
    c->pad.sLX = c->pad.sLY = c->pad.sRX = c->pad.sRY = (SHORT)0xFFC0;
    return 0;
}
#define X_DEF(k) static DWORD WINAPI x_st##k(DWORD i, XState *s) { return x_state_common(g_x_tr_state[k], i, s); } \
                 static DWORD WINAPI x_cp##k(DWORD i, DWORD f, XCaps *c) { return x_caps_common(g_x_tr_caps[k], i, f, c); }
X_DEF(0) X_DEF(1) X_DEF(2) X_DEF(3)
static void *const g_x_det_state[XMOD_N] = { (void *)x_st0, (void *)x_st1, (void *)x_st2, (void *)x_st3 };
static void *const g_x_det_caps[XMOD_N] = { (void *)x_cp0, (void *)x_cp1, (void *)x_cp2, (void *)x_cp3 };
static void mh_start(void);
static void xinput_hook_try(void)
{
    static int done[XMOD_N];
    static const char *mods[XMOD_N] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll" };
    if (!cfg_xinput) return;
    for (int k = 0; k < XMOD_N; k++) {
        if (done[k]) continue;
        HMODULE m = GetModuleHandleA(mods[k]);
        if (!m) continue;
        done[k] = 1;
        mh_start();
        void *fs = (void *)GetProcAddress(m, "XInputGetState"), *fc = (void *)GetProcAddress(m, "XInputGetCapabilities");
        MH_STATUS a = MH_ERROR_FUNCTION_NOT_FOUND, b = MH_ERROR_FUNCTION_NOT_FOUND;
        if (fs) { a = MH_CreateHook(fs, g_x_det_state[k], (void **)&g_x_tr_state[k]); if (a == MH_OK) a = MH_EnableHook(fs); }
        if (fc) { b = MH_CreateHook(fc, g_x_det_caps[k], (void **)&g_x_tr_caps[k]); if (b == MH_OK) b = MH_EnableHook(fc); }
        if (a == MH_OK) InterlockedIncrement(&g_x_hooked);
        vr_log("MANDO XBOX: %s enganchada (GetState %s, GetCapabilities %s)", mods[k], MH_StatusToString(a), MH_StatusToString(b));
    }
}
static SHORT x_axis(float v) { v = v > 1 ? 1 : (v < -1 ? -1 : v); return (SHORT)(v * 32767.0f); }

/* ------------------------------------------------ (E13) mandos como teclado y raton */
/* Mando de Quest (modo "heredado" de SteamVR): gatillo = eje 1, agarre = boton 2 (y eje 2), stick = eje 0 (clic = boton 32),
 * A/X = boton 7, B/Y = boton 1. Teclas = las del juego (user.cfg). Se mandan como "codigo de tecla" (scancode). */
static volatile LONG g_mandos_ok = 0;
/* codigo fisico de la tecla (el del teclado US, que es el que guarda el juego en user.cfg: kW, kLBRACKET...) */
static void key_send(WORD sc, int down)
{
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = sc;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
}
static void mouse_btn(int b, int down)
{
    INPUT in; memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = b == 0 ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                  : b == 1 ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                           : (down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
    SendInput(1, &in, sizeof(in));
}
/* una "salida": tecla (vk) o boton de raton (vk = 0x1000 + n) que se mantiene mientras 'on' */
typedef struct { WORD vk; int on; } Out;
enum { O_W, O_A, O_S, O_D, O_FIRE, O_AIM, O_USE, O_RELOAD, O_JUMP, O_CROUCH, O_SPRINT, O_LIGHT, O_MASK, O_THROW,
       O_NEXT, O_KNIFE, O_ESC, O_MEDKIT, O_COUNT };
static Out g_out[O_COUNT] = {   /* W A S D | raton izq/der | E R ESPACIO Z SHIFT_IZQ | F G C [ V ESC Q */
    {0x11,0},{0x1E,0},{0x1F,0},{0x20,0},{0x1000,0},{0x1001,0},{0x12,0},{0x13,0},{0x39,0},{0x2C,0},{0x2A,0},
    {0x21,0},{0x22,0},{0x2E,0},{0x1A,0},{0x2F,0},{0x01,0},{0x10,0} };
static int g_pulse[O_COUNT];      /* toques cortos: fotogramas que quedan pulsados */
static void out_pulse(int o);
static void out_set(int o, int on)
{
    if (g_out[o].on == on) return;
    g_out[o].on = on;
    if (g_out[o].vk >= 0x1000) mouse_btn(g_out[o].vk - 0x1000, on); else key_send(g_out[o].vk, on);
}
static void mandos_release_all(void) { for (int i = 0; i < O_COUNT; i++) { out_set(i, 0); g_pulse[i] = 0; } }
static void out_pulse(int o) { out_set(o, 1); g_pulse[o] = 4; }
static void mandos_step(float dt)
{
    static int was = 0;
    int active = cfg_mandos && p_RoleIdx && p_CtrlState && g_head_state == 1 && game_has_focus();
    if (!active) { g_lgrip = 0; if (was) { mandos_release_all(); was = 0; } return; }
    OvrCtrlState L, R; memset(&L, 0, sizeof(L)); memset(&R, 0, sizeof(R));
    uint32_t li = p_RoleIdx(1), ri = p_RoleIdx(2);
    static uint32_t lli = 0xFFFFFFFFu, lri = 0xFFFFFFFFu;
    if (li >= 64) li = lli; else lli = li;
    if (ri >= 64) ri = lri; else lri = ri;
    int lok = li < 64 && p_CtrlState(li, &L, sizeof(L));
    int rok = ri < 64 && p_CtrlState(ri, &R, sizeof(R));
    if (!lok && !rok) { g_lgrip = 0; if (was) { mandos_release_all(); was = 0; } return; }
    if (!was) { vr_log("MANDOS: leyendo mandos (izq=%u der=%u)", li, ri); was = 1; }
    for (int i = 0; i < O_COUNT; i++) if (g_pulse[i] > 0 && --g_pulse[i] == 0) out_set(i, 0);
    InterlockedExchange(&g_mandos_ok, 1);
#define BTN(st, id) (((st).pressed >> (id)) & 1ull)
    /* (E21) mando de Xbox de mentira: si el juego lo lee, el stick izquierdo va por el (andar analogico) */
    float lx = lok ? L.axis[0].x : 0, ly = lok ? L.axis[0].y : 0;
    float rx0 = rok ? R.axis[0].x : 0, ry0 = rok ? R.axis[0].y : 0;
    int xpad = cfg_xinput && g_x_hooked > 0;
    XPad xp; memset(&xp, 0, sizeof(xp));
    if (xpad) { xp.sLX = x_axis(lx); xp.sLY = x_axis(ly); }
    /* (E21) MENUS (pantalla flotante): los mandos VR son un mando de consola; nada de teclado ni raton */
    if (xpad && g_mode != 1) {
        static int menu_was = 0;
        if (!g_menu_pad) menu_was = 0;
        if (!menu_was) { mandos_release_all(); menu_was = 1; vr_log("MANDOS: menu -> mando de consola"); }
        g_menu_pad = 1; g_lgrip = 0;
        xp.sRX = x_axis(rx0); xp.sRY = x_axis(ry0);
        WORD b = 0;
        if (rok && (BTN(R, 7) || R.axis[1].x > 0.6f)) b |= 0x1000;       /* A (der) o gatillo der = aceptar (A) */
        if (rok && BTN(R, 1)) b |= 0x2000;                               /* B (der) = volver (B) */
        if (lok && BTN(L, 7)) b |= 0x0010;                               /* (E34) X (izq) = Start (pausa / salir de la pausa) */
        if (lok && BTN(L, 1)) b |= 0x8000;                               /* Y (izq) = Y */
        if (lok && BTN(L, 2)) b |= 0x0100;                               /* agarre izq = LB */
        if (rok && BTN(R, 2)) b |= 0x0200;                               /* agarre der = RB */
        if (lok && L.axis[1].x > 0.6f) b |= 0x0010;                      /* gatillo izq = Start */
        /* stick derecho tambien como cruceta */
        if (ry0 > 0.6f) b |= 0x0001; if (ry0 < -0.6f) b |= 0x0002; if (rx0 < -0.6f) b |= 0x0004; if (rx0 > 0.6f) b |= 0x0008;
        xp.wButtons = b;
        g_xpad = xp; InterlockedIncrement(&g_xpad_pkt); g_xpad_on = 1;
        return;
    }
    if (g_menu_pad) { g_menu_pad = 0; vr_log("MANDOS: partida -> teclado/raton (+ stick izq analogico)"); }
    if (xpad) {
        out_set(O_W, 0); out_set(O_S, 0); out_set(O_D, 0); out_set(O_A, 0);
    } else {
        out_set(O_W, ly > 0.45f); out_set(O_S, ly < -0.45f);
        out_set(O_D, lx > 0.45f); out_set(O_A, lx < -0.45f);
    }
    static int sprint = 0, lclick_prev = 0;
    int lclick = lok && BTN(L, 32);
    {   /* (E31) mantener el clic del stick izq: la mano izq vuelve a su sitio del juego; a los 1,5 s se calibra */
        static ULONGLONG ldown = 0; static int lcal = 0;
        if (lclick && !ldown) { ldown = GetTickCount64(); lcal = 0; }
        if (lclick && GetTickCount64() - ldown > 300) g_lhand_hold = 1;
        if (lclick && !lcal && GetTickCount64() - ldown > 1500) { g_lhand_req = 1; lcal = 1; }
        if (!lclick) { ldown = 0; g_lhand_hold = 0; }
    }
    if (lclick && !lclick_prev) sprint = !sprint;
    if (lx * lx + ly * ly < 0.04f) sprint = 0;          /* stick suelto: deja de correr */
    lclick_prev = lclick;
    if (xpad) { out_set(O_SPRINT, 0); if (sprint) xp.wButtons |= 0x0040; }   /* (E21) correr = clic del stick izq del mando */
    else out_set(O_SPRINT, sprint);
    g_lgrip = lok && BTN(L, 2);                          /* (E34) agarre izq = agarrar el arma con las dos manos (el botiquin ya no) */
    out_set(O_THROW, lok && L.axis[1].x > 0.6f);         /* gatillo izq = lanzar */
    /* (E34) X: toque = PAUSA (Esc); mantener medio segundo = mascara */
    static ULONGLONG xdown = 0; static int xmask = 0;
    int xb = lok && BTN(L, 7);
    if (xb && !xdown) { xdown = GetTickCount64(); xmask = 0; }
    if (xb && !xmask && GetTickCount64() - xdown > 600) { out_set(O_MASK, 1); xmask = 1; }
    if (!xb && xdown) {
        if (!xmask) out_pulse(O_ESC);
        out_set(O_MASK, 0); xdown = 0;
    }
    /* (E34) Y: toque corto = arma siguiente; mantener medio segundo = linterna (antes era la pausa, que pasa a X) */
    static ULONGLONG ydown = 0; static int ylight = 0;
    int y = lok && BTN(L, 1);
    if (y && !ydown) { ydown = GetTickCount64(); ylight = 0; }
    if (y && !ylight && GetTickCount64() - ydown > 600) { out_pulse(O_LIGHT); ylight = 1; }
    if (!y && ydown) {
        if (!ylight) { out_pulse(O_NEXT); g_wpn_switch_t = GetTickCount64(); }   /* (E21) el cambio de arma no es un menu */
        ydown = 0;
    }
    /* derecho: disparar (gatillo), apuntar (agarre), usar (A), recargar (B), saltar/agacharse (stick arriba/abajo),
     * cuchillo (clic del stick), girar (stick a los lados) */
    out_set(O_FIRE, rok && R.axis[1].x > 0.6f);
    out_set(O_AIM, rok && BTN(R, 2));
    out_set(O_USE, rok && BTN(R, 7));
    static int rl_hold = 0;   /* (E36) cargador metido con el gesto: R pulsada unos fotogramas */
    if (InterlockedExchange(&g_reload_req, 0)) rl_hold = 4;
    /* (E37) B: toque = expulsar el cargador; mantener 1 s = recarga de golpe (por si acaso) */
    static ULONGLONG bdown = 0; static int bdone = 0;
    int bb = rok && BTN(R, 1);
    if (bb && !bdown) { bdown = GetTickCount64(); bdone = 0; }
    if (bb && !bdone && GetTickCount64() - bdown > 1000) { rl_hold = 4; bdone = 1; g_out_ic = 0; }
    if (!bb && bdown) {
        if (!bdone) { if (cfg_recarga_manual) { InterlockedExchange(&g_expel_req, 1); g_out_si = g_wpn_si; g_out_ic = g_wpn_ic; haptic(2, 2500); } else rl_hold = 4; }
        bdown = 0;
    }
    out_set(O_RELOAD, rl_hold > 0);
    if (rl_hold > 0) rl_hold--;
    /* clic del stick derecho: toque = cuchillo; mantener 1,5 s = calibrar la mano del arma (E15) */
    static ULONGLONG rdown = 0; static int rcal = 0;
    int rc = rok && BTN(R, 32);
    if (rc && !rdown) { rdown = GetTickCount64(); rcal = 0; }
    if (rc && !rcal && GetTickCount64() - rdown > 1500) { g_hand_req = 1; rcal = 1; }
    if (!rc && rdown) { if (!rcal) out_pulse(O_KNIFE); rdown = 0; }
    float rx = rok ? R.axis[0].x : 0, ry = rok ? R.axis[0].y : 0;
    out_set(O_JUMP, ry > 0.7f && fabsf(rx) < 0.5f);
    static int crouch_prev = 0;
    int cr = ry < -0.7f && fabsf(rx) < 0.5f;
    out_set(O_CROUCH, cr);           /* Z = agacharse (alterna) */
    crouch_prev = cr; (void)crouch_prev;
    float steps_per_deg = (g_rt_cal == 2 && g_rt_kx > 1e-5f) ? 1.0f / (g_rt_kx * RAD2DEG) : cfg_raton_grado;
    if (cfg_giro_paso > 0.5f) {
        static int armed = 1;
        if (fabsf(rx) < 0.3f) armed = 1;
        if (armed && fabsf(rx) > 0.75f) { armed = 0; rt_send((LONG)(copysignf(cfg_giro_paso, rx) * steps_per_deg), 0); }
    } else if (fabsf(rx) > 0.2f) {
        static float acc = 0;
        float a = (fabsf(rx) - 0.2f) / 0.8f; a = a * a;
        acc += copysignf(a, rx) * cfg_giro * dt * steps_per_deg;
        LONG st = (LONG)acc; acc -= (float)st;
        if (st) rt_send(st, 0);
    }
    if (xpad) { g_xpad = xp; InterlockedIncrement(&g_xpad_pkt); g_xpad_on = 1; }
#undef BTN
}

/* ------------------------------------------------ (E9) camara nativa */
/* El motor 4A construye cada fotograma la base de la camara (posicion, adelante, arriba) en metro.exe+0x69B900,
 * llamada desde la actualizacion de la camara del jugador en +0x1F3960. (Encontrado por D-Rey86 en Metro 2033
 * Redux; el codigo de Last Light Redux es identico en esas direcciones.) Nos colamos ahi: el giro del cuerpo
 * sigue siendo el del juego (raton/mando), y le sumamos el giro de la cabeza y su inclinacion. Asi TODO el juego
 * (lo que se dibuja, lo que se ve, luces, reflejos, el arma, a donde apuntas) usa la cabeza desde el principio,
 * sin mover el raton y sin ir un fotograma por detras. La inclinacion de lado (roll), cada ojo y moverse siguen en H. */
typedef void (*nat_fn_t)(void *, const float *, const float *, const float *, void *, void *);
static float g_nat_ring[8][3]; static LONG g_nat_ringe[8]; static volatile LONG g_nat_ri = 0;
static OvrMat34 g_nat_ringpose[8];                 /* (E22) postura del visor con la que se monto cada camara */
static int nat_full(void) { return cfg_natfull && nat_active(); }
/* (E10) Cada camara que llega a la grafica: con que ojo la monte? (busco su posicion entre las ultimas que di).
 * Si no coincide con el ojo que se esta dibujando, el juego monta la camara un fotograma antes o despues: me cambio. */
/* la camara (f) sale de una posicion que dimos nosotros? (y con que ojo) */
static int g_nat_lastk = -1;
static int nat_pos_match(const float *f, LONG *eye)
{
    float cx = f[15], cyy = f[19], cz = f[23], best = 1e9f; LONG be = -2; int bk = -1;
    for (int k = 0; k < 8; k++) {
        float dx = g_nat_ring[k][0] - cx, dy = g_nat_ring[k][1] - cyy, dz = g_nat_ring[k][2] - cz;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; be = g_nat_ringe[k]; bk = k; }
    }
    if (eye) *eye = be;
    g_nat_lastk = bk;
    return best <= 0.003f * 0.003f && be >= 0;
}
static void nat_eye_check(const float *f)
{
    if (!nat_full()) return;
    LONG be;
    int ok = nat_pos_match(f, &be);
    if (ok && g_nat_lastk >= 0 && cfg_pose_cam) {   /* (E22) esta imagen se dibuja con la postura con la que se monto su camara */
        int both = !g_stereo_on || g_twin_on == 2;   /* (E27) con los dos ojos a la vez, la misma postura */
        LONG e = both ? 0 : g_eye;
        g_eye_rpose[e] = g_nat_ringpose[g_nat_lastk]; g_eye_rpose_ok[e] = 1;
        if (both) { g_eye_rpose[1] = g_eye_rpose[0]; g_eye_rpose_ok[1] = 1; }
    }
    if (!g_stereo_on || g_twin_on == 2) return;
    if (!ok) { InterlockedIncrement(&g_nat_enone); return; }
    if (be == g_eye) InterlockedIncrement(&g_nat_eok); else InterlockedIncrement(&g_nat_ebad);
}
static void *g_nat_tramp = NULL; static BYTE *g_nat_ret = NULL;
static float g_piv[3] = { 0, 0, 0 }; static volatile LONG g_piv_n = 0; static int g_nat_reset_piv = 0;   /* (E20) */
/* (E19) balas hacia donde apunta el arma */
static float g_bul_C[3][3], g_bul_hc[3]; static volatile ULONGLONG g_bul_t = 0;
static volatile LONG g_bul_mine = 0, g_bul_other = 0, g_bul_inst = 0;
static float g_bul_lastdev = 0;
static void nat_hook(void *mgr, const float *pos, const float *a, const float *b, void *a5, void *a6)
{
    nat_fn_t orig = (nat_fn_t)g_nat_tramp;
    if ((BYTE *)__builtin_return_address(0) == g_nat_ret) g_cam_seen = GetTickCount64();   /* (E20) hay partida */
    if ((BYTE *)__builtin_return_address(0) != g_nat_ret || !cfg_nativa || g_mode != 1 ||
        !(g_head_ok || cfg_fakehead) || !pos || !a || !b) {
        g_nat_was = 0;
        orig(mgr, pos, a, b, a5, a6); return;
    }
    float h = sqrtf(a[0] * a[0] + a[2] * a[2]);
    if (!(h > 1e-5f) || !isfinite(h)) { orig(mgr, pos, a, b, a5, a6); return; }
    float yh = atan2f(g_rt_Rg[0][2], g_rt_Rg[2][2]);
    float ph = asinf(clampf(g_rt_Rg[1][2], -1.0f, 1.0f));
    if (!g_nat_was || (GetTickCount64() - g_nat_last) > 500) { g_nat_ref = yh; g_nat_reset_piv = 1; vr_log("NATIVA: la cabeza entra en la camara del juego"); }
    g_nat_was = 1;
    float yaw = atan2f(a[0], a[2]) + (yh - g_nat_ref);
    float pitch = clampf(ph, -1.48f, 1.48f);
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    __attribute__((aligned(16))) float fwd[4] = { cp * sy, sp, cp * cy, 0.0f };
    __attribute__((aligned(16))) float up[4]  = { -sp * sy, cp, -sp * cy, 0.0f };
    __attribute__((aligned(16))) float npos[4] = { pos[0], pos[1], pos[2], pos[3] };
    if (cfg_natfull) {
        /* (E10) camara = cuerpo (giro del juego, sin la referencia) x cabeza entera (giro, inclinacion y LADEO);
         * posicion = la del juego + la de la cabeza + la del ojo que se va a dibujar con esta camara */
        float B[3][3], C[3][3], off[3], ev[3] = { 0, 0, 0 };
        euler_cam(atan2f(a[0], a[2]) - g_nat_ref, 0.0f, B);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
            float t = 0; for (int k = 0; k < 3; k++) t += B[i][k] * g_rt_Rg[k][j]; C[i][j] = t;
        }
        LONG e = -1;
        if (g_twin_on == 2) e = 0;   /* (E27) camara en el CENTRO de la cabeza: cada ojo se aparta luego en H */
        else if (g_stereo_on) { e = g_eye ^ g_nat_flip; for (int i = 0; i < 3; i++) ev[i] = g_eyepos[e][i] * cfg_ipdscale; }
        float hl[3];
        for (int i = 0; i < 3; i++) hl[i] = g_rt_dg[i] + g_rt_Rg[i][0] * ev[0] + g_rt_Rg[i][1] * ev[1] + g_rt_Rg[i][2] * ev[2];
        for (int i = 0; i < 3; i++) off[i] = B[i][0] * hl[0] + B[i][1] * hl[1] + B[i][2] * hl[2];
        /* (E20) GIRAR SOBRE TI MISMO. La cabeza (g_rt_dg) va respecto a un punto fijo de la habitacion, y al girar
         * con el stick el juego gira ese punto: si no estas justo encima, la cabeza daba vueltas alrededor de el
         * (como en un tiovivo). Al cambiar el giro del cuerpo se suma lo justo (g_piv) para que la cabeza se quede
         * donde esta. g_piv se va gastando al andar con el stick (no se nota) y nunca pasa de 1 m. */
        {
            static float lb = 0, lpos[3]; static int have = 0;
            float bang = atan2f(a[0], a[2]) - g_nat_ref;
            if (!cfg_pivote || g_nat_reset_piv) { g_piv[0] = g_piv[1] = g_piv[2] = 0; have = 0; g_nat_reset_piv = 0; }
            if (cfg_pivote && have) {
                float d = bang - lb;
                while (d > 3.14159265f) d -= 6.28318531f;
                while (d < -3.14159265f) d += 6.28318531f;
                if (fabsf(d) > 1e-5f) {
                    float Bo[3][3]; euler_cam(lb, 0.0f, Bo);
                    for (int i = 0; i < 3; i += 2)   /* solo en horizontal (x, z) */
                        g_piv[i] += (Bo[i][0] * g_rt_dg[0] + Bo[i][1] * g_rt_dg[1] + Bo[i][2] * g_rt_dg[2])
                                  - (B[i][0] * g_rt_dg[0] + B[i][1] * g_rt_dg[1] + B[i][2] * g_rt_dg[2]);
                    InterlockedIncrement(&g_piv_n);
                }
                /* al andar (el juego mueve el cuerpo), se gasta g_piv lo mismo que se ha andado */
                float mx = pos[0] - lpos[0], mz = pos[2] - lpos[2], mv = sqrtf(mx * mx + mz * mz);
                float pl = sqrtf(g_piv[0] * g_piv[0] + g_piv[2] * g_piv[2]);
                if (pl > 1e-6f && mv > 1e-4f && mv < 1.0f) {
                    float k = mv * 0.5f >= pl ? 0.0f : (pl - mv * 0.5f) / pl;
                    g_piv[0] *= k; g_piv[2] *= k; pl *= k;
                }
                if (pl > 1.0f) { g_piv[0] /= pl; g_piv[2] /= pl; }
            }
            lb = bang; lpos[0] = pos[0]; lpos[1] = pos[1]; lpos[2] = pos[2]; have = 1;
        }
        for (int i = 0; i < 3; i++) { fwd[i] = C[i][2]; up[i] = C[i][1]; npos[i] = pos[i] + off[i] + g_piv[i]; }
        {   /* (E19) para las balas: giro de la camara y centro de la cabeza (sin el ojo), en el mundo */
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) g_bul_C[i][j] = C[i][j];
            float hc[3];
            for (int i = 0; i < 3; i++) hc[i] = B[i][0] * g_rt_dg[0] + B[i][1] * g_rt_dg[1] + B[i][2] * g_rt_dg[2];
            for (int i = 0; i < 3; i++) g_bul_hc[i] = pos[i] + hc[i] + g_piv[i];
            g_bul_t = GetTickCount64();
        }
        LONG k = InterlockedIncrement(&g_nat_ri) & 7;
        g_nat_ring[k][0] = npos[0]; g_nat_ring[k][1] = npos[1]; g_nat_ring[k][2] = npos[2]; g_nat_ringe[k] = e;
        g_nat_ringpose[k] = g_frame_pose;   /* (E22) la postura de la que salio g_rt_Rg (head_apply con la misma lectura) */
    }
    g_nat_yh = yh;      /* raton_make_H: cuerpo = juego - yh, y la cabeza entera encima => el giro y la inclinacion quedan exactos; H solo lleva ladeo, ojos y posicion */
    g_nat_last = GetTickCount64();
    InterlockedIncrement(&g_nat_hits);
    orig(mgr, cfg_natfull ? npos : pos, fwd, up, a5, a6);
}

/* (E19) BALAS. metro.exe+0x3D7FF0 crea cada bala (la del jugador y las de los enemigos): RCX = dueno, RDX = origen,
 * R8 = direccion (ya con la dispersion del juego). Llama al constructor +0x3D6970, que copia la direccion a la bala.
 * (Encontrado por D-Rey86 en Metro 2033 Redux; mismas direcciones en Last Light Redux.)
 * El arma se pinta movida por G (mano ahora x mano calibrada^-1) en los ejes de la camara. A la bala del jugador le
 * aplicamos ese mismo movimiento: origen y direccion pasan de "como si el arma estuviera en reposo" a "con el arma
 * en la mano". Asi sale del arma y hacia donde apunta su canon, y se conserva la dispersion del juego.
 * Bala del jugador = sale a menos de 1,5 m del centro de la cabeza y va a menos de 20 grados de donde mira la camara
 * (las de los enemigos salen de ellos o te apuntan a ti). */
typedef long long (*bul_fn_t)(void *, const float *, const float *, void *, void *, void *, void *, void *, void *, void *);
static bul_fn_t g_bul_tramp = NULL;
static long long bul_hook(void *owner, const float *org, const float *dir, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10)
{
    __attribute__((aligned(16))) float no[4], nd[4];
    int mine = 0;
    if (cfg_balas && org && dir && g_hand_ok && g_hand_cal && nat_full() && g_nat_was && GetTickCount64() - g_bul_t < 500) {
        float (*C)[3] = g_bul_C;
        float rv[3], ov[3], dv[3];
        for (int i = 0; i < 3; i++) rv[i] = org[i] - g_bul_hc[i];
        float dist2 = rv[0] * rv[0] + rv[1] * rv[1] + rv[2] * rv[2];
        float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        float cosf_ = dl > 1e-4f ? (dir[0] * C[0][2] + dir[1] * C[1][2] + dir[2] * C[2][2]) / dl : -1.0f;
        if (isfinite(dist2) && dist2 < 1.5f * 1.5f && isfinite(cosf_) && cosf_ > 0.94f) {   /* 0.94 = 20 grados */
            /* a ejes de la camara (C^T), mover con la mano (G), y de vuelta al mundo (C) */
            for (int i = 0; i < 3; i++) {
                ov[i] = C[0][i] * rv[0] + C[1][i] * rv[1] + C[2][i] * rv[2];
                dv[i] = C[0][i] * dir[0] + C[1][i] * dir[1] + C[2][i] * dir[2];
            }
            float og[3], dg[3];
            for (int i = 0; i < 3; i++) {
                og[i] = g_hand_G[i][0] * ov[0] + g_hand_G[i][1] * ov[1] + g_hand_G[i][2] * ov[2] + g_hand_G[i][3];
                dg[i] = g_hand_G[i][0] * dv[0] + g_hand_G[i][1] * dv[1] + g_hand_G[i][2] * dv[2];
            }
            for (int i = 0; i < 3; i++) {
                no[i] = g_bul_hc[i] + C[i][0] * og[0] + C[i][1] * og[1] + C[i][2] * og[2];
                nd[i] = C[i][0] * dg[0] + C[i][1] * dg[1] + C[i][2] * dg[2];
            }
            no[3] = org[3]; nd[3] = dir[3];
            float l = sqrtf(nd[0] * nd[0] + nd[1] * nd[1] + nd[2] * nd[2]);
            if (isfinite(l) && l > 1e-4f && isfinite(no[0]) && isfinite(no[1]) && isfinite(no[2])) {
                for (int i = 0; i < 3; i++) nd[i] *= dl / l;     /* mismo largo que la del juego */
                mine = 1;
                g_bul_lastdev = acosf(clampf((nd[0] * dir[0] + nd[1] * dir[1] + nd[2] * dir[2]) / (dl * dl), -1.0f, 1.0f)) * 57.29578f;
                static int said = 0;
                if (said < 12) {
                    said++;
                    vr_log("BALA: del jugador. origen %.2f %.2f %.2f -> %.2f %.2f %.2f | direccion %.3f %.3f %.3f -> %.3f %.3f %.3f (%.1f grados)",
                           org[0], org[1], org[2], no[0], no[1], no[2], dir[0], dir[1], dir[2], nd[0], nd[1], nd[2], g_bul_lastdev);
                }
            }
        }
    }
    if (mine) InterlockedIncrement(&g_bul_mine); else InterlockedIncrement(&g_bul_other);
    return g_bul_tramp(owner, mine ? no : org, mine ? nd : dir, a4, a5, a6, a7, a8, a9, a10);
}
static void bul_install(void)
{
    if (g_bul_inst || !cfg_balas) return;
    g_bul_inst = -1;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    BYTE *fn = base + 0x3D7FF0, *ctor = base + 0x3D6970;
    int found = 0;   /* comprobacion: dentro de la funcion tiene que haber una llamada al constructor de la bala */
    for (int i = 0; i < 0x400 && !found; i++) if (fn[i] == 0xE8 && fn + i + 5 + *(INT32 *)(fn + i + 1) == ctor) found = 1;
    char line[64]; int L = 0; for (int q = 0; q < 16; q++) L += snprintf(line + L, sizeof(line) - L, "%02X ", fn[q]);
    if (!found) { vr_log("BALAS: no encuentro la funcion que crea la bala (%s); las balas iran hacia donde miras", line); return; }
    mh_start();
    MH_STATUS st = MH_CreateHook(fn, (void *)bul_hook, (void **)&g_bul_tramp);
    if (st == MH_OK) st = MH_EnableHook(fn);
    vr_log("BALAS: gancho en metro.exe+0x3D7FF0 (%s) -> %s", line, MH_StatusToString(st));
    if (st == MH_OK) g_bul_inst = 1;
}

/* Parcheo seguro con el juego en marcha: primero un "espera aqui" de 2 bytes (EB FE) escrito de golpe, para que
 * ningun hilo entre a medias; 2 ms para que el que ya estuviera dentro salga; el resto; y los 2 primeros de golpe. */
static int patch_bytes(BYTE *dst, const BYTE *src, SIZE_T n)
{
    DWORD old = 0;
    if (n < 2 || ((UINT_PTR)dst & 1)) return 0;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    __atomic_store_n((volatile SHORT *)dst, (SHORT)0xFEEB, __ATOMIC_SEQ_CST);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    Sleep(2);
    memcpy(dst + 2, src + 2, n - 2);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    __atomic_store_n((volatile SHORT *)dst, (SHORT)(src[0] | (src[1] << 8)), __ATOMIC_SEQ_CST);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}


/* (E14) Andar hacia donde miras. El juego convierte "adelante/atras/lados" a direccion del mundo con el giro del
 * CUERPO (metro.exe+0x28D110, llamada del jugador en +0x1F32C5). La cabeza gira la camara pero no el cuerpo: si te
 * giras en la habitacion, "adelante" del stick iba hacia donde mira el cuerpo (detras de ti). Se gira el resultado
 * con lo que la cabeza se ha girado desde la referencia (como D-Rey86). */
typedef void (*mov_fn_t)(void *, void *, int, void *, float *, void *, float, void *, void *, void *, void *, void *, void *);
static void *g_mov_tramp = NULL; static BYTE *g_mov_ret = NULL; static int g_mov_installed = 0;
static void mov_hook(void *pl, void *req, int prev, void *acc, float *mv, void *a6, float dt, void *basis,
                     void *a9, void *a10, void *a11, void *a12, void *a13)
{
    ((mov_fn_t)g_mov_tramp)(pl, req, prev, acc, mv, a6, dt, basis, a9, a10, a11, a12, a13);
    if ((BYTE *)__builtin_return_address(0) == g_mov_ret) {
        ULONGLONG t = GetTickCount64();
        g_mov_seen = t;   /* (E20e) hay jugador: partida */
        InterlockedIncrement(&g_mov_calls);
        if (dt > 1e-6f) g_mov_run = t; else InterlockedIncrement(&g_mov_dt0);   /* (E20h) en la pausa el tiempo se para */
        if (cfg_recarga_manual) expel_tick();   /* (E38) expulsar el cargador al momento */
    }
    if ((BYTE *)__builtin_return_address(0) != g_mov_ret || !mv || !nat_active() || !g_nat_was) return;
    if (!isfinite(mv[0]) || !isfinite(mv[2])) return;
    float d = atan2f(g_rt_Rg[0][2], g_rt_Rg[2][2]) - g_nat_ref;
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;
    float c = cosf(d), sn = sinf(d), x = mv[0], z = mv[2];
    mv[0] = c * x + sn * z;
    mv[2] = -sn * x + c * z;
    InterlockedIncrement(&g_mov_hits);
}
static void mov_install(void)
{
    if (g_mov_installed) return;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    BYTE *fn = base + 0x28D110, *call = base + 0x1F32C5;
    static const BYTE expect[14] = { 0x48,0x8B,0xC4, 0x44,0x89,0x40,0x18, 0x55, 0x53, 0x57, 0x48,0x8D,0x68,0xE1 };
    if (memcmp(fn, expect, 14) != 0 || call[0] != 0xE8 || call + 5 + *(INT32 *)(call + 1) != fn) {
        g_mov_installed = -1; vr_log("ANDAR: no encuentro la funcion del movimiento; el stick ira segun el cuerpo"); return;
    }
    BYTE *t = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) { g_mov_installed = -1; return; }
    memcpy(t, fn, 14);
    t[14] = 0xFF; t[15] = 0x25; *(UINT32 *)(t + 16) = 0; *(UINT64 *)(t + 20) = (UINT64)(fn + 14);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    g_mov_tramp = t; g_mov_ret = call + 5;
    BYTE j[14];
    j[0] = 0xFF; j[1] = 0x25; *(UINT32 *)(j + 2) = 0; *(UINT64 *)(j + 6) = (UINT64)(void *)mov_hook;
    if (!patch_bytes(fn, j, 14)) { g_mov_installed = -1; return; }
    g_mov_installed = 1;
    vr_log("ANDAR: gancho puesto en metro.exe+0x28D110 (se anda hacia donde miras)");
}
static void nat_install(void)
{
    if (g_nat_installed != 0 || !cfg_nativa) return;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    BYTE *fn = base + 0x69B900, *call = base + 0x1F3960;
    static const BYTE expect[18] = { 0x48,0x89,0x5C,0x24,0x18, 0x48,0x89,0x74,0x24,0x20, 0x57, 0x48,0x81,0xEC,0xA0,0x00,0x00,0x00 };
    static int tries = 0;
    if (memcmp(fn, expect, sizeof(expect)) != 0 || call[0] != 0xE8 || call + 5 + *(INT32 *)(call + 1) != fn) {
        if (++tries == 40) { vr_log("NATIVA: no encuentro la funcion de la camara (otra version del juego?); sigo con el raton"); g_nat_installed = -1; }
        return;
    }
    BYTE *t = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) { g_nat_installed = -1; return; }
    memcpy(t, fn, 18);
    t[18] = 0xFF; t[19] = 0x25; *(UINT32 *)(t + 20) = 0; *(UINT64 *)(t + 24) = (UINT64)(fn + 18);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    g_nat_tramp = t; g_nat_ret = call + 5;
    BYTE j[18];
    j[0] = 0xFF; j[1] = 0x25; *(UINT32 *)(j + 2) = 0; *(UINT64 *)(j + 6) = (UINT64)(void *)nat_hook;
    for (int i = 14; i < 18; i++) j[i] = 0x90;
    if (!patch_bytes(fn, j, 18)) { g_nat_installed = -1; vr_log("NATIVA: no pude parchear la funcion"); return; }
    g_nat_installed = 1;
    mov_install();
    bul_install();
    ar_install();   /* (E36) */
    vr_log("NATIVA: gancho puesto en metro.exe+0x69B900 (llamada de la camara del jugador en +0x1F3960)");
}

/* (E9) Oclusion de CPU (metro.exe+0x826B50): el juego proyecta la caja de cada objeto sobre una profundidad
 * pequena del fotograma anterior y lo descarta si "queda tapado". En VR (cabeza que se mueve, FOV distinto)
 * descarta cosas que si se ven: paredes y objetos que desaparecen un instante. Con F9: siempre "visible". */
static BYTE g_ocl_orig[6]; static int g_ocl_state = 0;   /* 0 sin tocar, 1 parcheado, -1 no encontrado */
static void ocl_update(void)
{
    BYTE *f = (BYTE *)GetModuleHandleA(NULL) + 0x826B50;
    static const BYTE expect[16] = { 0x4C,0x8B,0xDC, 0x49,0x89,0x5B,0x20, 0x41,0x56, 0x48,0x81,0xEC,0xA0,0x00,0x00,0x00 };
    static const BYTE yes[6] = { 0xB8,0x01,0x00,0x00,0x00, 0xC3 };
    int want = (g_mode == 1 && cfg_oclusion);
    if (want && g_ocl_state == 0) {
        if (memcmp(f, expect, sizeof(expect)) != 0) return;
        memcpy(g_ocl_orig, f, 6);
        if (patch_bytes(f, yes, 6)) { g_ocl_state = 1; vr_log("OCLUSION: desactivada (todo lo que este en el cono se dibuja)"); }
    } else if (!want && g_ocl_state == 1) {
        if (patch_bytes(f, g_ocl_orig, 6)) { g_ocl_state = 0; vr_log("OCLUSION: la del juego otra vez"); }
    }
}

/* ------------------------------------------------ buscador de la camara en memoria (Shift+H) */
/* Como el "Cheat Engine": buscamos en la memoria del juego numeros que valgan lo mismo que el giro
 * (izq/der) o la inclinacion (arriba/abajo) de su camara, y vamos descartando los que no siguen al raton. */
typedef struct { ULONG_PTR addr; unsigned char what, var, unit, miss; } Cand;   /* what: 0 giro, 1 inclinacion */
static Cand *g_cand = NULL;
static Cand g_conf_pitch;
static int g_ncand = 0;
static volatile LONG g_scan_running = 0;
#define NVAR 8
/* lleva un angulo a -180..180 grados. Sin bucles: con un numero basura (enorme o infinito) un bucle no acabaria nunca */
static float wrap_pi(float a)
{
    if (!isfinite(a) || fabsf(a) > 1.0e6f) return 1000.0f;   /* "no cuadra con nada" */
    a = fmodf(a + 3.14159265f, 6.2831853f);
    if (a < 0) a += 6.2831853f;
    return a - 3.14159265f;
}
static float variant(int what, int k, float yaw, float pitch)
{
    const float H = 1.5707963f, PI = 3.14159265f;
    float a = what == 0 ? yaw : pitch;
    float sg = (k & 1) ? -1.0f : 1.0f, offs[4] = { 0, H, PI, -H };
    return sg * a + offs[k >> 1];
}
static int matches(float v, int what, int k, int unit, float yaw, float pitch)
{
    if (!isfinite(v)) return 0;
    float x = unit ? v * 0.01745329f : v;           /* unit 1 = grados */
    if (fabsf(x) > 1000.0f) return 0;
    float d = wrap_pi(x - variant(what, k, yaw, pitch));
    return fabsf(d) < 0.006f;
}

/* what 0/1 = angulos; 2/3/4 = componentes x/z/y del vector "adelante" (por si guarda direccion o matriz) */
static int cmatch(float v, int what, int k, int unit, const float *cur)
{
    if (what < 2) return matches(v, what, k, unit, cur[0], cur[1]);
    if (!isfinite(v)) return 0;
    float t = cur[what] * (k ? -1.0f : 1.0f);
    return fabsf(v - t) < 0.002f;
}

/* Busca "flechas" (punteros) que apunten cerca de 'target' dentro de metro.exe: asi podremos encontrar
 * la camara en cada partida sin volver a buscar. */
static int find_static_ptrs(ULONG_PTR lo, ULONG_PTR hi, int level, const char *tag)
{
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)exe;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((unsigned char *)exe + dos->e_lfanew);
    ULONG_PTR b = (ULONG_PTR)exe, e = b + nt->OptionalHeader.SizeOfImage;
    int found = 0;
    for (ULONG_PTR a = b; a + 8 <= e; a += 8) {
        ULONG_PTR v; SIZE_T got = 0;
        if ((a & 0xFFF) == 0) {   /* saltar paginas que no se pueden leer */
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery((void *)a, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) { a += 0xFF8; continue; }
        }
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)a, &v, 8, &got) || got != 8) continue;
        if (v >= lo && v <= hi) {
            if (found < 12) vr_log("  PUNTERO%s nivel %d: metro.exe+0x%llX -> %p (+0x%llX)", tag, level,
                                   (unsigned long long)(a - b), (void *)v, (unsigned long long)(hi - v));
            found++;
        }
    }
    return found;
}

static void scan_neighbourhood(ULONG_PTR addr)
{
    vr_log("BUSCADOR: inclinacion CONFIRMADA en %p. Busco quien apunta a ella desde metro.exe...", (void *)addr);
    int n1 = find_static_ptrs(addr - 0x4000, addr, 1, "");
    if (!n1) {
        /* nivel 2: punteros en la memoria normal que apunten cerca, y luego punteros fijos a esos */
        vr_log("BUSCADOR: ninguno directo; busco a dos saltos...");
        static ULONG_PTR mids[512]; int nm = 0;
        MEMORY_BASIC_INFORMATION mbi; unsigned char *a = NULL;
        static ULONG_PTR chunk[1 << 16];
        while (nm < 512 && VirtualQuery(a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            unsigned char *next = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
                for (SIZE_T off = 0; off < mbi.RegionSize && nm < 512; off += sizeof(chunk)) {
                    SIZE_T want = mbi.RegionSize - off; if (want > sizeof(chunk)) want = sizeof(chunk);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), (unsigned char *)mbi.BaseAddress + off, chunk, want, &got)) continue;
                    for (SIZE_T i = 0; i < got / 8 && nm < 512; i++)
                        if (chunk[i] >= addr - 0x4000 && chunk[i] <= addr) mids[nm++] = (ULONG_PTR)mbi.BaseAddress + off + i * 8;
                }
            }
            if (next <= a) break;
            a = next;
        }
        vr_log("BUSCADOR: %d punteros intermedios", nm);
        int tot = 0;
        for (int i = 0; i < nm && tot < 12; i++) {
            char tag[64]; snprintf(tag, sizeof(tag), " (via %p)", (void *)mids[i]);
            tot += find_static_ptrs(mids[i] - 0x400, mids[i], 2, tag);
        }
        if (!tot) vr_log("BUSCADOR: tampoco a dos saltos (habra que buscar cada vez)");
    }
    /* Vecinos: el giro suele estar al lado de la inclinacion. Foto ahora, giras, y otra foto. */
    float before[128]; SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (void *)(addr - 256), before, sizeof(before), &got);
    float y0 = g_cam_yaw;
    vr_log("BUSCADOR: giro la camara yo solo para mirar las casillas vecinas...");
    for (int k = 0; k < 20; k++) {
        INPUT in; memset(&in, 0, sizeof(in));
        in.type = INPUT_MOUSE; in.mi.dx = 40; in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(in)); Sleep(15);
    }
    for (int w = 0; w < 300; w++) {
        Sleep(50);
        if (fabsf(wrap_pi(g_cam_yaw - y0)) > 0.3f) {
            float py = g_cam_yaw; int still = 0;
            for (int k = 0; k < 60 && still < 5; k++) { Sleep(40); if (fabsf(wrap_pi(g_cam_yaw - py)) < 0.0005f) still++; else still = 0; py = g_cam_yaw; }
            if (still >= 5) break;
        }
    }
    float after[128];
    ReadProcessMemory(GetCurrentProcess(), (void *)(addr - 256), after, sizeof(after), &got);
    float y1 = g_cam_yaw;
    vr_log("BUSCADOR: vecinos de la inclinacion (giro de la camara: antes %.4f rad, despues %.4f rad, cambio %.1f grados)",
           y0, y1, wrap_pi(y1 - y0) * RAD2DEG);
    for (int i = 0; i < 128; i++)
        if (before[i] != after[i] && isfinite(before[i]) && isfinite(after[i]))
            vr_log("  %+5d: %.5f -> %.5f", (i - 64) * 4, before[i], after[i]);
    Beep(1100, 120);
}

static DWORD WINAPI scan_thread(LPVOID u)
{
    (void)u;
    vr_log("BUSCADOR: empiezo (todo automatico: no toques raton ni teclado hasta los 3 pitidos finales)");
    Sleep(2000);
    float yaw0 = g_cam_yaw, pitch0 = g_cam_pitch;
    float cur0[5] = { yaw0, pitch0, g_cam_fx, g_cam_fz, g_cam_fy };
    if (g_cam_seq == 0) { vr_log("BUSCADOR: aun no he visto la camara; entra en partida y vuelve a pulsar Shift+H"); g_scan_running = 0; return 0; }
    int cap = 4000000;
    free(g_cand); g_cand = (Cand *)malloc(sizeof(Cand) * cap); g_ncand = 0;
    if (!g_cand) { g_scan_running = 0; return 0; }
    static float chunk[1 << 18];
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *a = NULL;
    SIZE_T scanned = 0;
    /* no mirar en nuestra propia memoria (nuestras variables y la pila de este hilo tambien valen "el giro") */
    HMODULE me = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)scan_thread, &me);
    ULONG_PTR me_lo = (ULONG_PTR)me, me_hi = me_lo;
    if (me) {
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)me;
        IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((unsigned char *)me + dos->e_lfanew);
        me_hi = me_lo + nt->OptionalHeader.SizeOfImage;
    }
    MEMORY_BASIC_INFORMATION stk; int here = 0;
    VirtualQuery(&here, &stk, sizeof(stk));
    while (VirtualQuery(a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        unsigned char *next = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
        int skip = ((ULONG_PTR)mbi.BaseAddress >= me_lo && (ULONG_PTR)mbi.BaseAddress < me_hi) ||
                   mbi.AllocationBase == stk.AllocationBase ||
                   ((ULONG_PTR)g_cand >= (ULONG_PTR)mbi.BaseAddress && (ULONG_PTR)g_cand < (ULONG_PTR)next);
        if (!skip && mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
            for (SIZE_T off = 0; off < mbi.RegionSize; off += sizeof(chunk)) {
                SIZE_T want = mbi.RegionSize - off; if (want > sizeof(chunk)) want = sizeof(chunk);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (unsigned char *)mbi.BaseAddress + off, chunk, want, &got)) continue;
                scanned += got;
                for (SIZE_T i = 0; i < got / 4; i++) {
                    float v = chunk[i];
                    if (v == 0.0f || !isfinite(v)) continue;
                    for (int w = 0; w < 2; w++) for (int un = 0; un < 2; un++) for (int k = 0; k < (w == 0 ? NVAR : 2); k++)
                        if (!(w >= 2 && fabsf(cur0[w]) < 0.25f) && cmatch(v, w, k, un, cur0) && g_ncand < cap) {
                            Cand c = { (ULONG_PTR)mbi.BaseAddress + off + i * 4, (unsigned char)w, (unsigned char)k, (unsigned char)un, 0 };
                            g_cand[g_ncand++] = c;
                        }
                }
            }
        }
        if (next <= a) break;
        a = next;
    }
    vr_log("BUSCADOR: mirados %llu MB, %d candidatos. Ahora muevo yo el raton: NO lo toques",
           (unsigned long long)(scanned >> 20), g_ncand);
    Beep(1000, 120); Beep(1000, 120);       /* dos pitidos: ya puedes mover el raton */
    float lyaw = yaw0, lpitch = pitch0, travel_y = 0, travel_p = 0;
    int rounds = 0;
    /* AUTOMATICO: la propia DLL mueve el raton (como si lo movieras tu), en un dibujo fijo */
    static const int pat[][2] = { {260,0}, {0,90}, {-180,0}, {0,-140}, {320,0}, {0,70}, {-400,0}, {0,-50},
                                  {150,0}, {0,110}, {-220,0}, {0,-90}, {300,0}, {0,60}, {-260,0}, {0,-40} };
    int pi_ = 0;
    for (int t = 0; t < 1200 && g_ncand > 0; t++) {
        if (t % 4 == 0) {
            int dx = pat[pi_ % 16][0], dy_ = pat[pi_ % 16][1]; pi_++;
            for (int k = 0; k < 10; k++) {          /* en 10 pasitos, para que parezca un movimiento normal */
                INPUT in; memset(&in, 0, sizeof(in));
                in.type = INPUT_MOUSE; in.mi.dx = dx / 10; in.mi.dy = dy_ / 10; in.mi.dwFlags = MOUSEEVENTF_MOVE;
                SendInput(1, &in, sizeof(in));
                Sleep(15);
            }
        }
        Sleep(50);
        float yaw = g_cam_yaw, pitch = g_cam_pitch;
        float dy = fabsf(wrap_pi(yaw - lyaw)), dp = fabsf(pitch - lpitch);
        if (dy < 0.05f && dp < 0.05f) continue;            /* esperar a que se haya movido algo */
        /* y ahora esperar a que el raton se PARE: con la camara quieta, el numero del juego y el de la
         * imagen coinciden (moviendose, uno puede ir un fotograma por delante del otro) */
        {
            int still = 0;
            float py = g_cam_yaw, pp = g_cam_pitch;
            for (int w = 0; w < 100 && still < 4; w++) {
                Sleep(40);
                float ny = g_cam_yaw, np = g_cam_pitch;
                if (fabsf(wrap_pi(ny - py)) < 0.0005f && fabsf(np - pp) < 0.0005f) still++; else still = 0;
                py = ny; pp = np;
            }
            if (still < 4) continue;
        }
        yaw = g_cam_yaw; pitch = g_cam_pitch;
        dy = fabsf(wrap_pi(yaw - lyaw)); dp = fabsf(pitch - lpitch);
        travel_y += dy; travel_p += dp; lyaw = yaw; lpitch = pitch; rounds++;
        static Cand last[64]; static int nlast = 0;
        if (g_ncand <= 64) { memcpy(last, g_cand, sizeof(Cand) * g_ncand); nlast = g_ncand; }
        int keep = 0;
        for (int i = 0; i < g_ncand; i++) {
            Cand c = g_cand[i];
            float v; SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (void *)c.addr, &v, 4, &got) || got != 4) continue;
            /* el giro se comprueba cuando se ha movido el giro, la inclinacion cuando se ha movido la inclinacion,
             * los componentes del vector cuando se ha movido cualquiera */
            float cur[5] = { yaw, pitch, g_cam_fx, g_cam_fz, g_cam_fy };
            if ((c.what == 0 && dy >= 0.05f) || (c.what == 1 && dp >= 0.05f) || c.what >= 2) {
                /* se permite fallar 2 veces (el bueno a veces va un poquito desfasado); a la 3a fuera */
                if (!cmatch(v, c.what, c.var, c.unit, cur) && ++c.miss >= 3) continue;
            }
            g_cand[keep++] = c;
        }
        g_ncand = keep;
        if (g_ncand == 0 && nlast) {
            vr_log("BUSCADOR: se han caido todos; estos eran los ultimos que quedaban:");
            memcpy(g_cand, last, sizeof(Cand) * nlast); g_ncand = nlast; nlast = 0;
            break;
        }
        if (rounds % 5 == 0) vr_log("BUSCADOR: quedan %d candidatos (giro recorrido %.0f grados, inclinacion %.0f)", g_ncand, travel_y * RAD2DEG, travel_p * RAD2DEG);
        if (travel_y > 2.0f && travel_p > 0.8f && rounds > 12 && g_ncand < 300) break;
        if (rounds > 40) break;
        Beep(800, 40);                                      /* pitidito: comprobado */
        t = (t / 4 + 1) * 4 - 1;                            /* siguiente movimiento automatico */
    }
    vr_log("BUSCADOR: terminado. %d candidatos:", g_ncand);
    Beep(1400, 400);                         /* pitido largo: terminado */
    const char *vn[NVAR] = { "a", "-a", "a+90", "90-a", "a+180", "180-a", "a-90", "-a-90" };
    for (int i = 0; i < g_ncand && i < 60; i++) {
        unsigned long long off; char mb[MAX_PATH];
        module_of((const void *)g_cand[i].addr, &off, mb);
        float v = 0; SIZE_T got = 0; ReadProcessMemory(GetCurrentProcess(), (void *)g_cand[i].addr, &v, 4, &got);
        const char *wn[5] = { "GIRO", "INCLINACION", "VECTOR_X", "VECTOR_Z", "VECTOR_Y" };
        vr_log("  %s %p (%s+0x%llX) forma=%s %s valor=%.4f", wn[g_cand[i].what], (void *)g_cand[i].addr,
               mb, off, g_cand[i].what < 2 ? vn[g_cand[i].var] : (g_cand[i].var ? "-v" : "v"), g_cand[i].unit ? "grados" : "radianes", v);
    }
    /* PRUEBA DE ESCRITURA: a cada candidato le sumamos un poco y miramos si la camara del juego se mueve */
    vr_log("BUSCADOR: prueba de escritura (no toques el raton unos segundos)...");
    Beep(500, 300);
    Sleep(1000);
    int confirmed = 0;
    int tested = 0;
    for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < g_ncand && tested < 120; i++) {
        Cand c = g_cand[i];
        if (c.miss != pass) continue;          /* primero los que nunca han fallado */
        tested++;
        float v; SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)c.addr, &v, 4, &got) || got != 4) continue;
        float delta_rad = 0.15f, dv = c.unit ? delta_rad * RAD2DEG : delta_rad;
        float before = c.what ? g_cam_pitch : g_cam_yaw;
        float nv = v + dv; SIZE_T w = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), (void *)c.addr, &nv, 4, &w)) continue;
        Sleep(150);
        float after = c.what ? g_cam_pitch : g_cam_yaw;
        float moved = wrap_pi(after - before);
        float expect = (c.var & 1) ? -delta_rad : delta_rad;   /* si la forma es "-a", la camara gira al reves */
        /* deshacer: restamos lo que sumamos (sobre el valor que haya ahora) */
        float cv; ReadProcessMemory(GetCurrentProcess(), (void *)c.addr, &cv, 4, &got);
        float back = cv - dv; WriteProcessMemory(GetCurrentProcess(), (void *)c.addr, &back, 4, &w);
        Sleep(100);
        int ok = fabsf(moved - expect) < 0.04f;
        if (ok) { if (!confirmed) g_conf_pitch = c; confirmed++; }
        vr_log("  prueba %s %p: la camara se movio %.1f grados (esperado %.1f) -> %s", c.what ? "INCLINACION" : "GIRO",
               (void *)c.addr, moved * RAD2DEG, expect * RAD2DEG, ok ? "CONFIRMADO" : "no");
    }
    vr_log("BUSCADOR: %d confirmados", confirmed);
    if (confirmed) scan_neighbourhood(g_conf_pitch.addr);
    Beep(1400, 150); Beep(1400, 150); Beep(1400, 150);
    g_scan_running = 0;
    return 0;
}

/* ------------------------------------------------ la linterna: FOV del juego agrandado (E6) */
/* El juego solo dibuja lo que cae dentro de su "linterna": su campo de vision (50.6 grados en vertical, unos 80
 * en horizontal), centrado donde apunta el raton. Las gafas ven 104 x 98: lo de fuera no se dibuja y salen huecos.
 * Buscamos en memoria el numero del FOV del juego y, con F9, lo subimos a linterna_grados (en vertical).
 * La imagen se sigue haciendo con la lente del visor (fov_xy): solo cambia QUE se dibuja, no COMO se ve. */
#define FOV_KINDS 6
#define FOV_MAXC 1000
static volatile LONG g_fov_running = 0;
static float * volatile g_fov_addr = NULL;
static volatile int g_fov_kind = 0;
static float g_fov_aspect = 16.0f / 9.0f;     /* ancho/alto de la lente del juego (P11/P00) al encontrarlo */
static volatile LONG g_fov_tries = 0;         /* busquedas automaticas seguidas sin exito */
static volatile ULONGLONG g_fov_next_auto = 0;/* no buscar solo antes de esta hora */
static float g_fov_saved = 0;                 /* lo que habia antes de agrandarlo */
static volatile LONG g_fov_applied = 0;       /* 1 = ahora mismo esta agrandado */
static const char *g_fov_kname[FOV_KINDS] = { "grados (vertical)", "radianes (vertical)", "medio angulo en radianes (vertical)",
                                              "tangente del medio angulo (vertical)", "grados (horizontal)", "radianes (horizontal)" };

/* el numero tal como lo guardaria el juego, para un FOV vertical de vdeg grados */
static float fov_value(int kind, float vdeg)
{
    float hv = vdeg * 0.00872665f;                       /* medio angulo vertical (radianes) */
    float hh = atanf(tanf(hv) * g_fov_aspect);           /* medio angulo horizontal */
    switch (kind) {
    case 0: return vdeg;
    case 1: return hv * 2.0f;
    case 2: return hv;
    case 3: return tanf(hv);
    case 4: return hh * 2.0f * RAD2DEG;
    default: return hh * 2.0f;
    }
}

/* FOV vertical con el que el juego decide que dibujar AHORA (sale de su lente original, antes de tocarla) */
static float fov_game_vdeg(void) { float p = g_orig_P11; return p > 0 ? 2.0f * atanf(1.0f / p) * RAD2DEG : 0; }

/* cuanto puedes girar la cabeza (respecto al raton) antes de que se vea el borde de la linterna */
static void fov_log_margin(void)
{
    if (!(g_vis_R > g_vis_L) || !(g_vis_UP > g_vis_DN)) return;
    float hv = cfg_linterna_deg * 0.00872665f;
    float hh = atanf(tanf(hv) * g_fov_aspect);
    float vh = fmaxf(atanf(g_vis_R), -atanf(g_vis_L)), vv = fmaxf(atanf(g_vis_UP), -atanf(g_vis_DN));
    vr_log("FOV: margen para girar la cabeza sin ver bordes: unos %.0f grados a los lados y %.0f arriba/abajo",
           (double)((hh - vh) * RAD2DEG), (double)((hv - vv) * RAD2DEG));
}

/* dejar de tocarlo (devolviendo lo que habia si estaba agrandado) */
static void fov_release(const char *why)
{
    float *addr = g_fov_addr;
    if (!addr) return;
    if (g_fov_applied) wr_float(addr, g_fov_saved);
    g_fov_applied = 0;
    g_fov_addr = NULL;
    vr_log("FOV: lo suelto (%s)", why);
}

/* espera a que el juego mande n camaras nuevas (como mucho ms milisegundos) */
static int fov_wait_frames(LONG n, int ms)
{
    LONG s0 = g_cam_seq;
    for (int t = 0; t < ms; t += 10) { if (g_cam_seq - s0 >= n) return 1; Sleep(10); }
    return 0;
}

static DWORD WINAPI fovscan_thread(LPVOID u)
{
    int manual = (int)(INT_PTR)u;
    Sleep(300);                                   /* por si se acaba de soltar: que la lente vuelva a la del juego */
    if (!g_cam_seq || g_orig_P11 <= 0 || g_orig_P00 <= 0 || !fov_wait_frames(3, 1000)) {
        vr_log("FOV: no veo la camara del juego ahora mismo; entra en partida");
        g_fov_running = 0; return 0;
    }
    float vdeg = fov_game_vdeg();
    g_fov_aspect = g_orig_P11 / g_orig_P00;
    vr_log("FOV: busqueda %s. El juego ve %.3f grados en vertical (P11=%.5f, ancho/alto=%.3f). Busco ese numero en memoria...",
           manual ? "a mano (Shift+F7)" : "automatica", (double)vdeg, (double)g_orig_P11, (double)g_fov_aspect);
    float tgt[FOV_KINDS];
    for (int k = 0; k < FOV_KINDS; k++) tgt[k] = fov_value(k, vdeg);

    /* lo que NO hay que mirar: nuestra propia DLL y la pila de este hilo (ahi estan nuestras copias del numero) */
    HMODULE me = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fovscan_thread, &me);
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    ULONG_PTR st_lo = (ULONG_PTR)tib->StackLimit, st_hi = (ULONG_PTR)tib->StackBase;

    static ULONG_PTR cand[FOV_MAXC]; static unsigned char ck[FOV_MAXC]; int nc = 0;
    static float chunk[1 << 18];
    ULONGLONG t0 = GetTickCount64();
    MEMORY_BASIC_INFORMATION mbi; unsigned char *a = NULL;
    while (nc < FOV_MAXC && VirtualQuery(a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        unsigned char *next = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
        ULONG_PTR rb = (ULONG_PTR)mbi.BaseAddress;
        int skip = (me && mbi.AllocationBase == (void *)me) || (rb < st_hi && rb + mbi.RegionSize > st_lo);
        if (!skip && mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
            for (SIZE_T off = 0; off < mbi.RegionSize && nc < FOV_MAXC; off += sizeof(chunk)) {
                SIZE_T want = mbi.RegionSize - off; if (want > sizeof(chunk)) want = sizeof(chunk);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (unsigned char *)mbi.BaseAddress + off, chunk, want, &got)) continue;
                for (SIZE_T i = 0; i < got / 4 && nc < FOV_MAXC; i++) {
                    float v = chunk[i];
                    if (!isfinite(v) || v <= 0) continue;
                    for (int k = 0; k < FOV_KINDS; k++)
                        if (fabsf(v - tgt[k]) <= 1e-4f * tgt[k]) { cand[nc] = rb + off + i * 4; ck[nc] = (unsigned char)k; nc++; break; }
                }
            }
        }
        if (next <= a) break;
        a = next;
    }
    vr_log("FOV: %d candidatos en %.1f s. Los pruebo uno a uno (en el monitor la imagen puede dar saltitos)",
           nc, (double)(GetTickCount64() - t0) / 1000.0);

    /* prueba: le sumo 20 grados y miro si la lente del juego cambia JUSTO lo esperado. Primero los "grados" */
    static const int order[FOV_KINDS] = { 0, 4, 1, 2, 3, 5 };
    float expect = 1.0f / tanf((vdeg + 20.0f) * 0.00872665f);   /* P11 que deberia salir */
    int found = 0, tested = 0;
    for (int o = 0; o < FOV_KINDS && !found; o++)
    for (int i = 0; i < nc && !found && tested < 300; i++) {
        if (ck[i] != order[o]) continue;
        float old; if (!rd_float((float *)cand[i], &old) || fabsf(old - tgt[ck[i]]) > 1e-4f * tgt[ck[i]]) continue;
        tested++;
        float before = g_orig_P11;
        float test = fov_value(ck[i], vdeg + 20.0f);
        if (!wr_float((float *)cand[i], test)) continue;
        fov_wait_frames(3, 300);
        float after = g_orig_P11;
        float nowv = old; rd_float((float *)cand[i], &nowv);
        wr_float((float *)cand[i], old);           /* deshacer */
        if (fabsf(after - before) > 0.05f && fabsf(after - expect) < 0.03f * expect &&
            fabsf(nowv - test) < 1e-3f * fabsf(test) + 1e-6f) {
            vr_log("FOV: CONFIRMADO %p (%s): al subirlo 20 grados, la lente paso de P11=%.4f a %.4f (esperado %.4f)",
                   (void *)cand[i], g_fov_kname[ck[i]], (double)before, (double)after, (double)expect);
            g_fov_kind = ck[i]; g_fov_addr = (float *)cand[i]; found = 1;
        }
        fov_wait_frames(2, 200);                   /* que la lente vuelva a la normal antes del siguiente */
    }
    if (found) {
        g_fov_tries = 0;
        vr_log("FOV: ENCONTRADO tras probar %d. Con F9 (y F8 linterna encendida) sube a %.0f grados en vertical", tested, (double)cfg_linterna_deg);
        Beep(800, 90); Beep(1100, 90); Beep(1500, 90);    /* tres pitidos subiendo: FOV encontrado */
    } else {
        LONG n = manual ? g_fov_tries : InterlockedIncrement(&g_fov_tries);
        g_fov_next_auto = GetTickCount64() + 20000ULL * (ULONGLONG)(n > 0 ? n : 1);
        vr_log("FOV: ninguno de los %d probados cambia la lente del juego :(%s", tested,
               n >= 4 ? " Dejo de buscar solo; pulsa Shift+F7 para probar otra vez" : " Lo vuelvo a intentar en un rato");
        Beep(400, 300);
    }
    g_fov_running = 0;
    return 0;
}

static void fov_search_start(int manual)
{
    if (InterlockedCompareExchange(&g_fov_running, 1, 0) != 0) return;
    if (manual) Beep(600, 150);
    HANDLE th = CreateThread(NULL, 0, fovscan_thread, (LPVOID)(INT_PTR)manual, 0, NULL);
    if (th) CloseHandle(th); else g_fov_running = 0;
}

/* buscarlo solo: cuando lleva unos segundos viendo la camara del juego (en partida) */
static void fov_auto(ULONGLONG now)
{
    static LONG lastseq = 0; static ULONGLONG seen_since = 0, last_seen = 0;
    LONG seq = g_cam_seq;
    if (seq != lastseq) { lastseq = seq; if (!seen_since || now - last_seen > 1000) seen_since = now; last_seen = now; }
    if (!cfg_linterna || g_fov_addr || g_fov_running) return;
    if (!seen_since || now - last_seen > 300 || now - seen_since < 5000) return;
    if (now < g_fov_next_auto || g_fov_tries >= 4) return;
    fov_search_start(0);
}

/* cada momento: con F9 (y linterna y lente del visor), el FOV del juego grande; si no, el que habia */
static void fov_keep(ULONGLONG now)
{
    static LONG lastseq = 0; static ULONGLONG badsince = 0;
    float *addr = g_fov_addr;
    if (!addr) { badsince = 0; return; }
    float cur;
    if (!rd_float(addr, &cur) || !isfinite(cur)) {
        vr_log("FOV: ya no puedo leerlo (cambio de nivel?); lo busco otra vez");
        g_fov_addr = NULL; g_fov_applied = 0; badsince = 0; g_fov_tries = 0; g_fov_next_auto = now + 5000;
        return;
    }
    /* seguridad: ese numero tiene que seguir cuadrando con la lente que usa el juego. Si deja de cuadrar
     * durante 8 s (cambio de nivel, memoria reutilizada...), se deja de tocar y se busca otra vez */
    LONG seq = g_cam_seq;
    if (seq != lastseq) {
        lastseq = seq;
        float should = fov_value(g_fov_kind, fov_game_vdeg());
        if (fabsf(cur - should) > 0.01f * fabsf(should) + 1e-4f) { if (!badsince) badsince = now; }
        else badsince = 0;
    }
    if (badsince && now - badsince > 8000) {
        vr_log("FOV: el numero (%.4f) ya no cuadra con la lente del juego (FOV %.1f)", (double)cur, (double)fov_game_vdeg());
        fov_release("no cuadra");
        badsince = 0; g_fov_tries = 0; g_fov_next_auto = now + 10000;
        return;
    }
    int want_on = g_mode == 1 && cfg_linterna && cfg_fov && g_fov_ok;
    if (want_on) {
        float want = fov_value(g_fov_kind, cfg_linterna_deg);
        if (!g_fov_applied) {
            g_fov_saved = cur; g_fov_applied = 1;
            vr_log("FOV: linterna ENCENDIDA: %.0f grados en vertical (antes %.3f)", (double)cfg_linterna_deg, (double)cur);
            fov_log_margin();
        }
        if (fabsf(cur - want) > 1e-4f * fabsf(want) + 1e-7f) wr_float(addr, want);
    } else if (g_fov_applied) {
        wr_float(addr, g_fov_saved); g_fov_applied = 0;
        vr_log("FOV: linterna apagada: devuelto a %.3f", (double)g_fov_saved);
    }
}

/* ------------------------------------------------ hilo de teclas (F9 / Insert) */
static const char *mode_text(LONG m)
{
    switch (m) {
    case 0: return "normal: no se toca nada";
    case 1: return "vista con teclado activa";
    }
    return "?";
}


static DWORD WINAPI keys_thread(LPVOID unused)
{
    (void)unused;
    int prev = 0;
    unsigned seen = 0;
    ULONGLONG last = GetTickCount64(), lastlog = last;
    float lyaw = 0, lpitch = 0, lroll = 0, lsx = 0;
    publish_H();
    for (;;) {
        Sleep(12);
        ULONGLONG now = GetTickCount64();
        float dt = (float)(now - last) / 1000.0f;
        last = now;

        /* F9 / Insert: activar o desactivar */
        /* "modificador": cualquier Shift (izquierdo o derecho) o Bloq Mayus activado */
        int rsh = ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_RSHIFT) | GetAsyncKeyState(VK_LSHIFT)) & 0x8000) != 0
                  || (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (g_out[O_SPRINT].on) rsh = 0;   /* (E13b) el Shift lo pone el mando (correr): no son atajos del mod */
        int down = ((GetAsyncKeyState(VK_F9) | GetAsyncKeyState(VK_INSERT)) & 0x8000) != 0 ||
                   (rsh && (GetAsyncKeyState('P') & 0x8000) != 0);
        if (down && !prev) {
            LONG m = (g_mode + 1) % NMODES;
            g_mode = m;
            if (cfg_auto && !g_auto_manual) { g_auto_manual = 1; vr_log(">>> F9: modo manual (el automatico se aparta hasta reiniciar el juego)"); }
            vr_log(">>> MODO %ld: %s", (long)m, mode_text(m));
            if (m == 0) Beep(300, 400); else Beep(900, 120);
        }
        prev = down;

        /* F5..F8: encender / apagar piezas en directo, para ver cual causa cada fallo */
        {
            static int prevf[4] = { 0, 0, 0, 0 };
            static int saved_stereo = 2;
            const int keys[4] = { VK_F5, VK_F6, VK_F7, VK_F8 };
            for (int k = 0; k < 4; k++) {
                int d = (GetAsyncKeyState(keys[k]) & 0x8000) != 0;
                if (d && !prevf[k] && k == 1) {                 /* (E17) F6 = piezas del arma (antes: luces) */
                    pieces_key(rsh, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
                } else if (d && !prevf[k] && rsh && k == 2) {          /* Shift+F7: buscar otra vez el FOV del juego */
                    fov_release("Shift+F7");
                    g_fov_tries = 0;
                    fov_search_start(1);
                } else if (d && !prevf[k]) {
                    int on = 0; const char *name = "";
                    switch (k) {
                    case 0: if (cfg_stereo) { saved_stereo = cfg_stereo; cfg_stereo = 0; } else cfg_stereo = saved_stereo ? saved_stereo : 2;
                            on = cfg_stereo != 0; name = "F5 ESTEREO"; break;
                    case 1: cfg_lights = !cfg_lights; on = cfg_lights; name = "F6 LUCES_VISOR"; break;
                    case 2: cfg_fov = !cfg_fov; on = cfg_fov; name = "F7 FOV_VISOR (lente del visor)"; break;
                    case 3: cfg_nativa = !cfg_nativa; on = cfg_nativa; name = "F8 CAMARA NATIVA (la cabeza dentro de la camara del juego; apagada = mover el raton)";
                            break;
                    }
                    vr_log(">>> %s: %s", name, on ? "ENCENDIDO" : "APAGADO");
                    if (on) { Beep(700, 70); Beep(1100, 70); } else { Beep(1100, 70); Beep(700, 70); }
                }
                prevf[k] = d;
            }
        }
        /* (E28) Inicio: ensenar una pieza guardada como brazo (para buscar la mano) */
        {
            static int prevhome = 0;
            int dh = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
            if (dh && !prevhome) pieces_show_key();
            prevhome = dh;
            static int prevend = 0;   /* (E29) Fin: islas de la pieza grande */
            int de = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
            if (de && !prevend) isl_key();
            prevend = de;
            static int prevpg = 0;    /* (E39) Av Pag: marcar el cargador de cada arma */
            int dp = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
            if (dp && !prevpg) mag_key(rsh, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
            prevpg = dp;
            static int prevpu = 0;    /* (E40) Re Pag: exportar el arma a Blender */
            int du = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
            if (du && !prevpu) {
                if (rsh) {   /* (E43) Shift+Re Pag: encoger el siguiente hueso del arma (para ver que parte mueve cada uno) */
                    int b = g_bone_try < 0 ? 0 : g_bone_try + 3;
                    if (b > 60) { g_bone_try = -1; vr_log(">>> Shift+RePag: ningun hueso encogido (vuelta completa)"); Beep(600, 100); }
                    else { g_bone_try = b; g_bdiag_req = 1; vr_log(">>> Shift+RePag: encojo el hueso %d del arma %u/%u (lo que desaparece es esa parte)", b, g_wpn_ic, g_wpn_si); Beep(1000, 60); }
                } else exp_key();
            }
            prevpu = du;
        }
        /* Shift + H: buscar la camara del juego en memoria */
        {
            static int prevh = 0;
            int hh = rsh && (GetAsyncKeyState('H') & 0x8000) != 0;
            if (hh && !prevh && InterlockedCompareExchange(&g_scan_running, 1, 0) == 0) {
                Beep(600, 150);
                HANDLE th = CreateThread(NULL, 0, scan_thread, NULL, 0, NULL);
                if (th) CloseHandle(th); else g_scan_running = 0;
            }
            prevh = hh;
        }
        /* F11: (E8f) por_objeto en directo */
        {
            static int prev11 = 0;
            int k11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            if (k11 && !prev11 && rsh) {
                cfg_previo = !cfg_previo;
                vr_log(">>> Shift+F11: previo=%d (reflejos y huecos 3/7 con el H de antes)", cfg_previo);
                if (cfg_previo) { Beep(700, 90); Beep(1000, 120); } else { Beep(1000, 90); Beep(700, 120); }
            } else if (k11 && !prev11) {
                cfg_porobjeto = !cfg_porobjeto;
                vr_log(">>> F11: por_objeto=%d", cfg_porobjeto);
                if (cfg_porobjeto) { Beep(700, 90); Beep(1000, 120); } else { Beep(1000, 90); Beep(700, 120); }
            }
            prev11 = k11;
        }
        /* F10: (E7e) corregir los objetos de los huecos 8 y 13 */
        {
            static int prevf8 = 0;
            int k8 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (k8 && !prevf8 && rsh) {   /* (E13) Shift+F10 = oclusion del juego si/no */
                cfg_oclusion = !cfg_oclusion;
                vr_log(">>> Shift+F10: %s", cfg_oclusion ? "oclusion del juego QUITADA (se dibuja todo, mas lento)" : "oclusion del juego PUESTA (mas rapido)");
                if (cfg_oclusion) { Beep(700, 90); Beep(1000, 120); } else { Beep(1000, 90); Beep(700, 120); }
                cfg_store("sin_oclusion", cfg_oclusion);   /* (E35) se recuerda para la proxima vez */
            } else if (k8 && !prevf8) {   /* (E10d) F10 = FOTO de los dos ojos */
                g_photo = 3;
                vr_log(">>> F10: FOTO de lo que ve cada ojo (en la carpeta TEMP)");
                Beep(1200, 60);
            }
            prevf8 = k8;
        }
        /* Shift + D: (E7c) 10 s buscando matrices del fotograma anterior (mueve el raton mientras) */
        {
            static int prevd = 0;
            int dd = rsh && (GetAsyncKeyState('D') & 0x8000) != 0;
            if (dd && !prevd) { g_pv_until = GetTickCount64() + 10000; vr_log(">>> Shift+D: busco 10 s la camara del fotograma ANTERIOR (mueve el raton)"); Beep(800, 100); Beep(800, 100); }
            prevd = dd;
        }
        /* Shift + R: calibrar otra vez el raton de la cabeza */
        {
            static int prevr = 0;
            int r = rsh && (GetAsyncKeyState('R') & 0x8000) != 0;
            if (r && !prevr) { vr_log(">>> Shift+R: calibrar el raton otra vez"); g_rt_cal = 0; Beep(600, 150); }
            prevr = r;
        }
        /* Shift + Y: arma pegada a la pantalla <-> arma girando con el mundo */
        {
            static int prevg = 0;
            int g = rsh && (GetAsyncKeyState('Y') & 0x8000) != 0;
            if (g && !prevg) {
                g_arma = !g_arma;
                vr_log(">>> ARMA: %s", g_arma ? "gira con el mundo" : "pegada a la pantalla");
                if (g_arma) { Beep(700, 80); Beep(1000, 80); } else { Beep(1000, 80); Beep(700, 80); }
            }
            prevg = g;
        }

        /* teclado numerico (con Bloq Num activado) */
        #define KEYDOWN(vk) ((GetAsyncKeyState(vk) & 0x8000) != 0)
        #define SEEN(id, name, cond) do { if ((cond) && !(seen & (1u << (id)))) { seen |= (1u << (id)); \
            vr_log("tecla detectada: %s", name); } } while (0)
        SEEN(0, "Shift (cualquiera)", (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        SEEN(1, "Shift derecho", KEYDOWN(VK_RSHIFT));
        SEEN(2, "Flecha izquierda", KEYDOWN(VK_LEFT));
        SEEN(3, "Flecha derecha", KEYDOWN(VK_RIGHT));
        SEEN(4, "Flecha arriba", KEYDOWN(VK_UP));
        SEEN(5, "Flecha abajo", KEYDOWN(VK_DOWN));
        SEEN(6, "J", KEYDOWN('J'));  SEEN(7, "L", KEYDOWN('L'));  SEEN(8, "I", KEYDOWN('I'));  SEEN(9, "K", KEYDOWN('K'));
        SEEN(10, "Bloq Mayus activado", (GetKeyState(VK_CAPITAL) & 1) != 0);
        if (KEYDOWN(VK_NUMPAD5) || (rsh && KEYDOWN('B'))) { g_yaw = g_pitch = g_roll = g_sx = g_sy = g_sz = 0; g_head_recenter = 1; publish_H(); }
        else if (g_mode == 1) {
            const float RATE = 45.0f, SHIFT = 1.0f;
            float dy = 0, dp = 0, dr = 0, dx = 0;
            if (KEYDOWN(VK_NUMPAD6) || KEYDOWN(VK_RIGHT) || (rsh && KEYDOWN('L'))) dy += RATE * dt;
            if (KEYDOWN(VK_NUMPAD4) || KEYDOWN(VK_LEFT)  || (rsh && KEYDOWN('J'))) dy -= RATE * dt;
            if (KEYDOWN(VK_NUMPAD8) || KEYDOWN(VK_UP)    || (rsh && KEYDOWN('I'))) dp += RATE * dt;
            if (KEYDOWN(VK_NUMPAD2) || KEYDOWN(VK_DOWN)  || (rsh && KEYDOWN('K'))) dp -= RATE * dt;
            if (KEYDOWN(VK_NUMPAD9) || (rsh && KEYDOWN('O'))) dr += RATE * dt;
            if (KEYDOWN(VK_NUMPAD7) || (rsh && KEYDOWN('U'))) dr -= RATE * dt;
            if (KEYDOWN(VK_NUMPAD3) || (rsh && KEYDOWN('M'))) dx += SHIFT * dt;
            if (KEYDOWN(VK_NUMPAD1) || (rsh && KEYDOWN('N'))) dx -= SHIFT * dt;
            if (dy != 0 || dp != 0 || dr != 0 || dx != 0) {
                g_yaw   = clampf(g_yaw + dy, -150.0f, 150.0f);
                g_pitch = clampf(g_pitch + dp, -80.0f, 80.0f);
                g_roll  = clampf(g_roll + dr, -90.0f, 90.0f);
                g_sx    = clampf(g_sx + dx, -5.0f, 5.0f);
                publish_H();
            }
        }
        #undef KEYDOWN
        #undef SEEN
        if (head_update()) publish_H();
        fov_keep(now);
        fov_auto(now);
        /* leer los avisos de SteamVR (si nadie los lee, puede creer que el juego no responde) */
        if (g_head_state == 1 && p_PollEvent) {
            struct { uint32_t type; uint32_t dev; float age; uint32_t pad; unsigned char data[48]; } ev;
            int n = 0;
            while (n < 64 && p_PollEvent(&ev, sizeof(ev))) {
                n++;
                static uint32_t seen_ev[32]; static int nseen = 0;
                int known = 0;
                for (int i = 0; i < nseen; i++) if (seen_ev[i] == ev.type) known = 1;
                if (!known && nseen < 32) { seen_ev[nseen++] = ev.type; vr_log("STEAMVR aviso tipo %u (dispositivo %u)", ev.type, ev.dev); }
            }
        }
        {
            static ULONGLONG lasthead = 0;
            if (g_head_ok && now - lasthead > 1000) {
                lasthead = now;
                vr_log("CABEZA: yaw=%.1f pitch=%.1f roll=%.1f pos=(%.2f, %.2f, %.2f)",
                       (double)g_head_yaw, (double)g_head_pitch, (double)g_head_roll,
                       (double)g_head_dx, (double)g_head_dy, (double)g_head_dz);
            }
        }
        if (now - lastlog > 1000 && (g_yaw != lyaw || g_pitch != lpitch || g_roll != lroll || g_sx != lsx)) {
            lyaw = g_yaw; lpitch = g_pitch; lroll = g_roll; lsx = g_sx; lastlog = now;
            vr_log("VISTA: yaw=%.1f pitch=%.1f roll=%.1f dx=%.2f", (double)lyaw, (double)lpitch, (double)lroll, (double)lsx);
        }
    }
    return 0;
}

/* ---------------------------------------------------- hilo de resumenes */
/* =========================================================================== (E27) DOS OJOS EN EL MISMO FOTOGRAMA
 * Como un PC con dos monitores: cada "pizarra" del juego que depende del ojo (texturas donde pinta la escena, la
 * profundidad, el brillo, la imagen final...) tiene una COPIA para el ojo derecho. Cada dibujo que va a una pizarra
 * con copia se repite: primero en la del juego (ojo izquierdo) y luego en la copia (ojo derecho), con los cajones de
 * camara del ojo derecho. Las sombras (pizarras cuadradas) se pintan una sola vez y valen para los dos ojos.
 * Asi el juego "piensa" (fisica, enemigos, camara...) UNA vez por pareja y solo se repite el pintado.
 * Idea y datos medidos: D-Rey86 (Metro 2033 Redux VR, docs/history/17-21). Codigo propio, no copiado.
 * Las copias y sus vistas se crean cuando hacen falta (la primera vez que se usan) y se sueltan solas cuando el
 * juego suelta la suya: a cada objeto del juego se le pega una "etiqueta" (SetPrivateDataInterface) que, al morir el
 * objeto, borra su apunte de la tabla y suelta la copia. */

#define TW_N 65536
#define TW_NONE ((void *)1)
typedef struct { void *k; void *v; void *tag; volatile LONG stamp; } TwEnt;
enum { TWT_RES = 0, TWT_VIEW = 1, TWT_CB = 2 };
static TwEnt g_tw[3][TW_N];
static SRWLOCK g_tw_lock = SRWLOCK_INIT;
static LONG g_tw_used[3];
static volatile LONG g_tw_gen = 1, g_tw_epoch = 1, g_tw_full = 0;
static volatile LONG g_tw_ntex = 0, g_tw_kb = 0, g_tw_nview = 0, g_tw_ncb = 0, g_tw_said = 0;
static volatile LONG g_tw_dbl = 0, g_tw_one = 0, g_tw_cbsw = 0, g_tw_clr = 0, g_tw_cpy = 0, g_tw_cs = 0;
static volatile LONG g_tw_cbup = 0, g_tw_us = 0, g_tw_fail = 0, g_tw_defer = 0, g_tw_srvsw = 0, g_tw_nobb = 0;
static __thread int t_tw_raw = 0;      /* 1 = llamadas nuestras: los ganchos de borrar/copiar las dejan pasar tal cual */
static const GUID GUID_tw_tag = { 0x6b1f3a52, 0x8d27, 0x4c1e, { 0x9a, 0x3e, 0x5b, 0x71, 0x2c, 0x0d, 0x4e, 0x93 } };

static inline UINT tw_hash(void *k) { ULONGLONG x = (ULONGLONG)(ULONG_PTR)k; x ^= x >> 21; x *= 0x9E3779B97F4A7C15ull; return (UINT)(x >> 40) & (TW_N - 1); }
static TwEnt *tw_find(int t, void *k)          /* con el cerrojo cogido (el que sea) */
{
    UINT h = tw_hash(k);
    for (int n = 0; n < 128; n++, h = (h + 1) & (TW_N - 1)) {
        TwEnt *e = &g_tw[t][h];
        if (e->k == k) return e;
        if (!e->k) return NULL;
    }
    return NULL;
}
static TwEnt *tw_slot(int t, void *k)          /* con el cerrojo EXCLUSIVO: busca o hace sitio */
{
    UINT h = tw_hash(k);
    for (int n = 0; n < 128; n++, h = (h + 1) & (TW_N - 1)) {
        TwEnt *e = &g_tw[t][h];
        if (e->k == k) return e;
        if (!e->k) {
            if (g_tw_used[t] > TW_N * 3 / 4) break;
            e->k = k; e->v = NULL; e->tag = NULL; e->stamp = 0; g_tw_used[t]++;
            return e;
        }
    }
    if (InterlockedIncrement(&g_tw_full) <= 3) vr_log("DOS OJOS: la tabla %d esta llena (no se haran mas copias)", t);
    return NULL;
}

/* --- la etiqueta: un objeto COM minimo (QueryInterface, AddRef, Release) --- */
typedef struct { IUnknownVtbl *vt; volatile LONG ref; int t; void *k; IUnknown *twin; } TwTag;
static HRESULT STDMETHODCALLTYPE tag_QI(IUnknown *u, REFIID r, void **o)
{
    if (o && IsEqualIID(r, &IID_IUnknown)) { *o = u; InterlockedIncrement(&((TwTag *)u)->ref); return S_OK; }
    if (o) *o = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE tag_AddRef(IUnknown *u) { return (ULONG)InterlockedIncrement(&((TwTag *)u)->ref); }
static ULONG STDMETHODCALLTYPE tag_Release(IUnknown *u)
{
    TwTag *g = (TwTag *)u;
    LONG r = InterlockedDecrement(&g->ref);
    if (r) return (ULONG)r;
    AcquireSRWLockExclusive(&g_tw_lock);
    TwEnt *e = tw_find(g->t, g->k);
    if (e && e->tag == (void *)g) { e->v = NULL; e->tag = NULL; e->stamp = 0; }   /* el objeto del juego ha muerto */
    ReleaseSRWLockExclusive(&g_tw_lock);
    if (g->twin) IUnknown_Release(g->twin);
    free(g);
    return 0;
}
static IUnknownVtbl g_tag_vt = { tag_QI, tag_AddRef, tag_Release };

/* apunta k -> twin (o "sin copia") en la tabla t y le pega la etiqueta al objeto del juego.
 * La etiqueta se queda con nuestra referencia de twin. Devuelve lo que queda apuntado (por si otro hilo gano). */
static void *tw_put(int t, void *k, IUnknown *twin, LONG stamp)
{
    TwTag *g = (TwTag *)calloc(1, sizeof(TwTag));
    if (!g) { if (twin) IUnknown_Release(twin); return NULL; }
    g->vt = &g_tag_vt; g->ref = 1; g->t = t; g->k = k; g->twin = twin;
    void *res = twin ? (void *)twin : TW_NONE;
    AcquireSRWLockExclusive(&g_tw_lock);
    TwEnt *e = tw_slot(t, k);
    if (e && e->v && e->v != TW_NONE && twin) {   /* otro hilo ya hizo la copia: me quedo con la suya */
        res = e->v;
        ReleaseSRWLockExclusive(&g_tw_lock);
        IUnknown_Release(twin); free(g);
        return res;
    }
    if (e) { e->v = res; e->tag = g; e->stamp = stamp; }
    ReleaseSRWLockExclusive(&g_tw_lock);
    if (!e) { if (twin) IUnknown_Release(twin); free(g); return NULL; }
    /* pegarla (si ya habia una, la vieja se suelta sola y no borra nada porque la tabla ya apunta a la nueva) */
    ID3D11DeviceChild_SetPrivateDataInterface((ID3D11DeviceChild *)k, &GUID_tw_tag, (IUnknown *)g);
    IUnknown_Release((IUnknown *)g);
    return res;
}

/* recurso con copia? -> la copia; NULL si no. */
static ID3D11Resource *tw_res_look(ID3D11Resource *r, LONG *neg)
{
    void *v = NULL; LONG st = 0;
    AcquireSRWLockShared(&g_tw_lock);
    TwEnt *e = tw_find(TWT_RES, r);
    if (e) { v = e->v; st = e->stamp; }
    ReleaseSRWLockShared(&g_tw_lock);
    if (neg) *neg = (v == TW_NONE) ? st : 0;
    return (v && v != TW_NONE) ? (ID3D11Resource *)v : NULL;
}

/* Lleva una pizarra copia? Texturas 2D normales, no cuadradas (las cuadradas son sombras: valen para los dos ojos),
 * de al menos 16x16, sin cubos ni pilas. need_rt: ademas tiene que ser pizarra (render target o profundidad). */
static int tw_tex_ok(ID3D11Resource *r, D3D11_TEXTURE2D_DESC *d, int need_rt)
{
    D3D11_RESOURCE_DIMENSION dim;
    ID3D11Resource_GetType(r, &dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return 0;
    ID3D11Texture2D_GetDesc((ID3D11Texture2D *)r, d);
    if (need_rt && !(d->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))) return 0;
    if (d->Usage != D3D11_USAGE_DEFAULT) return 0;
    if (d->ArraySize != 1 || (d->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)) return 0;
    if (d->Width == d->Height || d->Width < 16 || d->Height < 16) return 0;
    return 1;
}
static UINT tw_bpp(DXGI_FORMAT f)
{
    if (f >= DXGI_FORMAT_R32G32B32A32_TYPELESS && f <= DXGI_FORMAT_R32G32B32A32_SINT) return 16;
    if (f >= DXGI_FORMAT_R16G16B16A16_TYPELESS && f <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) return 8;
    if (f >= DXGI_FORMAT_R8G8_TYPELESS && f <= DXGI_FORMAT_R16_SINT) return 2;
    if (f >= DXGI_FORMAT_R8_TYPELESS && f <= DXGI_FORMAT_A8_UNORM) return 1;
    return 4;
}

/* La copia de un recurso (si toca, la crea). make: 0 = solo mirar; 1 = crearla si es pizarra; 2 = crearla aunque no
 * sea pizarra (es el destino de una copia de algo que ya tiene copia). La copia nueva sale IGUAL que el original. */
static ID3D11Resource *tw_res(ID3D11DeviceContext *c, ID3D11Resource *r, int make)
{
    if (!r) return NULL;
    LONG neg = 0;
    ID3D11Resource *t = tw_res_look(r, &neg);
    if (t || !make || neg >= make) return t;
    D3D11_TEXTURE2D_DESC d;
    ID3D11Texture2D *tw = NULL;
    if (tw_tex_ok(r, &d, make == 1)) {
        D3D11_TEXTURE2D_DESC td = d;
        td.MiscFlags &= D3D11_RESOURCE_MISC_GENERATE_MIPS;   /* nada de "compartida", "GDI"... (la imagen final) */
        td.CPUAccessFlags = 0;
        ID3D11Device *dev = NULL;
        ID3D11Resource_GetDevice(r, &dev);
        if (dev) {
            HRESULT hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &tw);
            if (FAILED(hr) || !tw) {
                tw = NULL;
                if (InterlockedIncrement(&g_tw_fail) <= 5) vr_log("DOS OJOS: no pude crear la copia de %ux%u formato=%d (hr=0x%08lX)", d.Width, d.Height, (int)d.Format, (unsigned long)hr);
            }
            ID3D11Device_Release(dev);
        }
        if (tw && c) { t_tw_raw++; ID3D11DeviceContext_CopyResource(c, (ID3D11Resource *)tw, r); t_tw_raw--; }
    }
    void *res = tw_put(TWT_RES, r, (IUnknown *)tw, tw ? 0 : make);
    if (res && res != TW_NONE && (void *)tw == res) {
        LONG n = InterlockedIncrement(&g_tw_ntex);
        InterlockedExchangeAdd(&g_tw_kb, (LONG)((ULONGLONG)d.Width * d.Height * tw_bpp(d.Format) * (d.SampleDesc.Count ? d.SampleDesc.Count : 1) / 1024));
        InterlockedIncrement(&g_tw_gen);   /* las vistas que se miraron "sin copia" hay que mirarlas otra vez */
        if (n <= 60) vr_log("DOS OJOS: copia %ld: %ux%u formato=%d muestras=%u pizarra=0x%X %s", (long)n, d.Width, d.Height, (int)d.Format,
                            d.SampleDesc.Count, d.BindFlags, make == 2 ? "(destino de una copia)" : "");
    }
    return (res && res != TW_NONE) ? (ID3D11Resource *)res : NULL;
}
static ID3D11Texture2D *tw_twin_tex(ID3D11Texture2D *t) { return (ID3D11Texture2D *)tw_res_look((ID3D11Resource *)t, NULL); }

/* La vista copia (kind: 0 = pizarra de color, 1 = profundidad, 2 = textura para leer). NULL si no tiene. */
static void *tw_view(ID3D11DeviceContext *c, ID3D11View *v, int kind)
{
    if (!v) return NULL;
    void *tv = NULL; LONG st = 0; int have = 0;
    AcquireSRWLockShared(&g_tw_lock);
    TwEnt *e = tw_find(TWT_VIEW, v);
    if (e && e->v) { have = 1; tv = e->v; st = e->stamp; }
    ReleaseSRWLockShared(&g_tw_lock);
    if (have && tv != TW_NONE) return tv;
    if (have && st == g_tw_gen) return NULL;      /* ya se miro y no tiene (y no ha salido ninguna copia nueva desde entonces) */
    ID3D11Resource *r = NULL;
    ID3D11View_GetResource(v, &r);
    if (!r) return NULL;
    ID3D11Resource *tr = tw_res(c, r, kind == 2 ? 0 : 1);   /* leer no crea copias: si no la tiene, el original vale */
    IUnknown *nv = NULL;
    if (tr) {
        ID3D11Device *dev = NULL;
        ID3D11Resource_GetDevice(tr, &dev);
        if (dev) {
            HRESULT hr = E_FAIL;
            if (kind == 0) { D3D11_RENDER_TARGET_VIEW_DESC dd; ID3D11RenderTargetView_GetDesc((ID3D11RenderTargetView *)v, &dd);
                             hr = ID3D11Device_CreateRenderTargetView(dev, tr, &dd, (ID3D11RenderTargetView **)&nv); }
            else if (kind == 1) { D3D11_DEPTH_STENCIL_VIEW_DESC dd; ID3D11DepthStencilView_GetDesc((ID3D11DepthStencilView *)v, &dd);
                             hr = ID3D11Device_CreateDepthStencilView(dev, tr, &dd, (ID3D11DepthStencilView **)&nv); }
            else { D3D11_SHADER_RESOURCE_VIEW_DESC dd; ID3D11ShaderResourceView_GetDesc((ID3D11ShaderResourceView *)v, &dd);
                             hr = ID3D11Device_CreateShaderResourceView(dev, tr, &dd, (ID3D11ShaderResourceView **)&nv); }
            if (FAILED(hr)) { nv = NULL; if (InterlockedIncrement(&g_tw_fail) <= 5) vr_log("DOS OJOS: no pude crear la vista copia (tipo %d, hr=0x%08lX)", kind, (unsigned long)hr); }
            ID3D11Device_Release(dev);
        }
    }
    ID3D11Resource_Release(r);
    if (!nv && have) {   /* sigue sin copia: solo se apunta que ya se miro (la etiqueta que tiene vale) */
        AcquireSRWLockExclusive(&g_tw_lock);
        TwEnt *e2 = tw_find(TWT_VIEW, v);
        if (e2 && e2->v == TW_NONE) e2->stamp = g_tw_gen;
        ReleaseSRWLockExclusive(&g_tw_lock);
        return NULL;
    }
    void *res = tw_put(TWT_VIEW, v, nv, g_tw_gen);
    if (nv && res == (void *)nv) InterlockedIncrement(&g_tw_nview);
    return (res && res != TW_NONE) ? res : NULL;
}

/* --- cajones del ojo derecho --- */
#define TW_CALL(c, fld, hk, orig) ((((void *)(c)->lpVtbl->fld == (void *)(hk)) && (orig)) ? (orig) : (c)->lpVtbl->fld)
static void tw_cb_upload(ID3D11DeviceContext *c, ID3D11Resource *r, const void *data, UINT bw)
{
    ID3D11Buffer *tb = NULL;
    AcquireSRWLockShared(&g_tw_lock);
    TwEnt *e = tw_find(TWT_CB, r);
    if (e && e->v && e->v != TW_NONE) tb = (ID3D11Buffer *)e->v;
    ReleaseSRWLockShared(&g_tw_lock);
    if (!tb) {
        D3D11_BUFFER_DESC d; memset(&d, 0, sizeof(d));
        d.ByteWidth = bw; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device *dev = NULL; ID3D11Resource_GetDevice(r, &dev);
        if (!dev) return;
        ID3D11Buffer *nb = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(dev, &d, NULL, &nb))) nb = NULL;
        ID3D11Device_Release(dev);
        if (!nb) return;
        void *res = tw_put(TWT_CB, r, (IUnknown *)nb, 0);
        if (!res || res == TW_NONE) return;
        tb = (ID3D11Buffer *)res;
        InterlockedIncrement(&g_tw_ncb);
    }
    PFN_Map mp = TW_CALL(c, Map, hk_Map, g_orig_Map);
    PFN_Unmap up = TW_CALL(c, Unmap, hk_Unmap, g_orig_Unmap);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(mp(c, (ID3D11Resource *)tb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) && m.pData) {
        memcpy(m.pData, data, bw);
        up(c, (ID3D11Resource *)tb, 0);
        AcquireSRWLockShared(&g_tw_lock);
        e = tw_find(TWT_CB, r);
        if (e && e->v == (void *)tb) InterlockedExchange(&e->stamp, g_tw_epoch);   /* al dia en esta "epoca" */
        ReleaseSRWLockShared(&g_tw_lock);
        InterlockedIncrement(&g_tw_cbup);
    }
}
static void tw_cb_invalidate(ID3D11Resource *r)   /* el juego lo ha escrito por otra via: su copia ya no vale */
{
    AcquireSRWLockShared(&g_tw_lock);
    TwEnt *e = tw_find(TWT_CB, r);
    if (e) InterlockedExchange(&e->stamp, 0);
    ReleaseSRWLockShared(&g_tw_lock);
}

/* --- un dibujo doble: guardar lo que hay puesto, poner lo del ojo derecho, y devolverlo todo --- */
#define TW_PS 16
#define TW_VS 8
#define TW_CBN 14
typedef struct {
    UINT nrt; ID3D11RenderTargetView *rt[8], *trt[8]; ID3D11DepthStencilView *ds, *tds; int rt_sw;
    ID3D11ShaderResourceView *ps[TW_PS], *vs[TW_VS]; int ps_sw, vs_sw;
    ID3D11Buffer *cb[5][TW_CBN]; int cb_have, cb_sw[5];
    LONG mode;
    LARGE_INTEGER q0;
} TwDraw;
static __thread TwDraw t_td;
typedef void (STDMETHODCALLTYPE *PFN_SetSRV)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
typedef void (STDMETHODCALLTYPE *PFN_SetCBs)(ID3D11DeviceContext *, UINT, UINT, ID3D11Buffer *const *);

static void tw_set_rt(ID3D11DeviceContext *c, ID3D11RenderTargetView **rt, ID3D11DepthStencilView *ds)
{
    TwDraw *s = &t_td;
    ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(c, s->nrt, s->nrt ? rt : NULL, ds,
        s->nrt, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, NULL, NULL);
}
static void tw_drop_rt(void)
{
    TwDraw *s = &t_td;
    for (int i = 0; i < 8; i++) if (s->rt[i]) { ID3D11RenderTargetView_Release(s->rt[i]); s->rt[i] = NULL; }
    if (s->ds) { ID3D11DepthStencilView_Release(s->ds); s->ds = NULL; }
}
/* 0 = dibujo normal; 1 = SOLO en las copias (modo prueba); 2 = normal y luego otra vez en las copias */
static int tw_begin(ID3D11DeviceContext *c)
{
    LONG mode = g_twin_on;
    if (!mode || g_mode != 1 || t_tw_raw) return 0;
    TwDraw *s = &t_td;
    QueryPerformanceCounter(&s->q0);
    memset(s, 0, offsetof(TwDraw, q0));
    s->mode = mode;
    ID3D11DeviceContext_OMGetRenderTargets(c, 8, s->rt, &s->ds);
    int any = 0;
    for (int i = 0; i < 8; i++) if (s->rt[i]) {
        s->nrt = i + 1;
        s->trt[i] = (ID3D11RenderTargetView *)tw_view(c, (ID3D11View *)s->rt[i], 0);
        if (s->trt[i]) any = 1;
    }
    for (UINT i = 0; i < s->nrt; i++) if (!s->trt[i]) s->trt[i] = s->rt[i];
    if (s->ds) { s->tds = (ID3D11DepthStencilView *)tw_view(c, (ID3D11View *)s->ds, 1); if (s->tds) any = 1; else s->tds = s->ds; }
    s->rt_sw = any;
    if (mode == 2 && !any) { tw_drop_rt(); return 0; }   /* sombras y demas: valen para los dos ojos */
    if (ID3D11DeviceContext_GetType(c) != D3D11_DEVICE_CONTEXT_IMMEDIATE) InterlockedIncrement(&g_tw_defer);
    if (mode == 1) {
        /* prueba: aunque la pizarra no tenga copia, si LEE algo que si la tiene, que lea la copia (el original esta vacio) */
        ID3D11DeviceContext_PSGetShaderResources(c, 0, TW_PS, s->ps);
        ID3D11DeviceContext_VSGetShaderResources(c, 0, TW_VS, s->vs);
        for (int i = 0; i < TW_PS && !s->ps_sw; i++) if (s->ps[i] && tw_view(c, (ID3D11View *)s->ps[i], 2)) s->ps_sw = 1;
        for (int i = 0; i < TW_VS && !s->vs_sw; i++) if (s->vs[i] && tw_view(c, (ID3D11View *)s->vs[i], 2)) s->vs_sw = 1;
        s->cb_have = 0;
        if (!any && !s->ps_sw && !s->vs_sw) {
            for (int i = 0; i < TW_PS; i++) if (s->ps[i]) ID3D11ShaderResourceView_Release(s->ps[i]);
            for (int i = 0; i < TW_VS; i++) if (s->vs[i]) ID3D11ShaderResourceView_Release(s->vs[i]);
            tw_drop_rt(); return 0;
        }
        return 1;
    }
    return 2;
}
static void tw_swap(ID3D11DeviceContext *c)
{
    TwDraw *s = &t_td;
    LONG mode = s->mode;
    if (s->rt_sw) tw_set_rt(c, s->trt, s->tds);
    if (mode == 2) {
        ID3D11DeviceContext_PSGetShaderResources(c, 0, TW_PS, s->ps);
        ID3D11DeviceContext_VSGetShaderResources(c, 0, TW_VS, s->vs);
        s->ps_sw = s->vs_sw = 1;
    }
    ID3D11ShaderResourceView *t[TW_PS]; int ch;
    if (s->ps_sw) {
        ch = 0;
        for (int i = 0; i < TW_PS; i++) { t[i] = s->ps[i]; void *v = s->ps[i] ? tw_view(c, (ID3D11View *)s->ps[i], 2) : NULL; if (v) { t[i] = v; ch = 1; } }
        if (ch) { ID3D11DeviceContext_PSSetShaderResources(c, 0, TW_PS, t); InterlockedIncrement(&g_tw_srvsw); } else s->ps_sw = 2;
    }
    if (s->vs_sw) {
        ch = 0;
        for (int i = 0; i < TW_VS; i++) { t[i] = s->vs[i]; void *v = s->vs[i] ? tw_view(c, (ID3D11View *)s->vs[i], 2) : NULL; if (v) { t[i] = v; ch = 1; } }
        if (ch) ID3D11DeviceContext_VSSetShaderResources(c, 0, TW_VS, t); else s->vs_sw = 2;
    }
    if (mode == 2) {
        /* cajones de camara/objeto del ojo derecho, en todas las etapas (vertices, pixeles, teselado, geometria) */
        ID3D11DeviceContext_VSGetConstantBuffers(c, 0, TW_CBN, s->cb[0]);
        ID3D11DeviceContext_PSGetConstantBuffers(c, 0, TW_CBN, s->cb[1]);
        ID3D11DeviceContext_HSGetConstantBuffers(c, 0, TW_CBN, s->cb[2]);
        ID3D11DeviceContext_DSGetConstantBuffers(c, 0, TW_CBN, s->cb[3]);
        ID3D11DeviceContext_GSGetConstantBuffers(c, 0, TW_CBN, s->cb[4]);
        s->cb_have = 1;
        ID3D11Buffer *nb[5][TW_CBN];
        LONG ep = g_tw_epoch;
        AcquireSRWLockShared(&g_tw_lock);
        for (int st = 0; st < 5; st++) {
            s->cb_sw[st] = 0;
            for (int i = 0; i < TW_CBN; i++) {
                nb[st][i] = s->cb[st][i];
                if (!s->cb[st][i]) continue;
                TwEnt *e = tw_find(TWT_CB, s->cb[st][i]);
                if (e && e->v && e->v != TW_NONE && e->stamp == ep) { nb[st][i] = (ID3D11Buffer *)e->v; s->cb_sw[st] = 1; }
            }
        }
        ReleaseSRWLockShared(&g_tw_lock);
        if (s->cb_sw[0]) ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, 0, TW_CBN, nb[0]);
        if (s->cb_sw[1]) ((PFN_SetCBs)TW_CALL(c, PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB))(c, 0, TW_CBN, nb[1]);
        if (s->cb_sw[2]) ID3D11DeviceContext_HSSetConstantBuffers(c, 0, TW_CBN, nb[2]);
        if (s->cb_sw[3]) ID3D11DeviceContext_DSSetConstantBuffers(c, 0, TW_CBN, nb[3]);
        if (s->cb_sw[4]) ID3D11DeviceContext_GSSetConstantBuffers(c, 0, TW_CBN, nb[4]);
        if (s->cb_sw[0] || s->cb_sw[1] || s->cb_sw[2] || s->cb_sw[3] || s->cb_sw[4]) InterlockedIncrement(&g_tw_cbsw);
    }
}
static void tw_end(ID3D11DeviceContext *c, int act)
{
    TwDraw *s = &t_td;
    if (s->rt_sw) tw_set_rt(c, s->rt, s->ds);
    if (s->ps_sw == 1) ID3D11DeviceContext_PSSetShaderResources(c, 0, TW_PS, s->ps);
    if (s->vs_sw == 1) ID3D11DeviceContext_VSSetShaderResources(c, 0, TW_VS, s->vs);
    if (s->cb_have) {
        if (s->cb_sw[0]) ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, 0, TW_CBN, s->cb[0]);
        if (s->cb_sw[1]) ((PFN_SetCBs)TW_CALL(c, PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB))(c, 0, TW_CBN, s->cb[1]);
        if (s->cb_sw[2]) ID3D11DeviceContext_HSSetConstantBuffers(c, 0, TW_CBN, s->cb[2]);
        if (s->cb_sw[3]) ID3D11DeviceContext_DSSetConstantBuffers(c, 0, TW_CBN, s->cb[3]);
        if (s->cb_sw[4]) ID3D11DeviceContext_GSSetConstantBuffers(c, 0, TW_CBN, s->cb[4]);
        for (int st = 0; st < 5; st++) for (int i = 0; i < TW_CBN; i++) if (s->cb[st][i]) ID3D11Buffer_Release(s->cb[st][i]);
    }
    for (int i = 0; i < TW_PS; i++) if (s->ps[i]) { ID3D11ShaderResourceView_Release(s->ps[i]); s->ps[i] = NULL; }
    for (int i = 0; i < TW_VS; i++) if (s->vs[i]) { ID3D11ShaderResourceView_Release(s->vs[i]); s->vs[i] = NULL; }
    tw_drop_rt();
    InterlockedIncrement(act == 2 ? &g_tw_dbl : &g_tw_one);
    LARGE_INTEGER q1; QueryPerformanceCounter(&q1);
    InterlockedExchangeAdd(&g_tw_us, (LONG)((q1.QuadPart - s->q0.QuadPart) * 1000000 / g_qpf));
}
/* el dibujo "call" una o dos veces segun toque */
#define TW_DRAW(c, call) do { int tw_ = tw_begin(c);                       \
        if (tw_ == 2) { call; tw_swap(c); call; tw_end(c, 2); }          \
        else if (tw_ == 1) { tw_swap(c); call; tw_end(c, 1); }           \
        else { call; } } while (0)

/* Al ENCENDER los dos ojos: las copias se ponen al dia con su original (pudo cambiar mientras estaba apagado) */
static void tw_resync(ID3D11DeviceContext *c)
{
    enum { MAXR = 4096 };
    static ID3D11Resource *o[MAXR], *t[MAXR];
    int n = 0;
    AcquireSRWLockShared(&g_tw_lock);
    for (int i = 0; i < TW_N && n < MAXR; i++) {
        TwEnt *e = &g_tw[TWT_RES][i];
        if (e->k && e->v && e->v != TW_NONE && e->tag) {
            o[n] = (ID3D11Resource *)e->k; t[n] = (ID3D11Resource *)e->v;
            ID3D11Resource_AddRef(o[n]); ID3D11Resource_AddRef(t[n]); n++;
        }
    }
    ReleaseSRWLockShared(&g_tw_lock);
    t_tw_raw++;
    for (int i = 0; i < n; i++) ID3D11DeviceContext_CopyResource(c, t[i], o[i]);
    t_tw_raw--;
    for (int i = 0; i < n; i++) { ID3D11Resource_Release(t[i]); ID3D11Resource_Release(o[i]); }
    if (n) vr_log("DOS OJOS: %d copias puestas al dia con su original", n);
}
static void tw_update_mode(IDXGISwapChain *sc)
{
    LONG want = (cfg_dosojos && g_mode == 1 && vr_active() && cfg_stereo && cfg_fov && g_fov_ok && g_vr_tex2) ? cfg_dosojos : 0;
    if (want == g_twin_on) return;
    if (want) {
        ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL;
        if (SUCCEEDED(IDXGISwapChain_GetDevice(sc, &IID_ID3D11Device, (void **)&dev)) && dev) {
            ID3D11Device_GetImmediateContext(dev, &ctx);
            InterlockedIncrement(&g_tw_epoch);          /* las copias de los cajones de antes ya no valen */
            if (ctx) { tw_resync(ctx); ID3D11DeviceContext_Release(ctx); }
            ID3D11Device_Release(dev);
        }
    }
    vr_log(">>> DOS OJOS: %s", want == 2 ? "LOS DOS OJOS EN CADA FOTOGRAMA" : want == 1 ? "PRUEBA (todo a las copias, un ojo por fotograma)" : "apagado");
    g_twin_on = want;
}

/* --- borrar, copiar, resolver, mipmaps: lo mismo en la copia (MinHook, como los dibujos) --- */
typedef void (STDMETHODCALLTYPE *PFN_ClrRTV)(ID3D11DeviceContext *, ID3D11RenderTargetView *, const FLOAT[4]);
typedef void (STDMETHODCALLTYPE *PFN_ClrDSV)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
typedef void (STDMETHODCALLTYPE *PFN_Resolve)(ID3D11DeviceContext *, ID3D11Resource *, UINT, ID3D11Resource *, UINT, DXGI_FORMAT);
typedef void (STDMETHODCALLTYPE *PFN_GenMips)(ID3D11DeviceContext *, ID3D11ShaderResourceView *);
typedef void (STDMETHODCALLTYPE *PFN_Dispatch)(ID3D11DeviceContext *, UINT, UINT, UINT);
static inline int tw_live(void) { return g_twin_on && g_mode == 1 && !t_tw_raw; }
static void tw_clr_rtv(PFN_ClrRTV tr, ID3D11DeviceContext *c, ID3D11RenderTargetView *v, const FLOAT col[4])
{
    void *t = (tw_live() && v) ? tw_view(c, (ID3D11View *)v, 0) : NULL;
    if (!t) { tr(c, v, col); return; }
    t_tw_raw++;
    if (g_twin_on == 2) tr(c, v, col);
    tr(c, (ID3D11RenderTargetView *)t, col);
    t_tw_raw--;
    InterlockedIncrement(&g_tw_clr);
}
static void tw_clr_dsv(PFN_ClrDSV tr, ID3D11DeviceContext *c, ID3D11DepthStencilView *v, UINT fl, FLOAT d, UINT8 st)
{
    void *t = (tw_live() && v) ? tw_view(c, (ID3D11View *)v, 1) : NULL;
    if (!t) { tr(c, v, fl, d, st); return; }
    t_tw_raw++;
    if (g_twin_on == 2) tr(c, v, fl, d, st);
    tr(c, (ID3D11DepthStencilView *)t, fl, d, st);
    t_tw_raw--;
    InterlockedIncrement(&g_tw_clr);
}
/* copias: si el origen tiene copia, el destino tambien la tendra (p. ej. una foto de la escena para el agua o el cristal) */
static void tw_copy(PFN_CopyRes tr, ID3D11DeviceContext *c, ID3D11Resource *dst, ID3D11Resource *src)
{
    if (!tw_live()) { tr(c, dst, src); return; }
    ID3D11Resource *ts = tw_res(c, src, 0);
    ID3D11Resource *td = tw_res(c, dst, ts ? 2 : 0);
    t_tw_raw++;
    if (td) { if (g_twin_on == 2) tr(c, dst, src); tr(c, td, ts ? ts : src); InterlockedIncrement(&g_tw_cpy); }
    else tr(c, dst, (g_twin_on == 1 && ts) ? ts : src);
    t_tw_raw--;
}
static void tw_copysub(PFN_CopySub tr, ID3D11DeviceContext *c, ID3D11Resource *dst, UINT ds, UINT x, UINT y, UINT z,
                       ID3D11Resource *src, UINT ss, const D3D11_BOX *box)
{
    if (!tw_live()) { tr(c, dst, ds, x, y, z, src, ss, box); return; }
    ID3D11Resource *ts = tw_res(c, src, 0);
    ID3D11Resource *td = tw_res(c, dst, ts ? 2 : 0);
    t_tw_raw++;
    if (td) { if (g_twin_on == 2) tr(c, dst, ds, x, y, z, src, ss, box); tr(c, td, ds, x, y, z, ts ? ts : src, ss, box); InterlockedIncrement(&g_tw_cpy); }
    else tr(c, dst, ds, x, y, z, (g_twin_on == 1 && ts) ? ts : src, ss, box);
    t_tw_raw--;
}
static void tw_resolve(PFN_Resolve tr, ID3D11DeviceContext *c, ID3D11Resource *dst, UINT ds, ID3D11Resource *src, UINT ss, DXGI_FORMAT f)
{
    if (!tw_live()) { tr(c, dst, ds, src, ss, f); return; }
    ID3D11Resource *ts = tw_res(c, src, 0);
    ID3D11Resource *td = tw_res(c, dst, ts ? 2 : 0);
    t_tw_raw++;
    if (td) { if (g_twin_on == 2) tr(c, dst, ds, src, ss, f); tr(c, td, ds, ts ? ts : src, ss, f); InterlockedIncrement(&g_tw_cpy); }
    else tr(c, dst, ds, (g_twin_on == 1 && ts) ? ts : src, ss, f);
    t_tw_raw--;
}
static void tw_mips(PFN_GenMips tr, ID3D11DeviceContext *c, ID3D11ShaderResourceView *v)
{
    void *t = (tw_live() && v) ? tw_view(c, (ID3D11View *)v, 2) : NULL;
    if (!t) { tr(c, v); return; }
    t_tw_raw++;
    if (g_twin_on == 2) tr(c, v);
    tr(c, (ID3D11ShaderResourceView *)t);
    t_tw_raw--;
}
/* calculo (compute): de momento NO se repite; solo se cuenta si escribe en algo con copia (si pasa, habra que hacerlo) */
static void tw_dispatch(PFN_Dispatch tr, ID3D11DeviceContext *c, UINT x, UINT y, UINT z)
{
    tr(c, x, y, z);
    if (!tw_live()) return;
    ID3D11UnorderedAccessView *u[8] = { 0 };
    ID3D11DeviceContext_CSGetUnorderedAccessViews(c, 0, 8, u);
    int hit = 0;
    for (int i = 0; i < 8; i++) if (u[i]) {
        ID3D11Resource *r = NULL; ID3D11View_GetResource((ID3D11View *)u[i], &r);
        if (r) { if (tw_res_look(r, NULL)) hit = 1; ID3D11Resource_Release(r); }
        ID3D11UnorderedAccessView_Release(u[i]);
    }
    if (hit) InterlockedIncrement(&g_tw_cs);
}

static DWORD WINAPI stats_thread(LPVOID unused)
{
    (void)unused;
    LONG pm = 0, pc = 0, pk = 0, pf = 0, pu = 0;
    int sec = 0, since_cam = -1, bursts = 0;
    for (;;) {
        Sleep(1000); sec++;
        LONG m = g_maps, c = g_cbmaps, k = g_cams, f = g_frames, u = g_updsub;
        if (since_cam < 0 && k > 0) {
            since_cam = 0;
            vr_log("=== primera camara detectada: empieza la medicion (600 s) ===");
        }
        if (since_cam >= 0) {
            since_cam++;
            LONG fps = InterlockedExchange(&g_real_frames, 0), wus = InterlockedExchange(&g_wait_us, 0);
            LONG frz = InterlockedExchange(&g_frozen_reads, 0), vlv = InterlockedExchange(&g_valve, 0);
            {
                LONG cb0n = InterlockedExchange(&g_prof_cb0_n, 0), cb0us = InterlockedExchange(&g_prof_cb0_us, 0);
                vr_log("TIEMPOS: en_grafica(Present)=%ldms/s en_el_mod=%ldms/s (objetos: %ld/s, %ldms/s, %.2fus cada uno) fotograma_mas_lento=%.1fms esperando_steamvr=%ldms/s fotogramas=%ld/s lente_principal=%.3f imagen=%ux%u borradores=%ld/s saltitos=%ld/s",
                   (long)(InterlockedExchange(&g_prof_present_us, 0) / 1000), (long)(InterlockedExchange(&g_prof_mod_us, 0) / 1000),
                   (long)cb0n, (long)(cb0us / 1000), cb0n ? (double)cb0us / cb0n : 0.0,
                   InterlockedExchange(&g_prof_maxframe_us, 0) / 1000.0, (long)(wus / 1000), (long)fps, g_orig_P00, (unsigned)g_vr_w, (unsigned)g_vr_h, (long)InterlockedExchange(&g_shadow_n, 0), (long)InterlockedExchange(&g_saltos, 0));
            }
            if (cfg_dosojos) {   /* (E27) */
                LONG dbl = InterlockedExchange(&g_tw_dbl, 0), one = InterlockedExchange(&g_tw_one, 0), us = InterlockedExchange(&g_tw_us, 0);
                vr_log("DOS OJOS: modo=%ld dibujos_dobles=%ld/s solo_copia=%ld/s (en_diferido=%ld) cajones_ojo_der=%ld/s cambios_de_cajones=%ld/s lecturas_cambiadas=%ld/s "
                       "borrados=%ld/s copias=%ld/s calculo_con_copia=%ld/s | coste_mod=%ldms/s (%.1fus por dibujo) | copias: texturas=%ld (%ld MB) vistas=%ld cajones=%ld fallos=%ld",
                       (long)g_twin_on, (long)dbl, (long)one, (long)InterlockedExchange(&g_tw_defer, 0), (long)InterlockedExchange(&g_tw_cbup, 0),
                       (long)InterlockedExchange(&g_tw_cbsw, 0), (long)InterlockedExchange(&g_tw_srvsw, 0), (long)InterlockedExchange(&g_tw_clr, 0),
                       (long)InterlockedExchange(&g_tw_cpy, 0), (long)InterlockedExchange(&g_tw_cs, 0), (long)(us / 1000), (dbl + one) ? (double)us / (dbl + one) : 0.0,
                       (long)g_tw_ntex, (long)(g_tw_kb / 1024), (long)g_tw_nview, (long)g_tw_ncb, (long)g_tw_fail);
            }
            if (g_ft_n > 0) {   /* (E12) */
                vr_log("STEAMVR: pares=%ld/s grafica_juego=%.1fms grafica_total=%.1fms grafica_steamvr=%.1fms entre_pares=%.1fms "
                       "steamvr_esperando=%.1fms veces_mostrado=%.2f repetidos=%ld fuera_de_hora=%ld con_reproyeccion=%ld",
                       g_ft_n, g_ft_gpu / g_ft_n, g_ft_tot / g_ft_n, g_ft_comp / g_ft_n, g_ft_int / g_ft_n,
                       g_ft_idle / g_ft_n, (double)g_ft_pres / g_ft_n, g_ft_drop, g_ft_mis, g_ft_reproj);
                g_ft_gpu = g_ft_tot = g_ft_int = g_ft_idle = g_ft_comp = 0; g_ft_n = g_ft_drop = g_ft_mis = g_ft_reproj = g_ft_pres = 0;
            }
            {   /* (E10) ojo de la camara nativa */
                LONG ok = InterlockedExchange(&g_nat_eok, 0), bad = InterlockedExchange(&g_nat_ebad, 0), none = InterlockedExchange(&g_nat_enone, 0);
                if (bad > 10 && bad > 2 * ok) { g_nat_flip ^= 1; vr_log("NATIVA: el juego monta la camara en otro momento; cambio de ojo (flip=%ld)", (long)g_nat_flip); }
                if (nat_full()) vr_log("NATIVA ojos: bien=%ld mal=%ld sin_saber=%ld flip=%ld | arma: piezas_corregidas=%ld otras_camaras=%ld | hud: dibujos_recolocados=%ld | andar=%ld | mano: camara_arma=%ld piezas=%ld calibrada=%d ok=%ld porque=%d pos=%.2f/%.2f/%.2f | brazos: ocultas=%ld pintadas=%ld guardadas=%d dibujos=%ld desvios_repuestos=%ld | minhook: versiones=%d/%d/%d/%d llamadas=%ld/%ld/%ld/%ld | balas: jugador=%ld otras=%ld ultima_desviada=%.1fgrados | giro: correcciones=%ld desplazado=%.2fm cabeza=%.2f/%.2fm", (long)ok, (long)bad, (long)none, (long)g_nat_flip,
                                       (long)InterlockedExchange(&g_nat_armaojo, 0), (long)InterlockedExchange(&g_nat_otras, 0), (long)InterlockedExchange(&g_hud_n, 0), (long)InterlockedExchange(&g_mov_hits, 0), (long)InterlockedExchange(&g_hand_cam, 0), (long)InterlockedExchange(&g_hand_n, 0), g_hand_cal, (long)g_hand_ok, g_hand_why,
                                       g_hand_rel[0][3], g_hand_rel[1][3], g_hand_rel[2][3],
                                       (long)InterlockedExchange(&g_pc_hidden, 0), (long)InterlockedExchange(&g_pc_drawn, 0), g_nhide,
                                       (long)InterlockedExchange(&g_draws_seen, 0), (long)InterlockedExchange(&g_vt_fix, 0),
                                       g_mh_n[0], g_mh_n[1], g_mh_n[2], g_mh_n[3], (long)InterlockedExchange(&g_mh_calls[0], 0), (long)InterlockedExchange(&g_mh_calls[1], 0), (long)InterlockedExchange(&g_mh_calls[2], 0), (long)InterlockedExchange(&g_mh_calls[3], 0),
                                       (long)InterlockedExchange(&g_bul_mine, 0), (long)InterlockedExchange(&g_bul_other, 0), g_bul_lastdev,
                                       (long)InterlockedExchange(&g_piv_n, 0), sqrtf(g_piv[0] * g_piv[0] + g_piv[2] * g_piv[2]), g_rt_dg[0], g_rt_dg[2]);
                vr_log("PANTALLA: dibujos_ultimo=%ld max=%ld modo=%ld cine=%d jugador=%ld/s (con tiempo parado %ld) inclinacion=%.0f | mando_xbox: lecturas=%ld | postura: de_su_camara=%ld (distinta de la ultima %ld)",
                       (long)g_bb_last, (long)InterlockedExchange(&g_bb_max, 0), (long)g_mode, g_cine_on,
                       (long)InterlockedExchange(&g_mov_calls, 0), (long)InterlockedExchange(&g_mov_dt0, 0), asinf(clampf(g_rt_Rg[1][2], -1.0f, 1.0f)) * 57.3f,
                       (long)InterlockedExchange(&g_x_reads, 0), (long)(g_pose_same + g_pose_diff), (long)g_pose_diff);
                g_pose_same = g_pose_diff = 0;
            }
            vr_log("seg %d: nativa=%ld(%ld/s) oclusion=%d h7(vp/ant)=%ld/%ld h3(rayos/P/reflejos)=%ld/%ld/%ld fotogramas_H=%ld luces7_anterior=%ld luces7_propias=%ld propios=%ld fallan=%ld iW_raro=%ld rescatados=%ld objetos8_13=%ld/%ld orden(hueco0 antes/despues de la camara, camaras)=%ld/%ld/%ld raton=%ld desajuste=%.1f pasos=%ld fov_juego=%.1f linterna=%d fps=%ld espera_steamvr=%ldms reloj_parado=%ld soltados=%ld luces7=%ld luces3=%ld modo=%ld estereo=%ld camaras=%ld cb1_mod=%ld cb0_mod=%ld cb0_salta=%ld no_cuadra=%ld",
                   since_cam, (long)(nat_active() ? 1 : g_nat_installed == 1 ? 0 : (long)g_nat_installed - 10), (long)InterlockedExchange(&g_nat_hits, 0), g_ocl_state, (long)InterlockedExchange(&g_s7vp, 0), (long)InterlockedExchange(&g_s7T, 0),
                   (long)InterlockedExchange(&g_s3rays, 0), (long)InterlockedExchange(&g_s3P, 0), (long)InterlockedExchange(&g_s3refl, 0),
                   (long)InterlockedExchange(&g_fs_frames, 0), (long)InterlockedExchange(&g_l7prev, 0), (long)InterlockedExchange(&g_l7own, 0), (long)InterlockedExchange(&g_cb0_own, 0), (long)InterlockedExchange(&g_own_fail, 0), (long)InterlockedExchange(&g_iw_bad, 0), (long)InterlockedExchange(&g_cb0_resc, 0), (long)g_inst_fix, (long)InterlockedExchange(&g_inst_mod, 0), (long)InterlockedExchange(&g_ord_before, 0), (long)InterlockedExchange(&g_ord_after, 0), (long)InterlockedExchange(&g_ord_cb1, 0), (long)(raton_active() ? 1 : g_rt_cal == 2 ? 0 : -1), (double)InterlockedExchange(&g_rt_resid, 0) / 10.0, (long)InterlockedExchange(&g_rt_sent, 0), (double)fov_game_vdeg(), (int)g_fov_applied, (long)fps, (long)(wus / 1000), (long)frz, (long)vlv, (long)InterlockedExchange(&g_l7, 0), (long)InterlockedExchange(&g_l3, 0), (long)g_mode, (long)g_stereo_on, (long)(k - pk),
                   (long)g_modded, (long)g_cb0_mod, (long)g_cb0_skip, (long)g_nomatch);
            (void)m; (void)c;
            if (0 && since_cam % 5 == 1 && bursts < 20) {   /* muestras detalladas apagadas (metian mucho texto al log) */
                bursts++;
                InterlockedExchange(&g_budget, 40);
                for (int sl = 0; sl < NSLOT; sl++) { InterlockedExchange(&g_samples_left[sl], 2); InterlockedExchange(&g_uslot_samples[sl], 2); }
                vr_log("--- muestra detallada %d ---", bursts);
                for (int sl = 0; sl < NSLOT; sl++)
                    if (g_slot_binds[sl] || g_slot_writes[sl])
                        vr_log("  hueco %d: binds=%ld map=%ld updsub=%ld copias=%ld ancho_cajon=%ld uso=%ld",
                               sl, (long)g_slot_binds[sl], (long)g_slot_writes[sl], (long)g_slot_upd[sl],
                               (long)g_slot_copy[sl], (long)g_slot_desc_bw[sl], (long)g_slot_desc_usage[sl]);
            }
            if (since_cam >= 600) { vr_log("=== fin de la medicion ==="); break; }
        } else if (sec <= 12 || sec % 5 == 0) {
            vr_log("latido %d s: fotogramas=%ld Map=%ld Map_cb=%ld UpdSub=%ld fallos_lectura=%ld",
                   sec, (long)f, (long)m, (long)c, (long)u, (long)g_readfail);
            if (sec > 900) break;
        }
        pm = m; pc = c; pk = k; pf = f; pu = u;
    }
    return 0;
}


/* ------------------------------------------------ (E11) HUD */
/* El juego pinta primero la escena entera en la imagen final (un triangulo que tapa la pantalla) y DESPUES, encima,
 * el HUD (inventario, avisos de "E", textos) con rectangulos en coordenadas de pantalla. En las gafas cada ojo tiene
 * su lente descentrada, asi que un mismo pixel apunta a sitios distintos en cada ojo: el HUD sale doble.
 * Arreglo: a los dibujos que van a la imagen final DESPUES de la escena se les cambia el "rectangulo de pantalla"
 * (viewport) para que el HUD quede como un cartel de tamano fijo, a cfg_hud_dist metros delante, igual en los dos ojos. */
typedef void (STDMETHODCALLTYPE *PFN_OMSetRT)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);
typedef void (STDMETHODCALLTYPE *PFN_RSSetVP)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
typedef void (STDMETHODCALLTYPE *PFN_Draw)(ID3D11DeviceContext *, UINT, UINT);
typedef void (STDMETHODCALLTYPE *PFN_DrawIdx)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef void (STDMETHODCALLTYPE *PFN_DrawInst)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
typedef void (STDMETHODCALLTYPE *PFN_DrawIdxInst)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
static PFN_OMSetRT g_orig_OMSetRT = NULL; static PFN_RSSetVP g_orig_RSSetVP = NULL;
static ID3D11DeviceContext *g_hud_ctx = NULL;
static void *g_bb_ptr = NULL;                 /* la imagen final (backbuffer) del juego, para reconocerla */

static int g_hud_bound = 0, g_vp_have = 0;
static D3D11_VIEWPORT g_vp_game;
static volatile LONG g_hud_n = 0;
static void STDMETHODCALLTYPE hk_OMSetRT(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *rtv, ID3D11DepthStencilView *dsv)
{
    if (c == g_hud_ctx) {
        int bb = 0;
        if (n >= 1 && rtv && rtv[0] && g_bb_ptr) {
            ID3D11Resource *r = NULL;
            ID3D11View_GetResource((ID3D11View *)rtv[0], &r);
            if (r) { bb = ((void *)r == g_bb_ptr); ID3D11Resource_Release(r); }
        }
        g_hud_bound = bb;
    }
    g_orig_OMSetRT(c, n, rtv, dsv);
}
static void STDMETHODCALLTYPE hk_RSSetVP(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *vp)
{
    if (c == g_hud_ctx && n >= 1 && vp) { g_vp_game = vp[0]; g_vp_have = 1; }
    g_orig_RSSetVP(c, n, vp);
}
/* 1 = se ha cambiado el viewport (hay que devolverlo despues del dibujo) */
static int hud_pre(ID3D11DeviceContext *c, int fulltri)
{
    if (c != g_hud_ctx || !g_hud_bound || !cfg_hud || g_mode != 1 || !g_stereo_on || !g_fov_ok || !g_vp_have) return 0;
    if (g_hud_draws++ == 0 || fulltri) return 0;       /* el primero es la escena; los triangulos de pantalla completa, efectos */
    LONG e = g_eye;
    float ehw = 1.0f / g_eyerow[e][0][0], ecx = -g_eyerow[e][0][2] * ehw;
    float ehh = 1.0f / g_eyerow[e][1][1], ecy = -g_eyerow[e][1][2] * ehh;
    float asp = g_aspect > 0.5f ? g_aspect : 16.0f / 9.0f;
    float T = cfg_hud_size, Ty = cfg_hud_size / asp;
    float cxe = -g_eyepos[e][0] * cfg_ipdscale / cfg_hud_dist;      /* el cartel a esa distancia, visto desde este ojo */
    float a = T / ehw, b = (cxe - ecx) / ehw, ay = Ty / ehh, by = -ecy / ehh;
    float W = (float)g_vr_w, Hh = (float)g_vr_h;
    if (W <= 0 || Hh <= 0) return 0;
    D3D11_VIEWPORT v = g_vp_game;
    v.TopLeftX = W * (b + 1.0f - a) * 0.5f + a * g_vp_game.TopLeftX;
    v.Width    = a * g_vp_game.Width;
    v.TopLeftY = Hh * (1.0f - ay - by) * 0.5f + ay * g_vp_game.TopLeftY;
    v.Height   = ay * g_vp_game.Height;
    g_orig_RSSetVP(c, 1, &v);
    InterlockedIncrement(&g_hud_n);
    return 1;
}
static void hud_post(ID3D11DeviceContext *c) { g_orig_RSSetVP(c, 1, &g_vp_game); }
/* (E29) la pieza guardada mas grande (la de manos + antebrazos) */
static int isl_is_big(UINT ic, UINT si)
{
    UINT bic = 0, bsi = 0;
    for (int i = 0; i < g_nhide; i++) if (g_hide[i].ic > bic) { bic = g_hide[i].ic; bsi = g_hide[i].si; }
    return bic > 1000 && ic == bic && si == bsi;
}
/* (E31) la mano izq va con su propia camara */
static int lh_active(void) { return cfg_mano_izq_mando && cfg_mano_izq && g_lhand_ok && g_isl_state == 2; }
static __thread ID3D11Buffer *t_lh_orig[2][14]; static __thread int t_lh_have = 0;
static ID3D11Buffer *g_lh_buf[2];
static void lh_unbind(ID3D11DeviceContext *c);
/* pone, en vez de la camara del arma, su version de la mano izq (twin = 1: la del ojo derecho). 0 = no hay */
static int lh_bind(ID3D11DeviceContext *c, int twin)
{
    if (!t_lh_have) {
        ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 14, t_lh_orig[0]);
        ID3D11DeviceContext_PSGetConstantBuffers(c, 0, 14, t_lh_orig[1]);
        t_lh_have = 1;
    }
    __attribute__((aligned(16))) float d[CAM_BYTES / 4];
    int found = 0, mark[2][14]; memset(mark, 0, sizeof(mark));
    for (int st = 0; st < 2; st++) for (int i = 0; i < 14; i++)
        if (t_lh_orig[st][i] && lh_get(t_lh_orig[st][i], twin, d)) { mark[st][i] = 1; found = 1; }
    if (!found) { InterlockedIncrement(&g_lh_nocam); return 0; }
    if (!g_lh_buf[twin]) {
        D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = CAM_BYTES; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device *dev = NULL; ID3D11DeviceContext_GetDevice(c, &dev);
        if (!dev) return 0;
        ID3D11Buffer *nb = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &nb))) nb = NULL;
        ID3D11Device_Release(dev);
        if (!nb) return 0;
        if (InterlockedCompareExchangePointer((PVOID *)&g_lh_buf[twin], nb, NULL) != NULL) ID3D11Buffer_Release(nb);
    }
    ID3D11Buffer *b = g_lh_buf[twin];
    PFN_Map mp = TW_CALL(c, Map, hk_Map, g_orig_Map);
    PFN_Unmap up = TW_CALL(c, Unmap, hk_Unmap, g_orig_Unmap);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(mp(c, (ID3D11Resource *)b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) || !m.pData) return 0;
    memcpy(m.pData, d, CAM_BYTES);
    up(c, (ID3D11Resource *)b, 0);
    ID3D11Buffer *nb[2][14];
    for (int st = 0; st < 2; st++) for (int i = 0; i < 14; i++) nb[st][i] = mark[st][i] ? b : t_lh_orig[st][i];
    ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, 0, 14, nb[0]);
    ((PFN_SetCBs)TW_CALL(c, PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB))(c, 0, 14, nb[1]);
    InterlockedIncrement(&g_lh_draw);
    return 1;
}
static void lh_unbind(ID3D11DeviceContext *c)
{
    if (!t_lh_have) return;
    ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, 0, 14, t_lh_orig[0]);
    ((PFN_SetCBs)TW_CALL(c, PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB))(c, 0, 14, t_lh_orig[1]);
    for (int st = 0; st < 2; st++) for (int i = 0; i < 14; i++) if (t_lh_orig[st][i]) { ID3D11Buffer_Release(t_lh_orig[st][i]); t_lh_orig[st][i] = NULL; }
    t_lh_have = 0;
}
/* tramos a pintar: con las islas listas, los suyos (0 = ese tramo no se pinta); si no, la pieza entera */
static int isl_nranges(void) { return g_isl_state == 2 ? g_nisr : 1; }
static int isl_range(int r, UINT ic, UINT si, UINT *s, UINT *n)
{
    if (g_isl_state != 2) { *s = si; *n = ic; return 1; }
    if (!g_isl_show[g_isr[r].isl]) return 0;
    *s = si + g_isr[r].start; *n = g_isr[r].count;
    if (lh_active() && cfg_isla_izq >= 1 && cfg_isla_izq <= g_nisl && g_isr[r].isl == g_isl_order[cfg_isla_izq - 1]) return 2;   /* (E31) */
    return 1;
}
/* (E43) pinta con una copia del cajon de huesos: keep = 0 -> los huesos t_bh encogidos; keep = 1 -> SOLO los huesos t_bk */
static int bone_bind(ID3D11DeviceContext *c, int keep)
{
    int slot = cfg_huesos_hueco; if (slot < 0 || slot > 13) return 0;
    ID3D11Buffer *o = NULL;
    ID3D11DeviceContext_VSGetConstantBuffers(c, slot, 1, &o);
    if (!o) { InterlockedIncrement(&g_bone_fail); return 0; }
    D3D11_BUFFER_DESC d; ID3D11Buffer_GetDesc(o, &d);
    UINT W = d.ByteWidth;
    if (W < 48 || W > 4096) { ID3D11Buffer_Release(o); InterlockedIncrement(&g_bone_fail); return 0; }
    if (g_bdiag_req && !g_bdiag_buf) {
        ID3D11Buffer *all[14]; ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 14, all);
        for (int i = 0; i < 14; i++) { g_bdiag_sz[i] = 0; if (all[i]) { D3D11_BUFFER_DESC dd; ID3D11Buffer_GetDesc(all[i], &dd); g_bdiag_sz[i] = dd.ByteWidth; ID3D11Buffer_Release(all[i]); } }
        ID3D11Buffer_AddRef(o); g_bdiag_req = 0; g_bdiag_buf = o;
    }
    ID3D11Device *dev = NULL; ID3D11DeviceContext_GetDevice(c, &dev);
    if (!dev) { ID3D11Buffer_Release(o); return 0; }
    if (!g_bz) {
        static BYTE zeros[4096];
        D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd)); bd.ByteWidth = 4096; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = { zeros, 0, 0 }; ID3D11Buffer *z = NULL;
        if (SUCCEEDED(ID3D11Device_CreateBuffer(dev, &bd, &sd, &z)) && z) { if (InterlockedCompareExchangePointer((PVOID *)&g_bz, z, NULL) != NULL) ID3D11Buffer_Release(z); }
    }
    if (!g_bcopy[keep] || g_bcopy_w[keep] != W) {
        D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd)); bd.ByteWidth = W; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ID3D11Buffer *nb = NULL;
        if (SUCCEEDED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &nb)) && nb) { g_bcopy[keep] = nb; g_bcopy_w[keep] = W; }   /* la vieja se deja (poca cosa) */
    }
    ID3D11Device_Release(dev);
    ID3D11Buffer *cp = g_bcopy[keep];
    if (!g_bz || !cp) { ID3D11Buffer_Release(o); InterlockedIncrement(&g_bone_fail); return 0; }
    if (!keep) {
        ID3D11DeviceContext_CopyResource(c, (ID3D11Resource *)cp, (ID3D11Resource *)o);
        for (int k = 0; k < t_bhn; k++) {
            UINT r = (UINT)(t_bh[k] + cfg_huesos_desfase) * 16; if (r + 48 > W) continue;
            D3D11_BOX bx = { 0, 0, 0, 48, 1, 1 };
            ID3D11DeviceContext_CopySubresourceRegion(c, (ID3D11Resource *)cp, 0, r, 0, 0, (ID3D11Resource *)g_bz, 0, &bx);
        }
    } else {
        D3D11_BOX bw = { 0, 0, 0, W, 1, 1 };
        ID3D11DeviceContext_CopySubresourceRegion(c, (ID3D11Resource *)cp, 0, 0, 0, 0, (ID3D11Resource *)g_bz, 0, &bw);
        for (int k = 0; k < t_bkn; k++) {
            UINT r = (UINT)(t_bk[k] + cfg_huesos_desfase) * 16; if (r + 48 > W) continue;
            D3D11_BOX bx = { r, 0, 0, r + 48, 1, 1 };
            ID3D11DeviceContext_CopySubresourceRegion(c, (ID3D11Resource *)cp, 0, r, 0, 0, (ID3D11Resource *)o, 0, &bx);
        }
    }
    ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, slot, 1, &cp);
    t_borig = o;
    InterlockedIncrement(&g_bone_draws);
    return 1;
}
static void bone_unbind(ID3D11DeviceContext *c)
{
    if (!t_borig) return;
    ((PFN_SetCBs)TW_CALL(c, VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB))(c, cfg_huesos_hueco, 1, &t_borig);
    ID3D11Buffer_Release(t_borig); t_borig = NULL;
}
/* (E17) 1 = no pintar esta pieza (es de los brazos, o es la que se esta probando con F6); (E29) 2 = pintarla por trozos;
 * (E33) 3 = pintarla entera con la camara de la mano izq (el reloj) */
static int piece_skip(ID3D11DeviceContext *c, UINT ic, UINT si)
{
    (void)c;
    g_draws_seen++;
    /* (E17b) el juego pinta tambien desde otros hilos (listas de dibujo "diferidas"): no se mira el contexto,
     * y lo de "esta pieza es del arma" se guarda por hilo */
    if (!g_cb0_weapon || g_mode != 1) {
        /* (E20f) fuera del VR no se oculta nada, pero se mira si se estan pintando los brazos guardados:
         * asi se sabe que hay partida (en la intro y en los menus no salen) */
        for (int i = 0; i < g_nhide; i++)
            if (g_hide[i].ic == ic && g_hide[i].si == si) { g_arm_seen = GetTickCount64(); break; }
        return 0;
    }
    while (InterlockedCompareExchange(&g_pc_lock, 1, 0) != 0) YieldProcessor();
    int known = 0;
    for (int i = 0; i < g_pc_ncur; i++) if (g_pc_cur[i].ic == ic && g_pc_cur[i].si == si) { known = 1; break; }
    if (!known && g_pc_ncur < MAXP) { g_pc_cur[g_pc_ncur].ic = ic; g_pc_cur[g_pc_ncur].si = si; g_pc_ncur++; }
    InterlockedExchange(&g_pc_lock, 0);
    if (g_try >= 0 && g_try_sig.ic == ic && g_try_sig.si == si) { InterlockedIncrement(&g_pc_hidden); return 1; }
    /* (E39) el cargador: fuera = no se pinta en el arma; en la mano izq (cogido de la cadera) = se pinta con la camara de esa mano
     * (4 = en el arma Y en la mano, si aun no se habia sacado el del arma) */
    if (g_nmag && g_wpn_ic && mag_is(g_wpn_ic, g_wpn_si, ic, si)) {
        int out = cfg_recarga_manual && g_out_ic == g_wpn_ic && g_out_si == g_wpn_si;
        if (cfg_recarga_manual && g_mag_state == 1 && lh_active()) { InterlockedIncrement(&g_mag_hand_n); return out ? 3 : 4; }
        if (out) { InterlockedIncrement(&g_mag_hid_n); InterlockedIncrement(&g_pc_hidden); return 1; }
    }
    /* (E43) la pieza grande del arma: huesos encogidos (cargador fuera, o el que se prueba con Shift+Re Pag) o cargador en la mano */
    if (g_wpn_ic && ic == g_wpn_ic && si == g_wpn_si) {
        t_bhn = 0; t_bkn = 0; t_blh = 0;
        if (g_bone_try >= 0) t_bh[t_bhn++] = g_bone_try;
        int mb[8], nmb = bone_list(ic, si, 1, mb, 8);
        int out = cfg_recarga_manual && g_out_ic == ic && g_out_si == si;
        if (out) for (int k = 0; k < nmb && t_bhn < 8; k++) t_bh[t_bhn++] = mb[k];
        if (nmb && cfg_recarga_manual && g_mag_state == 1 && lh_active()) { for (int k = 0; k < nmb; k++) t_bk[k] = mb[k]; t_bkn = nmb; t_blh = 1; }
        if (t_bhn || t_blh) { InterlockedIncrement(&g_pc_drawn); return 5; }
    }
    if (cfg_brazos) for (int i = 0; i < g_nhide; i++)
        if (g_hide[i].ic == ic && g_hide[i].si == si) {
            g_arm_seen = GetTickCount64();
            if (i == g_show) break;
            if (isl_is_big(ic, si)) {   /* (E29) la pieza grande: se apunta su buffer para buscar islas */
                if (g_isl_state == 0 && InterlockedCompareExchange(&g_isl_state, 1, 0) == 0) {
                    ID3D11Buffer *b = NULL; DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN; UINT o = 0;
                    ID3D11DeviceContext_IAGetIndexBuffer(c, &b, &f, &o);
                    if (b) { g_isl_ib = b; g_isl_fmt = f; g_isl_off = o; g_isl_ic = ic; g_isl_si = si; } else g_isl_state = 3;
                }
                if (g_isl_on) { InterlockedIncrement(&g_pc_drawn); return 2; }
            }
            /* (E33) el RELOJ: con solo las manos, las piezas pequenas guardadas (todas menos la grande) se pintan.
             * Con la mano izq en el mando, con la camara de esa mano (3); si no, con la del arma (0) */
            if (cfg_reloj && cfg_manos && g_isl_on) {
                InterlockedIncrement(&g_pc_drawn);
                if (lh_active()) { InterlockedIncrement(&g_watch_n); return 3; }
                return 0;
            }
            InterlockedIncrement(&g_pc_hidden); return 1;
        }
    InterlockedIncrement(&g_pc_drawn);
    return 0;
}
/* (E18) MinHook: las ordenes de dibujo se enganchan POR DENTRO de las funciones reales de d3d11.dll.
 * Por tabla no se podia (E17c): Windows reescribe las casillas Draw* del contexto en cada fotograma. Ademas el log de
 * la E17d ensena que Windows ALTERNA entre dos versiones de cada funcion (p. ej. Draw ...3540 y ...3610): por eso se
 * engancha cada version distinta que aparezca en la tabla (hasta MHVAR por funcion), cada una con su propio "puente". */
static int g_mh_ok = 0;
static __thread int t_in_draw = 0;                   /* una version puede llamar a otra por dentro: solo cuenta la de fuera */
/* (E29-E31) la pieza de los brazos por trozos: las islas normales con la camara del arma; la mano izq con la suya */
#define LH_RANGES(c, ic, si, CALL) for (int q_ = 0; q_ < nr_; q_++) { UINT s_, n_; if (isl_range(q_, ic, si, &s_, &n_) == 2) { CALL; } }
#define ISL_DRAW(c, ic, si, CALL) do { int nr_ = isl_nranges(), lh_ = 0;                                         \
        for (int r_ = 0; r_ < nr_; r_++) { UINT s_, n_; int k_ = isl_range(r_, ic, si, &s_, &n_);               \
            if (k_ == 1) TW_DRAW(c, CALL); else if (k_ == 2) lh_ = 1; }                                         \
        if (lh_) {                                                                                               \
            if (lh_bind(c, 0)) { int tw2_ = tw_begin(c);                                                         \
                if (tw2_ == 2) { LH_RANGES(c, ic, si, CALL); lh_bind(c, 1); tw_swap(c); LH_RANGES(c, ic, si, CALL); tw_end(c, 2); } \
                else if (tw2_ == 1) { tw_swap(c); LH_RANGES(c, ic, si, CALL); tw_end(c, 1); }                    \
                else { LH_RANGES(c, ic, si, CALL); }                                                             \
                lh_unbind(c); }                                                                                  \
            else { lh_unbind(c); LH_RANGES(c, ic, si, TW_DRAW(c, CALL)); } }                                     \
    } while (0)
/* (E33) una pieza entera (el reloj) con la camara de la mano izq, igual que ISL_DRAW pero sin trozos */
#define LH_WHOLE(c, CALL) do {                                                                                  \
        if (lh_bind(c, 0)) { int tw2_ = tw_begin(c);                                                            \
            if (tw2_ == 2) { CALL; lh_bind(c, 1); tw_swap(c); CALL; tw_end(c, 2); }                             \
            else if (tw2_ == 1) { tw_swap(c); CALL; tw_end(c, 1); }                                             \
            else { CALL; }                                                                                      \
            lh_unbind(c); }                                                                                     \
        else { lh_unbind(c); TW_DRAW(c, CALL); } } while (0)
/* (E43) la pieza grande del arma con huesos encogidos (t_bh) y, si toca, el cargador en la mano izq (solo sus huesos, t_bk) */
#define BONE_DRAW(c, CALL) do {                                                                                   \
        if (t_bhn && bone_bind(c, 0)) { TW_DRAW(c, CALL); bone_unbind(c); } else TW_DRAW(c, CALL);             \
        if (t_blh && bone_bind(c, 1)) { LH_WHOLE(c, CALL); bone_unbind(c); InterlockedIncrement(&g_bone_lh); }  \
    } while (0)
#define MH_D(v) static void STDMETHODCALLTYPE mh_D##v(ID3D11DeviceContext *c, UINT vc, UINT sv) { \
    PFN_Draw tr = (PFN_Draw)g_mh_tr[0][v]; g_mh_calls[0]++; if (c == g_hud_ctx && g_hud_bound) g_bb_draws++; \
    if (t_in_draw) { tr(c, vc, sv); return; }                                                       \
    t_in_draw = 1; { int h = hud_pre(c, vc == 3); TW_DRAW(c, tr(c, vc, sv)); if (h) hud_post(c); } t_in_draw = 0; }
#define MH_DI(v) static void STDMETHODCALLTYPE mh_DI##v(ID3D11DeviceContext *c, UINT ic, UINT si, INT bv) { \
    PFN_DrawIdx tr = (PFN_DrawIdx)g_mh_tr[1][v]; g_mh_calls[1]++; if (c == g_hud_ctx && g_hud_bound) g_bb_draws++; \
    if (t_in_draw) { tr(c, ic, si, bv); return; }                                                          \
    t_in_draw = 1; if (g_exp_state == 1) exp_capture(c, ic, si, bv, 0); { int ps_ = piece_skip(c, ic, si); \
      if (ps_ == 0 || ps_ == 4) { int h = hud_pre(c, 0); TW_DRAW(c, tr(c, ic, si, bv)); if (h) hud_post(c); } \
      else if (ps_ == 2) ISL_DRAW(c, ic, si, tr(c, n_, s_, bv));                                           \
      else if (ps_ == 5) BONE_DRAW(c, tr(c, ic, si, bv));                                                  \
      if (ps_ == 3 || ps_ == 4) LH_WHOLE(c, tr(c, ic, si, bv)); } t_in_draw = 0; }
#define MH_DN(v) static void STDMETHODCALLTYPE mh_DN##v(ID3D11DeviceContext *c, UINT vc, UINT ni, UINT sv, UINT si) { \
    PFN_DrawInst tr = (PFN_DrawInst)g_mh_tr[2][v]; g_mh_calls[2]++; if (c == g_hud_ctx && g_hud_bound) g_bb_draws++; \
    if (t_in_draw) { tr(c, vc, ni, sv, si); return; }                                                               \
    t_in_draw = 1; { int h = hud_pre(c, vc == 3 && ni == 1); TW_DRAW(c, tr(c, vc, ni, sv, si)); if (h) hud_post(c); } t_in_draw = 0; }
#define MH_DII(v) static void STDMETHODCALLTYPE mh_DII##v(ID3D11DeviceContext *c, UINT ic, UINT ni, UINT si, INT bv, UINT sinst) { \
    PFN_DrawIdxInst tr = (PFN_DrawIdxInst)g_mh_tr[3][v]; g_mh_calls[3]++; if (c == g_hud_ctx && g_hud_bound) g_bb_draws++; \
    if (t_in_draw) { tr(c, ic, ni, si, bv, sinst); return; }                                                                    \
    t_in_draw = 1; if (g_exp_state == 1) exp_capture(c, ic, si, bv, sinst); { int ps_ = piece_skip(c, ic, si);         \
      if (ps_ == 0 || ps_ == 4) { int h = hud_pre(c, 0); TW_DRAW(c, tr(c, ic, ni, si, bv, sinst)); if (h) hud_post(c); } \
      else if (ps_ == 2) ISL_DRAW(c, ic, si, tr(c, n_, ni, s_, bv, sinst));                                \
      else if (ps_ == 5) BONE_DRAW(c, tr(c, ic, ni, si, bv, sinst));                                       \
      if (ps_ == 3 || ps_ == 4) LH_WHOLE(c, tr(c, ic, ni, si, bv, sinst)); } t_in_draw = 0; }
#define MH_US(v) static void STDMETHODCALLTYPE mh_US##v(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub, const D3D11_BOX *box, const void *src, UINT rp, UINT dp) \
    { upd_core((PFN_UpdateSub)g_mh_tr[4][v], c, r, sub, box, src, rp, dp); }
/* (E27) borrar, copiar, resolver, mipmaps y calculo: para las copias del ojo derecho */
#define MH_CR(v) static void STDMETHODCALLTYPE mh_CR##v(ID3D11DeviceContext *c, ID3D11RenderTargetView *rv, const FLOAT col[4]) \
    { tw_clr_rtv((PFN_ClrRTV)g_mh_tr[5][v], c, rv, col); }
#define MH_CD(v) static void STDMETHODCALLTYPE mh_CD##v(ID3D11DeviceContext *c, ID3D11DepthStencilView *dv, UINT fl, FLOAT d, UINT8 st) \
    { tw_clr_dsv((PFN_ClrDSV)g_mh_tr[6][v], c, dv, fl, d, st); }
#define MH_CP(v) static void STDMETHODCALLTYPE mh_CP##v(ID3D11DeviceContext *c, ID3D11Resource *d, ID3D11Resource *s) \
    { tw_copy((PFN_CopyRes)g_mh_tr[7][v], c, d, s); }
#define MH_CS(v) static void STDMETHODCALLTYPE mh_CS##v(ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, UINT x, UINT y, UINT z, ID3D11Resource *s, UINT ss, const D3D11_BOX *b) \
    { tw_copysub((PFN_CopySub)g_mh_tr[8][v], c, d, ds, x, y, z, s, ss, b); }
#define MH_RS(v) static void STDMETHODCALLTYPE mh_RS##v(ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, ID3D11Resource *s, UINT ss, DXGI_FORMAT f) \
    { tw_resolve((PFN_Resolve)g_mh_tr[9][v], c, d, ds, s, ss, f); }
#define MH_GM(v) static void STDMETHODCALLTYPE mh_GM##v(ID3D11DeviceContext *c, ID3D11ShaderResourceView *sv) \
    { tw_mips((PFN_GenMips)g_mh_tr[10][v], c, sv); }
#define MH_DP(v) static void STDMETHODCALLTYPE mh_DP##v(ID3D11DeviceContext *c, UINT x, UINT y, UINT z) \
    { tw_dispatch((PFN_Dispatch)g_mh_tr[11][v], c, x, y, z); }
#define MH_ALL(M) M(0) M(1) M(2) M(3) M(4) M(5)
MH_ALL(MH_D) MH_ALL(MH_DI) MH_ALL(MH_DN) MH_ALL(MH_DII) MH_ALL(MH_US)
MH_ALL(MH_CR) MH_ALL(MH_CD) MH_ALL(MH_CP) MH_ALL(MH_CS) MH_ALL(MH_RS) MH_ALL(MH_GM) MH_ALL(MH_DP)
#define MH_ROW(p) { (void *)p##0, (void *)p##1, (void *)p##2, (void *)p##3, (void *)p##4, (void *)p##5 }
static void *const g_mh_det[MHK][MHVAR] = {
    { (void *)mh_D0,   (void *)mh_D1,   (void *)mh_D2,   (void *)mh_D3,   (void *)mh_D4,   (void *)mh_D5 },
    { (void *)mh_DI0,  (void *)mh_DI1,  (void *)mh_DI2,  (void *)mh_DI3,  (void *)mh_DI4,  (void *)mh_DI5 },
    { (void *)mh_DN0,  (void *)mh_DN1,  (void *)mh_DN2,  (void *)mh_DN3,  (void *)mh_DN4,  (void *)mh_DN5 },
    { (void *)mh_DII0, (void *)mh_DII1, (void *)mh_DII2, (void *)mh_DII3, (void *)mh_DII4, (void *)mh_DII5 },
    { (void *)mh_US0,  (void *)mh_US1,  (void *)mh_US2,  (void *)mh_US3,  (void *)mh_US4,  (void *)mh_US5 },
    MH_ROW(mh_CR), MH_ROW(mh_CD), MH_ROW(mh_CP), MH_ROW(mh_CS), MH_ROW(mh_RS), MH_ROW(mh_GM), MH_ROW(mh_DP) };
static const char *const g_mh_name[MHK] = { "Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced", "UpdateSubresource",
    "ClearRenderTargetView", "ClearDepthStencilView", "CopyResource", "CopySubresourceRegion", "ResolveSubresource", "GenerateMips", "Dispatch" };
/* (E20c) CHIVATO: si el juego no consigue crear un cajon o una textura, se apunta (y si la grafica se ha "caido") */
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateBuf)(ID3D11Device *, const D3D11_BUFFER_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateTex2D)(ID3D11Device *, const D3D11_TEXTURE2D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
static PFN_CreateBuf g_tr_CreateBuf = NULL; static PFN_CreateTex2D g_tr_CreateTex = NULL;
static volatile LONG g_create_fail = 0;
static void create_failed(ID3D11Device *d, const char *what, HRESULT hr, UINT a, UINT b, UINT c)
{
    if (InterlockedIncrement(&g_create_fail) > 20) return;
    HRESULT rr = d ? ID3D11Device_GetDeviceRemovedReason(d) : 0;
    vr_log("CHIVATO: %s FALLO hr=0x%08lX (%u x %u, formato/uso %u) | grafica caida? motivo=0x%08lX", what, (unsigned long)hr, a, b, c, (unsigned long)rr);
}
static HRESULT STDMETHODCALLTYPE hk_CreateBuf(ID3D11Device *d, const D3D11_BUFFER_DESC *ds, const D3D11_SUBRESOURCE_DATA *i, ID3D11Buffer **o)
{
    HRESULT hr = g_tr_CreateBuf(d, ds, i, o);
    if (FAILED(hr) && o) create_failed(d, "CreateBuffer", hr, ds ? ds->ByteWidth : 0, 0, ds ? ds->BindFlags : 0);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_CreateTex(ID3D11Device *d, const D3D11_TEXTURE2D_DESC *ds, const D3D11_SUBRESOURCE_DATA *i, ID3D11Texture2D **o)
{
    HRESULT hr = g_tr_CreateTex(d, ds, i, o);
    if (FAILED(hr) && o) create_failed(d, "CreateTexture2D", hr, ds ? ds->Width : 0, ds ? ds->Height : 0, ds ? (UINT)ds->Format : 0);
    return hr;
}
static void mh_start(void);
static void dev_hooks(ID3D11DeviceContext *ctx)
{
    static int done = 0; if (done || !ctx) return;
    ID3D11Device *dev = NULL; ID3D11DeviceContext_GetDevice(ctx, &dev);
    if (!dev) return;
    done = 1;
    mh_start();
    void *f1 = (void *)dev->lpVtbl->CreateBuffer, *f2 = (void *)dev->lpVtbl->CreateTexture2D;
    MH_STATUS s1 = MH_CreateHook(f1, (void *)hk_CreateBuf, (void **)&g_tr_CreateBuf); if (s1 == MH_OK) s1 = MH_EnableHook(f1);
    MH_STATUS s2 = MH_CreateHook(f2, (void *)hk_CreateTex, (void **)&g_tr_CreateTex); if (s2 == MH_OK) s2 = MH_EnableHook(f2);
    vr_log("CHIVATO: CreateBuffer -> %s, CreateTexture2D -> %s", MH_StatusToString(s1), MH_StatusToString(s2));
    void *f3 = (void *)dev->lpVtbl->CreateInputLayout;   /* (E40) para poder leer los vertices al exportar */
    MH_STATUS s3 = MH_CreateHook(f3, (void *)hk_CreateIL, (void **)&g_tr_CreateIL); if (s3 == MH_OK) s3 = MH_EnableHook(f3);
    vr_log("EXPORTAR: CreateInputLayout -> %s", MH_StatusToString(s3));
    ID3D11Device_Release(dev);
}
static void mh_start(void)
{
    static int tried = 0; if (tried) return; tried = 1;
    MH_STATUS s = MH_Initialize();
    g_mh_ok = (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED);
    vr_log("MINHOOK: arrancado -> %s", MH_StatusToString(s));
}
/* mira las casillas de dibujo de la tabla del contexto y engancha por dentro cada version nueva que aparezca */
static void mh_scan(ID3D11DeviceContext *c)
{
    if (!c || !g_mh_ok) return;
    ID3D11DeviceContextVtbl *vt = c->lpVtbl;
    void *p4[MHK] = { (void *)vt->Draw, (void *)vt->DrawIndexed, (void *)vt->DrawInstanced, (void *)vt->DrawIndexedInstanced, (void *)vt->UpdateSubresource,
                      (void *)vt->ClearRenderTargetView, (void *)vt->ClearDepthStencilView, (void *)vt->CopyResource, (void *)vt->CopySubresourceRegion,
                      (void *)vt->ResolveSubresource, (void *)vt->GenerateMips, (void *)vt->Dispatch };
    if (g_orig_UpdateSub && (void *)vt->UpdateSubresource == (void *)hk_UpdateSub) p4[4] = (void *)g_orig_UpdateSub;
    if (g_orig_CopyRes && (void *)vt->CopyResource == (void *)hk_CopyRes) p4[7] = (void *)g_orig_CopyRes;   /* (E27) por tabla ya estaba el nuestro */
    if (g_orig_CopySub && (void *)vt->CopySubresourceRegion == (void *)hk_CopySub) p4[8] = (void *)g_orig_CopySub;
    for (int k = 0; k < MHK; k++) {
        void *p = p4[k];
        if (!p || p == (void *)hk_UpdateSub || p == (void *)hk_CopyRes || p == (void *)hk_CopySub) continue;
        if (k < 4 && !(cfg_hud || cfg_brazos || cfg_dosojos)) continue;
        if (k >= 5 && !cfg_dosojos) continue;
        int known = 0;
        for (int i = 0; i < g_mh_n[k]; i++) if (g_mh_fn[k][i] == p) { known = 1; break; }
        for (int i = 0; i < MHVAR && !known; i++) if (g_mh_det[k][i] == p) known = 1;   /* (por si acaso) uno nuestro */
        if (known) continue;
        if (g_mh_n[k] >= MHVAR) {
            static int said[MHK]; if (said[k] < 3) { said[k]++; vr_log("MINHOOK %s: demasiadas versiones (%p sin enganchar)", g_mh_name[k], p); }
            continue;
        }
        int v = g_mh_n[k];
        unsigned char *b = (unsigned char *)p; char line[100]; int L = 0;
        for (int q = 0; q < 16; q++) L += snprintf(line + L, sizeof(line) - L, "%02X ", b[q]);
        HMODULE hm = NULL; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)b, &hm);
        char mn[MAX_PATH] = "?"; if (hm) GetModuleFileNameA(hm, mn, MAX_PATH);
        const char *mshort = strrchr(mn, '\\'); mshort = mshort ? mshort + 1 : mn;
        MH_STATUS s = MH_CreateHook(p, g_mh_det[k][v], &g_mh_tr[k][v]);
        if (s == MH_OK) s = MH_EnableHook(p);
        g_mh_fn[k][v] = p; g_mh_n[k]++;          /* se apunta aunque falle, para no reintentar cada fotograma */
        vr_log("MINHOOK %s version %d = %p (%s+0x%llx) %s-> %s", g_mh_name[k], v + 1, p, mshort,
               (unsigned long long)(b - (unsigned char *)hm), line, MH_StatusToString(s));
    }
}
/* (E17) fin de fotograma: el censo de piezas del arma, cambios de arma y el archivo de brazos */
static void brazos_path(char *path) { GetTempPathA(MAX_PATH, path); strcat(path, "metro_vr_brazos.txt"); }
static void brazos_load(void)
{
    static int done = 0; if (done) return; done = 1;
    char path[MAX_PATH]; brazos_path(path);
    FILE *fp = fopen(path, "r"); if (!fp) return;
    UINT a, b;
    while (g_nhide < MAXP && fscanf(fp, "%u %u", &a, &b) == 2) { g_hide[g_nhide].ic = a; g_hide[g_nhide].si = b; g_nhide++; }
    fclose(fp);
    vr_log("BRAZOS: %d piezas guardadas como brazos (%s)", g_nhide, path);
}
static void brazos_save(void)
{
    char path[MAX_PATH]; brazos_path(path);
    FILE *fp = fopen(path, "w"); if (!fp) return;
    for (int i = 0; i < g_nhide; i++) fprintf(fp, "%u %u\n", g_hide[i].ic, g_hide[i].si);
    fclose(fp);
}
/* (E39) metro_vr_cargadores.txt: una linea por pieza de cargador: "arma_ic arma_si pieza_ic pieza_si" */
static void mag_path(char *path) { GetTempPathA(MAX_PATH, path); strcat(path, "metro_vr_cargadores.txt"); }
static void mag_load(void)
{
    static int done = 0; if (done) return; done = 1;
    char path[MAX_PATH]; mag_path(path);
    FILE *fp = fopen(path, "r"); if (!fp) { vr_log("CARGADOR: ninguno marcado todavia (%s)", path); return; }
    UINT a, b, c, d;
    while (g_nmag < MAXMAG && fscanf(fp, "%u %u %u %u", &a, &b, &c, &d) == 4) { g_mag[g_nmag].wic = a; g_mag[g_nmag].wsi = b; g_mag[g_nmag].ic = c; g_mag[g_nmag].si = d; g_nmag++; }
    fclose(fp);
    vr_log("CARGADOR: %d piezas de cargador marcadas (%s)", g_nmag, path);
}
static void mag_save(void)
{
    char path[MAX_PATH]; mag_path(path);
    FILE *fp = fopen(path, "w"); if (!fp) return;
    for (int i = 0; i < g_nmag; i++) fprintf(fp, "%u %u %u %u\n", g_mag[i].wic, g_mag[i].wsi, g_mag[i].ic, g_mag[i].si);
    fclose(fp);
}
static void pieces_key(int shift, int ctrl);
/* (E39) Av Pag: ocultar la siguiente pieza (igual que F6). Shift+Av Pag: la pieza oculta es el CARGADOR de esta arma.
 * Ctrl+Av Pag: borrar lo marcado como cargador de esta arma. */
static void mag_key(int shift, int ctrl)
{
    UINT w = g_wpn_ic, ws = g_wpn_si;
    if (ctrl) {
        int j = 0; for (int i = 0; i < g_nmag; i++) if (!(g_mag[i].wic == w && g_mag[i].wsi == ws)) g_mag[j++] = g_mag[i];
        int del = g_nmag - j; g_nmag = j; mag_save();
        vr_log(">>> Ctrl+AvPag: borradas %d piezas de cargador del arma %u/%u", del, w, ws); Beep(500, 200); return;
    }
    if (shift) {
        if (!w) { vr_log(">>> Shift+AvPag: aun no se sabe que arma llevas (espera un segundo con el arma en la mano)"); Beep(400, 150); return; }
        if (g_try < 0) { vr_log(">>> Shift+AvPag: no hay ninguna pieza oculta (pulsa Av Pag hasta que desaparezca el cargador)"); Beep(400, 150); return; }
        if (g_try_sig.ic == w && g_try_sig.si == ws) { vr_log(">>> Shift+AvPag: esa es la pieza grande del arma, no puede ser el cargador"); Beep(400, 150); return; }
        if (!mag_is(w, ws, g_try_sig.ic, g_try_sig.si) && g_nmag < MAXMAG) {
            g_mag[g_nmag].wic = w; g_mag[g_nmag].wsi = ws; g_mag[g_nmag].ic = g_try_sig.ic; g_mag[g_nmag].si = g_try_sig.si; g_nmag++; mag_save();
        }
        vr_log(">>> Shift+AvPag: pieza %u/%u guardada como CARGADOR del arma %u/%u (esta arma tiene %d)", g_try_sig.ic, g_try_sig.si, w, ws, mag_count(w, ws));
        Beep(900, 80); Beep(1300, 120); Beep(1600, 120);
        g_try = -1;
        return;
    }
    pieces_key(0, 0);
}
static int sig_cmp(const void *a, const void *b)
{
    const PieceSig *x = a, *y = b;
    if (x->ic != y->ic) return x->ic < y->ic ? 1 : -1;      /* las piezas grandes primero */
    return x->si < y->si ? -1 : (x->si > y->si);
}
static void pieces_frame(void)
{
    brazos_load();
    mag_load();
    bone_load();
    {   /* (E31) chivato de la mano izquierda, cada 2 s */
        static ULONGLONG t0 = 0; ULONGLONG now = GetTickCount64();
        if (now - t0 > 2000) {
            t0 = now;
            if (g_mode == 1) vr_log("MANO IZQ: mando=%ld calibrada=%d calibrando=%ld dos_manos=%ld (boton=%ld giro=%.1f grados) recarga=%ld (auto_bloqueadas=%ld/%ld cargadores=%ld expulsados=%ld fuera=%d oculto=%ld en_mano=%ld huesos=%ld/%ld/%ld) | camaras=%ld dibujos=%ld sin_camara=%ld | islas=%ld reloj=%ld",
                                    (long)g_lhand_ok, g_lhand_cal, (long)g_lhand_hold, (long)g_dos_manos, (long)g_lgrip, g_tw_deg10 / 10.0, (long)g_mag_state, (long)InterlockedExchange(&g_ar_blocked, 0), (long)InterlockedExchange(&g_ar2_blocked, 0), (long)g_mag_ins, (long)g_expel_n, g_out_ic != 0,
                                    (long)InterlockedExchange(&g_mag_hid_n, 0), (long)InterlockedExchange(&g_mag_hand_n, 0),
                                    (long)InterlockedExchange(&g_bone_draws, 0), (long)InterlockedExchange(&g_bone_lh, 0), (long)InterlockedExchange(&g_bone_fail, 0), (long)InterlockedExchange(&g_lh_n, 0),
                                    (long)InterlockedExchange(&g_lh_draw, 0), (long)InterlockedExchange(&g_lh_nocam, 0), (long)g_isl_state, (long)InterlockedExchange(&g_watch_n, 0));
        }
    }
    while (InterlockedCompareExchange(&g_pc_lock, 1, 0) != 0) YieldProcessor();
    if (g_pc_ncur == 0) { InterlockedExchange(&g_pc_lock, 0); return; }
    qsort(g_pc_cur, g_pc_ncur, sizeof(PieceSig), sig_cmp);
    int same = (g_pc_ncur == g_pc_nlast);
    for (int i = 0; same && i < g_pc_ncur; i++) if (g_pc_cur[i].ic != g_pc_last[i].ic || g_pc_cur[i].si != g_pc_last[i].si) same = 0;
    static ULONGLONG last_log = 0;
    if (!same && GetTickCount64() - last_log > 1500) {
        char line[1100]; int L = 0;
        for (int i = 0; i < g_pc_ncur && L < 1000; i++) {
            int h = 0; for (int k = 0; k < g_nhide; k++) if (g_hide[k].ic == g_pc_cur[i].ic && g_hide[k].si == g_pc_cur[i].si) h = 1;
            int mg = !h && mag_is(g_wpn_ic, g_wpn_si, g_pc_cur[i].ic, g_pc_cur[i].si);
            L += snprintf(line + L, sizeof(line) - L, " %d:%u/%u%s", i + 1, g_pc_cur[i].ic, g_pc_cur[i].si, h ? "(brazo)" : mg ? "(cargador)" : "");
        }
        vr_log("PIEZAS del arma (%d):%s", g_pc_ncur, line);
        last_log = GetTickCount64();
    }
    {   /* (E28) que arma se ve: su pieza mas grande que no sea brazo (como D-Rey86, la "pieza cuerpo" de cada arma).
         * Tiene que salir la misma 15 fotogramas seguidos, para no cambiar a medias durante el cambio de arma. */
        UINT cic = 0, csi = 0;
        for (int i = 0; i < g_pc_ncur && !cic; i++) {
            int h = 0; for (int k = 0; k < g_nhide; k++) if (g_hide[k].ic == g_pc_cur[i].ic && g_hide[k].si == g_pc_cur[i].si) h = 1;
            if (!h) { cic = g_pc_cur[i].ic; csi = g_pc_cur[i].si; }
        }
        static UINT pic = 0, psi = 0; static int cnt = 0;
        if (cic == pic && csi == psi) cnt++; else { pic = cic; psi = csi; cnt = 0; }
        if (cic && cnt == 15 && (cic != g_wpn_ic || csi != g_wpn_si)) {
            g_wpn_ic = cic; g_wpn_si = csi;
            vr_log("ARMA: ahora %u/%u -> calibracion %s", cic, csi, wcal_find(cic, csi) >= 0 ? "PROPIA"
                   : "general (para darle la suya: mano donde ves la empunadura y mantener 1,5 s el clic del stick derecho)");
            int nm = mag_count(cic, csi);
            if (nm) vr_log("CARGADOR: esta arma tiene %d pieza(s) de cargador marcadas", nm);
            else vr_log("CARGADOR: esta arma NO tiene cargador marcado (Av Pag hasta que desaparezca, luego Shift+Av Pag)");
        }
    }
    memcpy(g_pc_last, g_pc_cur, sizeof(PieceSig) * g_pc_ncur); g_pc_nlast = g_pc_ncur;
    g_pc_ncur = 0;
    InterlockedExchange(&g_pc_lock, 0);
}
/* (E28) tecla Inicio: ensena UNA de las piezas guardadas como brazo (las demas siguen ocultas), para ver cual es la mano.
 * Cada pulsacion pasa a la siguiente; despues de la ultima, todas ocultas otra vez. No cambia el archivo. */
static void pieces_show_key(void)
{
    if (g_nhide == 0) { vr_log(">>> Inicio: no hay piezas guardadas como brazo"); Beep(400, 150); return; }
    g_show++;
    if (g_show >= g_nhide) { g_show = -1; vr_log(">>> Inicio: todas las piezas guardadas, ocultas otra vez"); Beep(600, 100); return; }
    vr_log(">>> Inicio: ENSENO la pieza guardada %d de %d (%u/%u)", g_show + 1, g_nhide, g_hide[g_show].ic, g_hide[g_show].si);
    Beep(1000, 60);
}
/* F6: oculta la siguiente pieza para ver cual es. Shift+F6: la pieza oculta ahora SON brazos (se guarda).
 * Ctrl+F6: borra todas las guardadas. */
static void pieces_key(int shift, int ctrl)
{
    if (ctrl) { g_nhide = 0; g_show = -1; brazos_save(); g_try = -1; vr_log(">>> Ctrl+F6: borradas las piezas de brazos"); Beep(500, 200); return; }
    if (shift) {
        if (g_try < 0) { vr_log(">>> Shift+F6: no hay ninguna pieza probandose (pulsa F6 primero)"); Beep(400, 150); return; }
        int dup = 0; for (int k = 0; k < g_nhide; k++) if (g_hide[k].ic == g_try_sig.ic && g_hide[k].si == g_try_sig.si) dup = 1;
        if (!dup && g_nhide < MAXP) { g_hide[g_nhide++] = g_try_sig; brazos_save(); }
        vr_log(">>> Shift+F6: pieza %u/%u guardada como BRAZO (%d guardadas)", g_try_sig.ic, g_try_sig.si, g_nhide);
        Beep(900, 80); Beep(1300, 120);
        g_try = -1;
        return;
    }
    /* siguiente pieza del ultimo fotograma que no este ya guardada */
    int n = g_pc_nlast, start = g_try;
    for (int step = 0; step < n + 1; step++) {
        g_try++;
        if (g_try >= n) { g_try = -1; vr_log(">>> F6: ninguna pieza oculta (vuelta completa)"); Beep(600, 100); return; }
        int h = 0; for (int k = 0; k < g_nhide; k++) if (g_hide[k].ic == g_pc_last[g_try].ic && g_hide[k].si == g_pc_last[g_try].si) h = 1;
        if (!h) break;
    }
    (void)start;
    g_try_sig = g_pc_last[g_try];
    vr_log(">>> F6: oculto la pieza %d de %d (%u/%u)", g_try + 1, n, g_try_sig.ic, g_try_sig.si);
    Beep(1000, 60);
}

/* (E17c) Cada fotograma se mira la tabla del contexto. (E18) Las casillas de DIBUJO ya no se tocan (Windows las
 * reescribe): se engancha por dentro con MinHook cada version nueva que aparezca. Las otras dos (OMSetRenderTargets,
 * RSSetViewports, para el HUD) siguen por tabla y se reponen si hiciera falta. */
static void *g_vt_seen = NULL;
static void draw_hooks_check(void)
{
    ID3D11DeviceContext *c = g_hud_ctx;
    if (!c) return;
    mh_scan(c);
    if (!(cfg_hud || cfg_brazos || cfg_dosojos)) return;
    ID3D11DeviceContextVtbl *vt = c->lpVtbl;
    if ((void *)vt != g_vt_seen) {
        if (g_vt_seen) vr_log("VTABLE: el contexto tiene OTRA tabla de funciones (%p -> %p)", g_vt_seen, (void *)vt);
        g_vt_seen = (void *)vt;
    }
    mh_scan(c);
    struct { void **slot; void *hook; void **orig; const char *n; } t[2] = {
        { (void **)&vt->OMSetRenderTargets, (void *)hk_OMSetRT, (void **)&g_orig_OMSetRT, "OMSetRenderTargets" },
        { (void **)&vt->RSSetViewports, (void *)hk_RSSetVP, (void **)&g_orig_RSSetVP, "RSSetViewports" } };
    for (int i = 0; i < 2; i++) {
        void *cur = *t[i].slot;
        if (cur == t[i].hook) continue;
        static int said[2];
        if (said[i] < 5) { said[i]++; vr_log("VTABLE: %s ya no es nuestro (antes %p, ahora %p): lo vuelvo a poner", t[i].n, *t[i].orig, cur); }
        DWORD old;
        if (!VirtualProtect((LPVOID)t[i].slot, sizeof(void *), PAGE_READWRITE, &old)) continue;
        *t[i].orig = cur;
        *t[i].slot = t[i].hook;
        VirtualProtect((LPVOID)t[i].slot, sizeof(void *), old, &old);
        InterlockedIncrement(&g_vt_fix);
    }
}

/* -------------------------------------------------- instalar los hooks */
static void install_hooks(ID3D11DeviceContext *ctx, IDXGISwapChain *sc)
{
    if (ctx) {
        if (cfg_map) {
            PATCH(&ctx->lpVtbl->Map,   hk_Map,   g_orig_Map,   "Map");
            PATCH(&ctx->lpVtbl->Unmap, hk_Unmap, g_orig_Unmap, "Unmap");
        }
        /* (E20c) UpdateSubresource va por MinHook (mh_scan): Windows tambien cambia esa casilla de la tabla */
        mh_start();
        mh_scan(ctx);
        dev_hooks(ctx);
        if (cfg_vscb) {
            PATCH(&ctx->lpVtbl->VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB, "VSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB, "PSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->CSSetConstantBuffers, hk_CSSetCB, g_orig_CSSetCB, "CSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->CopyResource, hk_CopyRes, g_orig_CopyRes, "CopyResource");
            PATCH(&ctx->lpVtbl->CopySubresourceRegion, hk_CopySub, g_orig_CopySub, "CopySubresourceRegion");
        }
        if (cfg_hud || cfg_brazos || cfg_dosojos) {   /* (E11) HUD, (E17) piezas del arma, (E27) dos ojos */
            g_hud_ctx = ctx;
            PATCH(&ctx->lpVtbl->OMSetRenderTargets, hk_OMSetRT, g_orig_OMSetRT, "OMSetRenderTargets");
            PATCH(&ctx->lpVtbl->RSSetViewports, hk_RSSetVP, g_orig_RSSetVP, "RSSetViewports");
            mh_start();      /* (E18) los dibujos, por dentro con MinHook (por tabla no se puede) */
            mh_scan(ctx);
        }
    }
    if (cfg_present) {
        if (sc) {
            give_private_vtable(sc);
        } else {
            ID3D11Device *dev = NULL;
            if (ctx) ID3D11DeviceContext_GetDevice(ctx, &dev);
            if (dev) {
                PATCH(&dev->lpVtbl->CreateRenderTargetView, hk_CreateRTV, g_orig_CreateRTV, "CreateRenderTargetView");
                ID3D11Device_Release(dev);
            }
        }
    }
    if (InterlockedCompareExchange(&g_keys_started, 1, 0) == 0) {
        HANDLE hk = CreateThread(NULL, 0, keys_thread, NULL, 0, NULL);
        if (hk) CloseHandle(hk);
    }
    if (cfg_thread && InterlockedCompareExchange(&g_thread_started, 1, 0) == 0) {
        HANDLE h = CreateThread(NULL, 0, stats_thread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
}

/* ------------------------------------------- funciones que exporta la DLL */
/* ---- compatibilidad con RenderDoc ----
 * RenderDoc engancha "la" d3d11.dll... y como nosotros tambien nos llamamos d3d11.dll, cuando RenderDoc quiere
 * llamar a la de verdad nos llama a NOSOTROS: bucle infinito. Si nos vuelven a llamar desde dentro (mismo hilo),
 * vamos directos a la funcion de Windows, buscandola a mano en su tabla de exportaciones (sin GetProcAddress,
 * que RenderDoc tambien engancha). */
static __thread int t_create_depth = 0;
static void *real_export(HMODULE m, const char *name)
{
    if (!m) return NULL;
    unsigned char *b = (unsigned char *)m;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)b;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(b + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd.VirtualAddress) return NULL;
    IMAGE_EXPORT_DIRECTORY *ed = (IMAGE_EXPORT_DIRECTORY *)(b + dd.VirtualAddress);
    DWORD *names = (DWORD *)(b + ed->AddressOfNames), *funcs = (DWORD *)(b + ed->AddressOfFunctions);
    WORD *ords = (WORD *)(b + ed->AddressOfNameOrdinals);
    for (DWORD i = 0; i < ed->NumberOfNames; i++)
        if (!strcmp((const char *)(b + names[i]), name)) return b + funcs[ords[i]];
    return NULL;
}

/* tras crear el dispositivo: enganchar. Si quien llama no pidio el "contexto" (p.ej. RenderDoc), lo sacamos del dispositivo */
static void hooks_after_create(HRESULT hr, ID3D11Device **ppDevice, ID3D11DeviceContext **ppContext, IDXGISwapChain **ppSwapChain)
{
    if (FAILED(hr)) return;
    IDXGISwapChain *sc = ppSwapChain ? *ppSwapChain : NULL;
    vr_log("enganchando: dispositivo=%p contexto=%p swapchain=%p", ppDevice ? (void *)*ppDevice : NULL,
           ppContext ? (void *)*ppContext : NULL, (void *)sc);
    if (ppContext && *ppContext) { install_hooks(*ppContext, sc); return; }
    if (ppDevice && *ppDevice) {
        ID3D11DeviceContext *ctx = NULL;
        ID3D11Device_GetImmediateContext(*ppDevice, &ctx);
        vr_log("Dispositivo creado sin pedir contexto: lo saco del dispositivo (%p)", (void *)ctx);
        if (ctx) { install_hooks(ctx, sc); ID3D11DeviceContext_Release(ctx); }
    } else {
        vr_log("AVISO: dispositivo creado sin dispositivo ni contexto a la vista; no puedo engancharme aqui");
        install_hooks(NULL, sc);    /* al menos arrancar las teclas y el resumen */
    }
}

HRESULT WINAPI Proxy_D3D11CreateDevice(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *featureLevels, UINT numLevels, UINT sdkVersion,
    ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppContext)
{
    vr_log("El juego llama a D3D11CreateDevice (flags=0x%X)", flags);
    HMODULE m = real_d3d11();
    PFN_D3D11_CREATE_DEVICE f = m ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice") : NULL;
    if (t_create_depth > 0) {   /* nos llama RenderDoc "creyendo" que somos la de Windows */
        PFN_D3D11_CREATE_DEVICE rf = (PFN_D3D11_CREATE_DEVICE)real_export(m, "D3D11CreateDevice");
        return rf ? rf(adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion, ppDevice, pFeatureLevel, ppContext) : E_FAIL;
    }
    if (!f) { vr_log("No encuentro D3D11CreateDevice real"); return E_FAIL; }
    t_create_depth++;
    HRESULT hr = f(adapter, driverType, software, flags, featureLevels, numLevels,
                   sdkVersion, ppDevice, pFeatureLevel, ppContext);
    t_create_depth--;
    vr_log("D3D11CreateDevice -> hr=0x%08lX, nivel=0x%X", (unsigned long)hr,
           pFeatureLevel ? (unsigned)*pFeatureLevel : 0);
    hooks_after_create(hr, ppDevice, ppContext, NULL);
    return hr;
}

HRESULT WINAPI Proxy_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *featureLevels, UINT numLevels, UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC *swapDesc, IDXGISwapChain **ppSwapChain,
    ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppContext)
{
    vr_log("El juego llama a D3D11CreateDeviceAndSwapChain (flags=0x%X)", flags);
    HMODULE m = real_d3d11();
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN f =
        m ? (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(m, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (t_create_depth > 0) {   /* nos llama RenderDoc "creyendo" que somos la de Windows */
        PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN rf = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)real_export(m, "D3D11CreateDeviceAndSwapChain");
        return rf ? rf(adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion,
                       swapDesc, ppSwapChain, ppDevice, pFeatureLevel, ppContext) : E_FAIL;
    }
    if (!f) { vr_log("No encuentro D3D11CreateDeviceAndSwapChain real"); return E_FAIL; }
    t_create_depth++;
    HRESULT hr = f(adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion,
                   swapDesc, ppSwapChain, ppDevice, pFeatureLevel, ppContext);
    t_create_depth--;
    vr_log("D3D11CreateDeviceAndSwapChain -> hr=0x%08lX", (unsigned long)hr);
    hooks_after_create(hr, ppDevice, ppContext, ppSwapChain);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        InitializeCriticalSection(&g_cs);
        char tmp[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, tmp);
        if (n > 0 && n < MAX_PATH - 32) {
            snprintf(g_logpath, sizeof(g_logpath), "%smetro_vr_log.txt", tmp);
            HANDLE h = CreateFileA(g_logpath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  /* log nuevo */
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            load_config(tmp);
            seed_files(tmp);   /* (E35) */
            char exe[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, exe, MAX_PATH);
            vr_log("=== HITO E43: d3d11 proxy cargada por: %s ===", exe);
            vr_log("linterna=%d linterna_grados=%.0f (con F9 se agranda el FOV del juego; la camara del juego no se toca)", cfg_linterna, (double)cfg_linterna_deg);
            hook_clocks();
            vr_log("interruptores: hook_map=%d hook_updatesub=%d hook_present=%d hilo_resumen=%d leer_camara=%d",
                   cfg_map, cfg_updsub, cfg_present, cfg_thread, cfg_read);
            vr_log("PRUEBA: modo inicial=%d prueba_grados=%.1f escritura_directa=%d hook_vscb=%d huecos_a_cero=0x%X (F9 o Insert cambian de modo)",
                   cfg_test, (double)cfg_deg, cfg_direct, cfg_vscb, g_mute_mask);
            AddVectoredExceptionHandler(1, crash_handler);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        vr_log("=== d3d11 proxy descargada (cierre %s) ===",
               reserved ? "normal del proceso" : "de la DLL");
    }
    return TRUE;
}
