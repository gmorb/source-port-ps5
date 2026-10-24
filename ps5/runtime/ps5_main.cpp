// PS5 entry point for the statically linked Source engine.
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>

extern "C" int sceKernelDebugOutText( int channel, const char *text );

extern "C" char zmr_rw_root[256];
extern "C" void zmr_fs_diagnose( const char *title_id );
extern "C" void zmr_fs_init( const char *title_id );
static char g_baseDir[256];
#define ZMR_PS5_BASEDIR g_baseDir
#ifndef ZMR_PS5_DEFAULT_ARGS
#define ZMR_PS5_DEFAULT_ARGS "-game hl2 -fullscreen -w 1920 -h 1080 -novid"
#endif

extern "C" void *ps5_dlopen( const char *path, int mode );
extern "C" void *ps5_dlsym( void *handle, const char *symbol );
extern "C" char __ImageBase[] __attribute__( ( weak ) );
extern "C" char __executable_start[] __attribute__( ( weak ) );

typedef int ( *LauncherMain_t )( int argc, char **argv );



// Mirror the log file to the kernel log (readable with the klogsrv payload:
// `nc <ps5-ip> 3232`), because /download0 is not directly reachable over FTP.
static pthread_mutex_t g_klogLock = PTHREAD_MUTEX_INITIALIZER;
// Files mirrored to the kernel log: our stdout/stderr log and the engine's
// own console log (-condebug), which carries everything shown in the console.
static const char *g_klogFiles[] = { "zmr.log", "hl2/console.log" };
static int g_klogFds[2] = { -1, -1 };

static void DrainLogToKlog()
{
	pthread_mutex_lock( &g_klogLock );
	for ( int i = 0; i < 2; ++i )
	{
		if ( g_klogFds[i] < 0 && g_baseDir[0] )
		{
			char path[300];
			snprintf( path, sizeof( path ), "%s/%s", g_baseDir, g_klogFiles[i] );
			struct stat st;
			if ( stat( path, &st ) == 0 )
			{
				g_klogFds[i] = open( path, O_RDONLY );
				// console.log is appended across runs: start at its current end.
				if ( i == 1 && g_klogFds[i] >= 0 )
					lseek( g_klogFds[i], 0, SEEK_END );
			}
		}
		if ( g_klogFds[i] >= 0 )
		{
			char buf[1024];
			ssize_t n;
			while ( ( n = read( g_klogFds[i], buf, sizeof( buf ) - 1 ) ) > 0 )
			{
				buf[n] = 0;
				if ( i == 1 )
					sceKernelDebugOutText( 0, "[con] " );
				sceKernelDebugOutText( 0, buf );
			}
		}
	}
	pthread_mutex_unlock( &g_klogLock );
}

static void *KlogMirror( void * )
{
	for ( ;; )
	{
		DrainLogToKlog();
		usleep( 100000 );
	}
	return NULL;
}

static void DrainAtExit()
{
	fflush( NULL );
	DrainLogToKlog();
	sceKernelDebugOutText( 0, "[zmr] process exiting\n" );
}

// ---- hang watchdog --------------------------------------------------------
// Every presented frame bumps g_frames (SDL_GL_SwapWindow is wrapped at link
// time). If frames stop for a while, the main thread gets SIGTRAP with the
// default action, so the system writes its crash report (registers and a
// full backtrace of the stuck thread) to the kernel log.
static volatile unsigned long g_frames;
static pthread_t g_mainThread;
static bool g_ps5Probe;
static int g_quitMode = 3; // -ps5_quitN: 1 KillApp, 2 LoadExec(exit), 3 _exit, 4 SIGKILL
static volatile unsigned g_frameUs[1024]; // duration of the interval while g_frames == i
extern "C" void __real_SDL_GL_SwapWindow( void *window );
// GL entry points from the static PS5 OpenGL SDK.
extern "C" void glReadPixels( int x, int y, int w, int h, unsigned fmt, unsigned type, void *data );
extern "C" void glGetIntegerv( unsigned pname, int *data );
extern "C" void glBindFramebuffer( unsigned target, unsigned fb );
extern "C" void glReadBuffer( unsigned mode );
extern "C" unsigned glGetError( void );
extern "C" unsigned char glIsFramebuffer( unsigned fb );
extern "C" void glReadBuffer( unsigned mode );

// Every 120 frames, sample what is about to be shown so the log tells us
// whether frames are really black or the picture is lost on presentation.
static void ProbeBackbuffer( unsigned long frame )
{
	int prevRead = 0;
	glGetIntegerv( 0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &prevRead );
	glBindFramebuffer( 0x8CA8 /* GL_READ_FRAMEBUFFER */, 0 );
	glReadBuffer( 0x0405 /* GL_BACK */ );
	static const int pts[9][2] = { { 960, 540 }, { 100, 100 }, { 1820, 100 }, { 100, 980 }, { 1820, 980 },
		{ 480, 540 }, { 1440, 540 }, { 960, 200 }, { 960, 880 } };
	char line[400];
	int len = snprintf( line, sizeof( line ), "[zmr] frame %lu pixels:", frame );
	for ( int i = 0; i < 9; ++i )
	{
		unsigned char px[4] = { 0, 0, 0, 0 };
		glReadPixels( pts[i][0], pts[i][1], 1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px );
		len += snprintf( line + len, sizeof( line ) - len, " %02x%02x%02x", px[0], px[1], px[2] );
	}
	snprintf( line + len, sizeof( line ) - len, " (glerr %#x, prev read fb %d)\n", glGetError(), prevRead );
	printf( "%s", line );
	// Whole-frame statistics: how much of the picture is not black, and where.
	{
		static unsigned char *buf;
		if ( !buf )
			buf = (unsigned char *)malloc( 1920 * 1080 * 4 );
		glReadPixels( 0, 0, 1920, 1080, 0x1908, 0x1401, buf );
		long lit = 0;
		int minx = 1920, miny = 1080, maxx = -1, maxy = -1;
		unsigned maxv = 0;
		for ( int y = 0; y < 1080; y += 2 )
			for ( int x = 0; x < 1920; x += 2 )
			{
				const unsigned char *p = buf + ( (size_t)y * 1920 + x ) * 4;
				unsigned v = p[0] > p[1] ? p[0] : p[1];
				v = v > p[2] ? v : p[2];
				if ( v > maxv )
					maxv = v;
				if ( v > 8 )
				{
					++lit;
					if ( x < minx ) minx = x;
					if ( x > maxx ) maxx = x;
					if ( y < miny ) miny = y;
					if ( y > maxy ) maxy = y;
				}
			}
		printf( "[zmr] frame %lu stats: lit=%ld/518400 brightest=%u box=(%d,%d)-(%d,%d) [GL coords, y up]\n",
			frame, lit, maxv, minx, miny, maxx, maxy );
	}
	// Also sample the engine's own framebuffers (togl renders into FBOs and
	// blits to the window): is the picture there before the final copy?
	for ( unsigned fb = 1; fb < 24 && frame <= 8; ++fb )
	{
		if ( !glIsFramebuffer( fb ) )
			continue;
		glBindFramebuffer( 0x8CA8, fb );
		glReadBuffer( 0x8CE0 /* GL_COLOR_ATTACHMENT0 */ );
		len = snprintf( line, sizeof( line ), "[zmr]   fbo %u:", fb );
		int nonzero = 0;
		for ( int i = 0; i < 9; ++i )
		{
			unsigned char px[4] = { 0, 0, 0, 0 };
			glReadPixels( pts[i][0], pts[i][1], 1, 1, 0x1908, 0x1401, px );
			nonzero |= px[0] | px[1] | px[2];
			len += snprintf( line + len, sizeof( line ) - len, " %02x%02x%02x", px[0], px[1], px[2] );
		}
		snprintf( line + len, sizeof( line ) - len, "%s (glerr %#x)\n", nonzero ? " <-- has content" : "", glGetError() );
		printf( "%s", line );
	}
	glBindFramebuffer( 0x8CA8, (unsigned)prevRead );
}

extern "C" unsigned long ps5_get_frames( void ) { return g_frames; }
extern "C" void ps5_gltrace_parse( const char *line );
extern "C" void __wrap_SDL_GL_SwapWindow( void *window )
{
	{
		static struct timespec s_prevSwap;
		struct timespec t; clock_gettime( CLOCK_MONOTONIC, &t );
		if ( s_prevSwap.tv_sec )
		{
			long us = ( t.tv_sec - s_prevSwap.tv_sec ) * 1000000L + ( t.tv_nsec - s_prevSwap.tv_nsec ) / 1000;
			g_frameUs[g_frames & 1023] = (unsigned)us;
			if ( us > 100000 )
				printf( "[zmr] hitch: frame %lu took %ld ms\n", g_frames, us / 1000 );
		}
		s_prevSwap = t;
	}
	++g_frames;
	if ( g_ps5Probe && ( g_frames <= 8 || g_frames % 30 == 0 ) )
		ProbeBackbuffer( g_frames );
	__real_SDL_GL_SwapWindow( window );
	// Frame-rate report every 5 seconds.
	static struct timespec s_last;
	static unsigned long s_lastFrames;
	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	if ( !s_last.tv_sec ) { s_last = now; s_lastFrames = g_frames; }
	double dt = ( now.tv_sec - s_last.tv_sec ) + ( now.tv_nsec - s_last.tv_nsec ) * 1e-9;
	if ( dt >= 5.0 )
	{
		printf( "[zmr] fps %.1f (frame %lu)\n", ( g_frames - s_lastFrames ) / dt, g_frames );
		s_last = now; s_lastFrames = g_frames;
	}
}

// Register access: the PS5 kernel's ucontext has a larger signal mask than
// FreeBSD's headers describe, so mcontext starts 48 bytes later (verified
// against the system crash dumps: our "rip" slot held r14).
static unsigned long CtxReg( void *ctx, int index )
{
	unsigned long *mc = (unsigned long *)( (char *)&( (ucontext_t *)ctx )->uc_mcontext + 48 );
	return mc[index];
}
enum { REG_RBP = 9, REG_RIP = 20, REG_RSP = 23 };

static void DumpThreadHandler( int, siginfo_t *, void *ctx )
{
	unsigned long base = (unsigned long)( __executable_start ? __executable_start : 0 );
	unsigned long textEnd = base + 0x2b00000;
	unsigned long rip = CtxReg( ctx, REG_RIP ), rsp = CtxReg( ctx, REG_RSP ), rbp = CtxReg( ctx, REG_RBP );
	char line[200];
	snprintf( line, sizeof( line ), "[zmr] main thread: rip=%#lx (offset %#lx) rsp=%#lx rbp=%#lx\n", rip, rip - base, rsp, rbp );
	sceKernelDebugOutText( 0, line );
	if ( rsp > 0x10000 && ( rsp & 7 ) == 0 )
	{
		unsigned long *sp = (unsigned long *)rsp;
		for ( int i = 0, n = 0; i < 4096 && n < 48; ++i )
		{
			unsigned long v = sp[i];
			if ( v > base && v < textEnd )
			{
				snprintf( line, sizeof( line ), "[zmr]   stack[%d] offset %#lx\n", i, v - base );
				sceKernelDebugOutText( 0, line );
				++n;
			}
		}
	}
	sceKernelDebugOutText( 0, "[zmr] main thread dump done\n" );
}


// ---- Sampling profiler (-ps5_prof): samples the main thread every ~2 ms ----
static volatile int g_profAck;
static unsigned long g_profStack[16];
static int g_profDepth;
static void ProfHandler( int, siginfo_t *, void *ctx )
{
	unsigned long rip = CtxReg( ctx, REG_RIP ), rsp = CtxReg( ctx, REG_RSP ), rbp = CtxReg( ctx, REG_RBP );
	int n = 0;
	g_profStack[n++] = rip;
	for ( int i = 0; i < 15; ++i )
	{
		if ( rbp < rsp || rbp > rsp + ( 32ul << 20 ) || ( rbp & 7 ) ) break;
		unsigned long *fp = (unsigned long *)rbp;
		unsigned long ret = fp[1];
		if ( ret < 0x400000 || ret > 0x8000000 ) break;
		g_profStack[n++] = ret;
		if ( fp[0] <= rbp ) break;
		rbp = fp[0];
	}
	g_profDepth = n;
	__atomic_store_n( &g_profAck, 1, __ATOMIC_RELEASE );
}
struct ProfEntry { unsigned long addr; unsigned long count; };
enum { PROF_SLOTS = 1 << 15 };
static ProfEntry g_profSelf[PROF_SLOTS], g_profIncl[PROF_SLOTS], g_hitchSelf[PROF_SLOTS], g_hitchIncl[PROF_SLOTS];
static void ProfAdd( ProfEntry *t, unsigned long a )
{
	unsigned h = (unsigned)( ( a * 0x9E3779B97F4A7C15ul ) >> 49 ) & ( PROF_SLOTS - 1 );
	for ( int i = 0; i < 64; ++i, h = ( h + 1 ) & ( PROF_SLOTS - 1 ) )
	{
		if ( t[h].addr == a ) { ++t[h].count; return; }
		if ( !t[h].addr ) { t[h].addr = a; t[h].count = 1; return; }
	}
}
static int ProfCmp( const void *a, const void *b )
{
	unsigned long x = ( (const ProfEntry *)a )->count, y = ( (const ProfEntry *)b )->count;
	return x < y ? 1 : x > y ? -1 : 0;
}
static void ProfReport( ProfEntry *t, const char *name, unsigned long total, int top )
{
	static ProfEntry tmp[PROF_SLOTS];
	int n = 0;
	for ( int i = 0; i < PROF_SLOTS; ++i ) if ( t[i].addr ) tmp[n++] = t[i];
	qsort( tmp, n, sizeof( ProfEntry ), ProfCmp );
	printf( "[prof] %s (top %d of %d, %lu samples)\n", name, top, n, total );
	for ( int i = 0; i < n && i < top; ++i )
		printf( "[prof]   %5.1f%% %#lx\n", 100.0 * tmp[i].count / total, tmp[i].addr );
}
static void *ProfThread( void * )
{
	struct sigaction sa;
	memset( &sa, 0, sizeof( sa ) );
	sa.sa_sigaction = ProfHandler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction( SIGUSR2, &sa, NULL );
	unsigned long total = 0;
	struct timespec last; clock_gettime( CLOCK_MONOTONIC, &last );
	printf( "[prof] started; anchor ProfThread=%p\n", (void *)&ProfThread );
	for ( ;; )
	{
		usleep( 2000 );
		if ( g_frames < 5 ) continue;
		__atomic_store_n( &g_profAck, 0, __ATOMIC_RELEASE );
		pthread_kill( g_mainThread, SIGUSR2 );
		int waited = 0;
		while ( !__atomic_load_n( &g_profAck, __ATOMIC_ACQUIRE ) && waited < 200 ) { usleep( 25 ); ++waited; }
		if ( !g_profAck ) continue;
		++total;
		// Hitch profile: keep this frame's samples; once the frame ends, merge
		// them into the hitch tables if it took longer than 80 ms.
		{
			enum { MAXPEND = 2048 };
			static unsigned long pend[MAXPEND][16];
			static int pendDepth[MAXPEND];
			static int nPend;
			static unsigned long pendFrame = ~0ul;
			static unsigned long hitchSamples, hitchFrames;
			unsigned long fr = g_frames;
			if ( fr != pendFrame )
			{
				if ( pendFrame != ~0ul && fr == pendFrame + 1 && g_frameUs[pendFrame & 1023] > 80000 )
				{
					++hitchFrames;
					for ( int k = 0; k < nPend; ++k )
					{
						++hitchSamples;
						ProfAdd( g_hitchSelf, pend[k][0] );
						for ( int i = 0; i < pendDepth[k]; ++i )
						{
							bool dup = false;
							for ( int j = 0; j < i; ++j ) if ( pend[k][j] == pend[k][i] ) dup = true;
							if ( !dup ) ProfAdd( g_hitchIncl, pend[k][i] );
						}
					}
				}
				nPend = 0;
				pendFrame = fr;
			}
			if ( nPend < MAXPEND )
			{
				memcpy( pend[nPend], g_profStack, sizeof( g_profStack ) );
				pendDepth[nPend++] = g_profDepth;
			}
			static struct timespec hlast;
			struct timespec hn; clock_gettime( CLOCK_MONOTONIC, &hn );
			if ( !hlast.tv_sec ) hlast = hn;
			if ( hn.tv_sec - hlast.tv_sec >= 30 && hitchSamples )
			{
				hlast = hn;
				printf( "[prof] hitch frames so far: %lu\n", hitchFrames );
				ProfReport( g_hitchSelf, "hitch-self", hitchSamples, 40 );
				ProfReport( g_hitchIncl, "hitch-inclusive", hitchSamples, 120 );
			}
		}
		ProfAdd( g_profSelf, g_profStack[0] );
		for ( int i = 0; i < g_profDepth; ++i )
		{
			bool dup = false;
			for ( int j = 0; j < i; ++j ) if ( g_profStack[j] == g_profStack[i] ) dup = true;
			if ( !dup ) ProfAdd( g_profIncl, g_profStack[i] );
		}
		struct timespec now; clock_gettime( CLOCK_MONOTONIC, &now );
		if ( now.tv_sec - last.tv_sec >= 30 )
		{
			last = now;
			ProfReport( g_profSelf, "self", total, 60 );
			ProfReport( g_profIncl, "inclusive", total, 120 );
		}
	}
	return NULL;
}

static void *HangWatchdog( void * )
{
	unsigned long last = 0;
	int stalled = 0;
	const int limit = 10; // seconds without a new frame
	for ( ;; )
	{
		sleep( 1 );
		unsigned long now = g_frames;
		if ( now != last || now == 0 )
		{
			if ( now != last && ( now % 600 ) == 0 )
				printf( "[zmr] %lu frames presented\n", now );
			last = now;
			stalled = 0;
			continue;
		}
		if ( ++stalled % limit == 0 && stalled <= limit * 3 )
		{
			printf( "[zmr] WATCHDOG: no frame for %d s after %lu frames; dumping main thread\n", limit, now );
			fflush( NULL );
			DrainLogToKlog();
			struct sigaction sa;
			memset( &sa, 0, sizeof( sa ) );
			sa.sa_sigaction = DumpThreadHandler;
			sa.sa_flags = SA_SIGINFO;
			sigaction( SIGUSR1, &sa, NULL );
			pthread_kill( g_mainThread, SIGUSR1 );
		}
	}
	return NULL;
}

static void CrashHandler( int sig, siginfo_t *info, void *ctx )
{
	ucontext_t *uc = (ucontext_t *)ctx;
	unsigned long rip = CtxReg( ctx, REG_RIP ); (void)uc;
	unsigned long base = (unsigned long)( __executable_start ? __executable_start : 0 );
	char line[256];
	snprintf( line, sizeof( line ), "\n*** ZMR FATAL signal %d at rip=%#lx (image offset %#lx) fault addr=%p\n",
		sig, rip, rip - base, info ? info->si_addr : NULL );
	fputs( line, stderr );
	sceKernelDebugOutText( 0, line );
	fflush( stderr );
	DrainLogToKlog();
	// Hand the fault to the system: it prints registers and a full backtrace
	// to the kernel log when the faulting instruction runs again.
	signal( sig, SIG_DFL );
}


// DualSense defaults. The engine execs 360controller.cfg once a game controller
// is found; HL2's PC content has none, so write one into the writable layer.
static void WriteControllerConfig( const char *game )
{
	char path[256];
	snprintf( path, sizeof( path ), "%s/cfg/360controller.cfg", game );
	FILE *f = fopen( path, "w" );
	if ( !f ) { printf( "[zmr] could not write %s\n", path ); return; }
	fputs(
		"// Written by the PS5 launcher (DualSense: A=Cross B=Circle X=Square Y=Triangle)\n"
		"joystick 1\n"
		"joy_advanced 1\n"
		"joy_advaxisx 3\njoy_advaxisy 1\njoy_advaxisz 0\njoy_advaxisr 2\njoy_advaxisu 4\njoy_advaxisv 0\n"
		"joy_forwardsensitivity -1\njoy_sidesensitivity 1\njoy_pitchsensitivity 1\njoy_yawsensitivity -1.5\n"
		"joy_forwardthreshold 0.15\njoy_sidethreshold 0.15\njoy_pitchthreshold 0.1\njoy_yawthreshold 0.1\n"
		"joy_response_move 1\njoy_response_look 1\njoy_lowend 1\njoy_lowmap 1\n"
		"joy_axisbutton_threshold 0.3\n"
		"+jlook\n"
		"lookspring 0\n"
		"hud_fastswitch 1\n"
		"alias sprint_toggle sprint_on\n"
		"alias sprint_on \"+speed; alias sprint_toggle sprint_off\"\n"
		"alias sprint_off \"-speed; alias sprint_toggle sprint_on\"\n"
		"bind A_BUTTON \"+jump\"\n"              /* Cross */
		"bind B_BUTTON \"+duck\"\n"              /* Circle (hold) */
		"bind X_BUTTON \"+reload\"\n"            /* Square */
		"bind Y_BUTTON \"+use\"\n"               /* Triangle */
		"bind R_TRIGGER \"+attack\"\n"           /* R2 */
		"bind L_TRIGGER \"+attack2\"\n"          /* L2 */
		"bind R_SHOULDER \"invnext\"\n"          /* R1 */
		"bind L_SHOULDER \"invprev\"\n"          /* L1 */
		"bind STICK1 \"sprint_toggle\"\n"        /* L3: sprint on/off */
		"bind STICK2 \"+zoom\"\n"                /* R3 (hold) */
		"bind UP \"impulse 100\"\n"              /* D-pad up: flashlight */
		"bind DOWN \"lastinv\"\n"                /* D-pad down: last weapon */
		"bind LEFT \"phys_swap\"\n"              /* D-pad left: gravity gun swap */
		"bind RIGHT \"+commandermousemove\"\n"   /* D-pad right: squad command */
		"bind BACK \"save quick\"\n"             /* touchpad click: quicksave */
		"bind START \"gameui_activate\"\n"       /* Options: pause menu */
		"joyadvancedupdate\n", f );
	fclose( f );
	snprintf( path, sizeof( path ), "%s/cfg/360controller-linux.cfg", game );
	if ( ( f = fopen( path, "w" ) ) ) { fputs( "// PS5: see 360controller.cfg\n", f ); fclose( f ); }
	printf( "[zmr] wrote %s/cfg/360controller.cfg\n", game );
}

static int SplitArgs( char *line, char **argv, int max )
{
	int argc = 0;
	for ( char *p = line; *p && argc < max; )
	{
		while ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' )
			++p;
		if ( !*p )
			break;
		bool quoted = *p == '"';
		if ( quoted )
			++p;
		argv[argc++] = p;
		while ( *p && ( quoted ? *p != '"' : !( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) ) )
			++p;
		if ( *p )
			*p++ = 0;
	}
	return argc;
}

int main( int, char ** )
{
	sceKernelDebugOutText( 0, "[zmr] Zombie Master PS5 launching\n" );

	// Find a writable directory: /download0 is the sandbox-approved one, the
	// others work only with some loaders/firmwares. First that accepts a file wins.
	static const char *candidates[] = {
		"/download0/zmr", "/data/zmr-ps5", "/user/data/zmr-ps5", "/temp0/zmr",
		"/savedata0/zmr", "/app0/game/.rw", "/mnt/usb0/zmr-ps5", "/usb0/zmr-ps5", NULL };
	for ( int i = 0; candidates[i] && !g_baseDir[0]; ++i )
	{
		char parent[256], probe[300], msg[400];
		snprintf( parent, sizeof( parent ), "%s", candidates[i] );
		*strrchr( parent, '/' ) = 0;
		mkdir( parent, 0777 );
		mkdir( candidates[i], 0777 );
		snprintf( probe, sizeof( probe ), "%s/.probe", candidates[i] );
		FILE *f = fopen( probe, "w" );
		snprintf( msg, sizeof( msg ), "[zmr] writable probe %s: %s (errno %d)\n", candidates[i], f ? "OK" : "no", f ? 0 : errno );
		sceKernelDebugOutText( 0, msg );
		if ( f )
		{
			fclose( f );
			snprintf( g_baseDir, sizeof( g_baseDir ), "%s", candidates[i] );
		}
	}
	if ( !g_baseDir[0] )
	{
		sceKernelDebugOutText( 0, "[zmr] no writable directory found\n" );
		return 1;
	}
	snprintf( zmr_rw_root, sizeof( zmr_rw_root ), "%s", g_baseDir );
	if ( chdir( ZMR_PS5_BASEDIR ) != 0 )
	{
		char msg[200];
		snprintf( msg, sizeof( msg ), "[zmr] chdir to base dir failed (errno %d)\n", errno );
		sceKernelDebugOutText( 0, msg );
		return 1;
	}
	{
		char msg[300];
		snprintf( msg, sizeof( msg ), "[zmr] base dir %s, packaged game files /app0/game\n", g_baseDir );
		sceKernelDebugOutText( 0, msg );
	}

	// Everything the engine prints goes to a log the user can fetch over FTP.
	{
		char path[300];
		snprintf( path, sizeof( path ), "%s/zmr.log", g_baseDir );
		if ( !freopen( path, "w", stdout ) )
			sceKernelDebugOutText( 0, "[zmr] could not open zmr.log\n" );
	}
	{
		// stderr gets its own append-mode stream on the same file (dup2 does
		// not rebind FILE streams in the native libc).
		char path[300];
		snprintf( path, sizeof( path ), "%s/zmr.log", g_baseDir );
		freopen( path, "a", stdout );
		freopen( path, "a", stderr );
	}
	setvbuf( stdout, NULL, _IONBF, 0 );
	setvbuf( stderr, NULL, _IONBF, 0 );
	printf( "Zombie Master PS5 starting, base dir %s\n", ZMR_PS5_BASEDIR );
	atexit( DrainAtExit );
	g_mainThread = pthread_self();
	{
		pthread_t wd;
		pthread_create( &wd, NULL, HangWatchdog, NULL );
	}
	pthread_t mirror;
	pthread_create( &mirror, NULL, KlogMirror, NULL );

	struct sigaction sa;
	memset( &sa, 0, sizeof( sa ) );
	sa.sa_sigaction = CrashHandler;
	sa.sa_flags = SA_SIGINFO;
	sigaction( SIGSEGV, &sa, NULL );
	sigaction( SIGBUS, &sa, NULL );
	sigaction( SIGILL, &sa, NULL );
	sigaction( SIGFPE, &sa, NULL );
	sigaction( SIGABRT, &sa, NULL );

	// Command line: args.txt in the base dir overrides the defaults.
	static char line[4096];
	snprintf( line, sizeof( line ), "%s", ZMR_PS5_DEFAULT_ARGS );
	if ( FILE *f = fopen( "args.txt", "r" ) )
	{
		size_t n = fread( line, 1, sizeof( line ) - 1, f );
		line[n] = 0;
		fclose( f );
	}
	// Controller defaults for the game named by -game (default hl2).
	char cfgGame[64] = "hl2";
	if ( const char *g = strstr( line, "-game " ) )
		sscanf( g + 6, "%63s", cfgGame );
	ps5_gltrace_parse( line );
	g_ps5Probe = strstr( line, "-ps5_probe" ) != NULL;
	if ( strstr( line, "-ps5_nogpublit" ) ) setenv( "PS5_GPU_BLIT", "0", 1 );
	if ( const char *q = strstr( line, "-ps5_quit" ) )
		g_quitMode = q[9] >= '0' && q[9] <= '9' ? q[9] - '0' : 1;
	if ( strstr( line, "-ps5_nogpupresent" ) ) setenv( "PS5_GPU_PRESENT_BLIT", "0", 1 );
	if ( const char *v = strstr( line, "-ps5_viewfmt" ) )
	{
		char mode[2] = { v[12] ? v[12] : '1', 0 };
		setenv( "PS5_BLIT_VIEWFMT", mode, 1 );
	}
	if ( strstr( line, "-ps5_prof" ) )
	{
		pthread_t prof;
		pthread_create( &prof, NULL, ProfThread, NULL );
	}
	static char *argv[128];
	static char exe[300];
	snprintf( exe, sizeof( exe ), "%s/hl2_linux", g_baseDir );
	argv[0] = exe;
	// HL2's PC client only runs 360controller.cfg once "joystick 1" is already
	// set, which the PC defaults never do: exec it from the command line.
	if ( !strstr( line, "360controller" ) )
		strncat( line, " +exec 360controller.cfg", sizeof( line ) - strlen( line ) - 1 );
	int argc = 1 + SplitArgs( line, argv + 1, 126 );
	argv[argc] = NULL;
	printf( "args:" );
	for ( int i = 1; i < argc; ++i )
		printf( " %s", argv[i] );
	printf( "\n" );

	// Our title ID, from the packaged param.json (so a renamed build needs no code change).
	static char titleId[16] = "PPSA20040";
	if ( FILE *pj = fopen( "/app0/sce_sys/param.json", "r" ) )
	{
		static char buf[8192];
		size_t n = fread( buf, 1, sizeof( buf ) - 1, pj );
		buf[n] = 0;
		fclose( pj );
		if ( const char *k = strstr( buf, "\"titleId\"" ) )
			if ( ( k = strchr( k + 9, '"' ) ) )
				sscanf( k + 1, "%9[A-Z0-9]", titleId );
	}
	printf( "[zmr] title %s\n", titleId );
	zmr_fs_diagnose( titleId );
	zmr_fs_init( titleId );
	WriteControllerConfig( cfgGame );
	// Show what the app can see of its packaged game files (helps users check
	// that hl2/ and platform/ ended up in the right folder).
	{
		static const char *dirs[] = { "/app0", "/app0/game", "/app0/game/hl2", "/app0/game/platform", NULL };
		for ( int i = 0; dirs[i]; ++i )
		{
			DIR *d = opendir( dirs[i] );
			printf( "[zmr] listing %s:%s", dirs[i], d ? "" : " (cannot open)" );
			int n = 0;
			if ( d )
			{
				struct dirent *e;
				while ( ( e = readdir( d ) ) && n < 25 )
				{
					if ( e->d_name[0] == '.' )
						continue;
					printf( " %s", e->d_name );
					++n;
				}
				closedir( d );
			}
			printf( "\n" );
		}
		printf( "[zmr] hl2/gameinfo.txt visible: %s\n", access( "hl2/gameinfo.txt", 0 ) == 0 ? "yes" : "NO" );
	}
	setenv( "HOME", g_baseDir, 1 );
	setenv( "TMPDIR", g_baseDir, 1 );

	void *launcher = ps5_dlopen( "launcher", 0 );
	LauncherMain_t launcherMain = (LauncherMain_t)ps5_dlsym( launcher, "LauncherMain" );
	if ( !launcherMain )
	{
		printf( "LauncherMain missing from static build\n" );
		return 1;
	}
	int ret = launcherMain( argc, argv );
	printf( "LauncherMain returned %d\n", ret );
	exit( ret );
}

// Quitting: the engine's static destructors and atexit handlers run against
// a half torn-down engine and GPU context, which ends in a crash (and the
// system's error dialog). Everything that matters (configs, saves) is
// already written by the engine's own shutdown, so leave immediately.
extern "C" void __real_exit( int status );
extern "C" int sceSystemServiceGetAppIdOfRunningBigApp( void );
extern "C" int sceSystemServiceKillApp( int appId, int opt, int method, int reason );
extern "C" int sceSystemServiceLoadExec( const char *path, char *const argv[] );

extern "C" void __wrap_exit( int status )
{
	printf( "[zmr] exit(%d): leaving without running destructors (quit mode %d)\n", status, g_quitMode );
	fflush( NULL );
	DrainLogToKlog();
	switch ( g_quitMode )
	{
	case 1: {
		int app = sceSystemServiceGetAppIdOfRunningBigApp();
		printf( "[zmr] quit: KillApp(%#x)\n", app ); fflush( NULL ); DrainLogToKlog();
		int rc = sceSystemServiceKillApp( app, -1, 0, 0 );
		printf( "[zmr] quit: KillApp returned %#x\n", rc ); fflush( NULL ); DrainLogToKlog();
		sleep( 2 );
		break; }
	case 2: {
		int rc = sceSystemServiceLoadExec( "exit", NULL );
		printf( "[zmr] quit: LoadExec(exit) returned %#x\n", rc ); fflush( NULL ); DrainLogToKlog();
		sleep( 2 );
		break; }
	case 4:
		kill( getpid(), SIGKILL );
		sleep( 2 );
		break;
	default:
		break;
	}
	_exit( status );
}
