// Static "dynamic loader" for the PS5 single-executable build.
//
// Source normally dlopen()s ~25 shared modules and dlsym()s their
// CreateInterface entry point. On PS5 every module is partially linked
// (ld -r), its internal symbols are localized, and its exports are renamed
// to <module>__<symbol>. ps5/staticlink.py generates g_ps5_static_symbols
// and rewrites each module's dlopen/dlsym/dlclose/dlerror references to the
// functions below.
#include <string.h>
#include <stdio.h>
#include <stdint.h>

struct PS5StaticSymbol
{
	const char *module;
	const char *name;
	void *addr;
};

extern "C" const PS5StaticSymbol g_ps5_static_symbols[]; // NULL-terminated

namespace
{
char g_error[512];
bool g_hasError = false;
// Handle for "the whole program" (dlopen(NULL) / RTLD_DEFAULT / RTLD_NEXT).
const char g_selfHandle = 0;

void SetError( const char *fmt, const char *arg )
{
	snprintf( g_error, sizeof( g_error ), fmt, arg );
	g_hasError = true;
}

// "/data/zmr/bin/libengine.so" -> "engine", "client.so" -> "client"
void ModuleName( const char *path, char *out, size_t size )
{
	const char *base = path;
	for ( const char *p = path; *p; ++p )
		if ( *p == '/' || *p == '\\' )
			base = p + 1;
	if ( !strncmp( base, "lib", 3 ) && strcmp( base, "lib" ) )
		base += 3;
	snprintf( out, size, "%s", base );
	char *dot = strrchr( out, '.' );
	if ( dot && ( !strcmp( dot, ".so" ) || !strcmp( dot, ".dll" ) || !strcmp( dot, ".dylib" ) ) )
		*dot = 0;
}
} // namespace

extern "C" void *ps5_dlopen( const char *path, int /*mode*/ )
{
	if ( !path )
		return (void *)&g_selfHandle;
	char name[256];
	ModuleName( path, name, sizeof( name ) );
	for ( const PS5StaticSymbol *s = g_ps5_static_symbols; s->module; ++s )
		if ( !strcmp( s->module, name ) )
			return (void *)s->module; // the module-name string doubles as the handle
	SetError( "%s: module is not linked into this PS5 build", path );
	printf( "[ps5_dl] dlopen(%s): not built in\n", path );
	return NULL;
}

extern "C" void *ps5_dlsym( void *handle, const char *symbol )
{
	const bool any = !handle || handle == (void *)&g_selfHandle || (intptr_t)handle == -1 || (intptr_t)handle == -2;
	for ( const PS5StaticSymbol *s = g_ps5_static_symbols; s->module; ++s )
	{
		if ( ( any || !strcmp( s->module, (const char *)handle ) ) && !strcmp( s->name, symbol ) )
			return s->addr;
	}
	SetError( "%s: symbol not exported by static module", symbol );
	return NULL;
}

extern "C" int ps5_dlclose( void * )
{
	return 0;
}

extern "C" char *ps5_dlerror( void )
{
	if ( !g_hasError )
		return NULL;
	g_hasError = false;
	return g_error;
}
