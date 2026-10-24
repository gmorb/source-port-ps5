/* Platform glue for the Zombie Master PS5 native app. */
#include <stddef.h>

/* CPU heap for the engine (app_heap.c tries smaller sizes if this fails). */
const size_t ps5_opengl_heap_size = 3ull * 1024 * 1024 * 1024;

/* Weak references the native-app linker must see defined. */
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
unsigned long long ZSTD_trace_decompress_begin(const ZSTD_DCtx *ctx) { (void)ctx; return 0; }
void ZSTD_trace_decompress_end(unsigned long long ctx, const void *trace) { (void)ctx; (void)trace; }
/* thread_local destructors are not run on PS5 (threads end with the process). */
int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso)
{
    (void)dtor; (void)obj; (void)dso;
    return 0;
}

/* Belt and braces for the engine's UDP polling: never block in recvfrom(). */
#include <sys/types.h>
#include <sys/socket.h>
ssize_t __real_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t __wrap_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    return __real_recvfrom(s, buf, len, flags | MSG_DONTWAIT, from, fromlen);
}
