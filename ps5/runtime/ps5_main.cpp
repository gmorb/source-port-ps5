// PS5 entry point for the statically linked Source engine.
#include <errno.h>
#include <math.h>
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
extern "C" void zmr_memdiag_print( unsigned n );
extern "C" void zmr_hid_preload( void );
extern "C" void zmr_crumb_note( const char *what, unsigned a, unsigned b );
extern "C" void zmr_crumbs_heartbeat( void );
extern "C" void zmr_crumbs_enable( int on );
extern "C" void zmr_crumbs_stall( unsigned long rip_offset, unsigned long frames );
extern "C" void zmr_crumbs_init( const char *dir, void ( *out )( const char * ) );
static void KlogOut( const char *s ) { sceKernelDebugOutText( 0, s ); }
extern "C" void zmr_breadcrumbs_dump( void (*out)( const char * ) );
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
extern "C" unsigned long zmr_frame_count( void ) { return g_frames; }
static pthread_t g_mainThread;
static unsigned long g_mainStackHi;	// a frame address near the top of the main thread's stack
static bool g_ps5Probe;
static int g_quitMode = 0; // -ps5_quitN: 0 auto (LoadExec, then KillApp), 1 KillApp, 2 LoadExec(exit), 3 _exit, 4 SIGKILL
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
	zmr_crumb_note( "swap begin", 0, 0 );
	__real_SDL_GL_SwapWindow( window );
	zmr_crumb_note( "swap end", 0, 0 );
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
		static unsigned s_nPrints = 0;
		if ( s_nPrints++ % 12 == 0 )	// about once a minute
			zmr_memdiag_print( s_nPrints / 12 );
		// Keep the log from filling the app's 256 MiB data area: stdout/stderr
		// are append-mode streams, so truncating the file is safe.
		char logPath[300];
		struct stat st;
		snprintf( logPath, sizeof( logPath ), "%s/zmr.log", g_baseDir );
		if ( stat( logPath, &st ) == 0 && st.st_size > 32 * 1024 * 1024 )
		{
			int tfd = open( logPath, O_WRONLY | O_TRUNC );	// no truncate()/statfs() in the PS5 libc
			if ( tfd >= 0 ) close( tfd );
			printf( "[zmr] zmr.log reached 32 MiB and was cleared (frame %lu)\n", g_frames );
		}
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

// hang.txt in the app's data: the freeze report, for fetching over FTP.
static int g_hangFd = -1;
static void HangFileWrite( const char *text );
static void HangOut( const char *text ) { sceKernelDebugOutText( 0, text ); HangFileWrite( text ); }
static void HangFileWrite( const char *text )
{
	if ( g_hangFd < 0 && g_baseDir[0] )
	{
		char path[300];
		snprintf( path, sizeof( path ), "%s/hang.txt", g_baseDir );
		g_hangFd = open( path, O_WRONLY | O_CREAT | O_TRUNC, 0666 );
	}
	if ( g_hangFd >= 0 )
		write( g_hangFd, text, strlen( text ) );
}

static void DumpThreadHandler( int, siginfo_t *, void *ctx )
{
	unsigned long base = (unsigned long)( __executable_start ? __executable_start : 0 );
	unsigned long textEnd = base + 0x2b00000;
	unsigned long rip = CtxReg( ctx, REG_RIP ), rsp = CtxReg( ctx, REG_RSP ), rbp = CtxReg( ctx, REG_RBP );
	char line[200];
	zmr_crumbs_stall( rip - base, g_frames );
	snprintf( line, sizeof( line ), "[zmr] main thread: rip=%#lx (offset %#lx) rsp=%#lx rbp=%#lx\n", rip, rip - base, rsp, rbp );
	sceKernelDebugOutText( 0, line );
	HangFileWrite( line );
	if ( rsp > 0x10000 && ( rsp & 7 ) == 0 )
	{
		unsigned long *sp = (unsigned long *)rsp;
		// Never read past the top of the stack (that faults: 033 crashed
		// here during a long load).
		int words = 4096;
		if ( g_mainStackHi > rsp && ( g_mainStackHi - rsp ) / 8 < (unsigned long)words )
			words = (int)( ( g_mainStackHi - rsp ) / 8 );
		else if ( g_mainStackHi <= rsp )
			words = 0;
		for ( int i = 0, n = 0; i < words && n < 48; ++i )
		{
			unsigned long v = sp[i];
			if ( v > base && v < textEnd )
			{
				snprintf( line, sizeof( line ), "[zmr]   stack[%d] offset %#lx\n", i, v - base );
				sceKernelDebugOutText( 0, line );
				HangFileWrite( line );
				++n;
			}
		}
	}
	sceKernelDebugOutText( 0, "[zmr] main thread dump done\n" );
	HangFileWrite( "[zmr] main thread dump done\n" );
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
	const int limit = 5; // seconds without a new frame
	for ( ;; )
	{
		for ( int q = 0; q < 4; ++q )
		{
			usleep( 250000 );
			zmr_crumbs_heartbeat();
		}
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
			// Straight to the kernel log and a file: stdio may be locked by
			// the stuck thread.
			char msg[160];
			snprintf( msg, sizeof( msg ), "[zmr] WATCHDOG: no frame for %d s after %lu frames; dumping main thread\n", stalled, now );
			sceKernelDebugOutText( 0, msg );
			HangFileWrite( msg );
			zmr_breadcrumbs_dump( HangOut );
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
		"hud_fastswitch 2\n"                      /* console-style weapon menu: weapon pictures around the crosshair */
		"con_enable 1\n"
		"cl_drawmonitors 1\n"                     /* live camera screens on (being fixed: see Troubleshooting tests) */
		"r_glint_procedural 1\n"                  /* eye glints by texture upload, not a shared render target (Barney freeze, white Vortigaunt eyes) */                          /* console: Square in menus, or the ` key */
		"cl_software_cursor 1\n"                  /* draw the menu pointer (no OS cursor on PS5) */
		"alias sprint_toggle sprint_on\n"
		"alias sprint_on \"+speed; alias sprint_toggle sprint_off\"\n"
		"alias sprint_off \"-speed; alias sprint_toggle sprint_on\"\n"
		"alias duck_toggle duck_on\n"
		"alias duck_on \"+duck; alias duck_toggle duck_off\"\n"
		"alias duck_off \"-duck; alias duck_toggle duck_on\"\n"
		"bind A_BUTTON \"+jump\"\n"              /* Cross */
		"bind B_BUTTON \"duck_toggle\"\n"        /* Circle: crouch on/off */
		"bind X_BUTTON \"+reload\"\n"            /* Square */
		"bind Y_BUTTON \"+use\"\n"               /* Triangle */
		"bind R_TRIGGER \"+attack\"\n"           /* R2 */
		"bind L_TRIGGER \"+attack2\"\n"          /* L2 */
		"bind R_SHOULDER \"invnext\"\n"          /* R1 */
		/* Hold L1: weapon shortcuts on the D-pad (slots 1-4), R1 (slot 5) and
		 * Square (slot 6); press a direction again to cycle within the slot. */
		"alias flashlight_toggle \"impulse 100\"\n"
		"alias +weaponmenu \"_ps5_weaponmenu_down; bind UP slot1; bind RIGHT slot2; bind DOWN slot3; bind LEFT slot4; bind R_SHOULDER slot5; bind X_BUTTON slot6\"\n"
		"alias -weaponmenu \"_ps5_weaponmenu_up; bind UP flashlight_toggle; bind RIGHT +commandermousemove; bind DOWN lastinv; bind LEFT phys_swap; bind R_SHOULDER invnext; bind X_BUTTON +reload\"\n"
		"bind L_SHOULDER \"+weaponmenu\"\n"      /* L1 (hold) */
		"bind STICK1 \"sprint_toggle\"\n"        /* L3: sprint on/off */
		"bind STICK2 \"+zoom\"\n"                /* R3 (hold) */
		"bind UP \"flashlight_toggle\"\n"              /* D-pad up: flashlight */
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

// Menu pointer images for cl_software_cursor (vgui/cursors/*). Written into the
// writable layer so menus show a pointer even if the game files lack them.
static void WriteVTF( const char *path, int size, const unsigned char *bgra )
{
	FILE *f = fopen( path, "wb" );
	if ( !f ) { printf( "[zmr] could not write %s\n", path ); return; }
	unsigned char hdr[80];
	memset( hdr, 0, sizeof( hdr ) );
	memcpy( hdr, "VTF", 4 );
	unsigned int u;
	unsigned short us;
	u = 7; memcpy( hdr + 4, &u, 4 );
	u = 2; memcpy( hdr + 8, &u, 4 );
	u = 80; memcpy( hdr + 12, &u, 4 );
	us = (unsigned short)size; memcpy( hdr + 16, &us, 2 ); memcpy( hdr + 18, &us, 2 );
	u = 0x4 | 0x8 | 0x100 | 0x200 | 0x2000; memcpy( hdr + 20, &u, 4 );	// clamp, nomip, nolod, 8-bit alpha
	us = 1; memcpy( hdr + 24, &us, 2 );
	float refl = 0.5f; memcpy( hdr + 32, &refl, 4 ); memcpy( hdr + 36, &refl, 4 ); memcpy( hdr + 40, &refl, 4 );
	float bump = 1.0f; memcpy( hdr + 48, &bump, 4 );
	u = 12; memcpy( hdr + 52, &u, 4 );			// IMAGE_FORMAT_BGRA8888
	hdr[56] = 1;								// mip count
	u = 0xFFFFFFFFu; memcpy( hdr + 57, &u, 4 );	// no low-res thumbnail
	us = 1; memcpy( hdr + 63, &us, 2 );			// depth
	fwrite( hdr, 1, sizeof( hdr ), f );
	fwrite( bgra, 1, size * size * 4, f );
	fclose( f );
}

static bool InPoly( float x, float y, const float *pts, int n )
{
	bool in = false;
	for ( int i = 0, j = n - 1; i < n; j = i++ )
	{
		float xi = pts[i * 2], yi = pts[i * 2 + 1], xj = pts[j * 2], yj = pts[j * 2 + 1];
		if ( ( ( yi > y ) != ( yj > y ) ) && ( x < ( xj - xi ) * ( y - yi ) / ( yj - yi ) + xi ) )
			in = !in;
	}
	return in;
}

static void WriteCursorImages( const char *game )
{
	enum { S = 64 };
	static unsigned char arrow[S * S * 4], ring[S * S * 4];
	static unsigned char fill[S * S], edge[S * S];
	// Classic pointer, tip near (4,4); the engine offsets the arrow by -10%.
	static const float pts[] = { 4,4, 4,50, 15,39, 23,57, 31,53, 23,36, 38,36 };
	for ( int y = 0; y < S; ++y )
		for ( int x = 0; x < S; ++x )
			fill[y * S + x] = InPoly( x + 0.5f, y + 0.5f, pts, 7 );
	for ( int y = 0; y < S; ++y )
		for ( int x = 0; x < S; ++x )
		{
			int e = 0;
			for ( int dy = -2; dy <= 2 && !e; ++dy )
				for ( int dx = -2; dx <= 2 && !e; ++dx )
				{
					int xx = x + dx, yy = y + dy;
					if ( xx >= 0 && yy >= 0 && xx < S && yy < S && fill[yy * S + xx] )
						e = 1;
				}
			edge[y * S + x] = e;
		}
	for ( int i = 0; i < S * S; ++i )
	{
		unsigned char *p = arrow + i * 4;
		if ( fill[i] ) { p[0] = p[1] = p[2] = 255; p[3] = 255; }
		else if ( edge[i] ) { p[0] = p[1] = p[2] = 0; p[3] = 230; }
		else { p[0] = p[1] = p[2] = 0; p[3] = 0; }
	}
	// Centered ring for the other cursor shapes (their hotspot is the center).
	for ( int y = 0; y < S; ++y )
		for ( int x = 0; x < S; ++x )
		{
			float dx = x + 0.5f - S / 2, dy = y + 0.5f - S / 2;
			float d = sqrtf( dx * dx + dy * dy );
			unsigned char *p = ring + ( y * S + x ) * 4;
			if ( d >= 10 && d <= 15 ) { p[0] = p[1] = p[2] = 255; p[3] = 255; }
			else if ( ( d >= 7.5f && d < 10 ) || ( d > 15 && d <= 17.5f ) || d < 3 ) { p[0] = p[1] = p[2] = 0; p[3] = 220; }
			else { p[0] = p[1] = p[2] = 0; p[3] = 0; }
		}

	char dir[300];
	snprintf( dir, sizeof( dir ), "%s/%s", g_baseDir, game ); mkdir( dir, 0777 );
	snprintf( dir, sizeof( dir ), "%s/%s/materials", g_baseDir, game ); mkdir( dir, 0777 );
	snprintf( dir, sizeof( dir ), "%s/%s/materials/vgui", g_baseDir, game ); mkdir( dir, 0777 );
	snprintf( dir, sizeof( dir ), "%s/%s/materials/vgui/cursors", g_baseDir, game ); mkdir( dir, 0777 );
	static const char *names[] = { "arrow", "ibeam", "hourglass", "crosshair", "waitarrow", "up",
		"sizenwse", "sizenesw", "sizewe", "sizens", "sizeall", "no", "hand", NULL };
	for ( int i = 0; names[i]; ++i )
	{
		char path[400];
		snprintf( path, sizeof( path ), "%s/%s.vtf", dir, names[i] );
		WriteVTF( path, S, i == 0 || i == 4 ? arrow : ring );
		snprintf( path, sizeof( path ), "%s/%s.vmt", dir, names[i] );
		if ( FILE *f = fopen( path, "w" ) )
		{
			fprintf( f, "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"vgui/cursors/%s\"\n\t\"$translucent\" 1\n"
				"\t\"$vertexcolor\" 1\n\t\"$vertexalpha\" 1\n\t\"$ignorez\" 1\n\t\"$nolod\" 1\n}\n", names[i] );
			fclose( f );
		}
	}
	printf( "[zmr] wrote menu cursor images to %s\n", dir );
}

// The Triangle command menu's list (generated into zmr_commands.c by
// gen_commands.py). Replaced when this build's list is newer than the file;
// the player's own entries live in ps5_commands_custom.txt, never touched.
extern "C" const int zmr_commands_version;
extern "C" const char zmr_commands_text[];
static void WriteCommandList( const char *game )
{
	char path[300];
	snprintf( path, sizeof( path ), "%s/%s/cfg/ps5_commands.txt", g_baseDir, game );
	int have = -1;
	if ( FILE *f = fopen( path, "r" ) )
	{
		char line[128] = "";
		if ( fgets( line, sizeof( line ), f ) )
			sscanf( line, "// ps5-commands-version %d", &have );
		fclose( f );
	}
	if ( have != zmr_commands_version )
	{
		if ( FILE *f = fopen( path, "w" ) )
		{
			fputs( zmr_commands_text, f );
			fclose( f );
			printf( "[zmr] wrote %s (version %d)\n", path, zmr_commands_version );
		}
	}
	snprintf( path, sizeof( path ), "%s/%s/cfg/ps5_commands_custom.txt", g_baseDir, game );
	struct stat st;
	if ( stat( path, &st ) != 0 )
	{
		if ( FILE *f = fopen( path, "w" ) )
		{
			fputs( "// Your own entries for the Triangle command menu (shown as \"My commands\").\n"
				"// Copy the example block, change the text, and keep each entry's number unique.\n"
				"\"Commands\"\n{\n"
				"\t\"1\"\n\t{\n\t\t\"label\"\t\"Example: say hello\"\n\t\t\"command\"\t\"echo Hello from the PS5\"\n"
				"\t\t\"description\"\t\"Prints a line in the console. Replace with any console command.\"\n\t}\n}\n", f );
			fclose( f );
		}
	}
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

	// Previous session: keep its log as zmr-prev.log and replay the freeze
	// report and the log's last lines into the kernel log, so a freeze or
	// crash can be diagnosed from the next launch's capture.
	{
		char path[300], prev[300], hang[300];
		snprintf( path, sizeof( path ), "%s/zmr.log", g_baseDir );
		snprintf( prev, sizeof( prev ), "%s/zmr-prev.log", g_baseDir );
		snprintf( hang, sizeof( hang ), "%s/hang.txt", g_baseDir );
		unlink( prev );
		rename( path, prev );
		int fd = open( hang, O_RDONLY );
		if ( fd >= 0 )
		{
			sceKernelDebugOutText( 0, "[zmr] ---- previous session froze; its report (hang.txt): ----\n" );
			char buf[1025];
			ssize_t n;
			while ( ( n = read( fd, buf, sizeof( buf ) - 1 ) ) > 0 )
			{
				buf[n] = 0;
				sceKernelDebugOutText( 0, buf );
			}
			close( fd );
			char old[300];
			snprintf( old, sizeof( old ), "%s/hang-prev.txt", g_baseDir );
			unlink( old );
			rename( hang, old );
		}
		fd = open( prev, O_RDONLY );
		if ( fd >= 0 )
		{
			// Last ~256 KB, without the driver's statistics noise.
			off_t end = lseek( fd, 0, SEEK_END );
			lseek( fd, end > 262144 ? end - 262144 : 0, SEEK_SET );
			static char tail[262145];
			ssize_t n = read( fd, tail, sizeof( tail ) - 1 );
			close( fd );
			if ( n > 0 )
			{
				tail[n] = 0;
				sceKernelDebugOutText( 0, "[zmr] ---- end of the previous session's log: ----\n" );
				char *line = strchr( tail, '\n' );	// skip the partial first line
				line = line ? line + 1 : tail;
				while ( line && *line )
				{
					char *nl = strchr( line, '\n' );
					if ( nl ) *nl = 0;
					// Driver statistics are noise, but its error lines (printed
					// just before it stops the app) are what we need.
					bool driverError = strstr( line, "fail" ) || strstr( line, "terminat" ) ||
						strstr( line, "reuse" ) || strstr( line, "error" ) || strstr( line, "abort" );
					if ( driverError || ( strncmp( line, "[ps5-", 5 ) && strncmp( line, "[gltrace]", 9 ) ) )
					{
						char out[600];
						snprintf( out, sizeof( out ), "[prev] %s\n", line );
						sceKernelDebugOutText( 0, out );
					}
					line = nl ? nl + 1 : NULL;
				}
				sceKernelDebugOutText( 0, "[zmr] ---- end of previous session ----\n" );
			}
		}
	}

	// Draw records that survive a GPU hang (see zmr_hid.c); reports the last
	// session's unfinished GPU work first.
	zmr_crumbs_init( g_baseDir, KlogOut );

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
	zmr_hid_preload();	// keyboard, mouse and on-screen keyboard system modules
	atexit( DrainAtExit );
	g_mainThread = pthread_self();
	g_mainStackHi = (unsigned long)__builtin_frame_address( 0 );
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
	if ( strstr( line, "-ps5_freeze_recorder" ) ) zmr_crumbs_enable( 1 );
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
	WriteCursorImages( cfgGame );
	WriteCommandList( cfgGame );
	// -condebug appends to console.log across sessions; start fresh when it gets big.
	{
		char p[300];
		struct stat st;
		snprintf( p, sizeof( p ), "%s/%s/console.log", g_baseDir, cfgGame );
		if ( stat( p, &st ) == 0 && st.st_size > 16 * 1024 * 1024 )
			unlink( p );
	}
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

// The GL driver stops the app with _Exit() or abort() when it cannot safely
// continue (for example a display flip that never completes). _Exit is a
// blocked syscall for apps, so the game just froze with no message. Report
// it, then leave through the normal quit path so the player gets back to the
// home screen instead of a frozen picture.
extern "C" void __wrap_exit( int status );
static void DriverStop( const char *how, int code )
{
	char msg[200];
	snprintf( msg, sizeof( msg ), "[zmr] DRIVER STOP: %s(%d) - the graphics driver gave up; quitting to the home screen\n", how, code );
	fflush( NULL );
	DrainLogToKlog();	// the driver's own reason, printed just before
	sceKernelDebugOutText( 0, msg );
	HangFileWrite( msg );
	zmr_crumb_note( how, (unsigned)code, 0 );
	__wrap_exit( 1 );
}
extern "C" void __wrap__Exit( int status ) { DriverStop( "_Exit", status ); for ( ;; ) sleep( 1 ); }
extern "C" void __wrap_abort( void ) { DriverStop( "abort", 0 ); for ( ;; ) sleep( 1 ); }

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
	case 0: {
		// _exit() is a blocked syscall for apps (SIGSYS -> crash report), so
		// ask the system to close the app the way SDL's PS5 main does.
		int rc = sceSystemServiceLoadExec( "exit", NULL );
		printf( "[zmr] quit: LoadExec(exit) returned %#x\n", rc ); fflush( NULL ); DrainLogToKlog();
		sleep( 2 );
		int app = sceSystemServiceGetAppIdOfRunningBigApp();
		rc = sceSystemServiceKillApp( app, -1, 0, 0 );
		printf( "[zmr] quit: KillApp(%#x) returned %#x\n", app, rc ); fflush( NULL ); DrainLogToKlog();
		sleep( 2 );
		break; }
	default:
		break;
	}
	_exit( status );
}
