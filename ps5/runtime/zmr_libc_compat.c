/* libc gaps of the PS5 native-app environment.
 *
 * Native titles resolve libc against the system's libSceLibcInternal, which is
 * not FreeBSD's libc: libc++ (built against FreeBSD headers) and the engine
 * reference a few dozen symbols it lacks. Locale-aware variants collapse to
 * the C locale; process/user APIs report "unsupported".
 */
#define _WITH_GETLINE
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <locale.h>
#include <xlocale.h>
#include <runetype.h>
#include <langinfo.h>
#include <nl_types.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* ---- setjmp aliases: tail jumps keep the caller's frame intact ---------- */
__asm__(".globl _setjmp\n.type _setjmp,@function\n_setjmp: jmp setjmp\n"
        ".globl _longjmp\n.type _longjmp,@function\n_longjmp: jmp longjmp\n");

/* ---- C-locale-only locale API ------------------------------------------- */
int ___mb_cur_max(void) { return 1; }
int ___mb_cur_max_l(locale_t l) { (void)l; return 1; }
unsigned long ___runetype_l(int c, locale_t l) { (void)c; (void)l; return 0; }
int ___tolower_l(int c, locale_t l) { (void)l; return tolower(c); }
int ___toupper_l(int c, locale_t l) { (void)l; return toupper(c); }
static _RuneLocale zmr_rune_locale;
static pthread_once_t zmr_rune_once = PTHREAD_ONCE_INIT;
static void zmr_rune_init(void)
{
    /* C-locale rune table derived from the system ctype functions. */
    for (int c = 0; c < _CACHED_RUNES; ++c) {
        unsigned long t = 0;
        if (c < 128) {
            if (isalpha(c)) t |= _CTYPE_A;
            if (iscntrl(c)) t |= _CTYPE_C;
            if (isdigit(c)) t |= _CTYPE_D | (unsigned long)(c - '0');
            if (isgraph(c)) t |= _CTYPE_G;
            if (islower(c)) t |= _CTYPE_L;
            if (isprint(c)) t |= _CTYPE_R;
            if (ispunct(c)) t |= _CTYPE_P;
            if (isspace(c)) t |= _CTYPE_S;
            if (isupper(c)) t |= _CTYPE_U;
            if (isxdigit(c)) t |= _CTYPE_X;
            if (c == ' ' || c == '\t') t |= _CTYPE_B;
        }
        zmr_rune_locale.__runetype[c] = t;
        zmr_rune_locale.__maplower[c] = c < 128 ? tolower(c) : c;
        zmr_rune_locale.__mapupper[c] = c < 128 ? toupper(c) : c;
    }
}
_RuneLocale *__runes_for_locale(locale_t l, int *mb_sc_max)
{
    (void)l;
    pthread_once(&zmr_rune_once, zmr_rune_init);
    if (mb_sc_max) *mb_sc_max = 1;
    return &zmr_rune_locale;
}
locale_t newlocale(int mask, const char *name, locale_t base) { (void)mask; (void)name; (void)base; return (locale_t)(uintptr_t)1; }
int freelocale(locale_t l) { (void)l; return 0; }
struct lconv *localeconv_l(locale_t l) { (void)l; return localeconv(); }
char *nl_langinfo(nl_item item) { (void)item; return (char *)""; }
int iswctype_l(wint_t c, wctype_t t, locale_t l) { (void)l; return iswctype(c, t); }
int strcoll_l(const char *a, const char *b, locale_t l) { (void)l; return strcoll(a, b); }
size_t strxfrm_l(char *d, const char *s, size_t n, locale_t l) { (void)l; return strxfrm(d, s, n); }
int wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t l) { (void)l; return wcscmp(a, b); }
size_t wcsxfrm_l(wchar_t *d, const wchar_t *s, size_t n, locale_t l)
{
    (void)l;
    size_t len = wcslen(s);
    if (n) { wcsncpy(d, s, n - 1); d[n - 1] = 0; }
    return len;
}
size_t strftime_l(char *s, size_t n, const char *f, const struct tm *t, locale_t l) { (void)l; return strftime(s, n, f, t); }
double strtod_l(const char *s, char **e, locale_t l) { (void)l; return strtod(s, e); }
float strtof_l(const char *s, char **e, locale_t l) { (void)l; return strtof(s, e); }
long double strtold_l(const char *s, char **e, locale_t l) { (void)l; return strtold(s, e); }
long long strtoll_l(const char *s, char **e, int b, locale_t l) { (void)l; return strtoll(s, e, b); }
unsigned long long strtoull_l(const char *s, char **e, int b, locale_t l) { (void)l; return strtoull(s, e, b); }
int snprintf_l(char *s, size_t n, locale_t l, const char *f, ...)
{
    (void)l; va_list ap; va_start(ap, f); int r = vsnprintf(s, n, f, ap); va_end(ap); return r;
}
int asprintf_l(char **s, locale_t l, const char *f, ...)
{
    (void)l; va_list ap; va_start(ap, f); int r = vasprintf(s, f, ap); va_end(ap); return r;
}
int sscanf_l(const char *s, locale_t l, const char *f, ...)
{
    (void)l; va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r;
}
size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, locale_t l) { (void)l; return mbrlen(s, n, ps); }
size_t mbrtowc_l(wchar_t *w, const char *s, size_t n, mbstate_t *ps, locale_t l) { (void)l; return mbrtowc(w, s, n, ps); }
size_t mbsrtowcs_l(wchar_t *d, const char **s, size_t n, mbstate_t *ps, locale_t l) { (void)l; return mbsrtowcs(d, s, n, ps); }
int mbtowc_l(wchar_t *w, const char *s, size_t n, locale_t l) { (void)l; return mbtowc(w, s, n); }
size_t wcrtomb_l(char *s, wchar_t w, mbstate_t *ps, locale_t l) { (void)l; return wcrtomb(s, w, ps); }
int wctob_l(wint_t c, locale_t l) { (void)l; return wctob(c); }
wint_t btowc_l(int c, locale_t l) { (void)l; return btowc(c); }
size_t mbsnrtowcs_l(wchar_t *d, const char **src, size_t nms, size_t len, mbstate_t *ps, locale_t l)
{
    (void)l; (void)ps;
    const char *s = *src; size_t i = 0;
    for (; i < nms && (!d || i < len); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (d) d[i] = c;
        if (!c) { *src = NULL; return i; }
    }
    if (d) *src = s + i;
    return i;
}
size_t wcsnrtombs_l(char *d, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps, locale_t l)
{
    (void)l; (void)ps;
    const wchar_t *s = *src; size_t i = 0;
    for (; i < nwc && (!d || i < len); ++i) {
        wchar_t c = s[i];
        if (d) d[i] = c < 0x80 ? (char)c : '?';
        if (!c) { *src = NULL; return i; }
    }
    if (d) *src = s + i;
    return i;
}

/* ---- message catalogs ---------------------------------------------------- */
nl_catd catopen(const char *n, int f) { (void)n; (void)f; return (nl_catd)-1; }
char *catgets(nl_catd c, int s, int m, const char *d) { (void)c; (void)s; (void)m; return (char *)d; }
int catclose(nl_catd c) { (void)c; return 0; }

/* ---- time ---------------------------------------------------------------- */
static pthread_mutex_t zmr_time_lock = PTHREAD_MUTEX_INITIALIZER;
struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    pthread_mutex_lock(&zmr_time_lock);
    struct tm *r = gmtime(t);
    if (r) *out = *r;
    pthread_mutex_unlock(&zmr_time_lock);
    return r ? out : NULL;
}
struct tm *localtime_r(const time_t *t, struct tm *out)
{
    pthread_mutex_lock(&zmr_time_lock);
    struct tm *r = localtime(t);
    if (r) *out = *r;
    pthread_mutex_unlock(&zmr_time_lock);
    return r ? out : NULL;
}
char *ctime_r(const time_t *t, char *buf)
{
    struct tm tm;
    if (!localtime_r(t, &tm)) return NULL;
    strftime(buf, 26, "%a %b %e %H:%M:%S %Y\n", &tm);
    return buf;
}
time_t timegm(struct tm *tm)
{
    static const int mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long long y = tm->tm_year + 1900LL, m = tm->tm_mon;
    y += m / 12; m %= 12; if (m < 0) { m += 12; --y; }
    long long days = (y - 1970) * 365 + ((y - 1969) / 4) - ((y - 1901) / 100) + ((y - 1601) / 400)
                   + mdays[m] + (m > 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) + tm->tm_mday - 1;
    return (time_t)(((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}

/* ---- sorting / directories ---------------------------------------------- */
static void zmr_swap(char *a, char *b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }
void qsort_r(void *base, size_t n, size_t w, void *thunk, int (*cmp)(void *, const void *, const void *))
{
    /* heap sort: no recursion, no allocation */
    char *b = (char *)base;
    if (n < 2) return;
    for (size_t start = n / 2; start-- > 0;) {
        for (size_t root = start;;) {
            size_t child = 2 * root + 1;
            if (child >= n) break;
            if (child + 1 < n && cmp(thunk, b + child * w, b + (child + 1) * w) < 0) ++child;
            if (cmp(thunk, b + root * w, b + child * w) >= 0) break;
            zmr_swap(b + root * w, b + child * w, w); root = child;
        }
    }
    for (size_t end = n - 1; end > 0; --end) {
        zmr_swap(b, b + end * w, w);
        for (size_t root = 0;;) {
            size_t child = 2 * root + 1;
            if (child >= end) break;
            if (child + 1 < end && cmp(thunk, b + child * w, b + (child + 1) * w) < 0) ++child;
            if (cmp(thunk, b + root * w, b + child * w) >= 0) break;
            zmr_swap(b + root * w, b + child * w, w); root = child;
        }
    }
}
int alphasort(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }
static int (*zmr_scandir_cmp)(const struct dirent **, const struct dirent **);
static int zmr_scandir_qsort(const void *a, const void *b) { return zmr_scandir_cmp((const struct dirent **)a, (const struct dirent **)b); }
int scandir(const char *dir, struct dirent ***out, int (*sel)(const struct dirent *),
            int (*cmp)(const struct dirent **, const struct dirent **))
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent **list = NULL, *e;
    size_t n = 0, cap = 0;
    while ((e = readdir(d))) {
        if (sel && !sel(e)) continue;
        if (n == cap) { cap = cap ? cap * 2 : 32; list = realloc(list, cap * sizeof(*list)); }
        struct dirent *copy = malloc(sizeof(*e));
        memcpy(copy, e, sizeof(*e));
        list[n++] = copy;
    }
    closedir(d);
    if (cmp && n > 1) { zmr_scandir_cmp = cmp; qsort(list, n, sizeof(*list), zmr_scandir_qsort); }
    *out = list;
    return (int)n;
}

/* ---- unsupported process / user / misc APIs ------------------------------ */
struct passwd *getpwuid(uid_t u) { (void)u; return NULL; }
int getpwuid_r(uid_t u, struct passwd *p, char *b, size_t n, struct passwd **r) { (void)u; (void)p; (void)b; (void)n; *r = NULL; return ENOENT; }
int execlp(const char *f, const char *a, ...) { (void)f; (void)a; errno = ENOSYS; return -1; }
int pipe2(int fd[2], int flags) { (void)fd; (void)flags; errno = ENOSYS; return -1; }
int getloadavg(double l[], int n) { (void)l; (void)n; return -1; }
int dladdr(const void *a, void *info) { (void)a; (void)info; return 0; }
unsigned int if_nametoindex(const char *n) { (void)n; return 0; }
ssize_t recvmmsg(int s, struct mmsghdr *m, size_t v, int f, const struct timespec *t) { (void)s; (void)m; (void)v; (void)f; (void)t; errno = ENOSYS; return -1; }
ssize_t sendmmsg(int s, struct mmsghdr *m, size_t v, int f) { (void)s; (void)m; (void)v; (void)f; errno = ENOSYS; return -1; }
int mkostemp(char *t, int f) { (void)t; (void)f; errno = ENOSYS; return -1; }
char *tmpnam(char *s) { (void)s; return NULL; }
int utimensat(int fd, const char *p, const struct timespec t[2], int f) { (void)fd; (void)p; (void)t; (void)f; return 0; }
void uuid_create(void *uuid, uint32_t *status)
{
    arc4random_buf(uuid, 16);
    if (status) *status = 0;
}

/* FreeBSD's inline <ctype.h> helpers reach these for non-cached runes. */
unsigned long ___runetype(__ct_rune_t c) { (void)c; return 0; }
const _RuneLocale *__getCurrentRuneLocale(void) { return __runes_for_locale(NULL, NULL); }
