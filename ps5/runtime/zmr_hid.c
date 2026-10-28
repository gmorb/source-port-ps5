/* USB keyboard and mouse for the PS5 port.
 *
 * The SDL bridge only handles the DualSense, so this reads the system's
 * keyboard and mouse libraries and feeds SDL's own keyboard/mouse event
 * functions, which the engine already understands.
 *
 * libSceKeyboard and libSceMouse can't be linked or loaded by path from a
 * homebrew title; they are loaded as system modules (sceSysmoduleLoadModule)
 * and their functions looked up with sceKernelDlsym. Record layouts follow
 * the public PS4/PS5 reimplementations (OpenOrbis, shadPS4, ps5-payload-dev
 * SDL):
 *   keyboard state (96 bytes): u64 time; 8 bytes; u8 connected (+3 pad);
 *     s32 key count; u32 locks; u32 modifiers; u16 HID usages[16]; ...
 *   mouse record (40 bytes): u64 time; u8 connected (+3 pad); u32 buttons;
 *     s32 dx, dy, wheel, tilt; 8 reserved bytes
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Minimal SDL declarations (avoids needing SDL headers in this unit). */
typedef struct SDL_Window SDL_Window;
typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef int SDL_Scancode;
typedef int SDL_MouseWheelDirection;
enum { SDL_RELEASED = 0, SDL_PRESSED = 1 };
enum { SDL_BUTTON_LEFT = 1, SDL_BUTTON_MIDDLE = 2, SDL_BUTTON_RIGHT = 3 };
enum { SDL_MOUSEWHEEL_NORMAL = 0 };

int sceSysmoduleLoadModule(uint16_t id);
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *handle);
int sceKernelDlsym(int32_t handle, const char *symbol, void **address);
int sceUserServiceGetLoginUserIdList(void *list);
int sceUserServiceGetForegroundUser(int32_t *user);

/* SDL internals (static SDL build): inject device events. */
int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
int SDL_SendKeyboardText(const char *text);
int SDL_SendMouseMotion(SDL_Window *window, Uint32 mouseID, int relative, int x, int y);
int SDL_SendMouseButton(SDL_Window *window, Uint32 mouseID, Uint8 state, Uint8 button);
int SDL_SendMouseWheel(SDL_Window *window, Uint32 mouseID, float x, float y, SDL_MouseWheelDirection direction);

enum { KB_STATE = 96, MOUSE_RECORD = 40, MOUSE_RECORDS = 32, MAX_KEYS = 16 };
enum { SYSMODULE_KEYBOARD = 0x0106, SYSMODULE_MOUSE = 0x00a9 };

static int (*s_kbInit)(void);
static int (*s_kbOpen)(int32_t, int32_t, int32_t, void *);
static int (*s_kbRead)(int32_t, void *);
static int (*s_mouseInit)(void);
static int (*s_mouseOpen)(int32_t, int32_t, int32_t, void *);
static int (*s_mouseRead)(int32_t, void *, int32_t);

static int32_t s_kb = -1, s_mouse = -1, s_user = -1;
static int s_kbModule = -2, s_mouseModule = -2;
static uint8_t s_keys[MAX_KEYS], s_mods;
static uint32_t s_buttons;
static int s_kbLogged, s_mouseLogged;

/* Which device the player used last, for on-screen hints: 1 keyboard/mouse,
 * 0 controller. The engine's SDL manager reports controller use. */
static volatile int s_keyboardLast;
int zmr_input_keyboard_last(void) { return s_keyboardLast; }
void zmr_input_controller_used(void) { s_keyboardLast = 0; }

int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceKernelLoadStartModule(const char *path, size_t args, const void *argp, uint32_t flags,
                             void *opt, int *res);

/* PS5 system libraries export functions under hashed IDs (NIDs: SHA-1 of the
 * name plus a fixed salt, base64), not names, so a lookup by name fails with
 * 0x80020003. Precomputed for the functions used here. */
static const char *nid_for(const char *name)
{
    static const char *tab[][2] = {
        { "sceKeyboardInit", "wadT3QBCGY0" }, { "sceKeyboardOpen", "HJ+KnEHcaxI" },
        { "sceKeyboardReadState", "6HpE68bzX6M" },
        { "sceMouseInit", "Qs0wWulgl7U" }, { "sceMouseOpen", "RaqxZIf6DvE" },
        { "sceMouseRead", "x8qnXqh-tiM" },
        { "sceImeDialogInit", "NUeBrN7hzf0" }, { "sceImeDialogGetStatus", "IADmD4tScBY" },
        { "sceImeDialogGetResult", "x01jxu+vxlc" }, { "sceImeDialogTerm", "gyTyVn+bXMw" },
    };
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
        if (!strcmp(tab[i][0], name)) return tab[i][1];
    return NULL;
}

int sceKernelGetModuleList(int32_t *handles, size_t count, size_t *available);
const char *sceKernelGetFsSandboxRandomWord(void);
int sceKernelGetModuleInfo(int32_t handle, void *info);

/* The kernel's handle for a loaded library, found by name in the process's
 * module list (sysmodule handles are not what sceKernelDlsym takes). Module
 * info: u64 size; char name[256]; ... (PS4/PS5 layout). */
static int32_t kernel_module_by_name(const char *name)
{
    int32_t handles[256];
    size_t n = 0;
    int rc = sceKernelGetModuleList(handles, 256, &n);
    if (rc != 0) {
        printf("[zmr] module list failed: %#x\n", (unsigned)rc);
        return -1;
    }
    for (size_t i = 0; i < n && i < 256; ++i) {
        static uint64_t info[0x400 / 8];
        memset(info, 0, sizeof(info));
        info[0] = 0x160;
        if (sceKernelGetModuleInfo(handles[i], info) != 0) {
            info[0] = sizeof(info);
            if (sceKernelGetModuleInfo(handles[i], info) != 0) continue;
        }
        const char *mod = (const char *)&info[1];
        if (strstr(mod, name)) {
            printf("[zmr] %s: kernel module %#x (%s)\n", name, (unsigned)handles[i], mod);
            return handles[i];
        }
    }
    printf("[zmr] %s: not in the module list (%u modules)\n", name, (unsigned)n);
    return -1;
}


/* Looks up a function in a loaded system module: by name, then by NID in the
 * forms the loader uses. Logs what worked (or every failure). */
static int resolve(int32_t h, const char *name, void **fn)
{
    const char *nid = nid_for(name);
    char forms[4][64];
    int n = 0;
    snprintf(forms[n++], sizeof(forms[0]), "%s", name);
    if (nid) {
        snprintf(forms[n++], sizeof(forms[0]), "%s", nid);
        snprintf(forms[n++], sizeof(forms[0]), "%s#A#A", nid);
        snprintf(forms[n++], sizeof(forms[0]), "%s#C#C", nid);
    }
    int rcs[4];
    for (int i = 0; i < n; ++i) {
        *fn = NULL;
        rcs[i] = sceKernelDlsym(h, forms[i], fn);
        if (rcs[i] == 0 && *fn) {
            if (i) printf("[zmr] %s resolved as \"%s\"\n", name, forms[i]);
            return 0;
        }
    }
    printf("[zmr] %s: lookup failed (handle %#x; rc %#x %#x %#x %#x)\n", name, (unsigned)h,
           (unsigned)rcs[0], n > 1 ? (unsigned)rcs[1] : 0u, n > 2 ? (unsigned)rcs[2] : 0u, n > 3 ? (unsigned)rcs[3] : 0u);
    return -1;
}

/* System library modules: the sysmodule loader first (the way PS5 titles load
 * them), then loading the library by path. Every attempt is logged. */
static int32_t module_handle(uint16_t id)
{
    const char *name = id == 0x0106 ? "libSceKeyboard" : id == 0x00a9 ? "libSceMouse" :
                       id == 0x0096 ? "libSceImeDialog" : "?";
    int32_t h = -1;
    int load = sceSysmoduleLoadModule(id);
    int g1 = sceSysmoduleGetModuleHandleInternal(id, &h);
    if (g1 == 0 && h >= 0) goto done;
    h = -1;
    int g2 = sceSysmoduleGetModuleHandleInternal(0x80000000u | id, &h);
    if (g2 == 0 && h >= 0) goto done;
    int li = sceSysmoduleLoadModuleInternal(0x80000000u | id);
    h = -1;
    int g3 = sceSysmoduleGetModuleHandleInternal(0x80000000u | id, &h);
    if (g3 == 0 && h >= 0) goto done;
    printf("[zmr] %s: sysmodule load=%#x handle=%#x/%#x internal load=%#x handle=%#x\n", name,
           (unsigned)load, (unsigned)g1, (unsigned)g2, (unsigned)li, (unsigned)g3);
    {
        static const char *dirs[] = { "/system/common/lib/", "/system/priv/lib/", NULL };
        for (int i = 0; dirs[i]; ++i) {
            char path[96];
            int res = 0;
            snprintf(path, sizeof(path), "%s%s.sprx", dirs[i], name);
            h = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &res);
            printf("[zmr] %s: load %s = %#x\n", name, path, (unsigned)h);
            if (h >= 0) return h;
        }
    }
    return -1;
done:
    printf("[zmr] %s: sysmodule handle %#x (load=%#x)\n", name, (unsigned)h, (unsigned)load);
    {
        int32_t k = kernel_module_by_name(name);
        if (k >= 0) return k;
        /* Already loaded by the system: loading it by path may just return
         * the existing kernel handle. */
        char path[96];
        int res = 0;
        const char *word = sceKernelGetFsSandboxRandomWord();
        snprintf(path, sizeof(path), "/%s/common/lib/%s.sprx", word ? word : "system", name);
        k = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &res);
        printf("[zmr] %s: load %s = %#x\n", name, path, (unsigned)k);
        if (k >= 0) return k;
    }
    return h;
}

static int32_t first_user(void)
{
    int32_t user = -1;
    int rc = sceUserServiceGetForegroundUser(&user);
    if (rc == 0 && user >= 0) return user;
    int32_t list[4] = { -1, -1, -1, -1 };
    int rc2 = sceUserServiceGetLoginUserIdList(list);
    if (rc2 == 0)
        for (int i = 0; i < 4; ++i)
            if (list[i] != -1) return list[i];
    static int s_logged;
    if (!s_logged++) printf("[zmr] no user id (foreground %#x, list %#x)\n", (unsigned)rc, (unsigned)rc2);
    return -1;
}

static void try_open(void)
{
    if (s_user < 0) s_user = first_user();
    if (s_user < 0) return;

    if (s_kb < 0) {
        if (s_kbModule == -2) {
            s_kbModule = module_handle(SYSMODULE_KEYBOARD);
            if (s_kbModule >= 0 &&
                (resolve(s_kbModule, "sceKeyboardInit", (void **)&s_kbInit) ||
                 resolve(s_kbModule, "sceKeyboardOpen", (void **)&s_kbOpen) ||
                 resolve(s_kbModule, "sceKeyboardReadState", (void **)&s_kbRead)))
                s_kbModule = -1;
            if (s_kbModule >= 0) s_kbInit();
            printf("[zmr] keyboard module %d\n", s_kbModule);
        }
        if (s_kbModule >= 0) {
            uint8_t param[8] = { 0 };
            s_kb = s_kbOpen(s_user, 0, 0, param);
            if (s_kb >= 0 || !s_kbLogged) printf("[zmr] keyboard open: %#x\n", (unsigned)s_kb);
            s_kbLogged = 1;
        }
    }
    if (s_mouse < 0) {
        if (s_mouseModule == -2) {
            s_mouseModule = module_handle(SYSMODULE_MOUSE);
            if (s_mouseModule >= 0 &&
                (resolve(s_mouseModule, "sceMouseInit", (void **)&s_mouseInit) ||
                 resolve(s_mouseModule, "sceMouseOpen", (void **)&s_mouseOpen) ||
                 resolve(s_mouseModule, "sceMouseRead", (void **)&s_mouseRead)))
                s_mouseModule = -1;
            if (s_mouseModule >= 0) s_mouseInit();
            printf("[zmr] mouse module %d\n", s_mouseModule);
        }
        if (s_mouseModule >= 0) {
            uint8_t param[8] = { 1 };   /* all mice through one handle */
            s_mouse = s_mouseOpen(s_user, 0, 0, param);
            if (s_mouse < 0) {
                param[0] = 0;
                s_mouse = s_mouseOpen(s_user, 0, 0, param);
            }
            if (s_mouse >= 0 || !s_mouseLogged) printf("[zmr] mouse open: %#x\n", (unsigned)s_mouse);
            s_mouseLogged = 1;
        }
    }
}

/* US layout: the character a key press types (text boxes and the console
 * take characters from SDL text events, not from key presses). */
static char key_char(uint8_t usage, int shift)
{
    static const char digits[] = "1234567890", shifted_digits[] = "!@#$%^&*()";
    if (usage >= 0x04 && usage <= 0x1D) return (char)((shift ? 'A' : 'a') + usage - 0x04);
    if (usage >= 0x1E && usage <= 0x27) return (shift ? shifted_digits : digits)[usage - 0x1E];
    switch (usage) {
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    }
    return 0;
}

static int has_key(const uint8_t *keys, uint8_t k)
{
    for (int i = 0; i < MAX_KEYS; ++i)
        if (keys[i] == k) return 1;
    return 0;
}

static void poll_keyboard(void)
{
    uint8_t st[KB_STATE];
    uint8_t now[MAX_KEYS] = { 0 };
    uint8_t mods = 0;
    memset(st, 0, sizeof(st));
    if (s_kbRead(s_kb, st) != 0) return;
    if (st[16]) {
        int32_t n;
        uint32_t m;
        memcpy(&n, st + 20, 4);
        memcpy(&m, st + 28, 4);
        if (n < 0) n = 0;
        if (n > MAX_KEYS) n = MAX_KEYS;
        mods = (uint8_t)m;
        for (int i = 0; i < n; ++i) {
            uint16_t u;
            memcpy(&u, st + 32 + 2 * i, 2);
            now[i] = (u > 3 && u < 0xE0) ? (uint8_t)u : 0;  /* skip error codes and modifiers */
        }
    }
    /* Modifiers: bit n is HID usage 0xE0 + n (LCtrl, LShift, LAlt, LGui, R...). */
    uint8_t diff = mods ^ s_mods;
    for (int b = 0; b < 8; ++b)
        if (diff & (1u << b))
            SDL_SendKeyboardKey((mods & (1u << b)) ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)(0xE0 + b));
    for (int i = 0; i < MAX_KEYS; ++i)
        if (s_keys[i] && !has_key(now, s_keys[i]))
            SDL_SendKeyboardKey(SDL_RELEASED, (SDL_Scancode)s_keys[i]);
    for (int i = 0; i < MAX_KEYS; ++i)
        if (now[i] && !has_key(s_keys, now[i])) {
            SDL_SendKeyboardKey(SDL_PRESSED, (SDL_Scancode)now[i]);
            s_keyboardLast = 1;
            /* No text while Ctrl, Alt or GUI is held (shortcuts). */
            char c = (mods & 0xDD) ? 0 : key_char(now[i], (mods & 0x22) != 0);
            if (c && c != '`') {   /* ` toggles the console; don't type it */
                char text[2] = { c, 0 };
                SDL_SendKeyboardText(text);
            }
        }
    memcpy(s_keys, now, sizeof(now));
    s_mods = mods;
}

static void poll_mouse(SDL_Window *window)
{
    uint8_t rec[MOUSE_RECORDS * MOUSE_RECORD];
    int got = s_mouseRead(s_mouse, rec, MOUSE_RECORDS);
    if (got <= 0) return;
    if (got > MOUSE_RECORDS) got = MOUSE_RECORDS;
    int32_t dx = 0, dy = 0, wheel = 0;
    uint64_t newest = 0;
    uint32_t buttons = s_buttons;
    for (int i = 0; i < got; ++i) {
        const uint8_t *r = rec + i * MOUSE_RECORD;
        if (!r[8]) continue;
        int32_t v;
        uint64_t t;
        memcpy(&v, r + 16, 4); dx += v;
        memcpy(&v, r + 20, 4); dy += v;
        memcpy(&v, r + 24, 4); wheel += v;
        memcpy(&t, r, 8);
        if (t >= newest) { newest = t; memcpy(&buttons, r + 12, 4); buttons &= 7; }
    }
    if (dx || dy) SDL_SendMouseMotion(window, 0, 1, dx, dy);
    if (dx * dx + dy * dy > 25 || wheel || buttons != s_buttons) s_keyboardLast = 1;
    if (wheel) SDL_SendMouseWheel(window, 0, 0, (float)wheel, SDL_MOUSEWHEEL_NORMAL);
    static const Uint8 map[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
    for (int b = 0; b < 3; ++b)
        if ((buttons ^ s_buttons) & (1u << b))
            SDL_SendMouseButton(window, 0, (buttons & (1u << b)) ? SDL_PRESSED : SDL_RELEASED, map[b]);
    s_buttons = buttons;
}

/* ---- System on-screen keyboard (IME dialog) ------------------------------
 * Typed text is sent to the focused text box, followed by Enter. Param
 * layout as in OpenOrbis' OrbisImeDialogParam (UTF-16 text). */
enum { SYSMODULE_IME_DIALOG = 0x0096, OSK_MAX = 256 };
typedef struct {
    int32_t userId;
    int32_t type;
    uint64_t supportedLanguages;
    int32_t enterLabel;
    int32_t inputMethod;
    void *filter;
    uint32_t option;
    uint32_t maxTextLength;
    uint16_t *inputTextBuffer;
    float posx, posy;
    int32_t halign, valign;
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} ImeDialogParam;
typedef struct { int32_t endstatus; int8_t reserved[12]; } ImeDialogResult;

static int (*s_imeInit)(const ImeDialogParam *, void *);
static int (*s_imeStatus)(void);
static int (*s_imeResult)(ImeDialogResult *);
static int (*s_imeTerm)(void);
static int s_imeModule = -2, s_imeOpen;
static uint16_t s_imeText[OSK_MAX + 1], s_imeTitle[64];

static int ime_ready(void)
{
    if (s_imeModule == -2) {
        s_imeModule = module_handle(SYSMODULE_IME_DIALOG);
        if (s_imeModule >= 0 &&
            (resolve(s_imeModule, "sceImeDialogInit", (void **)&s_imeInit) ||
             resolve(s_imeModule, "sceImeDialogGetStatus", (void **)&s_imeStatus) ||
             resolve(s_imeModule, "sceImeDialogGetResult", (void **)&s_imeResult) ||
             resolve(s_imeModule, "sceImeDialogTerm", (void **)&s_imeTerm)))
            s_imeModule = -1;
        printf("[zmr] on-screen keyboard module %d\n", s_imeModule);
    }
    return s_imeModule >= 0;
}

int zmr_osk_is_open(void) { return s_imeOpen; }

/* Triangle in a menu asks for the command menu (game/client/ps5_commandmenu.inc
 * polls this from its UI tick). */
static volatile int s_menuRequests;
void zmr_request_command_menu(void) { s_menuRequests++; }
int zmr_take_command_menu_request(void)
{
    if (!s_menuRequests) return 0;
    s_menuRequests = 0;
    return 1;
}

/* Opens the system keyboard; returns 0 when it is showing. */
int zmr_osk_open(void)
{
    printf("[zmr] on-screen keyboard requested (open=%d user=%d)\n", s_imeOpen, (int)s_user);
    if (s_imeOpen) return 0;
    if (s_user < 0) s_user = first_user();
    if (s_user < 0 || !ime_ready()) return -1;
    static const char title[] = "Enter text or a console command";
    for (size_t i = 0; i < sizeof(title); ++i) s_imeTitle[i] = (uint16_t)title[i];
    memset(s_imeText, 0, sizeof(s_imeText));
    ImeDialogParam p;
    memset(&p, 0, sizeof(p));
    p.userId = s_user;
    p.type = 0;                       /* default */
    p.maxTextLength = OSK_MAX;
    p.inputTextBuffer = s_imeText;
    p.posx = 960.0f;
    p.posy = 540.0f;
    p.halign = 1;                     /* center */
    p.valign = 1;                     /* center */
    p.title = s_imeTitle;
    int rc = s_imeInit(&p, NULL);
    printf("[zmr] on-screen keyboard open: %#x\n", (unsigned)rc);
    if (rc != 0) return -1;
    s_imeOpen = 1;
    return 0;
}

static void osk_poll(void)
{
    if (!s_imeOpen) return;
    int status = s_imeStatus();
    if (status != 2) return;          /* 2 = finished */
    ImeDialogResult r;
    memset(&r, 0, sizeof(r));
    s_imeResult(&r);
    s_imeTerm();
    s_imeOpen = 0;
    printf("[zmr] on-screen keyboard closed: %d\n", r.endstatus);
    if (r.endstatus != 0) return;     /* cancelled */
    /* UTF-16 -> UTF-8 */
    char out[OSK_MAX * 3 + 1];
    size_t o = 0;
    for (int i = 0; i < OSK_MAX && s_imeText[i]; ++i) {
        uint32_t c = s_imeText[i];
        if (c >= 0xD800 && c <= 0xDBFF && s_imeText[i + 1] >= 0xDC00 && s_imeText[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s_imeText[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        if (o > sizeof(out) - 5) break;
    }
    out[o] = 0;
    if (o) SDL_SendKeyboardText(out);
    SDL_SendKeyboardKey(SDL_PRESSED, 0x28);   /* Enter */
    SDL_SendKeyboardKey(SDL_RELEASED, 0x28);
}

/* Opens or closes the developer console (the ` key). */
void zmr_toggle_console(void)
{
    SDL_SendKeyboardKey(SDL_PRESSED, 0x35);
    SDL_SendKeyboardKey(SDL_RELEASED, 0x35);
}

/* Called early in main(): load the system modules before anything else
 * changes the process (the approach PS5 ports that work on hardware use). */
void zmr_hid_preload(void)
{
    s_kbModule = module_handle(SYSMODULE_KEYBOARD);
    if (s_kbModule >= 0 &&
        (resolve(s_kbModule, "sceKeyboardInit", (void **)&s_kbInit) ||
         resolve(s_kbModule, "sceKeyboardOpen", (void **)&s_kbOpen) ||
         resolve(s_kbModule, "sceKeyboardReadState", (void **)&s_kbRead)))
        s_kbModule = -1;
    if (s_kbModule >= 0) s_kbInit();
    s_mouseModule = module_handle(SYSMODULE_MOUSE);
    if (s_mouseModule >= 0 &&
        (resolve(s_mouseModule, "sceMouseInit", (void **)&s_mouseInit) ||
         resolve(s_mouseModule, "sceMouseOpen", (void **)&s_mouseOpen) ||
         resolve(s_mouseModule, "sceMouseRead", (void **)&s_mouseRead)))
        s_mouseModule = -1;
    if (s_mouseModule >= 0) s_mouseInit();
    ime_ready();
    printf("[zmr] input modules: keyboard %d mouse %d on-screen keyboard %d\n",
           s_kbModule, s_mouseModule, s_imeModule);
}

/* Called once per event pump from the engine's SDL manager. */
void zmr_hid_poll(SDL_Window *window)
{
    static struct timespec s_lastTry;
    static int s_first = 1;
    struct timespec now;
    if (s_first) { s_first = 0; printf("[zmr] input: keyboard/mouse polling started\n"); }
    if ((s_kb < 0 || s_mouse < 0)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - s_lastTry.tv_sec >= 3) {   /* retry for hot-plugged devices */
            s_lastTry = now;
            try_open();
        }
    }
    osk_poll();
    if (s_kb >= 0) poll_keyboard();
    if (s_mouse >= 0 && window) poll_mouse(window);
}

/* ---- ps5_skip_shader: leave out draws by shader name ----------------------
 * Set from the console (game/client/ps5_commandmenu.inc); togl asks before
 * every draw. Up to four comma-separated name fragments. */
static char s_skip[4][64];
static volatile int s_skipCount;

void zmr_set_skip_shader(const char *list)
{
    s_skipCount = 0;
    memset(s_skip, 0, sizeof(s_skip));
    int n = 0;
    while (list && *list && n < 4) {
        while (*list == ' ' || *list == ',') ++list;
        int len = 0;
        while (list[len] && list[len] != ',' && len < 63) ++len;
        while (len > 0 && list[len - 1] == ' ') --len;
        if (len > 0) { memcpy(s_skip[n], list, len); s_skip[n][len] = 0; ++n; }
        list += len;
        while (*list && *list != ',') ++list;
    }
    s_skipCount = n;
    printf("[zmr] skipping shaders: %d entr%s\n", n, n == 1 ? "y" : "ies");
}

int zmr_shader_skipped(const char *vs, const char *ps)
{
    if (!s_skipCount) return 0;
    for (int i = 0; i < s_skipCount; ++i)
        if ((vs && strstr(vs, s_skip[i])) || (ps && strstr(ps, s_skip[i])))
            return 1;
    return 0;
}

/* ---- First draw of each shader pair ---------------------------------------
 * togl calls this before every draw; a pair's first draw is when the driver
 * compiles it, so the last name printed before a GPU hang names the effect. */
static const void *s_seenPairs[8192];
void zmr_note_draw(const void *pair, const char *vs, const char *ps)
{
    uintptr_t h = ((uintptr_t)pair >> 4) * 2654435761u;
    for (unsigned i = 0; i < 8192; ++i) {
        unsigned slot = (unsigned)((h + i) & 8191);
        if (s_seenPairs[slot] == pair) return;
        if (!s_seenPairs[slot]) {
            s_seenPairs[slot] = pair;
            printf("[ps5-shader] first draw vs=%s ps=%s\n", vs, ps);
            return;
        }
    }
}

/* ---- Draw breadcrumbs ------------------------------------------------------
 * The material system names the material being drawn and togl records each
 * draw's shaders here, in a ring the hang watchdog prints. With
 * ps5_sync_draws on, togl waits for the GPU after every draw, so a GPU hang
 * stops the game on the draw responsible and the ring's last entry names it. */
typedef struct { const char *material, *vs, *ps; unsigned long frame; } Crumb;
static Crumb s_crumbs[64];
static volatile unsigned s_crumbNext;
static const char *volatile s_curMaterial = "?";
static volatile int s_syncDraws;
extern unsigned long zmr_frame_count(void);

void zmr_set_material(const char *name) { s_curMaterial = name ? name : "?"; }
void zmr_set_sync_draws(int on) { s_syncDraws = on; printf("[zmr] sync draws %s\n", on ? "on" : "off"); }
/* Counted lock-step: wait for the GPU after each of the next N draws (used
 * when a monitor camera first renders; see viewrender.cpp). */
static volatile int s_syncCount;
void zmr_sync_next_draws(int n) { s_syncCount = n; printf("[zmr] lock-step for the next %d draws\n", n); }
int zmr_sync_draws(void)
{
    if (s_syncCount > 0) { s_syncCount--; return 1; }
    return s_syncDraws;
}

/* ---- Persistent breadcrumbs -------------------------------------------------
 * A GPU hang stops the whole process at once, so nothing can be written
 * after it. These draw records live in a file mapped MAP_SHARED: every store
 * lands in the kernel's page cache, which survives the process being stopped
 * and killed. The next launch prints the draws of the GPU batch that never
 * finished (ps5_gl_batches_retired + 1), i.e. the draws the GPU hung on. */
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#define PCRUMBS 32768
typedef struct {
    uint32_t seq, frame, batch, retired;
    char phase[8], material[72], vs[20], ps[20];
} PCrumb;   /* 128 bytes */
typedef struct {
    char magic[8];
    volatile uint32_t next, pad;
    volatile uint64_t submitted, retired;
    /* Watchdog heartbeat (every 250 ms, plain memory stores): tells whether
     * the process kept running after the last draw. */
    volatile uint64_t wd_ticks, wd_last_next, wd_idle_ticks, stall_rip, stall_frames;
    volatile uint32_t stall_dumps, pad2;
    char reserved[48];
    PCrumb ring[PCRUMBS];
} PCrumbFile;
static PCrumbFile *s_pcrumbs;
/* Off by default in releases (each record costs a little); turned on by
 * ps5_freeze_recorder 1 or -ps5_freeze_recorder in args.txt. */
static volatile int s_crumbsOn;
void zmr_crumbs_enable(int on)
{
    s_crumbsOn = on;
    printf("[zmr] freeze recorder %s%s\n", on ? "on" : "off", s_pcrumbs ? "" : " (crumbs.bin unavailable)");
}
static const char *volatile s_phase = "main";
extern volatile unsigned long ps5_gl_batches_submitted, ps5_gl_batches_retired;
void zmr_set_phase(const char *phase) { s_phase = phase ? phase : "?"; }

static unsigned thread_tag(void)
{
    static __thread unsigned tag;
    static volatile unsigned next_tag;
    if (!tag) tag = __atomic_add_fetch(&next_tag, 1, __ATOMIC_RELAXED);
    return tag;
}

static void copy_tail(char *dst, size_t cap, const char *src)
{
    if (!src) src = "?";
    size_t n = strlen(src);
    if (n >= cap) src += n - (cap - 1), n = cap - 1;   /* keep the distinctive end */
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void zmr_crumb_persist(const char *vs, const char *ps)
{
    PCrumbFile *f = s_pcrumbs;
    if (!f || !s_crumbsOn) return;
    uint32_t i = f->next;
    PCrumb *c = &f->ring[i % PCRUMBS];
    c->seq = i;
    c->frame = (uint32_t)zmr_frame_count();
    c->batch = (uint32_t)ps5_gl_batches_submitted + 1;
    c->retired = (uint32_t)ps5_gl_batches_retired;
    snprintf(c->phase, sizeof(c->phase), "%.4s:%u", s_phase, thread_tag() % 100);
    copy_tail(c->material, sizeof(c->material), s_curMaterial);
    copy_tail(c->vs, sizeof(c->vs), vs);
    copy_tail(c->ps, sizeof(c->ps), ps);
    f->submitted = ps5_gl_batches_submitted;
    f->retired = ps5_gl_batches_retired;
    f->next = i + 1;
}

/* Driver/launcher milestones (blits, presents, submissions) in the same ring,
 * phase "drv", so a hang outside GL draws is still placed. */
void zmr_crumb_note(const char *what, unsigned a, unsigned b)
{
    PCrumbFile *f = s_pcrumbs;
    if (!f || !s_crumbsOn) return;
    uint32_t i = f->next;
    PCrumb *c = &f->ring[i % PCRUMBS];
    c->seq = i;
    c->frame = (uint32_t)zmr_frame_count();
    c->batch = (uint32_t)ps5_gl_batches_submitted + 1;
    c->retired = (uint32_t)ps5_gl_batches_retired;
    snprintf(c->phase, sizeof(c->phase), "drv:%u", thread_tag() % 100);
    snprintf(c->material, sizeof(c->material), "%s %u %u", what ? what : "?", a, b);
    c->vs[0] = c->ps[0] = 0;
    f->submitted = ps5_gl_batches_submitted;
    f->retired = ps5_gl_batches_retired;
    f->next = i + 1;
}

/* Watchdog thread: called every 250 ms. */
void zmr_crumbs_heartbeat(void)
{
    PCrumbFile *f = s_pcrumbs;
    if (!f || !s_crumbsOn) return;
    uint64_t next = f->next;
    if (next == f->wd_last_next) f->wd_idle_ticks++;
    else { f->wd_last_next = next; f->wd_idle_ticks = 0; }
    f->wd_ticks++;
}
/* Main-thread stack sample taken by the watchdog's signal. */
void zmr_crumbs_stall(unsigned long rip_offset, unsigned long frames)
{
    PCrumbFile *f = s_pcrumbs;
    if (!f) return;
    f->stall_rip = rip_offset;
    f->stall_frames = frames;
    f->stall_dumps++;
}

/* Called once at start-up with the data folder. Reports the previous
 * session's file (if it ended in a hang) through out(), then starts a new one. */
void zmr_crumbs_init(const char *dir, void (*out)(const char *))
{
    char path[300], prev[300], line[400];
    snprintf(path, sizeof(path), "%s/crumbs.bin", dir);
    snprintf(prev, sizeof(prev), "%s/crumbs-prev.bin", dir);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        static PCrumbFile old;
        ssize_t n = read(fd, &old, sizeof(old));
        close(fd);
        if (n == (ssize_t)sizeof(old) && !memcmp(old.magic, "ZMRCRMB", 8) && old.next) {
            uint32_t end = old.next, count = end < PCRUMBS ? end : PCRUMBS;
            uint32_t stuck = (uint32_t)old.retired + 1;
            snprintf(line, sizeof(line), "[crumbs] previous session: %u draws recorded, GPU batches submitted %llu, finished %llu (GPU %s on batch %u)\n",
                     end, (unsigned long long)old.submitted, (unsigned long long)old.retired,
                     old.submitted > old.retired ? "was busy" : "was idle", stuck);
            out(line);
            snprintf(line, sizeof(line), "[crumbs] watchdog heartbeats %llu; after the last record it kept running for %.2f s%s; main-thread samples %u (last at image offset %#llx, frame %llu)\n",
                     (unsigned long long)old.wd_ticks, old.wd_idle_ticks * 0.25,
                     old.wd_idle_ticks >= 8 ? " (process alive, main thread stuck)" : " (whole process stopped)",
                     old.stall_dumps, (unsigned long long)old.stall_rip, (unsigned long long)old.stall_frames);
            out(line);
            /* Per-frame summary of the last frames in the ring, and
             * materials/notes that appear only in the final frames. */
            {
                uint32_t lastFrame = old.ring[(end - 1) % PCRUMBS].frame;
                for (uint32_t fr = lastFrame > 6 ? lastFrame - 6 : 0; fr <= lastFrame; ++fr) {
                    unsigned draws = 0, notes = 0, resAdd = 0, resDel = 0, maps = 0;
                    for (uint32_t k = end - count; k != end; ++k) {
                        const PCrumb *c = &old.ring[k % PCRUMBS];
                        if (c->frame != fr) continue;
                        if (strncmp(c->phase, "drv", 3)) draws++;
                        else {
                            notes++;
                            if (!strncmp(c->material, "res+", 4)) resAdd++;
                            else if (!strncmp(c->material, "res-", 4)) resDel++;
                            else if (!strncmp(c->material, "map ", 4)) maps++;
                        }
                    }
                    snprintf(line, sizeof(line), "[crumbs] frame %u: %u draws, %u driver notes (%u created, %u destroyed, %u maps)\n",
                             fr, draws, notes, resAdd, resDel, maps);
                    out(line);
                }
                /* Materials drawn in the last 2 frames that the ring has not
                 * seen in any earlier frame. */
                unsigned shown = 0;
                for (uint32_t k = end - count; k != end && shown < 60; ++k) {
                    const PCrumb *c = &old.ring[k % PCRUMBS];
                    if (c->frame + 1 < lastFrame || !strncmp(c->phase, "drv", 3)) continue;
                    int seen = 0;
                    for (uint32_t j = end - count; j != end && !seen; ++j) {
                        const PCrumb *d = &old.ring[j % PCRUMBS];
                        if (d->frame + 1 < lastFrame && !strcmp(d->material, c->material)) seen = 1;
                    }
                    for (uint32_t j = end - count; j != k && !seen; ++j) {   /* print once */
                        const PCrumb *d = &old.ring[j % PCRUMBS];
                        if (d->frame + 1 >= lastFrame && !strcmp(d->material, c->material)) seen = 1;
                    }
                    if (!seen) {
                        snprintf(line, sizeof(line), "[crumbs] new in last frames: frame %u %s %s (%s / %s)\n",
                                 c->frame, c->phase, c->material, c->vs, c->ps);
                        out(line);
                        shown++;
                    }
                }
            }
            /* Sound starts and audio-thread notes from the whole ring
             * (the last 30), with how many records before the end. */
            {
                uint32_t idx[30]; unsigned n = 0;
                for (uint32_t k = end; k != end - count && n < 30; ) {
                    --k;
                    const PCrumb *c = &old.ring[k % PCRUMBS];
                    if (!strncmp(c->material, "snd ", 4) || !strncmp(c->material, "audio", 5))
                        idx[n++] = k;
                }
                while (n) {
                    const PCrumb *c = &old.ring[idx[--n] % PCRUMBS];
                    snprintf(line, sizeof(line), "[crumbs] sound: frame %u (%u records before the end) %s %s\n",
                             c->frame, end - 1 - idx[n], c->phase, c->material);
                    out(line);
                }
            }
            /* Draws of the unfinished batch, plus the 40 draws before it. */
            uint32_t first = end - count;
            for (uint32_t k = end - count; k != end; ++k) {
                const PCrumb *c = &old.ring[k % PCRUMBS];
                if (c->batch >= stuck) { first = k; break; }
            }
            first = first - (end - count) > 200 ? first - 200 : end - count;
            unsigned printed = 0;
            for (uint32_t k = first; k != end && printed < 400; ++k, ++printed) {
                const PCrumb *c = &old.ring[k % PCRUMBS];
                snprintf(line, sizeof(line), "[crumbs] #%u frame %u batch %u%s %-6s %s (%s / %s)\n",
                         c->seq, c->frame, c->batch, c->batch == stuck ? " <-STUCK" : (c->batch > stuck ? " (queued)" : ""),
                         c->phase, c->material, c->vs, c->ps);
                out(line);
            }
            snprintf(line, sizeof(line), "[crumbs] end (%u more draws after these not shown)\n",
                     end - first > printed ? end - first - printed : 0);
            out(line);
        }
        unlink(prev);
        rename(path, prev);
    }
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { out("[crumbs] could not create crumbs.bin\n"); return; }
    static char zero[65536];
    size_t left = sizeof(PCrumbFile);
    while (left) {
        size_t chunk = left < sizeof(zero) ? left : sizeof(zero);
        if (write(fd, zero, chunk) != (ssize_t)chunk) { close(fd); out("[crumbs] write failed\n"); return; }
        left -= chunk;
    }
    void *m = mmap(NULL, sizeof(PCrumbFile), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { out("[crumbs] mmap failed\n"); return; }
    PCrumbFile *f = (PCrumbFile *)m;
    memcpy(f->magic, "ZMRCRMB", 8);
    s_pcrumbs = f;
    out("[crumbs] recording draws to crumbs.bin\n");
}

static volatile int s_traceDraws;
void zmr_breadcrumb_draw(const char *vs, const char *ps)
{
    if (s_traceDraws) {
        printf("[trace] draw %s (%s / %s)\n", s_curMaterial ? s_curMaterial : "?", vs ? vs : "?", ps ? ps : "?");
        fflush(stdout);
        if (s_traceDraws > 0) s_traceDraws--;   /* counted mode; -1 = until switched off */
    }
    Crumb *c = &s_crumbs[s_crumbNext & 63];
    c->material = s_curMaterial;
    c->vs = vs;
    c->ps = ps;
    c->frame = zmr_frame_count();
    s_crumbNext++;
    zmr_crumb_persist(vs, ps);
}

/* Writes the last draws with the given writer (signal/thread safe enough:
 * only reads pointers to static strings). */
void zmr_breadcrumbs_dump(void (*out)(const char *))
{
    char line[400];
    unsigned end = s_crumbNext, n = end < 32 ? end : 32;
    snprintf(line, sizeof(line), "[zmr] last %u draws (oldest first, sync draws %s):\n", n, s_syncDraws ? "on" : "off");
    out(line);
    for (unsigned i = end - n; i != end; ++i) {
        const Crumb *c = &s_crumbs[i & 63];
        snprintf(line, sizeof(line), "[zmr]   frame %lu  %s  (%s / %s)\n", c->frame,
                 c->material ? c->material : "?", c->vs ? c->vs : "?", c->ps ? c->ps : "?");
        out(line);
    }
}

/* ---- Monitor trace -----------------------------------------------------------
 * A GPU memory fault stops the whole process, so nothing can report after
 * the fact. While tracing, every step and draw is written to zmr.log first
 * (write() completes before the draw is issued) and the GPU is waited on
 * after it, so the log's last line names what faulted. */
void glFinish(void);
/* on: 1 = trace until switched off, N > 1 = trace the next N draws, 0 = off */
void zmr_trace_draws(int on) { s_traceDraws = on == 1 ? -1 : on; }
int zmr_tracing_draws(void) { return s_traceDraws; }
void zmr_trace(const char *what)
{
    printf("[trace] %s\n", what);
    fflush(stdout);
    glFinish();
    printf("[trace] %s: GPU done\n", what);
    fflush(stdout);
}

/* ---- ps5_skip_material: leave out materials by name ----------------------- */
static char s_skipMat[4][96];
static volatile int s_skipMatCount;
void zmr_set_skip_material(const char *list)
{
    int n = 0;
    memset(s_skipMat, 0, sizeof(s_skipMat));
    while (list && *list && n < 4) {
        while (*list == ' ' || *list == ',') ++list;
        int len = 0;
        while (list[len] && list[len] != ',' && len < 95) ++len;
        while (len > 0 && list[len - 1] == ' ') --len;
        if (len > 0) { memcpy(s_skipMat[n], list, len); s_skipMat[n][len] = 0; ++n; }
        list += len;
        while (*list && *list != ',') ++list;
    }
    s_skipMatCount = n;
    printf("[zmr] skipping materials: %d entr%s\n", n, n == 1 ? "y" : "ies");
}
int zmr_material_skipped(const char *name)
{
    if (!s_skipMatCount || !name) return 0;
    for (int i = 0; i < s_skipMatCount; ++i)
        if (strstr(name, s_skipMat[i])) return 1;
    return 0;
}
