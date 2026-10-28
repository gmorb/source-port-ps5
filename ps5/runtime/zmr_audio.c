/* Audio safety for the PS5 port.
 *
 * Freezes stopped when the game ran with -nosound. SDL's audio thread runs at
 * the highest thread priority and loops "mix -> sceAudioOutOutput". If that
 * loop ever stops blocking (Output failing or returning at once), it never
 * yields its CPU core, and every other game thread scheduled there (main
 * thread, watchdog) is starved: the game looks frozen with no crash report.
 *
 *  - SDL_SetThreadPriority: SDL's TIME_CRITICAL/HIGH requests become NORMAL,
 *    so a runaway audio thread can't starve the rest of the game
 *    (-ps5_audio_rt in args.txt keeps SDL's priorities).
 *  - sceAudioOutOutput: errors and back-to-back immediate returns are
 *    counted, logged and slowed down with a short sleep instead of spinning.
 *  - A heartbeat from the audio thread goes into the freeze recorder, so a
 *    freeze report shows whether audio was still running.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern void zmr_crumb_note(const char *what, unsigned a, unsigned b);

static int s_keepRealtime;
/* -ps5_audio_null: keep the whole game-side sound system running but don't
 * hand the mixed audio to the PS5 (silence); the thread waits as long as the
 * buffer would have played. Splits "Source's sound code" from "sceAudioOut". */
static int s_nullSink;
static uint32_t s_grain[8];   /* samples per buffer for handles 1..8 (by low bits) */
void zmr_audio_null_sink(int on) { s_nullSink = on; printf("[zmr] audio null sink %s\n", on ? "on" : "off"); }
void zmr_audio_keep_realtime(int on) { s_keepRealtime = on; }

typedef enum { ZMR_PRIO_LOW, ZMR_PRIO_NORMAL, ZMR_PRIO_HIGH, ZMR_PRIO_TIME_CRITICAL } zmr_prio;
int __real_SDL_SetThreadPriority(int priority);
int __wrap_SDL_SetThreadPriority(int priority)
{
    if (!s_keepRealtime && priority > ZMR_PRIO_NORMAL) {
        static int logged;
        if (logged++ < 4)
            printf("[zmr] SDL thread priority %d requested; using normal priority\n", priority);
        priority = ZMR_PRIO_NORMAL;
    }
    return __real_SDL_SetThreadPriority(priority);
}

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int32_t __real_sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                               uint32_t len, uint32_t freq, uint32_t param);
int32_t __wrap_sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                               uint32_t len, uint32_t freq, uint32_t param)
{
    int32_t h = __real_sceAudioOutOpen(userId, type, index, len, freq, param);
    printf("[zmr] sceAudioOutOpen(len %u, freq %u, param %#x) = %#x\n", len, freq, param, (unsigned)h);
    if (h > 0) s_grain[h & 7] = len;
    return h;
}

int32_t __real_sceAudioOutOutput(int32_t handle, const void *p);
int32_t __wrap_sceAudioOutOutput(int32_t handle, const void *p)
{
    if (s_nullSink && p) {
        static uint64_t next;
        uint32_t grain = s_grain[handle & 7] ? s_grain[handle & 7] : 1024;
        uint64_t period = (uint64_t)grain * 1000000u / 48000u, t = now_us();
        if (!next || t > next + 4 * period) next = t;
        next += period;
        if (next > t) usleep((useconds_t)(next - t));
        return 0;
    }
    static unsigned calls, fast_run, errors, slept;
    static uint64_t last_beat;
    const uint64_t t0 = now_us();
    int32_t rc = __real_sceAudioOutOutput(handle, p);
    const uint64_t dt = now_us() - t0;
    ++calls;
    if (rc < 0) {
        if (errors++ < 8)
            printf("[zmr] sceAudioOutOutput(%d) failed: %#x\n", handle, (unsigned)rc);
        zmr_crumb_note("audio error", (unsigned)rc, errors);
    }
    /* A 256..2048-sample buffer at 48 kHz takes 5..43 ms to play; Output
     * blocks for most of that once the queue is full. Many returns in under
     * 0.3 ms in a row means it is no longer blocking. */
    if (rc < 0 || dt < 300) {
        if (++fast_run >= 16) {
            if (slept++ < 8)
                printf("[zmr] audio output not blocking (rc %#x, %u fast returns); throttling\n",
                       (unsigned)rc, fast_run);
            zmr_crumb_note("audio throttle", (unsigned)rc, fast_run);
            usleep(5000);
        }
    } else {
        fast_run = 0;
    }
    if (t0 - last_beat > 2000000) {
        last_beat = t0;
        zmr_crumb_note("audio alive", calls, (unsigned)dt);
    }
    return rc;
}
