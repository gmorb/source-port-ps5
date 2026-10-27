/* Memory diagnostics: live pthread mutex/condvar counts (each one is a libc
 * heap allocation inside libkernel) and the app heap's usage, printed
 * periodically by the launcher. Used to chase long-session crashes. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_cond_destroy(pthread_cond_t *);

int __real_pthread_mutexattr_init(pthread_mutexattr_t *);
int __real_pthread_mutexattr_destroy(pthread_mutexattr_t *);
static atomic_long g_attrLive;
int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *a)
{
    int rc = __real_pthread_mutexattr_init(a);
    if (rc == 0) atomic_fetch_add(&g_attrLive, 1);
    return rc;
}
int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
    int rc = __real_pthread_mutexattr_destroy(a);
    if (rc == 0) atomic_fetch_sub(&g_attrLive, 1);
    return rc;
}

static atomic_long g_mutexLive, g_mutexTotal, g_condLive, g_condTotal, g_initFail;

int __wrap_pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    int rc = __real_pthread_mutex_init(m, a);
    if (rc == 0) { atomic_fetch_add(&g_mutexLive, 1); atomic_fetch_add(&g_mutexTotal, 1); }
    else atomic_fetch_add(&g_initFail, 1);
    return rc;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *m)
{
    int rc = __real_pthread_mutex_destroy(m);
    if (rc == 0) atomic_fetch_sub(&g_mutexLive, 1);
    return rc;
}
int __wrap_pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    int rc = __real_pthread_cond_init(c, a);
    if (rc == 0) { atomic_fetch_add(&g_condLive, 1); atomic_fetch_add(&g_condTotal, 1); }
    else atomic_fetch_add(&g_initFail, 1);
    return rc;
}
int __wrap_pthread_cond_destroy(pthread_cond_t *c)
{
    int rc = __real_pthread_cond_destroy(c);
    if (rc == 0) atomic_fetch_sub(&g_condLive, 1);
    return rc;
}

void ps5_opengl_heap_snapshot(const char *phase, unsigned iteration);
void ps5_opengl_heap_stats_print(unsigned iteration);

void zmr_memdiag_print(unsigned n)
{
    if (n == 0) ps5_opengl_heap_stats_print(0);
    printf("[zmr] mem: mutexes live=%ld (created %ld) condvars live=%ld (created %ld) mutex attrs live=%ld init failures=%ld\n",
           (long)g_mutexLive, (long)g_mutexTotal, (long)g_condLive, (long)g_condTotal, (long)g_attrLive, (long)g_initFail);
    ps5_opengl_heap_snapshot("run", n);
}
