// PS5 draw tracing / state overrides for togl (diagnostics for black frames).
// togl calls ps5_draw_pre/ps5_draw_post around every glDrawRangeElementsBaseVertex.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" unsigned long ps5_get_frames( void );

static bool g_noBlend, g_noDepth, g_noCull, g_noScissor, g_white, g_noStencil;
static unsigned long g_traceFrames[4] = { 0, 0, 0, 0 };
static unsigned long g_curFrame = ~0ul;
static int g_drawInFrame;
static unsigned long g_totalDraws;

extern "C" void ps5_gltrace_parse( const char *line )
{
	g_noBlend = strstr( line, "-ps5_noblend" ) != NULL;
	g_noDepth = strstr( line, "-ps5_nodepth" ) != NULL;
	g_noCull = strstr( line, "-ps5_nocull" ) != NULL;
	g_noScissor = strstr( line, "-ps5_noscissor" ) != NULL;
	g_noStencil = strstr( line, "-ps5_nostencil" ) != NULL;
	g_white = strstr( line, "-ps5_white" ) != NULL;
	if ( strstr( line, "-ps5_gltrace" ) ) { g_traceFrames[0] = 12; g_traceFrames[1] = 13; g_traceFrames[2] = 300; }
	printf( "[gltrace] overrides: noblend=%d nodepth=%d nocull=%d noscissor=%d nostencil=%d white=%d\n",
		g_noBlend, g_noDepth, g_noCull, g_noScissor, g_noStencil, g_white );
}

static bool Tracing( unsigned long f )
{
	for ( unsigned long t : g_traceFrames )
		if ( t && t == f )
			return true;
	return false;
}

static void FrameStats( const char *tag )
{
	GLint fbo = 0, vp[4];
	glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fbo );
	glGetIntegerv( GL_VIEWPORT, vp );
	GLint prevRead = 0;
	glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &prevRead );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
	GLint w = vp[2] > 0 ? vp[2] : 1920, h = vp[3] > 0 ? vp[3] : 1080;
	if ( w > 1920 ) w = 1920;
	if ( h > 1080 ) h = 1080;
	static unsigned char *buf;
	if ( !buf ) buf = (unsigned char *)malloc( 1920 * 1080 * 4 );
	GLboolean scis = glIsEnabled( GL_SCISSOR_TEST );
	glDisable( GL_SCISSOR_TEST );
	glReadPixels( vp[0], vp[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf );
	if ( scis ) glEnable( GL_SCISSOR_TEST );
	long lit = 0; unsigned maxv = 0, maxa = 0, mina = 255;
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for ( int y = 0; y < h; y += 2 )
		for ( int x = 0; x < w; x += 2 )
		{
			unsigned char *p = buf + ( (size_t)y * w + x ) * 4;
			unsigned m = p[0] > p[1] ? p[0] : p[1];
			if ( p[2] > m ) m = p[2];
			if ( m > maxv ) maxv = m;
			if ( p[3] > maxa ) maxa = p[3];
			if ( p[3] < mina ) mina = p[3];
			if ( m > 8 )
			{
				++lit;
				if ( x < x0 ) x0 = x; if ( y < y0 ) y0 = y;
				if ( x > x1 ) x1 = x; if ( y > y1 ) y1 = y;
			}
		}
	glBindFramebuffer( GL_READ_FRAMEBUFFER, prevRead );
	printf( "[gltrace]   %s fbo=%d vp=%d,%d %dx%d lit=%ld max=%u alpha=%u..%u box=(%d,%d)-(%d,%d) err=0x%x\n",
		tag, fbo, vp[0], vp[1], vp[2], vp[3], lit, maxv, mina, maxa, x0, y0, x1, y1, glGetError() );
}

static void DumpState( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint baseVertex )
{
	GLint prog = 0, fbo = 0, vp[4], sc[4], ebo = 0, vao = 0;
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
	glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fbo );
	glGetIntegerv( GL_VIEWPORT, vp );
	glGetIntegerv( GL_SCISSOR_BOX, sc );
	glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo );
	glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &vao );
	GLint bsrc = 0, bdst = 0, bsrca = 0, bdsta = 0, beq = 0, dfunc = 0, cullm = 0, front = 0;
	GLboolean cm[4], dmask;
	glGetIntegerv( GL_BLEND_SRC_RGB, &bsrc ); glGetIntegerv( GL_BLEND_DST_RGB, &bdst );
	glGetIntegerv( GL_BLEND_SRC_ALPHA, &bsrca ); glGetIntegerv( GL_BLEND_DST_ALPHA, &bdsta );
	glGetIntegerv( GL_BLEND_EQUATION_RGB, &beq );
	glGetIntegerv( GL_DEPTH_FUNC, &dfunc ); glGetIntegerv( GL_CULL_FACE_MODE, &cullm ); glGetIntegerv( GL_FRONT_FACE, &front );
	glGetBooleanv( GL_COLOR_WRITEMASK, cm ); glGetBooleanv( GL_DEPTH_WRITEMASK, &dmask );
	printf( "[gltrace] f%lu d%d mode=0x%x range=%u..%u count=%d type=0x%x idx=%p base=%d prog=%d fbo=%d vao=%d ebo=%d\n",
		g_curFrame, g_drawInFrame, mode, start, end, count, type, indices, baseVertex, prog, fbo, vao, ebo );
	printf( "[gltrace]   vp=%d,%d,%d,%d scissor=%d (%d,%d,%d,%d) blend=%d src=0x%x dst=0x%x srcA=0x%x dstA=0x%x eq=0x%x mask=%d%d%d%d\n",
		vp[0], vp[1], vp[2], vp[3], glIsEnabled( GL_SCISSOR_TEST ), sc[0], sc[1], sc[2], sc[3],
		glIsEnabled( GL_BLEND ), bsrc, bdst, bsrca, bdsta, beq, cm[0], cm[1], cm[2], cm[3] );
	printf( "[gltrace]   depth=%d func=0x%x dmask=%d stencil=%d cull=%d mode=0x%x front=0x%x alphatest=%d srgb=%d logic=%d\n",
		glIsEnabled( GL_DEPTH_TEST ), dfunc, dmask, glIsEnabled( GL_STENCIL_TEST ), glIsEnabled( GL_CULL_FACE ), cullm, front,
		glIsEnabled( GL_ALPHA_TEST ), glIsEnabled( GL_FRAMEBUFFER_SRGB ), glIsEnabled( GL_COLOR_LOGIC_OP ) );
	if ( fbo )
	{
		GLint ot = 0, on = 0, w = 0, h = 0, fmt = 0;
		glGetFramebufferAttachmentParameteriv( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &ot );
		glGetFramebufferAttachmentParameteriv( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &on );
		if ( ot == GL_TEXTURE )
		{
			GLint prev; glGetIntegerv( GL_TEXTURE_BINDING_2D, &prev );
			glBindTexture( GL_TEXTURE_2D, on );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt );
			glBindTexture( GL_TEXTURE_2D, prev );
		}
		printf( "[gltrace]   rt0 type=0x%x name=%d %dx%d ifmt=0x%x status=0x%x\n", ot, on, w, h, fmt,
			glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) );
	}
	// First index -> vertex fetch
	GLuint firstIndex = 0;
	if ( ebo )
	{
		if ( type == GL_UNSIGNED_SHORT ) { GLushort v = 0; glGetBufferSubData( GL_ELEMENT_ARRAY_BUFFER, (GLintptr)indices, 2, &v ); firstIndex = v; }
		else { glGetBufferSubData( GL_ELEMENT_ARRAY_BUFFER, (GLintptr)indices, 4, &firstIndex ); }
	}
	printf( "[gltrace]   firstIndex=%u vertex=%d\n", firstIndex, (int)firstIndex + baseVertex );
	for ( int a = 0; a < 16; ++a )
	{
		GLint en = 0;
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &en );
		if ( !en )
		{
			if ( a < 8 )
			{
				float cv[4] = { 0 };
				glGetVertexAttribfv( a, GL_CURRENT_VERTEX_ATTRIB, cv );
				printf( "[gltrace]   attr%d disabled current=(%g %g %g %g)\n", a, cv[0], cv[1], cv[2], cv[3] );
			}
			continue;
		}
		GLint size = 0, atype = 0, norm = 0, stride = 0, buf = 0, isint = 0, div = 0;
		void *ptr = NULL;
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_TYPE, &atype );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &norm );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buf );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &isint );
		glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &div );
		glGetVertexAttribPointerv( a, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr );
		unsigned char raw[16] = { 0 };
		if ( buf )
		{
			GLint prev; glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &prev );
			glBindBuffer( GL_ARRAY_BUFFER, buf );
			GLint64 bsize = 0; glGetBufferParameteri64v( GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bsize );
			GLintptr off = (GLintptr)ptr + (GLintptr)( (int)firstIndex + baseVertex ) * ( stride ? stride : 16 );
			if ( off >= 0 && off + 16 <= bsize )
				glGetBufferSubData( GL_ARRAY_BUFFER, off, 16, raw );
			if ( a == 0 && stride > 0 && stride <= 64 && off >= 0 && off + stride <= bsize )
			{
				unsigned char vtx[64];
				glGetBufferSubData( GL_ARRAY_BUFFER, off, stride, vtx );
				char hex[200]; int o = 0;
				for ( int i = 0; i < stride; ++i ) o += snprintf( hex + o, sizeof hex - o, "%02x%s", vtx[i], ( i & 3 ) == 3 ? " " : "" );
				printf( "[gltrace]   vertex bytes: %s\n", hex );
			}
			glBindBuffer( GL_ARRAY_BUFFER, prev );
		}
		float *fv = (float *)raw;
		printf( "[gltrace]   attr%d size=0x%x type=0x%x norm=%d int=%d stride=%d buf=%d off=%p div=%d raw=%02x%02x%02x%02x %02x%02x%02x%02x f=(%g %g %g %g)\n",
			a, size, atype, norm, isint, stride, buf, ptr, div, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
			fv[0], fv[1], fv[2], fv[3] );
	}
	if ( prog )
	{
		GLint na = 0;
		glGetProgramiv( prog, GL_ACTIVE_ATTRIBUTES, &na );
		for ( int i = 0; i < na && i < 16; ++i )
		{
			char nm[64]; GLint sz = 0; GLenum ty = 0;
			glGetActiveAttrib( prog, i, sizeof nm, NULL, &sz, &ty, nm );
			printf( "[gltrace]   active attrib '%s' loc=%d type=0x%x\n", nm, glGetAttribLocation( prog, nm ), ty );
		}
		static int dumpedProgs[8]; static int nDumped;
		bool seen = false;
		for ( int i = 0; i < nDumped; ++i ) if ( dumpedProgs[i] == prog ) seen = true;
		if ( !seen && nDumped < 8 )
		{
			dumpedProgs[nDumped++] = prog;
			GLuint sh[4]; GLsizei nsh = 0;
			glGetAttachedShaders( prog, 4, &nsh, sh );
			static char src[32768];
			for ( int i = 0; i < nsh; ++i )
			{
				GLint st = 0; glGetShaderiv( sh[i], GL_SHADER_TYPE, &st );
				GLsizei len = 0; glGetShaderSource( sh[i], sizeof src, &len, src );
				printf( "[gltrace]   ---- prog %d shader 0x%x (%d bytes) ----\n", prog, st, len );
				// print line by line so each line gets the log prefix
				char *line = src;
				while ( line && *line )
				{
					char *nl = strchr( line, '\n' );
					if ( nl ) *nl = 0;
					printf( "[glsl] %s\n", line );
					line = nl ? nl + 1 : NULL;
				}
			}
		}
		const char *names[] = { "vc", "pc" };
		for ( int n = 0; n < 2; ++n )
		{
			GLint loc = glGetUniformLocation( prog, names[n] );
			if ( loc < 0 ) { char nm[16]; snprintf( nm, sizeof nm, "%s[0]", names[n] ); loc = glGetUniformLocation( prog, nm ); }
			if ( loc < 0 ) { printf( "[gltrace]   %s: no uniform\n", names[n] ); continue; }
			for ( int i = 0; i < 12; ++i )
			{
				float v[4] = { 0 };
				glGetUniformfv( prog, loc + i, v );
				printf( "[gltrace]   %s[%d]=(%g %g %g %g)\n", names[n], i, v[0], v[1], v[2], v[3] );
			}
		}
		GLint tex = 0, tw = 0, th = 0, tf = 0, prevUnit = 0;
		glGetIntegerv( GL_ACTIVE_TEXTURE, &prevUnit );
		glActiveTexture( GL_TEXTURE0 );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &tex );
		if ( tex )
		{
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf );
		}
		glActiveTexture( prevUnit );
		printf( "[gltrace]   tex0=%d %dx%d ifmt=0x%x\n", tex, tw, th, tf );
	}
	printf( "[gltrace]   pre-err=0x%x\n", glGetError() );
}

extern "C" void ps5_draw_pre( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint baseVertex )
{
	unsigned long f = ps5_get_frames();
	if ( f != g_curFrame )
	{
		if ( g_curFrame != ~0ul && ( Tracing( g_curFrame ) || g_curFrame % 120 == 0 ) )
			printf( "[gltrace] frame %lu had %d draws (total %lu)\n", g_curFrame, g_drawInFrame, g_totalDraws );
		g_curFrame = f;
		g_drawInFrame = 0;
	}
	++g_totalDraws;
	if ( g_noBlend ) glDisable( GL_BLEND );
	if ( g_noDepth ) glDisable( GL_DEPTH_TEST );
	if ( g_noCull ) glDisable( GL_CULL_FACE );
	if ( g_noScissor ) glDisable( GL_SCISSOR_TEST );
	if ( g_noStencil ) glDisable( GL_STENCIL_TEST );
	if ( g_white ) { glEnable( GL_COLOR_LOGIC_OP ); glLogicOp( GL_SET ); glColorMask( 1, 1, 1, 1 ); }
	if ( Tracing( f ) && g_drawInFrame < 48 )
		DumpState( mode, start, end, count, type, indices, baseVertex );
}

extern "C" void ps5_draw_post( void )
{
	if ( g_white ) glDisable( GL_COLOR_LOGIC_OP );
	if ( Tracing( g_curFrame ) && g_drawInFrame < 48 )
	{
		char tag[32];
		snprintf( tag, sizeof tag, "after d%d", g_drawInFrame );
		FrameStats( tag );
	}
	++g_drawInFrame;
}
