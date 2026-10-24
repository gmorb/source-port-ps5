/* Two-layer file system for the PS5 app sandbox.
 *
 * Native titles can read their own folder (/app0, read-only) and write
 * /download0. The engine wants one writable game directory, so it runs in
 * ZMR_RW_ROOT and every path under it is resolved like an overlay:
 *   - reads use the writable copy if present, else the packaged one in
 *     ZMR_RO_ROOT (the "game" folder inside the app folder);
 *   - writes always go to the writable layer (parent dirs created on demand);
 *   - directory listings merge both layers.
 * Implemented with ld --wrap so every library in the executable sees it.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Writable root, chosen at startup by ps5_main.cpp (empty = overlay off). */
char zmr_rw_root[256] = "";
#define ZMR_RW_ROOT zmr_rw_root
/* Read-only game files, chosen at startup by zmr_fs_init(). */
char zmr_ro_root[256] = "/app0/game";
#define ZMR_RO_ROOT zmr_ro_root

int __real_open(const char *, int, ...);
FILE *__real_fopen(const char *, const char *);
FILE *__real_freopen(const char *, const char *, FILE *);
int __real_stat(const char *, struct stat *);
int __real_lstat(const char *, struct stat *);
int __real_access(const char *, int);
int __real_mkdir(const char *, mode_t);
int __real_unlink(const char *);
int __real_remove(const char *);
int __real_rename(const char *, const char *);
int __real_chmod(const char *, mode_t);
int __real_rmdir(const char *);
DIR *__real_opendir(const char *);
struct dirent *__real_readdir(DIR *);
int __real_closedir(DIR *);
char *__real_realpath(const char *, char *);

/* The native-app libc has no working chdir(), so the current directory is
 * tracked here and every wrapped call resolves relative paths against it. */
static char zmr_cwd[PATH_MAX] = "/";

/* Absolute, normalized version of path (no "." / ".." / "//"). */
static void absolute(const char *path, char *out)
{
    char tmp[PATH_MAX * 2];
    if (path[0] == '/') {
        snprintf(tmp, sizeof(tmp), "%s", path);
    } else {
        snprintf(tmp, sizeof(tmp), "%s/%s", zmr_cwd, path);
    }
    for (char *p = tmp; *p; ++p) if (*p == '\\') *p = '/';
    char *parts[512]; int n = 0;
    for (char *save = NULL, *tok = strtok_r(tmp, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) { if (n) --n; continue; }
        if (n < 512) parts[n++] = tok;
    }
    out[0] = 0;
    size_t len = 0;
    for (int i = 0; i < n; ++i) len += snprintf(out + len, PATH_MAX - len, "/%s", parts[i]);
    if (!n) strcpy(out, "/");
}

/* If path is inside the writable root, return the part after it (may be ""). */
static const char *overlay_rel(const char *abs)
{
    size_t n = strlen(ZMR_RW_ROOT);
    if (!n || strncmp(abs, ZMR_RW_ROOT, n)) return NULL;
    if (abs[n] == 0) return abs + n;
    if (abs[n] == '/') return abs + n + 1;
    return NULL;
}

static int exists(const char *p) { struct stat st; return __real_stat(p, &st) == 0; }

/* Resolve for reading: writable copy, else packaged copy, else writable path. */
static const char *resolve_read(const char *path, char *buf)
{
    if (!path) return path;
    char abs[PATH_MAX];
    absolute(path, abs);
    const char *rel = overlay_rel(abs);
    if (!rel) return path;
    if (exists(abs)) { strcpy(buf, abs); return buf; }
    char ro[PATH_MAX];
    snprintf(ro, sizeof(ro), "%s%s%s", ZMR_RO_ROOT, *rel ? "/" : "", rel);
    if (exists(ro)) { strcpy(buf, ro); return buf; }
    strcpy(buf, abs);
    return buf;
}

static void mkdirs_for(const char *file)
{
    char p[PATH_MAX];
    snprintf(p, sizeof(p), "%s", file);
    for (char *s = p + 1; *s; ++s) {
        if (*s != '/') continue;
        *s = 0;
        __real_mkdir(p, 0777);
        *s = '/';
    }
}

/* Resolve for writing: always the writable layer, parents created. */
static const char *resolve_write(const char *path, char *buf)
{
    if (!path) return path;
    char abs[PATH_MAX];
    absolute(path, abs);
    if (!overlay_rel(abs)) return path;
    strcpy(buf, abs);
    mkdirs_for(buf);
    return buf;
}

static int is_write_mode(const char *mode) { return strpbrk(mode, "wa+") != NULL; }

int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    char buf[PATH_MAX];
    int writing = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
    const char *p = writing ? resolve_write(path, buf) : resolve_read(path, buf);
    return __real_open(p, flags, mode);
}
FILE *__wrap_fopen(const char *path, const char *mode)
{
    char buf[PATH_MAX];
    return __real_fopen(is_write_mode(mode) ? resolve_write(path, buf) : resolve_read(path, buf), mode);
}
FILE *__wrap_freopen(const char *path, const char *mode, FILE *f)
{
    char buf[PATH_MAX];
    if (!path) return __real_freopen(path, mode, f);
    return __real_freopen(is_write_mode(mode) ? resolve_write(path, buf) : resolve_read(path, buf), mode, f);
}
int __wrap_stat(const char *path, struct stat *st) { char b[PATH_MAX]; return __real_stat(resolve_read(path, b), st); }
int __wrap_lstat(const char *path, struct stat *st) { char b[PATH_MAX]; return __real_lstat(resolve_read(path, b), st); }
int __wrap_access(const char *path, int m)
{
    /* access() is refused inside the app sandbox for files stat() can see, so
     * answer from stat(): packaged files are readable, the overlay makes them
     * writable too. */
    char b[PATH_MAX];
    struct stat st;
    (void)m;
    if (__real_stat(resolve_read(path, b), &st) != 0) return -1;
    return 0;
}
int __wrap_mkdir(const char *path, mode_t m) { char b[PATH_MAX]; return __real_mkdir(resolve_write(path, b), m); }
int __wrap_unlink(const char *path) { char b[PATH_MAX]; return __real_unlink(resolve_write(path, b)); }
int __wrap_remove(const char *path) { char b[PATH_MAX]; return __real_remove(resolve_write(path, b)); }
int __wrap_rmdir(const char *path) { char b[PATH_MAX]; return __real_rmdir(resolve_write(path, b)); }
int __wrap_chmod(const char *path, mode_t m) { char b[PATH_MAX]; return __real_chmod(resolve_write(path, b), m); }
int __wrap_rename(const char *a, const char *b)
{
    char ba[PATH_MAX], bb[PATH_MAX];
    return __real_rename(resolve_write(a, ba), resolve_write(b, bb));
}
char *__wrap_realpath(const char *path, char *out)
{
    char abs[PATH_MAX];
    absolute(path, abs);
    if (!overlay_rel(abs)) return __real_realpath(path, out);
    char b[PATH_MAX];
    if (!exists(resolve_read(path, b))) { errno = ENOENT; return NULL; }
    if (!out) out = malloc(PATH_MAX);
    strcpy(out, abs); /* keep the virtual path so later lookups stay in the overlay */
    return out;
}

/* ---- merged directory listings ------------------------------------------ */
typedef struct {
    struct dirent *entries;
    size_t count, next;
} ZmrDir;

int sceKernelGetdents(int fd, char *buf, int nbytes);
int sceKernelOpen(const char *path, int flags, int mode);
int sceKernelClose(int fd);

static void add_entry(ZmrDir *d, size_t *cap, const char *name, unsigned char type)
{
    for (size_t i = 0; i < d->count; ++i)
        if (!strcmp(d->entries[i].d_name, name)) return;
    if (d->count == *cap) { *cap = *cap ? *cap * 2 : 64; d->entries = realloc(d->entries, *cap * sizeof(struct dirent)); }
    struct dirent *e = &d->entries[d->count++];
    memset(e, 0, sizeof(*e));
    e->d_type = type;
    snprintf(e->d_name, sizeof(e->d_name), "%s", name);
    e->d_namlen = (unsigned char)strlen(e->d_name);
    e->d_reclen = sizeof(*e);
}

/* libc opendir() is refused in the app sandbox; the kernel's getdents works
 * on the writable mount (FreeBSD 11 dirent layout). */
static int add_dir_kernel(ZmrDir *d, size_t *cap, const char *path)
{
    int fd = sceKernelOpen(path, 0x20000 /* O_DIRECTORY */, 0);
    if (fd < 0) return 0;
    char buf[8192];
    int len, any = 0;
    while ((len = sceKernelGetdents(fd, buf, sizeof(buf))) > 0) {
        for (int off = 0; off < len;) {
            unsigned short reclen = *(unsigned short *)(buf + off + 4);
            unsigned char type = *(unsigned char *)(buf + off + 6);
            unsigned char namlen = *(unsigned char *)(buf + off + 7);
            if (!reclen) break;
            char name[256];
            memcpy(name, buf + off + 8, namlen);
            name[namlen] = 0;
            add_entry(d, cap, name, type);
            any = 1;
            off += reclen;
        }
    }
    sceKernelClose(fd);
    return any;
}

static void add_dir(ZmrDir *d, size_t *cap, const char *path)
{
    DIR *real = __real_opendir(path);
    if (!real) { add_dir_kernel(d, cap, path); return; }
    struct dirent *e;
    while ((e = __real_readdir(real))) add_entry(d, cap, e->d_name, e->d_type);
    __real_closedir(real);
}

DIR *__wrap_opendir(const char *path)
{
    char abs[PATH_MAX];
    absolute(path, abs);
    const char *rel = overlay_rel(abs);
    char ro[PATH_MAX];
    if (rel) snprintf(ro, sizeof(ro), "%s%s%s", ZMR_RO_ROOT, *rel ? "/" : "", rel);
    int have_rw = exists(rel ? abs : path), have_ro = rel && exists(ro);
    if (!have_rw && !have_ro) { errno = ENOENT; return NULL; }
    ZmrDir *d = calloc(1, sizeof(*d));
    size_t cap = 0;
    if (have_rw) add_dir(d, &cap, rel ? abs : path);
    if (have_ro) add_dir(d, &cap, ro);
    return (DIR *)d;
}
struct dirent *__wrap_readdir(DIR *dir)
{
    ZmrDir *d = (ZmrDir *)dir;
    return d->next < d->count ? &d->entries[d->next++] : NULL;
}
int __wrap_closedir(DIR *dir)
{
    ZmrDir *d = (ZmrDir *)dir;
    free(d->entries);
    free(d);
    return 0;
}

/* ---- current directory --------------------------------------------------- */
int __wrap_chdir(const char *path)
{
    char abs[PATH_MAX], b[PATH_MAX];
    struct stat st;
    absolute(path, abs);
    if (__real_stat(resolve_read(abs, b), &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    snprintf(zmr_cwd, sizeof(zmr_cwd), "%s", abs);
    return 0;
}
char *__wrap_getcwd(char *buf, size_t size)
{
    size_t n = strlen(zmr_cwd) + 1;
    if (!buf) { buf = malloc(size > n ? size : n); size = size > n ? size : n; }
    if (n > size) { errno = ERANGE; return NULL; }
    memcpy(buf, zmr_cwd, n);
    return buf;
}

/* ---- startup diagnostics -------------------------------------------------- */

static void diag_list(const char *dir)
{
    /* libc opendir/readdir */
    DIR *d = __real_opendir(dir);
    printf("[fsdiag] opendir(%s) = %s errno=%d:", dir, d ? "ok" : "FAIL", d ? 0 : errno);
    if (d) {
        struct dirent *e; int n = 0;
        while ((e = __real_readdir(d)) && n < 20) { printf(" '%s'", e->d_name); ++n; }
        if (!n) printf(" (no entries, errno=%d)", errno);
        __real_closedir(d);
    }
    printf("\n");
    /* raw kernel getdents (FreeBSD 11 dirent: u32 ino, u16 reclen, u8 type, u8 namlen, name) */
    int fd = sceKernelOpen(dir, 0x20000 /* O_DIRECTORY */, 0);
    printf("[fsdiag] sceKernelOpen(%s) = %#x:", dir, (unsigned)fd);
    if (fd >= 0) {
        char buf[4096];
        int len = sceKernelGetdents(fd, buf, sizeof(buf));
        printf(" getdents=%d", len);
        for (int off = 0, n = 0; len > 0 && off < len && n < 20; ++n) {
            unsigned short reclen = *(unsigned short *)(buf + off + 4);
            unsigned char namlen = *(unsigned char *)(buf + off + 7);
            if (!reclen) break;
            printf(" '%.*s'", namlen, buf + off + 8);
            off += reclen;
        }
        sceKernelClose(fd);
    }
    printf("\n");
}

static void diag_file(const char *path)
{
    struct stat st;
    int s = __real_stat(path, &st);
    int se = errno;
    int fd = __real_open(path, O_RDONLY);
    int oe = errno;
    printf("[fsdiag] %s: stat=%d(errno %d) size=%lld open=%d(errno %d)\n",
           path, s, s ? se : 0, s ? -1LL : (long long)st.st_size, fd, fd < 0 ? oe : 0);
    if (fd >= 0) close(fd);
}

static int can_list(const char *dir)
{
    DIR *d = __real_opendir(dir);
    if (!d) return 0;
    int n = 0;
    while (__real_readdir(d)) ++n;
    __real_closedir(d);
    return n > 0;
}

/* Pick where the packaged game files are read from. /app0 can open files but
 * (on 7.60 at least) refuses directory listings; the real folder under
 * /data/homebrew may allow both. */
void zmr_fs_init(const char *title_id)
{
    char cands[3][256];
    snprintf(cands[0], 256, "/data/homebrew/%s/game", title_id);
    snprintf(cands[1], 256, "/mnt/sandbox/pfsmnt/%s-app0/game", title_id);
    snprintf(cands[2], 256, "/app0/game");
    int chosen = -1;
    for (int i = 0; i < 3 && chosen < 0; ++i) {
        struct stat st;
        int ok = __real_stat(cands[i], &st) == 0;
        int list = ok && can_list(cands[i]);
        printf("[zmr] game dir candidate %s: exists=%d listable=%d\n", cands[i], ok, list);
        if (ok && list) chosen = i;
    }
    if (chosen < 0) {
        chosen = 2;
        printf("[zmr] WARNING: no listable game dir; directory scans of packaged files will be empty\n");
    }
    snprintf(zmr_ro_root, sizeof(zmr_ro_root), "%s", cands[chosen]);
    printf("[zmr] packaged game files read from %s\n", zmr_ro_root);
}

void zmr_fs_diagnose(const char *title_id)
{
    char p1[128], p2[160];
    snprintf(p1, sizeof(p1), "/data/homebrew/%s/game", title_id);
    snprintf(p2, sizeof(p2), "/data/homebrew/%s/game/hl2/gameinfo.txt", title_id);
    diag_list(p1);
    diag_list("/download0/zmr");
    diag_file("/app0/eboot.bin");
    diag_file("/app0/sce_sys/param.json");
    diag_file("/app0/game/args.txt");
    diag_file("/app0/game/hl2/gameinfo.txt");
    diag_file("/app0/game/hl2/GameInfo.txt");
    diag_file(p2);
    diag_list("/app0");
    diag_list("/app0/game");
    diag_list("/app0/game/hl2");
}
