/*
 * HITO E5b - d3d11.dll (luces y sombras con la lente del visor) (detective: que otros cajones llevan la lente del juego) (la camara del juego sigue la cabeza) (buscador de la camara del juego en memoria: Shift+H) (estereo sincronizado: los dos ojos ven el mismo instante, con el reloj del juego parado en el 2o ojo) (lente del juego = lente del visor; cada ojo recibe su trozo) (imagen al visor: la misma para los dos ojos; visor via SteamVR + teclado) (vista con el teclado: flechas, o cualquier Shift / Bloq Mayus + letras, o numerico) "adaptador" (proxy) para Metro Last Light Redux (64 bits)
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
static int cfg_cj = 1;
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
    "# (marca de version: E5b)\r\n"
    "# luces_visor=1: corrige tambien los cajones de luces y sombras (hueco 7 = lente, hueco 3 = rayos de reconstruccion)\r\n"
    "luces_visor=1\r\n"
    "# camara_juego=1: con F9, la camara DE VERDAD del juego sigue tu cabeza (asi no desaparecen cosas al girarla). 0 = como antes\r\n"
    "camara_juego=1\r\n"
    "# reloj_hilos=1: solo se para el reloj al hilo que dibuja (el resto, sonido etc., sigue normal). 0 = a todos\r\n"
    "reloj_hilos=1\r\n"
    "# estereo: con F9. 2 = sincronizado (el juego dibuja izq y der del MISMO instante: se para su reloj en el 2o ojo)\r\n"
    "#          1 = alternos (un ojo por fotograma, el mundo sigue moviendose) | 0 = mono\r\n"
    "estereo=2\r\n"
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
    else if (!strcmp(s, "camara_juego"))    cfg_cj = v;
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
        if (!strstr(buf, "E5b")) {
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
    if (f[24] < 0.3f || f[29] < 0.3f || f[24] > 40.0f || f[29] > 40.0f) return 0;   /* P00, P11 */
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
    build_H(g_yaw, g_pitch, g_roll, g_sx, g_sy, g_sz, Hk, Hki);
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

static void get_H(float H[4][4], float Hi[4][4])
{
    LONG i = g_Hidx;
    memcpy(H,  g_Hbuf[i][0], 16 * sizeof(float));
    memcpy(Hi, g_Hbuf[i][1], 16 * sizeof(float));
    if (g_stereo_on) {
        /* el ojo esta desplazado t respecto al centro de la cabeza: vista_ojo = T(-t) * vista */
        LONG e = g_eye;
        float t[3] = { g_eyepos[e][0] * cfg_ipdscale, g_eyepos[e][1] * cfg_ipdscale, g_eyepos[e][2] * cfg_ipdscale };
        for (int r = 0; r < 3; r++) {
            H[r][3] -= t[r];
            Hi[r][3] += Hi[r][0] * t[0] + Hi[r][1] * t[1] + Hi[r][2] * t[2];
        }
    }
}

/* ---- lente del visor ---- filas 0 y 1 de la proyeccion (x e y); la profundidad (filas 2 y 3) es la del juego */
static float g_fovrow[2][4];
static volatile LONG g_fov_ok = 0;
static float g_eyeb[2][4];   /* trozo de la imagen para cada ojo: uMin vMin uMax vMax */

static void fov_xy(float P[4][4])
{
    if (!cfg_fov || !g_fov_ok) return;
    if (g_stereo_on) {      /* lente del ojo de este fotograma */
        LONG e = g_eye;
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
    mul4(WVP, iWV4, Pe);                                   /* la lente de este dibujo */
    if (fabsf(Pe[3][2] - 1.0f) > 1e-3f || fabsf(Pe[3][3]) > 1e-3f ||
        fabsf(Pe[3][0]) > 1e-3f || fabsf(Pe[3][1]) > 1e-3f) return 0;
    if (Pe[0][0] < 0.3f || Pe[1][1] < 0.3f) return 0;       /* no parece una lente en perspectiva */
    if (Pe[0][0] >= 1.428f && !g_arma) return 0;             /* lente estrecha (~57 grados): es el arma */
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
static volatile float g_orig_P00 = 0, g_orig_P11 = 0, g_orig_P22 = 0, g_orig_P23 = 0;   /* lente ORIGINAL del juego */
static float wrap_pi(float a);

/* ---- camara DE VERDAD del juego (encontrada con el buscador): giro e inclinacion del raton ----
 * Objeto apuntado desde metro.exe+0xD08E18 (y otros); en +0x650 = -inclinacion, +0x654 = giro (radianes). */
static const unsigned g_cj_offs[] = { 0xD08E18, 0xD08E28, 0xD08E48, 0xD092D8, 0xD092E8, 0xD09318,
                                      0xD09778, 0xD09788, 0xD097B8, 0xD09C28, 0xD09C38, 0xD09C88 };
static float * volatile g_cj_pitch = NULL, * volatile g_cj_yaw = NULL;
static int g_cj_copy = 0;       /* bits: 1 = hay copia en +12/+16, 2 = hay copia en +24/+28 (se mueven igual) */
static volatile LONG g_cj_active = 0;
static volatile LONG g_cj_dirty = 0;
static float g_dpsi = 0, g_dtheta = 0;     /* lo que le hemos sumado nosotros al giro / inclinacion */
static float g_psi_prev = 0, g_th_prev = 0; /* lo ultimo que puso el raton (para saber si el juego borro lo nuestro) */
static volatile LONG g_cj_resets = 0;
static float g_Cd[3][3];                   /* orientacion que queremos para la camara (mundo) */
static float g_Ym[3][3];                   /* giro del cuerpo (raton), para mover la cabeza de lado */
static float g_head_Rg[3][3] = {{1,0,0},{0,1,0},{0,0,1}}, g_head_dg[3];
static float g_lastV[9];

static void publish_H(void);
static int rd_float(const float *p, float *v) { SIZE_T g = 0; return ReadProcessMemory(GetCurrentProcess(), p, v, 4, &g) && g == 4; }
static int wr_float(float *p, float v) { SIZE_T g = 0; return WriteProcessMemory(GetCurrentProcess(), p, &v, 4, &g) && g == 4; }

static void m3mul(const float A[3][3], const float B[3][3], float C[3][3])
{
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float a = 0; for (int k = 0; k < 3; k++) a += A[i][k] * B[k][j]; C[i][j] = a; }
}
static void yawm(float a, float R[3][3])
{
    float c = cosf(a), s_ = sinf(a);
    float T[3][3] = {{c,0,s_},{0,1,0},{-s_,0,c}}; memcpy(R, T, sizeof(T));
}
static void pitchm(float a, float R[3][3])
{
    float c = cosf(a), s_ = sinf(a);
    float T[3][3] = {{1,0,0},{0,c,s_},{0,-s_,c}}; memcpy(R, T, sizeof(T));
}

/* direcciones posibles de la "inclinacion" (la del giro va justo detras, +4) */
static ULONG_PTR g_cj_scan[16]; static volatile LONG g_cj_nscan = 0; static volatile LONG g_cj_scanning = 0;

/* Busqueda de emergencia por la "firma": inclinacion, giro, ?, inclinacion, giro, ?, inclinacion, giro
 * (tres copias seguidas). Solo si los punteros fijos no sirven en esta partida. */
static DWORD WINAPI cj_sig_thread(LPVOID u)
{
    (void)u;
    float yaw = g_cam_yaw, pitch = g_cam_pitch;
    vr_log("CAMARA DEL JUEGO: los punteros fijos no sirven ahora; la busco por su forma en la memoria (unos segundos)...");
    static float chunk[1 << 18];
    MEMORY_BASIC_INFORMATION mbi; unsigned char *a = NULL; int n = 0;
    while (n < 16 && VirtualQuery(a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        unsigned char *next = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
            for (SIZE_T off = 0; off < mbi.RegionSize && n < 16; off += sizeof(chunk) - 64) {
                SIZE_T want = mbi.RegionSize - off; if (want > sizeof(chunk)) want = sizeof(chunk);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (unsigned char *)mbi.BaseAddress + off, chunk, want, &got)) continue;
                SIZE_T cnt = got / 4;
                for (SIZE_T k = 0; k + 8 < cnt && n < 16; k++) {
                    float pv = chunk[k], yv = chunk[k + 1];
                    if (fabsf(-pv - pitch) > 0.03f) continue;
                    if (fabsf(wrap_pi(yv - yaw)) > 0.03f) continue;
                    if (!(fabsf(chunk[k + 3] - pv) < 0.6f) || !(fabsf(wrap_pi(chunk[k + 4] - yv)) < 0.6f) ||
                        !(fabsf(chunk[k + 6] - pv) < 0.6f) || !(fabsf(wrap_pi(chunk[k + 7] - yv)) < 0.6f)) continue;
                    if (fabsf(pv) < 0.01f && fabsf(yv) < 0.01f) continue;
                    g_cj_scan[n++] = (ULONG_PTR)mbi.BaseAddress + off + k * 4;
                }
            }
        }
        if (next <= a) break;
        a = next;
    }
    vr_log("CAMARA DEL JUEGO: busqueda por forma: %d candidatas", n);
    g_cj_nscan = n;
    g_cj_scanning = 0;
    return 0;
}

static void cj_accept(ULONG_PTR pitch_addr, const char *via)
{
    float pv, yv;
    rd_float((float *)pitch_addr, &pv); rd_float((float *)(pitch_addr + 4), &yv);
    g_cj_pitch = (float *)pitch_addr; g_cj_yaw = (float *)(pitch_addr + 4);
    /* las tres parejas (inclinacion, giro) seguidas son la camara: actual, objetivo y anterior. Se escriben
     * SIEMPRE las tres; si solo escribiamos una, el juego la rehacia desde las otras al mover el raton */
    g_cj_copy = 3;
    {
        float c1p = 0, c1y = 0, c2p = 0, c2y = 0;
        rd_float(g_cj_pitch + 3, &c1p); rd_float(g_cj_yaw + 3, &c1y); rd_float(g_cj_pitch + 6, &c2p); rd_float(g_cj_yaw + 6, &c2y);
        vr_log("CAMARA DEL JUEGO: parejas: (%.3f, %.3f) (%.3f, %.3f) (%.3f, %.3f)", pv, yv, c1p, c1y, c2p, c2y);
    }
    vr_log("CAMARA DEL JUEGO: encontrada (%s) en %p (inclinacion %.3f, giro %.3f)", via, (void *)pitch_addr, -pv, yv);
    Beep(1200, 80); Beep(1500, 80);     /* dos pitidos agudos: camara del juego encontrada */
}

static void cj_find(void)
{
    if (g_cj_yaw || !g_cam_seq || g_cj_active) return;
    ULONG_PTR exe = (ULONG_PTR)GetModuleHandleA(NULL);
    /* lista de candidatas: primero los punteros fijos, luego lo que haya encontrado la busqueda por forma */
    ULONG_PTR cand[32]; const char *src[32]; int nc = 0;
    for (unsigned i = 0; i < sizeof(g_cj_offs) / sizeof(g_cj_offs[0]); i++) {
        ULONG_PTR base = 0; SIZE_T g = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void *)(exe + g_cj_offs[i]), &base, 8, &g) || g != 8 || !base) continue;
        cand[nc] = base + 0x650; src[nc] = "puntero fijo"; nc++;
    }
    for (int i = 0; i < g_cj_nscan && nc < 32; i++) { cand[nc] = g_cj_scan[i]; src[nc] = "busqueda por forma"; nc++; }

    static ULONG_PTR cand_a = 0; static float cand_yaw = 0;
    static int misses = 0;
    int any = 0, seen_a = 0;
    for (int i = 0; i < nc; i++) {
        float pv, yv;
        if (!rd_float((float *)cand[i], &pv) || !rd_float((float *)(cand[i] + 4), &yv)) continue;
        if (!isfinite(pv) || !isfinite(yv) || fabsf(pv) > 10.0f || fabsf(yv) > 1000.0f) continue;   /* basura */
        int nonzero = fabsf(yv) > 0.05f || fabsf(pv) > 0.05f;
        if (!(fabsf(-pv - g_cam_pitch) < 0.02f && fabsf(wrap_pi(yv - g_cam_yaw)) < 0.02f && nonzero)) continue;
        any = 1;
        /* para fiarnos: tiene que cuadrar en DOS direcciones distintas de la camara */
        if (cand_a != cand[i]) { if (!cand_a) { cand_a = cand[i]; cand_yaw = yv; seen_a = 1; } continue; }
        seen_a = 1;
        if (fabsf(wrap_pi(yv - cand_yaw)) < 0.15f) continue;
        vr_log("CAMARA DEL JUEGO: comprobada en dos direcciones (%.2f y %.2f)", cand_yaw, yv);
        cj_accept(cand[i], src[i]);
        cand_a = 0; misses = 0;
        return;
    }
    if (!seen_a) cand_a = 0;        /* la que teniamos apuntada ya no cuadra: se olvida */
    if (!any) {
        /* tras ~10 s sin ninguna que cuadre, buscar por forma (como mucho una vez por minuto) */
        static ULONGLONG lastscan = 0;
        ULONGLONG now = GetTickCount64();
        if (++misses > 40 && now - lastscan > 60000 && InterlockedCompareExchange(&g_cj_scanning, 1, 0) == 0) {
            lastscan = now; misses = 0;
            HANDLE th = CreateThread(NULL, 0, cj_sig_thread, NULL, 0, NULL);
            if (th) CloseHandle(th); else g_cj_scanning = 0;
        }
    }
    static int warned = 0;
    if (!warned++) vr_log("CAMARA DEL JUEGO: aun no la tengo confirmada; mueve el raton a los lados un poco (lo compruebo cada momento)");
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
    /* solo la camara PRINCIPAL: forma de pantalla 16:9 (las camaras de reflejos/cubos son cuadradas) */
    if (f[24] < 1.428f && f[24] > 0.3f && fabsf(f[29] / f[24] - g_aspect) < 0.03f) {
        float fy = f[9]; if (fy > 1) fy = 1; if (fy < -1) fy = -1;
        g_cam_yaw = atan2f(f[8], f[10]);
        g_cam_fx = f[8]; g_cam_fy = f[9]; g_cam_fz = f[10];
        g_cam_pitch = asinf(fy);
        g_orig_P00 = f[24]; g_orig_P11 = f[29]; g_orig_P22 = f[34]; g_orig_P23 = f[35];
        InterlockedIncrement(&g_cam_seq);
        /* camara del juego girada con la cabeza: lo que falte se pone aqui:  H = [Cd^T*Cg | -Cd^T*Ym*dg] */
        if (g_cj_active) {
            float Vn[9] = { f[0], f[1], f[2], f[4], f[5], f[6], f[8], f[9], f[10] };
            if (g_cj_dirty || memcmp(Vn, g_lastV, sizeof(Vn)) != 0) {
                memcpy(g_lastV, Vn, sizeof(Vn));
                g_cj_dirty = 0;
                float Cg[3][3], CdT[3][3], Rot[3][3], t[3], ymd[3];
                for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { Cg[i][j] = f[j * 4 + i]; CdT[i][j] = g_Cd[j][i]; }
                m3mul(CdT, Cg, Rot);
                for (int i = 0; i < 3; i++) ymd[i] = g_Ym[i][0] * g_head_dg[0] + g_Ym[i][1] * g_head_dg[1] + g_Ym[i][2] * g_head_dg[2];
                for (int i = 0; i < 3; i++) t[i] = -(CdT[i][0] * ymd[0] + CdT[i][1] * ymd[1] + CdT[i][2] * ymd[2]);
                float H[4][4], Hi[4][4];
                memset(H, 0, sizeof(H)); memset(Hi, 0, sizeof(Hi)); H[3][3] = Hi[3][3] = 1.0f;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) { H[i][j] = Rot[i][j]; Hi[i][j] = Rot[j][i]; }
                    H[i][3] = t[i];
                    Hi[i][3] = -(Rot[0][i] * t[0] + Rot[1][i] * t[1] + Rot[2][i] * t[2]);
                }
                memcpy(g_headH, H, sizeof(H));
                memcpy(g_headHi, Hi, sizeof(Hi));
                publish_H();
            }
        }
    }

    InterlockedIncrement(&g_cams);
    if (res && !g_cam_buf) {
        g_cam_buf = res;
        vr_log("El cajon de la camara es %p (ancho=%u)", res, bw);
    }
    float p00 = f[24], p11 = f[29];
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

    /* --- PRUEBA: giro "en el espacio de vista" (modos 7 y 8) --- */
    LONG mode = g_mode;
    if (mode == 1 && dst && (p00 < 1.428f || g_arma)) {      /* solo la camara ancha: el arma (57 grados) se queda quieta */
        float g[CAM_BYTES / 4], Hc[4][4], Hic[4][4];
        memcpy(g, f, sizeof(g));
        get_H(Hc, Hic);
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

typedef struct { void *res; const void *p; UINT bw; UINT type; } Pending;
static Pending g_pend[16];

static void pend_add(void *res, const void *p, UINT bw, UINT type)
{
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < 16; i++) {
        if (!g_pend[i].res || g_pend[i].res == res) {
            g_pend[i].res = res; g_pend[i].p = p; g_pend[i].bw = bw; g_pend[i].type = type;
            break;
        }
    }
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
    mul4(Pv, H, P2);
    mul4(iP, M2, T);
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

static void slot_stuff(void *res, const Pending *p)
{
    int slot = slot_of(res);
    if (slot < 0) return;
    if (g_mode == 1 && cfg_lights && cfg_fov && g_fov_ok && g_orig_P00 > 0) {
        if (slot == 7 && p->bw == 144) {
            float f[36]; SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), p->p, f, 144, &got) && got == 144 && fix_slot7(f)) {
                memcpy((void *)p->p, f, 128); _mm_sfence(); InterlockedIncrement(&g_l7);
            }
        } else if (slot == 3 && p->bw == 272) {
            float f[68]; SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), p->p, f, 272, &got) && got == 272 && fix_slot3(f)) {
                memcpy((char *)p->p + 36 * 4, f + 36, 8 * 4); _mm_sfence(); InterlockedIncrement(&g_l3);
            }
        }
    }
    InterlockedIncrement(&g_slot_writes[slot]);
    g_slot_bw[slot] = (LONG)p->bw;
    {
        LONG mode = g_mode;
        if (mode == 1 && slot == 0 && p->bw == 208) {
            float f[52], Hc[4][4], Hic[4][4]; SIZE_T got = 0;
            get_H(Hc, Hic);
            if (ReadProcessMemory(GetCurrentProcess(), p->p, f, 208, &got) && got == 208) {
                if (apply_cb0_yaw(f, Hc)) {
                    memcpy((char *)p->p + 144, f + 36, 64);   /* solo m_WVP */
                    _mm_sfence();
                    InterlockedIncrement(&g_cb0_mod);
                } else {
                    InterlockedIncrement(&g_cb0_skip);
                }
            }
        }
    }
    if (slot != 1 && g_samples_left[slot] > 0) {
        InterlockedDecrement(&g_samples_left[slot]);
        dump_floats(slot, "map", p->p, p->bw);
    }
}

static void STDMETHODCALLTYPE hk_Unmap(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub)
{
    Pending p;
    if (cfg_read && pend_take(r, &p)) {
        if (p.bw >= CAM_BYTES)
            check_camera(p.p, (void *)p.p, r, p.bw, p.type, "Map");   /* leer/cambiar ANTES de soltar */
        slot_stuff(r, &p);
        if (g_mode == 1) lens_detective(r, p.p, p.bw);
    }
    g_orig_Unmap(c, r, sub);
}

static void STDMETHODCALLTYPE hk_UpdateSub(ID3D11DeviceContext *c, ID3D11Resource *r, UINT sub,
                                           const D3D11_BOX *box, const void *src, UINT rp, UINT dp)
{
    InterlockedIncrement(&g_updsub);
    UINT bw;
    if (cfg_read && src && cb_buffer_info(r, &bw)) {
        if (!box && bw >= CAM_BYTES)
            check_camera(src, NULL, r, bw, 0, "UpdateSubresource");
        if (!box && g_mode == 1) lens_detective(r, src, bw);
        int slot = slot_of(r);
        if (slot >= 0) {
            InterlockedIncrement(&g_slot_upd[slot]);
            if (!box && g_uslot_samples[slot] > 0) {
                InterlockedDecrement(&g_uslot_samples[slot]);
                dump_floats(slot, "upd", src, bw);
            }
        }
    }
    g_orig_UpdateSub(c, r, sub, box, src, rp, dp);
}

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
static volatile LONG g_real_frames = 0;   /* fotogramas de verdad (sin los de prueba) */
static volatile LONG g_wait_us = 0;       /* tiempo esperando a SteamVR en el ultimo segundo (microsegundos) */
static void vr_after_present(void);
static int vr_active(void);

static HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    /* si nos vuelven a llamar desde dentro (bucle con la overlay), vamos directos al de fabrica */
    PFN_Present next = g_orig_Present;
    if (t_in_present) return next(sc, sync, flags);
    t_in_present = 1;
    LONG n = InterlockedIncrement(&g_frames);
    if (n <= 10)
        vr_log("Present #%ld (sync=%u flags=0x%X) Map=%ld Map_cb=%ld UpdSub=%ld",
               (long)n, sync, flags, (long)g_maps, (long)g_cbmaps, (long)g_updsub);
    int real_frame = !(flags & DXGI_PRESENT_TEST);
    if (cfg_novsync && vr_active()) sync = 0;   /* SteamVR marca el ritmo */
    if (real_frame) { InterlockedIncrement(&g_real_frames); vr_submit_frame(sc); }
    HRESULT hr = next(sc, sync, flags);
    if (real_frame) vr_after_present();
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
typedef struct { OvrMat34 mat; float vel[3]; float avel[3]; int result; unsigned char valid; unsigned char connected; } OvrPose;
typedef intptr_t (*PFN_VR_InitInternal)(int *err, int type);
typedef intptr_t (*PFN_VR_GetGenericInterface)(const char *ver, int *err);
typedef void     (*PFN_GetPose)(int origin, float predict, OvrPose *poses, unsigned count);

static PFN_VR_InitInternal        p_VR_Init = NULL;
static PFN_VR_GetGenericInterface p_VR_GetIf = NULL;
static PFN_GetPose                p_GetPose = NULL;
typedef struct { void *handle; int type; int colorspace; } OvrTexture;
typedef struct { float uMin, vMin, uMax, vMax; } OvrBounds;
typedef int (*PFN_WaitGetPoses)(OvrPose *render, unsigned nr, OvrPose *game, unsigned ng);
typedef int (*PFN_Submit)(int eye, OvrTexture *tex, OvrBounds *bounds, int flags);
static PFN_WaitGetPoses p_WaitGetPoses = NULL;
static PFN_Submit       p_Submit = NULL;
typedef unsigned char (*PFN_PollEvent)(void *ev, uint32_t size);
static PFN_PollEvent    p_PollEvent = NULL;
static int head_apply(const OvrPose *pz);
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
            p_PollEvent = (PFN_PollEvent)tbl[30];  /* PollNextEvent: SteamVR espera que la escena "lea su correo" */
            int32_t ai = -1; ((PFN_GetDXGIOutputInfo)tbl[8])(&ai); g_vr_adapter = ai;
            {
                typedef OvrMat34 (*PFN_EyeToHead)(int eye);
                for (int k = 0; k < 2; k++) {
                    OvrMat34 m = ((PFN_EyeToHead)tbl[5])(k);
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
                /* union de los dos ojos. En OpenVR "bottom" es el borde de ARRIBA y "top" el de abajo (van al reves) */
                float L = fminf(e[0][0], e[1][0]), R = fmaxf(e[0][1], e[1][1]);
                float UP = fmaxf(e[0][3], e[1][3]), DN = fminf(e[0][2], e[1][2]);
                vr_log("VISOR: lente ojo izq: %.3f %.3f %.3f %.3f | der: %.3f %.3f %.3f %.3f",
                       e[0][0], e[0][1], e[0][2], e[0][3], e[1][0], e[1][1], e[1][2], e[1][3]);
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
                vr_log("VISOR: compositor listo (%s): la imagen ira al visor", cv[i] + 8);
            }
        }
        if (!p_Submit) vr_log("VISOR: no hay compositor; solo se leera el visor");
    }
    g_head_state = 1;
}

/* ---- imagen al visor: copiamos la imagen final del juego y se la damos a SteamVR ---- */
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

static void vr_submit_frame(IDXGISwapChain *sc)
{
    if (!cfg_vrimg || g_head_state != 1 || !p_Submit) return;
    ID3D11Texture2D *bb = NULL;
    if (FAILED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb)) || !bb) return;
    D3D11_TEXTURE2D_DESC d;
    ID3D11Texture2D_GetDesc(bb, &d);
    ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL;
    ID3D11Texture2D_GetDevice(bb, &dev);
    if (dev) ID3D11Device_GetImmediateContext(dev, &ctx);
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
            LONG eye = stereo ? g_eye : 0;
            ID3D11Texture2D *dst = eye ? g_vr_tex2 : g_vr_tex;
            if (d.SampleDesc.Count > 1)
                ID3D11DeviceContext_ResolveSubresource(ctx, (ID3D11Resource *)dst, 0, (ID3D11Resource *)bb, 0, d.Format);
            else
                ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)dst, (ID3D11Resource *)bb);
            ID3D11DeviceContext_Flush(ctx);     /* que la copia llegue antes de que SteamVR la lea */
            if (g_have_frame_pose) g_eye_pose[eye] = g_frame_pose;
            if (!stereo) g_eye_pose[1] = g_eye_pose[0];
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
            if (stereo && cfg_stereo == 2 && eye == 0) goto done_submit;
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

static void cj_add(float dyaw, float dpitch_stored)
{
    for (int c = 0; c <= 2; c++) {
        if (c && !(g_cj_copy & c)) continue;
        float v;
        if (dyaw != 0 && rd_float(g_cj_yaw + 3 * c, &v)) wr_float(g_cj_yaw + 3 * c, v + dyaw);
        if (dpitch_stored != 0 && rd_float(g_cj_pitch + 3 * c, &v)) wr_float(g_cj_pitch + 3 * c, v + dpitch_stored);
    }
}

static void cj_set(float yaw, float pitch_stored)
{
    for (int c = 0; c <= 2; c++) {
        if (c && !(g_cj_copy & c)) continue;
        wr_float(g_cj_yaw + 3 * c, yaw);
        wr_float(g_cj_pitch + 3 * c, pitch_stored);
    }
}

/* Mueve la camara de verdad del juego: raton + cabeza. Una vez por fotograma (o pareja de ojos). */
static void cj_update(void)
{
    int want = cfg_cj && g_mode == 1 && g_cj_yaw && g_head_ok;
    if (!want) {
        if (g_cj_active) {
            cj_set(g_psi_prev, -g_th_prev);     /* dejar solo lo que puso el raton */
            g_dpsi = g_dtheta = 0;
            g_cj_active = 0;
            vr_log("CAMARA DEL JUEGO: suelto (vuelve a ser solo el raton)");
        }
        return;
    }
    float y, p;
    if (!rd_float(g_cj_yaw, &y) || !rd_float(g_cj_pitch, &p) || !isfinite(y) || !isfinite(p)) {
        vr_log("CAMARA DEL JUEGO: ya no se puede leer (cambio de nivel?); la busco otra vez");
        g_cj_yaw = g_cj_pitch = NULL; g_cj_active = 0; g_dpsi = g_dtheta = 0;
        return;
    }
    /* seguridad: lo que hay en esa memoria tiene que seguir cuadrando con la camara que vemos; si no, la soltamos */
    {
        static int bad = 0;
        float ey = fminf(fabsf(wrap_pi(y - g_cam_yaw)), fabsf(wrap_pi(y + g_dpsi - g_cam_yaw)));
        float ep = fminf(fabsf(-p - g_cam_pitch), fabsf(-p + g_dtheta - g_cam_pitch));
        if (ey > 0.6f || ep > 0.6f) {
            if (++bad > 30) {
                vr_log("CAMARA DEL JUEGO: ya no cuadra con la imagen (giro %.2f vs %.2f); la suelto y la vuelvo a buscar", y, g_cam_yaw);
                g_cj_yaw = g_cj_pitch = NULL; g_cj_active = 0; g_dpsi = g_dtheta = 0; bad = 0;
                return;
            }
        } else bad = 0;
    }
    /* Lo que puso el raton: o el valor menos lo nuestro (el juego lo respeto) o el valor tal cual (el juego lo
     * recalculo al mover el raton y borro lo nuestro). Nos quedamos con la opcion mas cercana a la de antes. */
    float psi_m, th_m;
    if (!g_cj_active) { psi_m = y; th_m = -p; }
    else {
        float a1 = y - g_dpsi, a2 = y;
        psi_m = fabsf(wrap_pi(a1 - g_psi_prev)) <= fabsf(wrap_pi(a2 - g_psi_prev)) ? a1 : a2;
        float b1 = -p - g_dtheta, b2 = -p;
        th_m = fabsf(b1 - g_th_prev) <= fabsf(b2 - g_th_prev) ? b1 : b2;
        if ((psi_m == a2 && fabsf(g_dpsi) > 0.01f) || (th_m == b2 && fabsf(g_dtheta) > 0.01f)) InterlockedIncrement(&g_cj_resets);
    }
    g_psi_prev = psi_m; g_th_prev = th_m;
    float Y[3][3], X[3][3], B[3][3], Cd[3][3];
    yawm(psi_m, Y); pitchm(th_m, X);
    m3mul(Y, X, B);
    m3mul(B, g_head_Rg, Cd);
    float fx = Cd[0][2], fy = Cd[1][2], fz = Cd[2][2];
    float psi_d = atan2f(fx, fz), th_d = asinf(clampf(fy, -1.0f, 1.0f));
    if (th_d > 1.40f) th_d = 1.40f;
    if (th_d < -1.40f) th_d = -1.40f;
    float dps = wrap_pi(psi_d - psi_m), dth = th_d - th_m;
    memcpy(g_Cd, Cd, sizeof(Cd)); memcpy(g_Ym, Y, sizeof(Y));
    /* dejar el valor EXACTAMENTE en raton + cabeza, haya hecho el juego lo que haya hecho */
    cj_set(y + wrap_pi(psi_m + dps - y), -(th_m + dth));
    g_dpsi = dps; g_dtheta = dth;
    g_cj_dirty = 1;
    if (!g_cj_active) { g_cj_active = 1; vr_log("CAMARA DEL JUEGO: ahora sigue tu cabeza"); }
}

/* SteamVR quiere que la "escena" le pida las posturas una vez por fotograma (tambien marca el ritmo) */
static void vr_after_present(void)
{
    if (!cfg_vrimg || g_head_state != 1 || !p_WaitGetPoses) return;
    /* el siguiente fotograma es para el otro ojo */
    {
        LONG want = cfg_stereo && cfg_fov && g_fov_ok && g_mode == 1 && g_vr_tex2;
        if (want != g_stereo_on) {
            g_stereo_on = want; g_eye = 0; g_freeze = 0;
            vr_log(">>> ESTEREO %s", !want ? "apagado" : (cfg_stereo == 2 ? "SINCRONIZADO (reloj parado en el 2o ojo)" : "alternos (un ojo por fotograma)"));
            if (want && cfg_stereo == 2 && !o_QPC) vr_log("AVISO: el reloj del juego no esta enganchado; los ojos no iran sincronizados");
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
    OvrPose rp[1];
    memset(rp, 0, sizeof(rp));
    LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0);
    p_WaitGetPoses(rp, 1, NULL, 0);
    QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
    InterlockedExchangeAdd(&g_wait_us, (LONG)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart));
    /* esta postura es la que SteamVR espera para el proximo fotograma: la usamos para dibujarlo */
    if (rp[0].valid) { g_frame_pose = rp[0].mat; g_have_frame_pose = 1; }
    if (cfg_head && head_apply(&rp[0])) publish_H();
    cj_update();
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
    memcpy(g_head_Rg, Rg, sizeof(Rg));
    memcpy(g_head_dg, dg, sizeof(dg));
    if (!g_cj_active) {
        memcpy(g_headH, H, sizeof(H));
        memcpy(g_headHi, Hi, sizeof(Hi));
    }
    g_cj_dirty = 1;
    /* para el log: hacia donde mira (columna "adelante") e inclinacion */
    g_head_yaw   = atan2f(Rg[0][2], Rg[2][2]) * RAD2DEG;
    g_head_pitch = asinf(clampf(Rg[1][2], -1.0f, 1.0f)) * RAD2DEG;
    g_head_roll  = atan2f(-Rg[1][0], Rg[1][1]) * RAD2DEG;
    g_head_dx = dg[0]; g_head_dy = dg[1]; g_head_dz = dg[2];
    return 1;
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
        int down = ((GetAsyncKeyState(VK_F9) | GetAsyncKeyState(VK_INSERT)) & 0x8000) != 0 ||
                   (rsh && (GetAsyncKeyState('P') & 0x8000) != 0);
        if (down && !prev) {
            LONG m = (g_mode + 1) % NMODES;
            g_mode = m;
            vr_log(">>> MODO %ld: %s", (long)m, mode_text(m));
            if (m == 0) Beep(300, 400); else Beep(900, 120);
        }
        prev = down;

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
        { static ULONGLONG lastr = 0; if (now - lastr > 5000) { lastr = now; LONG n = InterlockedExchange(&g_cj_resets, 0);
            if (n) vr_log("CAMARA DEL JUEGO: el juego borro lo nuestro %ld veces (al mover el raton); recolocado", (long)n); } }
        { static ULONGLONG lastfind = 0; if (cfg_cj && !g_cj_yaw && now - lastfind > 250) { lastfind = now; cj_find(); } }
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
            vr_log("seg %d: fps=%ld espera_steamvr=%ldms reloj_parado=%ld soltados=%ld luces7=%ld luces3=%ld modo=%ld estereo=%ld camaras=%ld cb1_mod=%ld cb0_mod=%ld cb0_salta=%ld no_cuadra=%ld",
                   since_cam, (long)fps, (long)(wus / 1000), (long)frz, (long)vlv, (long)InterlockedExchange(&g_l7, 0), (long)InterlockedExchange(&g_l3, 0), (long)g_mode, (long)g_stereo_on, (long)(k - pk),
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

/* -------------------------------------------------- instalar los hooks */
static void install_hooks(ID3D11DeviceContext *ctx, IDXGISwapChain *sc)
{
    if (ctx) {
        if (cfg_map) {
            PATCH(&ctx->lpVtbl->Map,   hk_Map,   g_orig_Map,   "Map");
            PATCH(&ctx->lpVtbl->Unmap, hk_Unmap, g_orig_Unmap, "Unmap");
        }
        if (cfg_updsub)
            PATCH(&ctx->lpVtbl->UpdateSubresource, hk_UpdateSub, g_orig_UpdateSub, "UpdateSubresource");
        if (cfg_vscb) {
            PATCH(&ctx->lpVtbl->VSSetConstantBuffers, hk_VSSetCB, g_orig_VSSetCB, "VSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->PSSetConstantBuffers, hk_PSSetCB, g_orig_PSSetCB, "PSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->CSSetConstantBuffers, hk_CSSetCB, g_orig_CSSetCB, "CSSetConstantBuffers");
            PATCH(&ctx->lpVtbl->CopyResource, hk_CopyRes, g_orig_CopyRes, "CopyResource");
            PATCH(&ctx->lpVtbl->CopySubresourceRegion, hk_CopySub, g_orig_CopySub, "CopySubresourceRegion");
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
HRESULT WINAPI Proxy_D3D11CreateDevice(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *featureLevels, UINT numLevels, UINT sdkVersion,
    ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppContext)
{
    vr_log("El juego llama a D3D11CreateDevice (flags=0x%X)", flags);
    HMODULE m = real_d3d11();
    PFN_D3D11_CREATE_DEVICE f = m ? (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice") : NULL;
    if (!f) { vr_log("No encuentro D3D11CreateDevice real"); return E_FAIL; }
    HRESULT hr = f(adapter, driverType, software, flags, featureLevels, numLevels,
                   sdkVersion, ppDevice, pFeatureLevel, ppContext);
    vr_log("D3D11CreateDevice -> hr=0x%08lX, nivel=0x%X", (unsigned long)hr,
           pFeatureLevel ? (unsigned)*pFeatureLevel : 0);
    if (SUCCEEDED(hr) && ppContext && *ppContext)
        install_hooks(*ppContext, NULL);
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
    if (!f) { vr_log("No encuentro D3D11CreateDeviceAndSwapChain real"); return E_FAIL; }
    HRESULT hr = f(adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion,
                   swapDesc, ppSwapChain, ppDevice, pFeatureLevel, ppContext);
    vr_log("D3D11CreateDeviceAndSwapChain -> hr=0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr) && ppContext && *ppContext)
        install_hooks(*ppContext, ppSwapChain ? *ppSwapChain : NULL);
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
            char exe[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, exe, MAX_PATH);
            vr_log("=== HITO E5b: d3d11 proxy cargada por: %s ===", exe);
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
