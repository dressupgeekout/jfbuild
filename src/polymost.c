/**************************************************************************************************
"POLYMOST" code written by Ken Silverman
Ken Silverman's official web site: http://www.advsys.net/ken

Motivation:
When 3D Realms released the Duke Nukem 3D source code, I thought somebody would do a OpenGL or
Direct3D port. Well, after a few months passed, I saw no sign of somebody working on a true
hardware-accelerated port of Build, just people saying it wasn't possible. Eventually, I realized
the only way this was going to happen was for me to do it myself. First, I needed to port Build to
Windows. I could have done it myself, but instead I thought I'd ask my Australian buddy, Jonathon
Fowler, if he would upgrade his Windows port to my favorite compiler (MSVC) - which he did. Once
that was done, I was ready to start the "POLYMOST" project.

About:
This source file is basically a complete rewrite of the entire rendering part of the Build engine.
There are small pieces in ENGINE.C to activate this code, and other minor hacks in other source
files, but most of it is in here. If you're looking for polymost-related code in the other source
files, you should find most of them by searching for either "polymost" or "rendmode". Speaking of
rendmode, there are now 4 rendering modes in Build:

	rendmode 0: The original code I wrote from 1993-1997
	rendmode 1: Solid-color rendering: my debug code before I did texture mapping
	rendmode 2: Software rendering before I started the OpenGL code (Note: this is just a quick
						hack to make testing easier - it's not optimized to my usual standards!)
	rendmode 3: The OpenGL code

The original Build engine did hidden surface removal by using a vertical span buffer on the tops
and bottoms of walls. This worked nice back in the day, but it it's not suitable for a polygon
engine. So I decided to write a brand new hidden surface removal algorithm - using the same idea
as the original Build - but one that worked with vectors instead of already rasterized data.

Brief history:
06/20/2000: I release Build Source code
04/01/2003: 3D Realms releases Duke Nukem 3D source code
10/04/2003: Jonathon Fowler gets his Windows port working in Visual C
10/04/2003: I start writing POLYMOST.BAS, a new hidden surface removal algorithm for Build that
					works on a polygon level instead of spans.
10/16/2003: Ported POLYMOST.BAS to C inside JonoF KenBuild's ENGINE.C; later this code was split
					out of ENGINE.C and put in this file, POLYMOST.C.
12/10/2003: Started OpenGL code for POLYMOST (rendmode 3)
12/23/2003: 1st public release
01/01/2004: 2nd public release: fixed stray lines, status bar, mirrors, sky, and lots of other bugs.

----------------------------------------------------------------------------------------------------

Todo list (in approximate chronological order):

High priority:
	*   BOTH: Do accurate software sorting/chopping for sprites: drawing in wrong order is bad :/
	*   BOTH: Fix hall of mirrors near "zenith". Call polymost_drawrooms twice?
	* OPENGL: drawmapview()

Low priority:
	* SOFT6D: Do back-face culling of sprites during up/down/tilt transformation (top of drawpoly)
	* SOFT6D: Fix depth shading: use saturation&LUT
	* SOFT6D: Optimize using hyperbolic mapping (similar to KUBE algo)
	* SOFT6D: Slab6-style voxel sprites. How to accelerate? :/
	* OPENGL: KENBUILD: Write flipping code for floor mirrors
	*   BOTH: KENBUILD: Parallaxing sky modes 1&2
	*   BOTH: Masked/1-way walls don't clip correctly to sectors of intersecting ceiling/floor slopes
	*   BOTH: Editart x-center is not working correctly with Duke's camera/turret sprites
	*   BOTH: Get rid of horizontal line above Duke full-screen status bar
	*   BOTH: Combine ceilings/floors into a single triangle strip (should lower poly count by 2x)
	*   BOTH: Optimize/clean up texture-map setup equations

**************************************************************************************************/

static long animateoffs(short tilenum, short fakevar);
long rendmode = 0;
#ifdef USE_PMDBGKEYS
extern char keystatus[256];
#endif
long usemodels=1;

#include <math.h> //<-important!
typedef struct { float x, cy[2], fy[2]; long n, p, tag, ctag, ftag; } vsptyp;
#define VSPMAX 4096 //<- careful!
static vsptyp vsp[VSPMAX];
static long vcnt, gtag;

static double dxb1[MAXWALLSB], dxb2[MAXWALLSB];

#define SCISDIST 1.0 //1.0: Close plane clipping distance
#define USEZBUFFER 1 //1:use zbuffer (slow, nice sprite rendering), 0:no zbuffer (fast, bad sprite rendering)
#define LINTERPSIZ 4 //log2 of interpolation size. 4:pretty fast&acceptable quality, 0:best quality/slow!
#define DEPTHDEBUG 0 //1:render distance instead of texture, for debugging only!, 0:default
#define FOGSCALE 0.00004
#define PI 3.14159265358979323

static double gyxscale, gxyaspect, gviewxrange, ghalfx, grhalfxdown10, grhalfxdown10x, ghoriz;
static double gcosang, gsinang, gcosang2, gsinang2;
static double gchang, gshang, gctang, gstang;
static float gtang = 0.0;
double guo, gux, guy; //Screen-based texture mapping parameters
double gvo, gvx, gvy;
double gdo, gdx, gdy;

#if (USEZBUFFER != 0)
long zbufmem = 0, zbufysiz = 0, zbufbpl = 0, *zbufoff = 0;
#endif

#ifdef USE_OPENGL
long glredbluemode = 0;
static long lastglredbluemode = 0, redblueclearcnt = 0;

static struct glfiltermodes {
	char *name;
	long min,mag;
} glfiltermodes[] = {
	{"GL_NEAREST",GL_NEAREST,GL_NEAREST},
	{"GL_LINEAR",GL_LINEAR,GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST",GL_NEAREST_MIPMAP_NEAREST,GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST",GL_LINEAR_MIPMAP_NEAREST,GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR",GL_NEAREST_MIPMAP_LINEAR,GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR",GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR}
};
#define numglfiltermodes (sizeof(glfiltermodes)/sizeof(glfiltermodes[0]))

long glanisotropy = 1;            // 0 = maximum supported by card
long glusetexcompr = 1;
long gltexfiltermode = 3;   // GL_LINEAR_MIPMAP_NEAREST
long gltexmaxsize = 0;      // 0 means autodetection on first run
extern char nofog;
#endif

#if defined(USE_MSC_PRAGMAS)
static inline void ftol (float f, long *a)
{
	_asm
	{
		mov eax, a
		fld f
		fistp dword ptr [eax]
	}
}

static inline void dtol (double d, long *a)
{
	_asm
	{
		mov eax, a
		fld d
		fistp dword ptr [eax]
	}
}
#elif defined(USE_WATCOM_PRAGMAS)

#pragma aux ftol =\
	"fistp dword ptr [eax]",\
	parm [eax 8087]
#pragma aux dtol =\
	"fistp dword ptr [eax]",\
	parm [eax 8087]

#elif defined(USE_GCC_PRAGMAS)

static inline void ftol (float f, long *a)
{
	__asm__ __volatile__ (
#if 0 //(__GNUC__ >= 3)
			"flds %1; fistpl %0;"
#else
			"flds %1; fistpl (%0);"
#endif
			: "=r" (a) : "m" (f) : "memory","cc");
}

static inline void dtol (double d, long *a)
{
	__asm__ __volatile__ (
#if 0 //(__GNUC__ >= 3)
			"fldl %1; fistpl %0;"
#else
			"fldl %1; fistpl (%0);"
#endif
			: "=r" (a) : "m" (d) : "memory","cc");
}

#else
static inline void ftol (float f, long *a)
{
	*a = (long)f;
}

static inline void dtol (double d, long *a)
{
	*a = (long)d;
}
#endif

static inline long imod (long a, long b)
{
	if (a >= 0) return(a%b);
	return(((a+1)%b)+b-1);
}

void drawline2d (float x1, float y1, float x2, float y2, char col)
{
	float dx, dy, fxresm1, fyresm1, f, tot;
	long i, x, y, xi, yi, xup16, yup16;

		//Always draw lines in same direction
	if ((y2 > y1) || ((y2 == y1) && (x2 > x1))) { f = x1; x1 = x2; x2 = f; f = y1; y1 = y2; y2 = f; }

	dx = x2-x1; dy = y2-y1; if ((dx == 0) && (dy == 0)) return;
	fxresm1 = (float)xdimen-.5; fyresm1 = (float)ydimen-.5;
		  if (x1 >= fxresm1) { if (x2 >= fxresm1) return; y1 += (fxresm1-x1)*dy/dx; x1 = fxresm1; }
	else if (x1 <        0) { if (x2 <        0) return; y1 += (      0-x1)*dy/dx; x1 =       0; }
		  if (x2 >= fxresm1) {                            y2 += (fxresm1-x2)*dy/dx; x2 = fxresm1; }
	else if (x2 <        0) {                            y2 += (      0-x2)*dy/dx; x2 =       0; }
		  if (y1 >= fyresm1) { if (y2 >= fyresm1) return; x1 += (fyresm1-y1)*dx/dy; y1 = fyresm1; }
	else if (y1 <        0) { if (y2 <        0) return; x1 += (      0-y1)*dx/dy; y1 =       0; }
		  if (y2 >= fyresm1) {                            x2 += (fyresm1-y2)*dx/dy; y2 = fyresm1; }
	else if (y2 <        0) {                            x2 += (      0-y2)*dx/dy; y2 =       0; }

	dx = x2-x1; dy = y2-y1;
	i = (long)(max(fabs(dx),fabs(dy))); f = 65536.f/((float)i);
	x = (long)(x1*65536.f)+32768; xi = (long)(dx*f); xup16 = (xdimen<<16);
	y = (long)(y1*65536.f)+32768; yi = (long)(dy*f); yup16 = (ydimen<<16);
	do
	{
		if (((unsigned long)x < (unsigned long)xup16) && ((unsigned long)y < (unsigned long)yup16))
			*(char *)(ylookup[y>>16]+(x>>16)+frameoffset) = col;
		x += xi; y += yi; i--;
	} while (i >= 0);
}

#ifdef USE_OPENGL
typedef struct { unsigned char r, g, b, a; } coltype;

static void uploadtexture(long doalloc, long xsiz, long ysiz, long intexfmt, long texfmt, coltype *pic, long tsizx, long tsizy, long dameth);

static long md2tims, omd2tims;
#include "md2sprite.c"


//--------------------------------------------------------------------------------------------------
//TEXTURE MANAGEMENT: treats same texture with different .PAL as a separate texture. This makes the
//   max number of virtual textures very large (MAXTILES*256). Instead of allocating a handle for
//   every virtual texture, I use a cache where indexing is managed through a hash table.
//

typedef struct pthtyp_t
{
	struct pthtyp_t *next;
	unsigned int glpic;
	short picnum;
	char palnum;
	char effects;
	char flags;      // 1 = clamped (dameth&4), 2 = hightile, 4 = skybox face, 128 = invalidated
	char skyface;
	hicreplctyp *hicr;

	unsigned short sizx, sizy;
	float scalex, scaley;
} pthtyp;

#define GLTEXCACHEADSIZ 8192
static pthtyp *gltexcachead[GLTEXCACHEADSIZ];

static long drawingskybox = 0;

int gloadtile_art(long,long,long,pthtyp*,long);
int gloadtile_hi(long,long,hicreplctyp*,long,pthtyp*,long,char);
static pthtyp * gltexcache (long dapicnum, long dapalnum, long dameth)
{
	long i, j;
	hicreplctyp *si;
	pthtyp *pth;

	j = (dapicnum&(GLTEXCACHEADSIZ-1));

	si = hicfindsubst(dapicnum,dapalnum,drawingskybox);
	if (!si) {
		if (drawingskybox) return NULL;
		goto tryart;
	}

	/* if palette > 0 && replacement found
	 *    no effects are applied to the texture
	 * else if palette > 0 && no replacement found
	 *    effects are applied to the palette 0 texture if it exists
	  */

	// load a replacement
	for(pth=gltexcachead[j]; pth; pth=pth->next) {
		if (pth->picnum == dapicnum &&
			pth->palnum == si->palnum &&
			(si->palnum>0 ? 1 : (pth->effects == hictinting[dapalnum].f)) &&
			(pth->flags & (1+2+4)) == (((dameth&4)>>2)+2+((drawingskybox>0)<<2)) &&
			(drawingskybox>0 ? (pth->skyface == drawingskybox) : 1)
			)
		{
			if (pth->flags & 128)
			{
				pth->flags &= ~128;
				if (gloadtile_hi(dapicnum,drawingskybox,si,dameth,pth,0,
							(si->palnum>0) ? 0 : hictinting[dapalnum].f)) {  // reload tile
					if (drawingskybox) return NULL;
					goto tryart;   // failed, so try for ART
				}
			}
			return(pth);
		}
	}

	pth = (pthtyp *)calloc(1,sizeof(pthtyp));
	if (!pth) return NULL;

	if (gloadtile_hi(dapicnum,drawingskybox,si,dameth,pth,1, (si->palnum>0) ? 0 : hictinting[dapalnum].f)) {
		free(pth);
		if (drawingskybox) return NULL;
		goto tryart;   // failed, so try for ART
	}
	pth->palnum = si->palnum;
	pth->next = gltexcachead[j];
	gltexcachead[j] = pth;
	return(pth);

tryart:
	// load from art
	for(pth=gltexcachead[j]; pth; pth=pth->next)
		if (pth->picnum == dapicnum &&
			pth->palnum == dapalnum &&
			(pth->flags & 1) == ((dameth&4)>>2)
			)
		{
			if (pth->flags & 128)
			{
				pth->flags &= ~128;
				if (gloadtile_art(dapicnum,dapalnum,dameth,pth,0)) return NULL; //reload tile (for animations)
			}
			return(pth);
		}

	pth = (pthtyp *)calloc(1,sizeof(pthtyp));
	if (!pth) return NULL;

	if (gloadtile_art(dapicnum,dapalnum,dameth,pth,1)) {
		free(pth);
		return NULL;
	}
	pth->next = gltexcachead[j];
	gltexcachead[j] = pth;
	return(pth);
}

void gltexinvalidate (long dapicnum, long dapalnum, long dameth)
{
	long i, j;
	pthtyp *pth;

	j = (dapicnum&(GLTEXCACHEADSIZ-1));
	for(pth=gltexcachead[j]; pth; pth=pth->next)
		if (pth->picnum == dapicnum && pth->palnum == dapalnum && (pth->flags & 1) == ((dameth&4)>>2) )
			{ pth->flags |= 128; }
}

	//Make all textures "dirty" so they reload, but not re-allocate
	//This should be much faster than polymost_glreset()
	//Use this for palette effects ... but not ones that change every frame!
void gltexinvalidateall ()
{
	long j;
	pthtyp *pth;

	for(j=GLTEXCACHEADSIZ-1;j>=0;j--)
		for(pth=gltexcachead[j];pth;pth=pth->next)
			pth->flags |= 128;
	clearskins();
#ifdef DEBUGGINGAIDS
	OSD_Printf("gltexinvalidateall()\n");
#endif
}


void gltexapplyprops (void)
{
	long i;
	pthtyp *pth;
	
	if (glinfo.maxanisotropy > 1.0)
	{
		if (glanisotropy <= 0 || glanisotropy > glinfo.maxanisotropy) glanisotropy = glinfo.maxanisotropy;
	}
	
	if (gltexfiltermode < 0) gltexfiltermode = 0;
	else if (gltexfiltermode >= (long)numglfiltermodes) gltexfiltermode = numglfiltermodes-1;
	for(i=GLTEXCACHEADSIZ-1;i>=0;i--) {
		for(pth=gltexcachead[i];pth;pth=pth->next) {
			bglBindTexture(GL_TEXTURE_2D,pth->glpic);
			bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
			bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);
			if (glinfo.maxanisotropy > 1.0)
				bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
		}
	}

	{
		int j;
		md2skinmap *sk;

		for (i=0;i<nextmodelid;i++) {
			for (j=0;j<models[i].numskins*(HICEFFECTMASK+1);j++) {
				if (!models[i].texid[j]) continue;
				bglBindTexture(GL_TEXTURE_2D,models[i].texid[j]);
				bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
				bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);
				if (glinfo.maxanisotropy > 1.0)
					bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
			}

			for (sk = models[i].skinmap; sk; sk = sk->next) {
				for (j=0;j<(HICEFFECTMASK+1);j++) {
					if (!sk->texid[j]) continue;
					bglBindTexture(GL_TEXTURE_2D,sk->texid[j]);
					bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
					bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);
					if (glinfo.maxanisotropy > 1.0)
						bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
				}
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------

static float glox1, gloy1, glox2, gloy2;

	//Use this for both initialization and uninitialization of OpenGL.
static int gltexcacnum = -1;
void polymost_glreset ()
{
	long i;
	pthtyp *pth, *next;
	//Reset if this is -1 (meaning 1st texture call ever), or > 0 (textures in memory)
	if (gltexcacnum < 0) gltexcacnum = 0;
	else
	{
		for (i=GLTEXCACHEADSIZ-1; i>=0; i--) {
			for (pth=gltexcachead[i]; pth;) {
				next = pth->next;
				bglDeleteTextures(1,&pth->glpic);
				free(pth);
				pth = next;
			}
		}
		clearskins();
	}
	memset(gltexcachead,0,sizeof(gltexcachead));
	glox1 = -1;
}

void resizeglcheck ()
{
	float m[4][4];

	if (glredbluemode < lastglredbluemode) {
		glox1 = -1;
		bglColorMask(1,1,1,1);
	} else if (glredbluemode != lastglredbluemode) {
		redblueclearcnt = 0;
	}
	lastglredbluemode = glredbluemode;

	if ((glox1 != windowx1) || (gloy1 != windowy1) || (glox2 != windowx2) || (gloy2 != windowy2))
	{
		float col[4];

		glox1 = windowx1; gloy1 = windowy1;
		glox2 = windowx2; gloy2 = windowy2;

		bglViewport(windowx1,yres-(windowy2+1),windowx2-windowx1+1,windowy2-windowy1+1);

		bglMatrixMode(GL_PROJECTION);
		memset(m,0,sizeof(m));
		m[0][0] = (float)ydimen; m[0][2] = 1.0;
		m[1][1] = (float)xdimen; m[1][2] = 1.0;
		m[2][2] = 1.0; m[2][3] = (float)ydimen;
		m[3][2] =-1.0;
		bglLoadMatrixf(&m[0][0]);

		bglMatrixMode(GL_MODELVIEW);
		bglLoadIdentity();

		if (!nofog) {
		bglEnable(GL_FOG);
		bglFogi(GL_FOG_MODE,GL_EXP); //GL_EXP(default),GL_EXP2,GL_LINEAR
		//bglHint(GL_FOG_HINT,GL_NICEST);
		bglFogf(GL_FOG_DENSITY,1.0); //must be > 0, default is 1
		bglFogf(GL_FOG_START,0.0); //default is 0
		bglFogf(GL_FOG_END,1.0); //default is 1
		col[0] = 0; col[1] = 0; col[2] = 0; col[3] = 0; //range:0 to 1
		bglFogfv(GL_FOG_COLOR,col); //default is 0,0,0,0
		}

		bglEnable(GL_TEXTURE_2D);
		bglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}
}

void fixtransparency (coltype *dapic, long daxsiz, long daysiz, long daxsiz2, long daysiz2, long dameth)
{
	coltype *wpptr;
	long j, x, y, r, g, b, dox, doy, naxsiz2;

	dox = daxsiz2-1; doy = daysiz2-1;
	if (dameth&4) { dox = min(dox,daxsiz); doy = min(doy,daysiz); }
				else { daxsiz = daxsiz2; daysiz = daysiz2; } //Make repeating textures duplicate top/left parts

	daxsiz--; daysiz--; naxsiz2 = -daxsiz2; //Hacks for optimization inside loop

		//Set transparent pixels to average color of neighboring opaque pixels
		//Doing this makes bilinear filtering look much better for masked textures (I.E. sprites)
	for(y=doy;y>=0;y--)
	{
		wpptr = &dapic[y*daxsiz2+dox];
		for(x=dox;x>=0;x--,wpptr--)
		{
			if (wpptr->a) continue;
			r = g = b = j = 0;
			if ((x>     0) && (wpptr[     -1].a)) { r += (long)wpptr[     -1].r; g += (long)wpptr[     -1].g; b += (long)wpptr[     -1].b; j++; }
			if ((x<daxsiz) && (wpptr[     +1].a)) { r += (long)wpptr[     +1].r; g += (long)wpptr[     +1].g; b += (long)wpptr[     +1].b; j++; }
			if ((y>     0) && (wpptr[naxsiz2].a)) { r += (long)wpptr[naxsiz2].r; g += (long)wpptr[naxsiz2].g; b += (long)wpptr[naxsiz2].b; j++; }
			if ((y<daysiz) && (wpptr[daxsiz2].a)) { r += (long)wpptr[daxsiz2].r; g += (long)wpptr[daxsiz2].g; b += (long)wpptr[daxsiz2].b; j++; }
			switch(j)
			{
				case 1: wpptr->r =   r            ; wpptr->g =   g            ; wpptr->b =   b            ; break;
				case 2: wpptr->r = ((r   +  1)>>1); wpptr->g = ((g   +  1)>>1); wpptr->b = ((b   +  1)>>1); break;
				case 3: wpptr->r = ((r*85+128)>>8); wpptr->g = ((g*85+128)>>8); wpptr->b = ((b*85+128)>>8); break;
				case 4: wpptr->r = ((r   +  2)>>2); wpptr->g = ((g   +  2)>>2); wpptr->b = ((b   +  2)>>2); break;
				default: break;
			}
		}
	}
}

static void uploadtexture(long doalloc, long xsiz, long ysiz, long intexfmt, long texfmt, coltype *pic, long tsizx, long tsizy, long dameth)
{
	coltype *wpptr, *rpptr;
	long x2, y2, j, js=0, x3, y3, y, x, r, g, b, a, k;

	if (gltexmaxsize <= 0) {
		GLint i = 0;
		bglGetIntegerv(GL_MAX_TEXTURE_SIZE, &i);
		if (!i) gltexmaxsize = 6;   // 2^6 = 64 == default GL max texture size
		else {
			gltexmaxsize = 0;
			for (; i>1; i>>=1) gltexmaxsize++;
		}
	}
	
	while ((xsiz>>js) > (1<<gltexmaxsize) || (ysiz>>js) > (1<<gltexmaxsize)) js++;
	
	if (js == 0) {
		if (doalloc)
			bglTexImage2D(GL_TEXTURE_2D,0,intexfmt,xsiz,ysiz,0,texfmt,GL_UNSIGNED_BYTE,pic); //loading 1st time
		else
			bglTexSubImage2D(GL_TEXTURE_2D,0,0,0,xsiz,ysiz,texfmt,GL_UNSIGNED_BYTE,pic); //overwrite old texture
	}

#if 0
	gluBuild2DMipmaps(GL_TEXTURE_2D,GL_RGBA8,xsiz,ysiz,texfmt,GL_UNSIGNED_BYTE,pic); //Needs C++ to link?
#elif 1
	x2 = xsiz; y2 = ysiz;
	for(j=1;(x2 > 1) || (y2 > 1);j++)
	{
		x3 = ((x2+1)>>1); y3 = ((y2+1)>>1);
		for(y=0;y<y3;y++)
		{
			wpptr = &pic[y*x3]; rpptr = &pic[(y<<1)*x2];
			for(x=0;x<x3;x++,wpptr++,rpptr+=2)
			{
				r = g = b = a = k = 0;
				if  (rpptr[0].a)                  { r += (long)rpptr[0].r; g += (long)rpptr[0].g; b += (long)rpptr[0].b; a += (long)rpptr[0].a; k++; }
				if ((x+x+1 < x2) && (rpptr[1].a)) { r += (long)rpptr[1].r; g += (long)rpptr[1].g; b += (long)rpptr[1].b; a += (long)rpptr[1].a; k++; }
				if (y+y+1 < y2)
				{
					if ((rpptr[x2].a)                  ) { r += (long)rpptr[x2  ].r; g += (long)rpptr[x2  ].g; b += (long)rpptr[x2  ].b; a += (long)rpptr[x2  ].a; k++; }
					if ((x+x+1 < x2) && (rpptr[x2+1].a)) { r += (long)rpptr[x2+1].r; g += (long)rpptr[x2+1].g; b += (long)rpptr[x2+1].b; a += (long)rpptr[x2+1].a; k++; }
				}
				switch(k)
				{
					case 0:
					case 1: wpptr->r = r; wpptr->g = g; wpptr->b = b; wpptr->a = a; break;
					case 2: wpptr->r = ((r+1)>>1); wpptr->g = ((g+1)>>1); wpptr->b = ((b+1)>>1); wpptr->a = ((a+1)>>1); break;
					case 3: wpptr->r = ((r*85+128)>>8); wpptr->g = ((g*85+128)>>8); wpptr->b = ((b*85+128)>>8); wpptr->a = ((a*85+128)>>8); break;
					case 4: wpptr->r = ((r+2)>>2); wpptr->g = ((g+2)>>2); wpptr->b = ((b+2)>>2); wpptr->a = ((a+2)>>2); break;
					default: break;
				}
				//if (wpptr->a) wpptr->a = 255;
			}
		}
		if (tsizx >= 0) fixtransparency(pic,(tsizx+(1<<j)-1)>>j,(tsizy+(1<<j)-1)>>j,x3,y3,dameth);
		if (j >= js) {
			if (doalloc)
				bglTexImage2D(GL_TEXTURE_2D,j-js,intexfmt,x3,y3,0,texfmt,GL_UNSIGNED_BYTE,pic); //loading 1st time
			else
				bglTexSubImage2D(GL_TEXTURE_2D,j-js,0,0,x3,y3,texfmt,GL_UNSIGNED_BYTE,pic); //overwrite old texture
		}
		x2 = x3; y2 = y3;
	}
#endif
}

int gloadtile_art (long dapic, long dapal, long dameth, pthtyp *pth, long doalloc)
{
	coltype *pic, *wpptr;
	long j, x, y, x2, y2, xsiz, ysiz, dacol, tsizx, tsizy;

	tsizx = tilesizx[dapic]; for(xsiz=1;xsiz<tsizx;xsiz+=xsiz);
	tsizy = tilesizy[dapic]; for(ysiz=1;ysiz<tsizy;ysiz+=ysiz);
	pic = (coltype *)malloc(xsiz*ysiz*sizeof(coltype));
	if (!pic) return 1;

	if (!waloff[dapic])
	{
			//Force invalid textures to draw something - an almost purely transparency texture
			//This allows the Z-buffer to be updated for mirrors (which are invalidated textures)
		pic[0].r = pic[0].g = pic[0].b = 0; pic[0].a = 1;
		tsizx = tsizy = 1;
	}
	else
	{
		for(y=0;y<ysiz;y++)
		{
			if (y < tsizy) y2 = y; else y2 = y-tsizy;
			wpptr = &pic[y*xsiz];
			for(x=0;x<xsiz;x++,wpptr++)
			{
				if ((dameth&4) && ((x >= tsizx) || (y >= tsizy))) //Clamp texture
					{ wpptr->r = wpptr->g = wpptr->b = wpptr->a = 0; continue; }
				if (x < tsizx) x2 = x; else x2 = x-tsizx;
				dacol = (long)(*(unsigned char *)(waloff[dapic]+x2*tsizy+y2));
				if (dacol == 255)
				{
					wpptr->r = curpalette[255].r;
					wpptr->g = curpalette[255].g;
					wpptr->b = curpalette[255].b;
					wpptr->a = 0;
				}
				else
				{
					j = (long)((unsigned char)palookup[dapal][dacol]);
					wpptr->r = curpalette[j].r;
					wpptr->g = curpalette[j].g;
					wpptr->b = curpalette[j].b;
					wpptr->a = 255;
				}
			}
		}
	}

	if (doalloc) bglGenTextures(1,&pth->glpic);  //# of textures (make OpenGL allocate structure)
	bglBindTexture(GL_TEXTURE_2D,pth->glpic);

	fixtransparency(pic,tsizx,tsizy,xsiz,ysiz,dameth);
	uploadtexture(doalloc,xsiz,ysiz,GL_RGBA,GL_RGBA,pic,tsizx,tsizy,dameth);

	if (gltexfiltermode < 0) gltexfiltermode = 0;
	else if (gltexfiltermode >= (long)numglfiltermodes) gltexfiltermode = numglfiltermodes-1;
	bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
	bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);

	if (glinfo.maxanisotropy > 1.0)
	{
		if (glanisotropy <= 0 || glanisotropy > glinfo.maxanisotropy) glanisotropy = glinfo.maxanisotropy;
		bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
	}

	if (!(dameth&4))
	{
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	}
	else
	{     //For sprite textures, clamping looks better than wrapping
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,glinfo.clamptoedge?GL_CLAMP_TO_EDGE:GL_CLAMP);
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,glinfo.clamptoedge?GL_CLAMP_TO_EDGE:GL_CLAMP);
	}

	if (pic) free(pic);

	pth->picnum = dapic;
	pth->palnum = dapal;
	pth->effects = 0;
	pth->flags = ((dameth&4)>>2);
	pth->hicr = NULL;
	
	return 0;
}

int gloadtile_hi(long dapic, long facen, hicreplctyp *hicr, long dameth, pthtyp *pth, long doalloc, char effect)
{
	coltype *pic, *rpptr;
	long j, x, y, x2, y2, xsiz, ysiz, tsizx, tsizy;

	char *picfil = 0, *fn;
	long picfillen, texfmt = GL_RGBA, intexfmt = GL_RGBA, filh;

	if (!hicr) return -1;
	if (facen > 0) {
		if (!hicr->skybox) return -1;
		if (facen > 6) return -1;
		if (!hicr->skybox->face[facen-1]) return -1;
		fn = hicr->skybox->face[facen-1];
	} else {
		if (!hicr->filename) return -1;
		fn = hicr->filename;
	}

	if ((filh = kopen4load(fn, 0)) < 0) {
		OSD_Printf("Hightile: %s not found!\n", fn);
		if (facen > 0)
			hicr->skybox->ignore = 1;
		else
			hicr->ignore = 1;
		return -1;
	}
	picfillen = kfilelength(filh);
	picfil = (char *)malloc(picfillen); if (!picfil) { kclose(filh); return 1; }
	kread(filh, picfil, picfillen);
	kclose(filh);

	// tsizx/y = replacement texture's natural size
	// xsiz/y = 2^x size of replacement

	kpgetdim(picfil,picfillen,&tsizx,&tsizy);
	if (tsizx == 0 || tsizy == 0) { free(picfil); return -1; }
	pth->sizx = tsizx;
	pth->sizy = tsizy;

	for(xsiz=1;xsiz<tsizx;xsiz+=xsiz);
	for(ysiz=1;ysiz<tsizy;ysiz+=ysiz);
	pic = (coltype *)malloc(xsiz*ysiz*sizeof(coltype)); if (!pic) { free(picfil); return 1; }
	memset(pic,0,xsiz*ysiz*sizeof(coltype));

	if (kprender(picfil,picfillen,(long)pic,xsiz*sizeof(coltype),xsiz,ysiz,0,0)) { free(picfil); free(pic); return -2; }
	for(y=0,j=0;y<tsizy;y++,j+=xsiz)
	{
		coltype tcol;
		char *cptr = &britable[curbrightness][0]; rpptr = &pic[j];

		for(x=0;x<tsizx;x++)
		{
			tcol.b = cptr[rpptr[x].b];
			tcol.g = cptr[rpptr[x].g];
			tcol.r = cptr[rpptr[x].r];
			tcol.a = rpptr[x].a;

			if (effect & 1) {
				// greyscale
				tcol.b = max(tcol.b, max(tcol.g, tcol.r));
				tcol.g = tcol.r = tcol.b;
			}
			if (effect & 2) {
				// invert
				tcol.b = 255-tcol.b;
				tcol.g = 255-tcol.g;
				tcol.r = 255-tcol.r;
			}

			rpptr[x].b = tcol.b;
			rpptr[x].g = tcol.g;
			rpptr[x].r = tcol.r;
			rpptr[x].a = tcol.a;
		}
	}
	if (!(dameth&4)) //Duplicate texture pixels (wrapping tricks for non power of 2 texture sizes)
	{
		if (xsiz > tsizx) //Copy left to right
		{
			long *lptr = (long *)pic;
			for(y=0;y<tsizy;y++,lptr+=xsiz)
				memcpy(&lptr[tsizx],lptr,(xsiz-tsizx)<<2);
		}
		if (ysiz > tsizy)  //Copy top to bottom
			memcpy(&pic[xsiz*tsizy],pic,(ysiz-tsizy)*xsiz<<2);
	}
	if (!glinfo.bgra) {
		for(j=xsiz*ysiz-1;j>=0;j++) {
			swapchar(&pic[j].r, &pic[j].b);
		}
	}
	free(picfil); picfil = 0;

	// precalculate scaling parameters for replacement
	if (facen > 0) {
		pth->scalex = ((float)tsizx) / 64.0;
		pth->scaley = ((float)tsizy) / 64.0;
	} else {
		//for(x2=1;x2<tilesizx[dapic];x2+=x2);
		//for(y2=1;y2<tilesizy[dapic];y2+=y2);
		pth->scalex = ((float)tsizx) / ((float)tilesizx[dapic]);
		pth->scaley = ((float)tsizy) / ((float)tilesizy[dapic]);
	}

	if (glinfo.texcompr && glusetexcompr) intexfmt = GL_COMPRESSED_RGBA_ARB;
	if (glinfo.bgra) texfmt = GL_BGRA;

	if (doalloc) bglGenTextures(1,&pth->glpic);  //# of textures (make OpenGL allocate structure)
	bglBindTexture(GL_TEXTURE_2D,pth->glpic);

	fixtransparency(pic,tsizx,tsizy,xsiz,ysiz,dameth);
	uploadtexture(doalloc,xsiz,ysiz,intexfmt,texfmt,pic,-1,tsizy,dameth);

	if (gltexfiltermode < 0) gltexfiltermode = 0;
	else if (gltexfiltermode >= (long)numglfiltermodes) gltexfiltermode = numglfiltermodes-1;
	bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,glfiltermodes[gltexfiltermode].mag);
	bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,glfiltermodes[gltexfiltermode].min);

	if (glinfo.maxanisotropy > 1.0)
	{
		if (glanisotropy <= 0 || glanisotropy > glinfo.maxanisotropy) glanisotropy = glinfo.maxanisotropy;
		bglTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,glanisotropy);
	}

	if (!(dameth&4))
	{
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	}
	else
	{     //For sprite textures, clamping looks better than wrapping
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,glinfo.clamptoedge?GL_CLAMP_TO_EDGE:GL_CLAMP);
		bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,glinfo.clamptoedge?GL_CLAMP_TO_EDGE:GL_CLAMP);
	}

	if (pic) free(pic);

	pth->picnum = dapic;
	pth->effects = effect;
	pth->flags = ((dameth&4)>>2) + 2 + ((facen>0)<<2);
	pth->skyface = facen;
	pth->hicr = hicr;

	return 0;
}

#endif

	//(dpx,dpy) specifies an n-sided polygon. The polygon must be a convex clockwise loop.
	//    n must be <= 8 (assume clipping can double number of vertices)
	//method: 0:solid, 1:masked(255 is transparent), 2:transluscent #1, 3:transluscent #2
	//    +4 means it's a sprite, so wraparound isn't needed
static long pow2xsplit = 0, skyclamphack = 0;
void drawpoly (double *dpx, double *dpy, long n, long method)
{
	#define PI 3.14159265358979323
	double ngdx, ngdy, ngdo, ngux, nguy, nguo, ngvx, ngvy, ngvo, dp, up, vp, rdp, du0, du1, dui, duj;
	double ngdx2, ngux2, ngvx2;
	double f, r, ox, oy, oz, ox2, oy2, oz2, dd[16], uu[16], vv[16], px[16], py[16], uoffs;
	long i, j, k, x, y, z, nn, ix0, ix1, mini, maxi, tsizx, tsizy, tsizxm1, tsizym1, ltsizy;
	long xx, yy, xi, d0, u0, v0, d1, u1, v1, xmodnice, ymulnice, dorot;
	char dacol, *walptr, *palptr, *vidp, *vide;

	pthtyp *pth;

	if (n == 3)
	{
		if ((dpx[0]-dpx[1])*(dpy[2]-dpy[1]) >= (dpx[2]-dpx[1])*(dpy[0]-dpy[1])) return; //for triangle
	}
	else
	{
		f = 0; //f is area of polygon / 2
		for(i=n-2,j=n-1,k=0;k<n;i=j,j=k,k++) f += (dpx[i]-dpx[k])*dpy[j];
		if (f <= 0) return;
	}

		//Load texture (globalpicnum)
	if ((unsigned long)globalpicnum >= MAXTILES) globalpicnum = 0;
	setgotpic(globalpicnum);
	tsizx = tilesizx[globalpicnum];
	tsizy = tilesizy[globalpicnum];
	if (!palookup[globalpal]) globalpal = 0;
	if (!waloff[globalpicnum])
	{
		loadtile(globalpicnum);
		if (!waloff[globalpicnum])
		{
			if (rendmode != 3) return;
			tsizx = tsizy = 1; method = 1; //Hack to update Z-buffer for invalid mirror textures
		}
	}
	walptr = (char *)waloff[globalpicnum];

	j = 0; dorot = ((gchang != 1.0) || (gctang != 1.0));
	if (dorot)
	{
		for(i=0;i<n;i++)
		{
			ox = dpx[i]-ghalfx;
			oy = dpy[i]-ghoriz;
			oz = ghalfx;

				//Up/down rotation
			ox2 = ox;
			oy2 = oy*gchang - oz*gshang;
			oz2 = oy*gshang + oz*gchang;

				//Tilt rotation
			ox = ox2*gctang - oy2*gstang;
			oy = ox2*gstang + oy2*gctang;
			oz = oz2;

			if ((oz < SCISDIST) && (rendmode != 3)) return; //annoying hack to avoid bugs in software rendering

			r = ghalfx / oz;

			dd[j] = (dpx[i]*gdx + dpy[i]*gdy + gdo)*r;
			uu[j] = (dpx[i]*gux + dpy[i]*guy + guo)*r;
			vv[j] = (dpx[i]*gvx + dpy[i]*gvy + gvo)*r;

			px[j] = ox*r + ghalfx;
			py[j] = oy*r + ghoriz;
			if ((!j) || (px[j] != px[j-1]) || (py[j] != py[j-1])) j++;
		}
	}
	else
	{
		for(i=0;i<n;i++)
		{
			px[j] = dpx[i];
			py[j] = dpy[i];
			if ((!j) || (px[j] != px[j-1]) || (py[j] != py[j-1])) j++;
		}
	}
	while ((j >= 3) && (px[j-1] == px[0]) && (py[j-1] == py[0])) j--;
	if (j < 3) return;
	n = j;

#ifdef USE_OPENGL
	if (rendmode == 3)
	{
		float hackscx, hackscy;

		if (skyclamphack) method |= 4;
		pth = gltexcache(globalpicnum,globalpal,method&(~3));
		bglBindTexture(GL_TEXTURE_2D, pth ? pth->glpic : 0);

		if (pth && (pth->flags & 2))
		{
			hackscx = pth->scalex;
			hackscy = pth->scaley;
			tsizx = pth->sizx;
			tsizy = pth->sizy;
		}
		else { hackscx = 1.0; hackscy = 1.0; }

		for(xx=1;xx<tsizx;xx+=xx); ox2 = (double)1.0/(double)xx;
		for(yy=1;yy<tsizy;yy+=yy); oy2 = (double)1.0/(double)yy;

		if (!(method&3)) bglDisable(GL_BLEND); else bglEnable(GL_BLEND);

		//bglEnable(GL_ALPHA_TEST);
		//bglAlphaFunc(GL_GREATER,0.5);

		if (!dorot)
		{
			for(i=n-1;i>=0;i--)
			{
				dd[i] = px[i]*gdx + py[i]*gdy + gdo;
				uu[i] = px[i]*gux + py[i]*guy + guo;
				vv[i] = px[i]*gvx + py[i]*gvy + gvo;
			}
		}

		{
			float pc[4];
			f = ((float)(numpalookups-min(max(globalshade,0),numpalookups)))/((float)numpalookups);
			pc[0] = pc[1] = pc[2] = f;
			switch(method&3)
			{
				case 0: pc[3] = 1.0; break;
				case 1: pc[3] = 1.0; break;
				case 2: pc[3] = 0.66; break;
				case 3: pc[3] = 0.33; break;
			}
			// tinting happens only to hightile textures, and only if the texture we're
			// rendering isn't for the same palette as what we asked for
			if (pth && (pth->flags & 2) && (pth->palnum != globalpal)) {
				// apply tinting for replaced textures
				pc[0] *= (float)hictinting[globalpal].r / 255.0;
				pc[1] *= (float)hictinting[globalpal].g / 255.0;
				pc[2] *= (float)hictinting[globalpal].b / 255.0;
			}
			bglColor4f(pc[0],pc[1],pc[2],pc[3]);
		}

			//Hack for walls&masked walls which use textures that are not a power of 2
		if ((pow2xsplit) && (tsizx != xx))
		{
			if (!dorot)
			{
				ngdx = gdx; ngdy = gdy; ngdo = gdo+(ngdx+ngdy)*.5;
				ngux = gux; nguy = guy; nguo = guo+(ngux+nguy)*.5;
				ngvx = gvx; ngvy = gvy; ngvo = gvo+(ngvx+ngvy)*.5;
			}
			else
			{
				ox = py[1]-py[2]; oy = py[2]-py[0]; oz = py[0]-py[1];
				r = 1.0 / (ox*px[0] + oy*px[1] + oz*px[2]);
				ngdx = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				ngux = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				ngvx = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				ox = px[2]-px[1]; oy = px[0]-px[2]; oz = px[1]-px[0];
				ngdy = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				nguy = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				ngvy = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				ox = px[0]-.5; oy = py[0]-.5; //.5 centers texture nicely
				ngdo = dd[0] - ox*ngdx - oy*ngdy;
				nguo = uu[0] - ox*ngux - oy*nguy;
				ngvo = vv[0] - ox*ngvx - oy*ngvy;
			}

			ngux *= hackscx; nguy *= hackscx; nguo *= hackscx;
			ngvx *= hackscy; ngvy *= hackscy; ngvo *= hackscy;
			uoffs = ((double)(xx-tsizx)*.5);
			ngux -= ngdx*uoffs;
			nguy -= ngdy*uoffs;
			nguo -= ngdo*uoffs;

				//Find min&max u coordinates (du0...du1)
			for(i=0;i<n;i++)
			{
				ox = px[i]; oy = py[i];
				f = (ox*ngux + oy*nguy + nguo) / (ox*ngdx + oy*ngdy + ngdo);
				if (!i) { du0 = du1 = f; continue; }
					  if (f < du0) du0 = f;
				else if (f > du1) du1 = f;
			}

			f = 1.0/(double)tsizx;
			ix0 = (long)floor(du0*f);
			ix1 = (long)floor(du1*f);
			for(;ix0<=ix1;ix0++)
			{
				du0 = (double)((ix0  )*tsizx); // + uoffs;
				du1 = (double)((ix0+1)*tsizx); // + uoffs;

				i = 0; nn = 0;
				duj = (px[i]*ngux + py[i]*nguy + nguo) / (px[i]*ngdx + py[i]*ngdy + ngdo);
				do
				{
					j = i+1; if (j == n) j = 0;

					dui = duj; duj = (px[j]*ngux + py[j]*nguy + nguo) / (px[j]*ngdx + py[j]*ngdy + ngdo);

					if ((du0 <= dui) && (dui <= du1)) { uu[nn] = px[i]; vv[nn] = py[i]; nn++; }
					if (duj <= dui)
					{
						if ((du1 < duj) != (du1 < dui))
						{
								//ox*(ngux-ngdx*du1) + oy*(nguy-ngdy*du1) + (nguo-ngdo*du1) = 0
								//(px[j]-px[i])*f + px[i] = ox
								//(py[j]-py[i])*f + py[i] = oy

								///Solve for f
								//((px[j]-px[i])*f + px[i])*(ngux-ngdx*du1) +
								//((py[j]-py[i])*f + py[i])*(nguy-ngdy*du1) + (nguo-ngdo*du1) = 0

							f = -(       px[i] *(ngux-ngdx*du1) +  py[i]       *(nguy-ngdy*du1) + (nguo-ngdo*du1)) /
								  ((px[j]-px[i])*(ngux-ngdx*du1) + (py[j]-py[i])*(nguy-ngdy*du1));
							uu[nn] = (px[j]-px[i])*f + px[i];
							vv[nn] = (py[j]-py[i])*f + py[i]; nn++;
						}
						if ((du0 < duj) != (du0 < dui))
						{
							f = -(       px[i] *(ngux-ngdx*du0) +        py[i] *(nguy-ngdy*du0) + (nguo-ngdo*du0)) /
								  ((px[j]-px[i])*(ngux-ngdx*du0) + (py[j]-py[i])*(nguy-ngdy*du0));
							uu[nn] = (px[j]-px[i])*f + px[i];
							vv[nn] = (py[j]-py[i])*f + py[i]; nn++;
						}
					}
					else
					{
						if ((du0 < duj) != (du0 < dui))
						{
							f = -(       px[i] *(ngux-ngdx*du0) +        py[i] *(nguy-ngdy*du0) + (nguo-ngdo*du0)) /
								  ((px[j]-px[i])*(ngux-ngdx*du0) + (py[j]-py[i])*(nguy-ngdy*du0));
							uu[nn] = (px[j]-px[i])*f + px[i];
							vv[nn] = (py[j]-py[i])*f + py[i]; nn++;
						}
						if ((du1 < duj) != (du1 < dui))
						{
							f = -(       px[i] *(ngux-ngdx*du1) +  py[i]       *(nguy-ngdy*du1) + (nguo-ngdo*du1)) /
								  ((px[j]-px[i])*(ngux-ngdx*du1) + (py[j]-py[i])*(nguy-ngdy*du1));
							uu[nn] = (px[j]-px[i])*f + px[i];
							vv[nn] = (py[j]-py[i])*f + py[i]; nn++;
						}
					}
					i = j;
				} while (i);
				if (nn < 3) continue;

				bglBegin(GL_TRIANGLE_FAN);
				for(i=0;i<nn;i++)
				{
					ox = uu[i]; oy = vv[i];
					dp = ox*ngdx + oy*ngdy + ngdo;
					up = ox*ngux + oy*nguy + nguo;
					vp = ox*ngvx + oy*ngvy + ngvo;
					r = 1.0/dp; bglTexCoord2d((up*r-du0+uoffs)*ox2,vp*r*oy2);
					bglVertex3d((ox-ghalfx)*r*grhalfxdown10x,(ghoriz-oy)*r*grhalfxdown10,r*(1.0/1024.0));
				}
				bglEnd();
			}
		}
		else
		{
			ox2 *= hackscx; oy2 *= hackscy;
			bglBegin(GL_TRIANGLE_FAN);
			for(i=0;i<n;i++)
			{
				r = 1.0/dd[i]; bglTexCoord2d(uu[i]*r*ox2,vv[i]*r*oy2);
				bglVertex3d((px[i]-ghalfx)*r*grhalfxdown10x,(ghoriz-py[i])*r*grhalfxdown10,r*(1.0/1024.0));
			}
			bglEnd();
		}
		return;
	}
#endif

	if (rendmode == 2)
	{
#if (USEZBUFFER != 0)
		if ((!zbufmem) || (zbufbpl != bytesperline) || (zbufysiz != ydim))
		{
			zbufbpl = bytesperline;
			zbufysiz = ydim;
			zbufmem = (long)realloc((void *)zbufmem,zbufbpl*zbufysiz*4);
		}
		zbufoff = (long *)(zbufmem-(frameplace<<2));
#endif
		if ((!transluc)) method = (method&~3)+1; //In case translucent table doesn't exist

		if (!dorot)
		{
			ngdx = gdx; ngdy = gdy; ngdo = gdo+(ngdx+ngdy)*.5;
			ngux = gux; nguy = guy; nguo = guo+(ngux+nguy)*.5;
			ngvx = gvx; ngvy = gvy; ngvo = gvo+(ngvx+ngvy)*.5;
		}
		else
		{
				//General equations:
				//dd[i] = (px[i]*gdx + py[i]*gdy + gdo)
				//uu[i] = (px[i]*gux + py[i]*guy + guo)/dd[i]
				//vv[i] = (px[i]*gvx + py[i]*gvy + gvo)/dd[i]
				//
				//px[0]*gdx + py[0]*gdy + 1*gdo = dd[0]
				//px[1]*gdx + py[1]*gdy + 1*gdo = dd[1]
				//px[2]*gdx + py[2]*gdy + 1*gdo = dd[2]
				//
				//px[0]*gux + py[0]*guy + 1*guo = uu[0]*dd[0] (uu[i] premultiplied by dd[i] above)
				//px[1]*gux + py[1]*guy + 1*guo = uu[1]*dd[1]
				//px[2]*gux + py[2]*guy + 1*guo = uu[2]*dd[2]
				//
				//px[0]*gvx + py[0]*gvy + 1*gvo = vv[0]*dd[0] (vv[i] premultiplied by dd[i] above)
				//px[1]*gvx + py[1]*gvy + 1*gvo = vv[1]*dd[1]
				//px[2]*gvx + py[2]*gvy + 1*gvo = vv[2]*dd[2]
			ox = py[1]-py[2]; oy = py[2]-py[0]; oz = py[0]-py[1];
			r = 1.0 / (ox*px[0] + oy*px[1] + oz*px[2]);
			ngdx = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
			ngux = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
			ngvx = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
			ox = px[2]-px[1]; oy = px[0]-px[2]; oz = px[1]-px[0];
			ngdy = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
			nguy = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
			ngvy = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
			ox = px[0]-.5; oy = py[0]-.5; //.5 centers texture nicely
			ngdo = dd[0] - ox*ngdx - oy*ngdy;
			nguo = uu[0] - ox*ngux - oy*nguy;
			ngvo = vv[0] - ox*ngvx - oy*ngvy;
		}
		palptr = &palookup[globalpal][min(max(globalshade,0),numpalookups-1)<<8]; //<-need to make shade not static!

		tsizxm1 = tsizx-1; xmodnice = (!(tsizxm1&tsizx));
		tsizym1 = tsizy-1; ymulnice = (!(tsizym1&tsizy));
		if ((method&4) && (!xmodnice)) //Sprites don't need a mod on texture coordinates
			{ xmodnice = 1; for(tsizxm1=1;tsizxm1<tsizx;tsizxm1=(tsizxm1<<1)+1); }
		if (!ymulnice) { for(tsizym1=1;tsizym1+1<tsizy;tsizym1=(tsizym1<<1)+1); }
		ltsizy = (picsiz[globalpicnum]>>4);
	}
	else
	{
		dacol = palookup[0][(long)(*(char *)(waloff[globalpicnum]))+(min(max(globalshade,0),numpalookups-1)<<8)];
	}

	if (grhalfxdown10x < 0) //Hack for mirrors
	{
		for(i=((n-1)>>1);i>=0;i--)
		{
			r = px[i]; px[i] = ((double)xdimen)-px[n-1-i]; px[n-1-i] = ((double)xdimen)-r;
			r = py[i]; py[i] = py[n-1-i]; py[n-1-i] = r;
		}
		ngdo += ((double)xdimen)*ngdx; ngdx = -ngdx;
		nguo += ((double)xdimen)*ngux; ngux = -ngux;
		ngvo += ((double)xdimen)*ngvx; ngvx = -ngvx;
	}

	ngdx2 = ngdx*(1<<LINTERPSIZ);
	ngux2 = ngux*(1<<LINTERPSIZ);
	ngvx2 = ngvx*(1<<LINTERPSIZ);

	mini = (py[0] >= py[1]); maxi = 1-mini;
	for(z=2;z<n;z++)
	{
		if (py[z] < py[mini]) mini = z;
		if (py[z] > py[maxi]) maxi = z;
	}

	i = maxi; dtol(py[i],&yy); if (yy > ydimen) yy = ydimen;
	do
	{
		j = i+1; if (j == n) j = 0;
		dtol(py[j],&y); if (y < 0) y = 0;
		if (y < yy)
		{
			f = (px[j]-px[i])/(py[j]-py[i]); dtol(f*16384.0,&xi);
			dtol((((double)yy-.5-py[j])*f + px[j])*16384.0+8192.0,&x);
			for(;yy>y;yy--,x-=xi) lastx[yy-1] = (x>>14);
		}
		i = j;
	} while (i != mini);
	do
	{
		j = i+1; if (j == n) j = 0;
		dtol(py[j],&yy); if (yy > ydimen) yy = ydimen;
		if (y < yy)
		{
			f = (px[j]-px[i])/(py[j]-py[i]); dtol(f*16384.0,&xi);
			dtol((((double)y+.5-py[j])*f + px[j])*16384.0+8192.0,&x);
			for(;y<yy;y++,x+=xi)
			{
				ix0 = lastx[y]; if (ix0 < 0) ix0 = 0;
				ix1 = (x>>14); if (ix1 > xdimen) ix1 = xdimen;
				if (ix0 < ix1)
				{
					if (rendmode == 1)
						memset((void *)(ylookup[y]+ix0+frameoffset),dacol,ix1-ix0);
					else
					{
						vidp = (char *)(ylookup[y]+frameoffset+ix0);
						dp = ngdx*(double)ix0 + ngdy*(double)y + ngdo;
						up = ngux*(double)ix0 + nguy*(double)y + nguo;
						vp = ngvx*(double)ix0 + ngvy*(double)y + ngvo;
						rdp = 65536.0/dp; dp += ngdx2;
						dtol(   rdp,&d0);
						dtol(up*rdp,&u0); up += ngux2;
						dtol(vp*rdp,&v0); vp += ngvx2;
						rdp = 65536.0/dp;

						switch (method&3)
						{
							case 0:
								if (xmodnice&ymulnice) //both u&v texture sizes are powers of 2 :)
								{
									for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
									{
										dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
										dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
										dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
										rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
										while (vidp < vide)
										{
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
											zbufoff[(long)vidp] = d0+16384; //+ hack so wall&floor sprites don't disappear
#endif
											vidp[0] = palptr[walptr[(((u0>>16)&tsizxm1)<<ltsizy) + ((v0>>16)&tsizym1)]]; //+((d0>>13)&0x3f00)];
#else
											vidp[0] = ((d0>>16)&255);
#endif
											d0 += d1; u0 += u1; v0 += v1; vidp++;
										} while (vidp < vide);
									}
								}
								else
								{
									for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
									{
										dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
										dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
										dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
										rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
										while (vidp < vide)
										{
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
											zbufoff[(long)vidp] = d0;
#endif
											vidp[0] = palptr[walptr[imod(u0>>16,tsizx)*tsizy + ((v0>>16)&tsizym1)]]; //+((d0>>13)&0x3f00)];
#else
											vidp[0] = ((d0>>16)&255);
#endif
											d0 += d1; u0 += u1; v0 += v1; vidp++;
										} while (vidp < vide);
									}
								}
								break;
							case 1:
								if (xmodnice) //both u&v texture sizes are powers of 2 :)
								{
									for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
									{
										dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
										dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
										dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
										rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
										while (vidp < vide)
										{
											dacol = walptr[(((u0>>16)&tsizxm1)*tsizy) + ((v0>>16)&tsizym1)];
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
											if ((dacol != 255) && (d0 <= zbufoff[(long)vidp]))
											{
												zbufoff[(long)vidp] = d0;
												vidp[0] = palptr[((long)dacol)]; //+((d0>>13)&0x3f00)];
											}
#else
											if (dacol != 255) vidp[0] = palptr[((long)dacol)]; //+((d0>>13)&0x3f00)];
#endif
#else
											if ((dacol != 255) && (vidp[0] > (d0>>16))) vidp[0] = ((d0>>16)&255);
#endif
											d0 += d1; u0 += u1; v0 += v1; vidp++;
										} while (vidp < vide);
									}
								}
								else
								{
									for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
									{
										dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
										dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
										dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
										rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
										while (vidp < vide)
										{
											dacol = walptr[imod(u0>>16,tsizx)*tsizy + ((v0>>16)&tsizym1)];
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
											if ((dacol != 255) && (d0 <= zbufoff[(long)vidp]))
											{
												zbufoff[(long)vidp] = d0;
												vidp[0] = palptr[((long)dacol)]; //+((d0>>13)&0x3f00)];
											}
#else
											if (dacol != 255) vidp[0] = palptr[((long)dacol)]; //+((d0>>13)&0x3f00)];
#endif
#else
											if ((dacol != 255) && (vidp[0] > (d0>>16))) vidp[0] = ((d0>>16)&255);
#endif
											d0 += d1; u0 += u1; v0 += v1; vidp++;
										} while (vidp < vide);
									}
								}
								break;
							case 2: //Transluscence #1
								for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
								{
									dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
									dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
									dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
									rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
									while (vidp < vide)
									{
										dacol = walptr[imod(u0>>16,tsizx)*tsizy + ((v0>>16)&tsizym1)];
										//dacol = walptr[(((u0>>16)&tsizxm1)<<ltsizy) + ((v0>>16)&tsizym1)];
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
										if ((dacol != 255) && (d0 <= zbufoff[(long)vidp]))
										{
											zbufoff[(long)vidp] = d0;
											vidp[0] = transluc[(((long)vidp[0])<<8)+((long)palptr[((long)dacol)])]; //+((d0>>13)&0x3f00)])];
										}
#else
										if (dacol != 255)
											vidp[0] = transluc[(((long)vidp[0])<<8)+((long)palptr[((long)dacol)])]; //+((d0>>13)&0x3f00)])];
#endif
#else
										if ((dacol != 255) && (vidp[0] > (d0>>16))) vidp[0] = ((d0>>16)&255);
#endif
										d0 += d1; u0 += u1; v0 += v1; vidp++;
									} while (vidp < vide);
								}
								break;
							case 3: //Transluscence #2
								for(xx=ix0;xx<ix1;xx+=(1<<LINTERPSIZ))
								{
									dtol(   rdp,&d1); dp += ngdx2; d1 = ((d1-d0)>>LINTERPSIZ);
									dtol(up*rdp,&u1); up += ngux2; u1 = ((u1-u0)>>LINTERPSIZ);
									dtol(vp*rdp,&v1); vp += ngvx2; v1 = ((v1-v0)>>LINTERPSIZ);
									rdp = 65536.0/dp; vide = &vidp[min(ix1-xx,1<<LINTERPSIZ)];
									while (vidp < vide)
									{
										dacol = walptr[imod(u0>>16,tsizx)*tsizy + ((v0>>16)&tsizym1)];
										//dacol = walptr[(((u0>>16)&tsizxm1)<<ltsizy) + ((v0>>16)&tsizym1)];
#if (DEPTHDEBUG == 0)
#if (USEZBUFFER != 0)
										if ((dacol != 255) && (d0 <= zbufoff[(long)vidp]))
										{
											zbufoff[(long)vidp] = d0;
											vidp[0] = transluc[((long)vidp[0])+(((long)palptr[((long)dacol)/*+((d0>>13)&0x3f00)*/])<<8)];
										}
#else
										if (dacol != 255)
											vidp[0] = transluc[((long)vidp[0])+(((long)palptr[((long)dacol)/*+((d0>>13)&0x3f00)*/])<<8)];
#endif
#else
										if ((dacol != 255) && (vidp[0] > (d0>>16))) vidp[0] = ((d0>>16)&255);
#endif
										d0 += d1; u0 += u1; v0 += v1; vidp++;
									} while (vidp < vide);
								}
								break;
						}
					}
				}
			}
		}
		i = j;
	} while (i != maxi);

	if (rendmode == 1)
	{
		for(i=0,j=n-1;i<n;j=i,i++) drawline2d(px[i],py[i],px[j],py[j],31); //hopefully color index 31 is white

		//ox = 0; oy = 0;
		//for(i=0;i<n;i++) { ox += px[i]; oy += py[i]; }
		//ox /= (double)n; oy /= (double)n;
		//for(i=0,j=n-1;i<n;j=i,i++) drawline2d(px[i]+(ox-px[i])*.125,py[i]+(oy-py[i])*.125,px[j]+(ox-px[j])*.125,py[j]+(oy-py[j])*.125,31);
	}
}

	/*Init viewport boundary (must be 4 point convex loop):
	//      (px[0],py[0]).----.(px[1],py[1])
	//                  /      \
	//                /          \
	// (px[3],py[3]).--------------.(px[2],py[2])
	*/
void initmosts (double *px, double *py, long n)
{
	long i, j, k, imin;

	vcnt = 1; //0 is dummy solid node

	if (n < 3) return;
	imin = (px[1] < px[0]);
	for(i=n-1;i>=2;i--) if (px[i] < px[imin]) imin = i;


	vsp[vcnt].x = px[imin];
	vsp[vcnt].cy[0] = vsp[vcnt].fy[0] = py[imin];
	vcnt++;
	i = imin+1; if (i >= n) i = 0;
	j = imin-1; if (j < 0) j = n-1;
	do
	{
		if (px[i] < px[j])
		{
			if ((vcnt > 1) && (px[i] <= vsp[vcnt-1].x)) vcnt--;
			vsp[vcnt].x = px[i];
			vsp[vcnt].cy[0] = py[i];
			k = j+1; if (k >= n) k = 0;
				//(px[k],py[k])
				//(px[i],?)
				//(px[j],py[j])
			vsp[vcnt].fy[0] = (px[i]-px[k])*(py[j]-py[k])/(px[j]-px[k]) + py[k];
			vcnt++;
			i++; if (i >= n) i = 0;
		}
		else if (px[j] < px[i])
		{
			if ((vcnt > 1) && (px[j] <= vsp[vcnt-1].x)) vcnt--;
			vsp[vcnt].x = px[j];
			vsp[vcnt].fy[0] = py[j];
			k = i-1; if (k < 0) k = n-1;
				//(px[k],py[k])
				//(px[j],?)
				//(px[i],py[i])
			vsp[vcnt].cy[0] = (px[j]-px[k])*(py[i]-py[k])/(px[i]-px[k]) + py[k];
			vcnt++;
			j--; if (j < 0) j = n-1;
		}
		else
		{
			if ((vcnt > 1) && (px[i] <= vsp[vcnt-1].x)) vcnt--;
			vsp[vcnt].x = px[i];
			vsp[vcnt].cy[0] = py[i];
			vsp[vcnt].fy[0] = py[j];
			vcnt++;
			i++; if (i >= n) i = 0; if (i == j) break;
			j--; if (j < 0) j = n-1;
		}
	} while (i != j);
	if (px[i] > vsp[vcnt-1].x)
	{
		vsp[vcnt].x = px[i];
		vsp[vcnt].cy[0] = vsp[vcnt].fy[0] = py[i];
		vcnt++;
	}


	for(i=0;i<vcnt;i++)
	{
		vsp[i].cy[1] = vsp[i+1].cy[0]; vsp[i].ctag = i;
		vsp[i].fy[1] = vsp[i+1].fy[0]; vsp[i].ftag = i;
		vsp[i].n = i+1; vsp[i].p = i-1;
	}
	vsp[vcnt-1].n = 0; vsp[0].p = vcnt-1;
	gtag = vcnt;

		//VSPMAX-1 is dummy empty node
	for(i=vcnt;i<VSPMAX;i++) { vsp[i].n = i+1; vsp[i].p = i-1; }
	vsp[VSPMAX-1].n = vcnt; vsp[vcnt].p = VSPMAX-1;
}

void vsdel (long i)
{
	long pi, ni;
		//Delete i
	pi = vsp[i].p;
	ni = vsp[i].n;
	vsp[ni].p = pi;
	vsp[pi].n = ni;

		//Add i to empty list
	vsp[i].n = vsp[VSPMAX-1].n;
	vsp[i].p = VSPMAX-1;
	vsp[vsp[VSPMAX-1].n].p = i;
	vsp[VSPMAX-1].n = i;
}

long vsinsaft (long i)
{
	long r;
		//i = next element from empty list
	r = vsp[VSPMAX-1].n;
	vsp[vsp[r].n].p = VSPMAX-1;
	vsp[VSPMAX-1].n = vsp[r].n;

	vsp[r] = vsp[i]; //copy i to r

		//insert r after i
	vsp[r].p = i; vsp[r].n = vsp[i].n;
	vsp[vsp[i].n].p = r; vsp[i].n = r;

	return(r);
}

long testvisiblemost (float x0, float x1)
{
	long i, newi;

	for(i=vsp[0].n;i;i=newi)
	{
		newi = vsp[i].n;
		if ((x0 < vsp[newi].x) && (vsp[i].x < x1) && (vsp[i].ctag >= 0)) return(1);
	}
	return(0);
}

void domost (float x0, float y0, float x1, float y1)
{
	double dpx[4], dpy[4];
	float d, f, n, t, slop, dx, dx0, dx1, nx, nx0, ny0, nx1, ny1;
	float spx[4], spy[4], cy[2], cv[2];
	long i, j, k, z, ni, vcnt, scnt, newi, dir, spt[4];
	
	if (x0 < x1)
	{
		dir = 1; //clip dmost (floor)
		y0 -= .01; y1 -= .01;
	}
	else
	{
		if (x0 == x1) return;
		f = x0; x0 = x1; x1 = f;
		f = y0; y0 = y1; y1 = f;
		dir = 0; //clip umost (ceiling)
		//y0 += .01; y1 += .01; //necessary?
	}

	slop = (y1-y0)/(x1-x0);
	for(i=vsp[0].n;i;i=newi)
	{
		newi = vsp[i].n; nx0 = vsp[i].x; nx1 = vsp[newi].x;
		if ((x0 >= nx1) || (nx0 >= x1) || (vsp[i].ctag <= 0)) continue;
		dx = nx1-nx0;
		cy[0] = vsp[i].cy[0]; cv[0] = vsp[i].cy[1]-cy[0];
		cy[1] = vsp[i].fy[0]; cv[1] = vsp[i].fy[1]-cy[1];

		scnt = 0;

			//Test if left edge requires split (x0,y0) (nx0,cy(0)),<dx,cv(0)>
		if ((x0 > nx0) && (x0 < nx1))
		{
			t = (x0-nx0)*cv[dir] - (y0-cy[dir])*dx;
			if (((!dir) && (t < 0)) || ((dir) && (t > 0)))
				{ spx[scnt] = x0; spy[scnt] = y0; spt[scnt] = -1; scnt++; }
		}

			//Test for intersection on umost (j == 0) and dmost (j == 1)
		for(j=0;j<2;j++)
		{
			d = (y0-y1)*dx - (x0-x1)*cv[j];
			n = (y0-cy[j])*dx - (x0-nx0)*cv[j];
			if ((fabs(n) <= fabs(d)) && (d*n >= 0) && (d != 0))
			{
				t = n/d; nx = (x1-x0)*t + x0;
				if ((nx > nx0) && (nx < nx1))
				{
					spx[scnt] = nx; spy[scnt] = (y1-y0)*t + y0;
					spt[scnt] = j; scnt++;
				}
			}
		}

			//Nice hack to avoid full sort later :)
		if ((scnt >= 2) && (spx[scnt-1] < spx[scnt-2]))
		{
			f = spx[scnt-1]; spx[scnt-1] = spx[scnt-2]; spx[scnt-2] = f;
			f = spy[scnt-1]; spy[scnt-1] = spy[scnt-2]; spy[scnt-2] = f;
			j = spt[scnt-1]; spt[scnt-1] = spt[scnt-2]; spt[scnt-2] = j;
		}

			//Test if right edge requires split
		if ((x1 > nx0) && (x1 < nx1))
		{
			t = (x1-nx0)*cv[dir] - (y1-cy[dir])*dx;
			if (((!dir) && (t < 0)) || ((dir) && (t > 0)))
				{ spx[scnt] = x1; spy[scnt] = y1; spt[scnt] = -1; scnt++; }
		}

		vsp[i].tag = vsp[newi].tag = -1;
		for(z=0;z<=scnt;z++,i=vcnt)
		{
			if (z < scnt)
			{
				vcnt = vsinsaft(i);
				t = (spx[z]-nx0)/dx;
				vsp[i].cy[1] = t*cv[0] + cy[0];
				vsp[i].fy[1] = t*cv[1] + cy[1];
				vsp[vcnt].x = spx[z];
				vsp[vcnt].cy[0] = vsp[i].cy[1];
				vsp[vcnt].fy[0] = vsp[i].fy[1];
				vsp[vcnt].tag = spt[z];
			}

			ni = vsp[i].n; if (!ni) continue; //this 'if' fixes many bugs!
			dx0 = vsp[i].x; if (x0 > dx0) continue;
			dx1 = vsp[ni].x; if (x1 < dx1) continue;
			ny0 = (dx0-x0)*slop + y0;
			ny1 = (dx1-x0)*slop + y0;

				//      dx0           dx1
				//       �             �
				//----------------------------
				//     t0+=0         t1+=0
				//   vsp[i].cy[0]  vsp[i].cy[1]
				//============================
				//     t0+=1         t1+=3
				//============================
				//   vsp[i].fy[0]    vsp[i].fy[1]
				//     t0+=2         t1+=6
				//
				//     ny0 ?         ny1 ?

			k = 1+3;
			if ((vsp[i].tag == 0) || (ny0 <= vsp[i].cy[0]+.01)) k--;
			if ((vsp[i].tag == 1) || (ny0 >= vsp[i].fy[0]-.01)) k++;
			if ((vsp[ni].tag == 0) || (ny1 <= vsp[i].cy[1]+.01)) k -= 3;
			if ((vsp[ni].tag == 1) || (ny1 >= vsp[i].fy[1]-.01)) k += 3;

			if (!dir)
			{
				switch(k)
				{
					case 1: case 2:
						dpx[0] = dx0; dpy[0] = vsp[i].cy[0];
						dpx[1] = dx1; dpy[1] = vsp[i].cy[1];
						dpx[2] = dx0; dpy[2] = ny0; drawpoly(dpx,dpy,3,0);
						vsp[i].cy[0] = ny0; vsp[i].ctag = gtag; break;
					case 3: case 6:
						dpx[0] = dx0; dpy[0] = vsp[i].cy[0];
						dpx[1] = dx1; dpy[1] = vsp[i].cy[1];
						dpx[2] = dx1; dpy[2] = ny1; drawpoly(dpx,dpy,3,0);
						vsp[i].cy[1] = ny1; vsp[i].ctag = gtag; break;
					case 4: case 5: case 7:
						dpx[0] = dx0; dpy[0] = vsp[i].cy[0];
						dpx[1] = dx1; dpy[1] = vsp[i].cy[1];
						dpx[2] = dx1; dpy[2] = ny1;
						dpx[3] = dx0; dpy[3] = ny0; drawpoly(dpx,dpy,4,0);
						vsp[i].cy[0] = ny0; vsp[i].cy[1] = ny1; vsp[i].ctag = gtag; break;
					case 8:
						dpx[0] = dx0; dpy[0] = vsp[i].cy[0];
						dpx[1] = dx1; dpy[1] = vsp[i].cy[1];
						dpx[2] = dx1; dpy[2] = vsp[i].fy[1];
						dpx[3] = dx0; dpy[3] = vsp[i].fy[0]; drawpoly(dpx,dpy,4,0);
						vsp[i].ctag = vsp[i].ftag = -1; break;
					default: break;
				}
			}
			else
			{
				switch(k)
				{
					case 7: case 6:
						dpx[0] = dx0; dpy[0] = ny0;
						dpx[1] = dx1; dpy[1] = vsp[i].fy[1];
						dpx[2] = dx0; dpy[2] = vsp[i].fy[0]; drawpoly(dpx,dpy,3,0);
						vsp[i].fy[0] = ny0; vsp[i].ftag = gtag; break;
					case 5: case 2:
						dpx[0] = dx0; dpy[0] = vsp[i].fy[0];
						dpx[1] = dx1; dpy[1] = ny1;
						dpx[2] = dx1; dpy[2] = vsp[i].fy[1]; drawpoly(dpx,dpy,3,0);
						vsp[i].fy[1] = ny1; vsp[i].ftag = gtag; break;
					case 4: case 3: case 1:
						dpx[0] = dx0; dpy[0] = ny0;
						dpx[1] = dx1; dpy[1] = ny1;
						dpx[2] = dx1; dpy[2] = vsp[i].fy[1];
						dpx[3] = dx0; dpy[3] = vsp[i].fy[0]; drawpoly(dpx,dpy,4,0);
						vsp[i].fy[0] = ny0; vsp[i].fy[1] = ny1; vsp[i].ftag = gtag; break;
					case 0:
						dpx[0] = dx0; dpy[0] = vsp[i].cy[0];
						dpx[1] = dx1; dpy[1] = vsp[i].cy[1];
						dpx[2] = dx1; dpy[2] = vsp[i].fy[1];
						dpx[3] = dx0; dpy[3] = vsp[i].fy[0]; drawpoly(dpx,dpy,4,0);
						vsp[i].ctag = vsp[i].ftag = -1; break;
					default: break;
				}
			}
		}
	}

	gtag++;

		//Combine neighboring vertical strips with matching collinear top&bottom edges
		//This prevents x-splits from propagating through the entire scan
	i = vsp[0].n;
	while (i)
	{
		ni = vsp[i].n;
		if ((vsp[i].cy[0] >= vsp[i].fy[0]) && (vsp[i].cy[1] >= vsp[i].fy[1])) { vsp[i].ctag = vsp[i].ftag = -1; }
		if ((vsp[i].ctag == vsp[ni].ctag) && (vsp[i].ftag == vsp[ni].ftag))
			{ vsp[i].cy[1] = vsp[ni].cy[1]; vsp[i].fy[1] = vsp[ni].fy[1]; vsdel(ni); }
		else i = ni;
	}
}

static void polymost_scansector (long sectnum);

static void polymost_drawalls (long bunch)
{
	sectortype *sec, *nextsec;
	walltype *wal, *wal2, *nwal;
	double ox, oy, oz, ox2, oy2, px[3], py[3], dd[3], uu[3], vv[3];
	double fx, fy, x0, x1, y0, y1, cy0, cy1, fy0, fy1, xp0, yp0, xp1, yp1, ryp0, ryp1, nx0, ny0, nx1, ny1;
	double t, r, t0, t1, ocy0, ocy1, ofy0, ofy1, oxp0, oyp0, ft[4];
	double oguo, ogux, oguy;
	long i, x, y, z, cz, fz, wallnum, sectnum, nextsectnum;

	sectnum = thesector[bunchfirst[bunch]]; sec = &sector[sectnum];

#ifdef USE_OPENGL
	if (!nofog) {
	if (rendmode == 3)
		bglFogf(GL_FOG_DENSITY,((float)globalvisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE);
	}
#endif

		//DRAW WALLS SECTION!
	for(z=bunchfirst[bunch];z>=0;z=p2[z])
	{
		wallnum = thewall[z]; wal = &wall[wallnum]; wal2 = &wall[wal->point2];
		nextsectnum = wal->nextsector; nextsec = &sector[nextsectnum];

			//Offset&Rotate 3D coordinates to screen 3D space
		x = wal->x-globalposx; y = wal->y-globalposy;
		xp0 = (double)y*gcosang  - (double)x*gsinang;
		yp0 = (double)x*gcosang2 + (double)y*gsinang2;
		x = wal2->x-globalposx; y = wal2->y-globalposy;
		xp1 = (double)y*gcosang  - (double)x*gsinang;
		yp1 = (double)x*gcosang2 + (double)y*gsinang2;

		oxp0 = xp0; oyp0 = yp0;

			//Clip to close parallel-screen plane
		if (yp0 < SCISDIST)
		{
			if (yp1 < SCISDIST) continue;
			t0 = (SCISDIST-yp0)/(yp1-yp0); xp0 = (xp1-xp0)*t0+xp0; yp0 = SCISDIST;
			nx0 = (wal2->x-wal->x)*t0+wal->x;
			ny0 = (wal2->y-wal->y)*t0+wal->y;
		}
		else { t0 = 0.f; nx0 = wal->x; ny0 = wal->y; }
		if (yp1 < SCISDIST)
		{
			t1 = (SCISDIST-oyp0)/(yp1-oyp0); xp1 = (xp1-oxp0)*t1+oxp0; yp1 = SCISDIST;
			nx1 = (wal2->x-wal->x)*t1+wal->x;
			ny1 = (wal2->y-wal->y)*t1+wal->y;
		}
		else { t1 = 1.f; nx1 = wal2->x; ny1 = wal2->y; }

		ryp0 = 1.f/yp0; ryp1 = 1.f/yp1;

			//Generate screen coordinates for front side of wall
		x0 = ghalfx*xp0*ryp0 + ghalfx;
		x1 = ghalfx*xp1*ryp1 + ghalfx;
		if (x1 <= x0) continue;

		ryp0 *= gyxscale; ryp1 *= gyxscale;

		getzsofslope(sectnum,nx0,ny0,&cz,&fz);
		cy0 = ((float)(cz-globalposz))*ryp0 + ghoriz;
		fy0 = ((float)(fz-globalposz))*ryp0 + ghoriz;
		getzsofslope(sectnum,nx1,ny1,&cz,&fz);
		cy1 = ((float)(cz-globalposz))*ryp1 + ghoriz;
		fy1 = ((float)(fz-globalposz))*ryp1 + ghoriz;


		globalpicnum = sec->floorpicnum; globalshade = sec->floorshade; globalpal = (long)((unsigned char)sec->floorpal);
		globalorientation = sec->floorstat;
		if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,sectnum);
		if (!(globalorientation&1))
		{
				//(singlobalang/-16384*(sx-ghalfx) + 0*(sy-ghoriz) + (cosviewingrangeglobalang/16384)*ghalfx)*d + globalposx    = u*16
				//(cosglobalang/ 16384*(sx-ghalfx) + 0*(sy-ghoriz) + (sinviewingrangeglobalang/16384)*ghalfx)*d + globalposy    = v*16
				//(                  0*(sx-ghalfx) + 1*(sy-ghoriz) + (                             0)*ghalfx)*d + globalposz/16 = (sec->floorz/16)
			if (!(globalorientation&64))
				{ ft[0] = globalposx; ft[1] = globalposy; ft[2] = cosglobalang; ft[3] = singlobalang; }
			else
			{
					//relative alignment
				fx = (double)(wall[wall[sec->wallptr].point2].x-wall[sec->wallptr].x);
				fy = (double)(wall[wall[sec->wallptr].point2].y-wall[sec->wallptr].y);
				r = 1.0/sqrt(fx*fx+fy*fy); fx *= r; fy *= r;
				ft[2] = cosglobalang*fx + singlobalang*fy;
				ft[3] = singlobalang*fx - cosglobalang*fy;
				ft[0] = ((double)(globalposx-wall[sec->wallptr].x))*fx + ((double)(globalposy-wall[sec->wallptr].y))*fy;
				ft[1] = ((double)(globalposy-wall[sec->wallptr].y))*fx - ((double)(globalposx-wall[sec->wallptr].x))*fy;
				if (!(globalorientation&4)) globalorientation ^= 32; else globalorientation ^= 16;
			}
			gdx = 0;
			gdy = gxyaspect; if (!(globalorientation&2)) gdy /= (double)(sec->floorz-globalposz);
			gdo = -ghoriz*gdy;
			if (globalorientation&8) { ft[0] /= 8; ft[1] /= -8; ft[2] /= 2097152; ft[3] /= 2097152; }
									  else { ft[0] /= 16; ft[1] /= -16; ft[2] /= 4194304; ft[3] /= 4194304; }
			gux = (double)ft[3]*((double)viewingrange)/-65536.0;
			gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
			guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
			guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
			guo += (double)(ft[2]-gux)*ghalfx;
			gvo -= (double)(ft[3]+gvx)*ghalfx;

				//Texture flipping
			if (globalorientation&4)
			{
				r = gux; gux = gvx; gvx = r;
				r = guy; guy = gvy; gvy = r;
				r = guo; guo = gvo; gvo = r;
			}
			if (globalorientation&16) { gux = -gux; guy = -guy; guo = -guo; }
			if (globalorientation&32) { gvx = -gvx; gvy = -gvy; gvo = -gvo; }

				//Texture panning
			fx = (float)sec->floorxpanning*((float)(1<<(picsiz[globalpicnum]&15)))/256.0;
			fy = (float)sec->floorypanning*((float)(1<<(picsiz[globalpicnum]>>4)))/256.0;
			if ((globalorientation&(2+64)) == (2+64)) //Hack for panning for slopes w/ relative alignment
			{
				r = (float)sec->floorheinum / 4096.0; r = 1.0/sqrt(r*r+1);
				if (!(globalorientation&4)) fy *= r; else fx *= r;
			}
			guy += gdy*fx; guo += gdo*fx;
			gvy += gdy*fy; gvo += gdo*fy;

			if (globalorientation&2) //slopes
			{
				px[0] = x0; py[0] = ryp0 + ghoriz;
				px[1] = x1; py[1] = ryp1 + ghoriz;

					//Pick some point guaranteed to be not collinear to the 1st two points
				ox = nx0 + (ny1-ny0);
				oy = ny0 + (nx0-nx1);
				ox2 = (double)(oy-globalposy)*gcosang  - (double)(ox-globalposx)*gsinang;
				oy2 = (double)(ox-globalposx)*gcosang2 + (double)(oy-globalposy)*gsinang2;
				oy2 = 1.0/oy2;
				px[2] = ghalfx*ox2*oy2 + ghalfx; oy2 *= gyxscale;
				py[2] = oy2 + ghoriz;

				for(i=0;i<3;i++)
				{
					dd[i] = px[i]*gdx + py[i]*gdy + gdo;
					uu[i] = px[i]*gux + py[i]*guy + guo;
					vv[i] = px[i]*gvx + py[i]*gvy + gvo;
				}

				py[0] = fy0;
				py[1] = fy1;
				py[2] = (getflorzofslope(sectnum,ox,oy)-globalposz)*oy2 + ghoriz;

				ox = py[1]-py[2]; oy = py[2]-py[0]; oz = py[0]-py[1];
				r = 1.0 / (ox*px[0] + oy*px[1] + oz*px[2]);
				gdx = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				gux = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				gvx = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				ox = px[2]-px[1]; oy = px[0]-px[2]; oz = px[1]-px[0];
				gdy = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				guy = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				gvy = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				gdo = dd[0] - px[0]*gdx - py[0]*gdy;
				guo = uu[0] - px[0]*gux - py[0]*guy;
				gvo = vv[0] - px[0]*gvx - py[0]*gvy;

				if (globalorientation&64) //Hack for relative alignment on slopes
				{
					r = (float)sec->floorheinum / 4096.0;
					r = sqrt(r*r+1);
					if (!(globalorientation&4)) { gvx *= r; gvy *= r; gvo *= r; }
												  else { gux *= r; guy *= r; guo *= r; }
				}
			}
			pow2xsplit = 0; domost(x0,fy0,x1,fy1); //flor
		}
		else if ((nextsectnum < 0) || (!(sector[nextsectnum].floorstat&1)))
		{
				//Parallaxing sky... hacked for Ken's mountain texture; paper-sky only :/
#ifdef USE_OPENGL
			if (rendmode == 3)
			{
				if (!nofog) {
				bglDisable(GL_FOG);
				//r = ((float)globalpisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE;
				//r *= ((double)xdimscale*(double)viewingrange*gdo) / (65536.0*65536.0);
				//bglFogf(GL_FOG_DENSITY,r);
				}

					//Use clamping for tiled sky textures
				for(i=(1<<pskybits)-1;i>0;i--)
					if (pskyoff[i] != pskyoff[i-1])
						{ skyclamphack = 1; break; }
			}
#endif
			if (!hicfindsubst(globalpicnum,globalpal,1))
			{
				dd[0] = (float)xdimen*.0000001; //Adjust sky depth based on screen size!
				t = (double)((1<<(picsiz[globalpicnum]&15))<<pskybits);
				vv[1] = dd[0]*((double)xdimscale*(double)viewingrange)/(65536.0*65536.0);
				vv[0] = dd[0]*((double)((tilesizy[globalpicnum]>>1)+parallaxyoffs)) - vv[1]*ghoriz;
				i = (1<<(picsiz[globalpicnum]>>4)); if (i != tilesizy[globalpicnum]) i += i;
				vv[0] += dd[0]*((double)sec->floorypanning)*((double)i)/256.0;


					//Hack to draw black rectangle below sky when looking down...
				gdx = 0; gdy = gxyaspect / 262144.0; gdo = -ghoriz*gdy;
				gux = 0; guy = 0; guo = 0;
				gvx = 0; gvy = (double)(tilesizy[globalpicnum]-1)*gdy; gvo = (double)(tilesizy[globalpicnum-1])*gdo;
				oy = (((double)tilesizy[globalpicnum])*dd[0]-vv[0])/vv[1];
				if ((oy > fy0) && (oy > fy1)) domost(x0,oy,x1,oy);
				else if ((oy > fy0) != (oy > fy1))
				{     //  fy0                      fy1
						//     \                    /
						//oy----------      oy----------
						//        \              /
						//         fy1        fy0
					ox = (oy-fy0)*(x1-x0)/(fy1-fy0) + x0;
					if (oy > fy0) { domost(x0,oy,ox,oy); domost(ox,oy,x1,fy1); }
							  else { domost(x0,fy0,ox,oy); domost(ox,oy,x1,oy); }
				} else domost(x0,fy0,x1,fy1);


				gdx = 0; gdy = 0; gdo = dd[0];
				gux = gdo*(t*((double)xdimscale)*((double)yxaspect)*((double)viewingrange))/(16384.0*65536.0*65536.0*5.0*1024.0);
				guy = 0; //guo calculated later
				gvx = 0; gvy = vv[1]; gvo = vv[0];

				i = globalpicnum; r = (fy1-fy0)/(x1-x0); //slope of line
				oy = ((double)viewingrange)/(ghalfx*256.0); oz = 1/oy;

				y = ((((long)((x0-ghalfx)*oy))+globalang)>>(11-pskybits));
				fx = x0;
				do
				{
					globalpicnum = pskyoff[y&((1<<pskybits)-1)]+i;
					guo = gdo*(t*((double)(globalang-(y<<(11-pskybits))))/2048.0 + (double)sec->floorxpanning) - gux*ghalfx;
					y++;
					ox = fx; fx = ((double)((y<<(11-pskybits))-globalang))*oz+ghalfx;
					if (fx > x1) { fx = x1; i = -1; }

					pow2xsplit = 0; domost(ox,(ox-x0)*r+fy0,fx,(fx-x0)*r+fy0); //flor
				} while (i >= 0);

			}
			else  //NOTE: code copied from ceiling code... lots of duplicated stuff :/
			{     //Skybox code for parallax ceiling!
				double _xp0, _yp0, _xp1, _yp1, _oxp0, _oyp0, _t0, _t1, _nx0, _ny0, _nx1, _ny1;
				double _ryp0, _ryp1, _x0, _x1, _cy0, _fy0, _cy1, _fy1, _ox0, _ox1;
				double nfy0, nfy1;
				long skywalx[4] = {-512,512,512,-512}, skywaly[4] = {-512,-512,512,512};

				pow2xsplit = 0;
				skyclamphack = 1;

				for(i=0;i<4;i++)
				{
					x = skywalx[i&3]; y = skywaly[i&3];
					_xp0 = (double)y*gcosang  - (double)x*gsinang;
					_yp0 = (double)x*gcosang2 + (double)y*gsinang2;
					x = skywalx[(i+1)&3]; y = skywaly[(i+1)&3];
					_xp1 = (double)y*gcosang  - (double)x*gsinang;
					_yp1 = (double)x*gcosang2 + (double)y*gsinang2;

					_oxp0 = _xp0; _oyp0 = _yp0;

						//Clip to close parallel-screen plane
					if (_yp0 < SCISDIST)
					{
						if (_yp1 < SCISDIST) continue;
						_t0 = (SCISDIST-_yp0)/(_yp1-_yp0); _xp0 = (_xp1-_xp0)*_t0+_xp0; _yp0 = SCISDIST;
						_nx0 = (skywalx[(i+1)&3]-skywalx[i&3])*_t0+skywalx[i&3];
						_ny0 = (skywaly[(i+1)&3]-skywaly[i&3])*_t0+skywaly[i&3];
					}
					else { _t0 = 0.f; _nx0 = skywalx[i&3]; _ny0 = skywaly[i&3]; }
					if (_yp1 < SCISDIST)
					{
						_t1 = (SCISDIST-_oyp0)/(_yp1-_oyp0); _xp1 = (_xp1-_oxp0)*_t1+_oxp0; _yp1 = SCISDIST;
						_nx1 = (skywalx[(i+1)&3]-skywalx[i&3])*_t1+skywalx[i&3];
						_ny1 = (skywaly[(i+1)&3]-skywaly[i&3])*_t1+skywaly[i&3];
					}
					else { _t1 = 1.f; _nx1 = skywalx[(i+1)&3]; _ny1 = skywaly[(i+1)&3]; }

					_ryp0 = 1.f/_yp0; _ryp1 = 1.f/_yp1;

						//Generate screen coordinates for front side of wall
					_x0 = ghalfx*_xp0*_ryp0 + ghalfx;
					_x1 = ghalfx*_xp1*_ryp1 + ghalfx;
					if (_x1 <= _x0) continue;
					if ((_x0 >= x1) || (x0 >= _x1)) continue;

					_ryp0 *= gyxscale; _ryp1 *= gyxscale;

					_cy0 = -8192.f*_ryp0 + ghoriz;
					_fy0 =  8192.f*_ryp0 + ghoriz;
					_cy1 = -8192.f*_ryp1 + ghoriz;
					_fy1 =  8192.f*_ryp1 + ghoriz;

					_ox0 = _x0; _ox1 = _x1;

						//Make sure: x0<=_x0<_x1<=_x1
					nfy0 = fy0; nfy1 = fy1;
					if (_x0 < x0)
					{
						t = (x0-_x0)/(_x1-_x0);
						_cy0 += (_cy1-_cy0)*t;
						_fy0 += (_fy1-_fy0)*t;
						_x0 = x0;
					}
					else if (_x0 > x0) nfy0 += (_x0-x0)*(fy1-fy0)/(x1-x0);
					if (_x1 > x1)
					{
						t = (x1-_x1)/(_x1-_x0);
						_cy1 += (_cy1-_cy0)*t;
						_fy1 += (_fy1-_fy0)*t;
						_x1 = x1;
					}
					else if (_x1 < x1) nfy1 += (_x1-x1)*(fy1-fy0)/(x1-x0);

						//   (skybox floor)
						//(_x0,_fy0)-(_x1,_fy1)
						//   (skybox wall)
						//(_x0,_cy0)-(_x1,_cy1)
						//   (skybox ceiling)
						//(_x0,nfy0)-(_x1,nfy1)

						//ceiling of skybox
					ft[0] = 512/16; ft[1] = 512/-16;
					ft[2] = ((float)cosglobalang)*(1.f/2147483648.f);
					ft[3] = ((float)singlobalang)*(1.f/2147483648.f);
					gdx = 0;
					gdy = gxyaspect*(1.f/4194304.f);
					gdo = -ghoriz*gdy;
					gux = (double)ft[3]*((double)viewingrange)/-65536.0;
					gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
					guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
					guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
					guo += (double)(ft[2]-gux)*ghalfx;
					gvo -= (double)(ft[3]+gvx)*ghalfx;
					gvx = -gvx; gvy = -gvy; gvo = -gvo; //y-flip skybox floor
					drawingskybox = 6; //ceiling/5th texture/index 4 of skybox
					if ((_fy0 > nfy0) && (_fy1 > nfy1)) domost(_x0,_fy0,_x1,_fy1);
					else if ((_fy0 > nfy0) != (_fy1 > nfy1))
					{
							//(ox,oy) is intersection of: (_x0,_cy0)-(_x1,_cy1)
							//                            (_x0,nfy0)-(_x1,nfy1)
							//ox = _x0 + (_x1-_x0)*t
							//oy = _cy0 + (_cy1-_cy0)*t
							//oy = nfy0 + (nfy1-nfy0)*t
						t = (_fy0-nfy0)/(nfy1-nfy0-_fy1+_fy0);
						ox = _x0 + (_x1-_x0)*t;
						oy = _fy0 + (_fy1-_fy0)*t;
						if (nfy0 > _fy0) { domost(_x0,nfy0,ox,oy); domost(ox,oy,_x1,_fy1); }
										else { domost(_x0,_fy0,ox,oy); domost(ox,oy,_x1,nfy1); }
					} else domost(_x0,nfy0,_x1,nfy1);

						//wall of skybox
					drawingskybox = i+1; //i+1th texture/index i of skybox
					gdx = (_ryp0-_ryp1)*gxyaspect*(1.f/512.f) / (_ox0-_ox1);
					gdy = 0;
					gdo = _ryp0*gxyaspect*(1.f/512.f) - gdx*_ox0;
					gux = (_t0*_ryp0 - _t1*_ryp1)*gxyaspect*(64.f/512.f) / (_ox0-_ox1);
					guo = _t0*_ryp0*gxyaspect*(64.f/512.f) - gux*_ox0;
					guy = 0;
					_t0 = -8192.0*_ryp0 + ghoriz;
					_t1 = -8192.0*_ryp1 + ghoriz;
					t = ((gdx*_ox0 + gdo)*8.f) / ((_ox1-_ox0) * _ryp0 * 2048.f);
					gvx = (_t0-_t1)*t;
					gvy = (_ox1-_ox0)*t;
					gvo = -gvx*_ox0 - gvy*_t0;
					if ((_cy0 > nfy0) && (_cy1 > nfy1)) domost(_x0,_cy0,_x1,_cy1);
					else if ((_cy0 > nfy0) != (_cy1 > nfy1))
					{
							//(ox,oy) is intersection of: (_x0,_fy0)-(_x1,_fy1)
							//                            (_x0,nfy0)-(_x1,nfy1)
							//ox = _x0 + (_x1-_x0)*t
							//oy = _fy0 + (_fy1-_fy0)*t
							//oy = nfy0 + (nfy1-nfy0)*t
						t = (_cy0-nfy0)/(nfy1-nfy0-_cy1+_cy0);
						ox = _x0 + (_x1-_x0)*t;
						oy = _cy0 + (_cy1-_cy0)*t;
						if (nfy0 > _cy0) { domost(_x0,nfy0,ox,oy); domost(ox,oy,_x1,_cy1); }
										else { domost(_x0,_cy0,ox,oy); domost(ox,oy,_x1,nfy1); }
					} else domost(_x0,nfy0,_x1,nfy1);
				}

					//Floor of skybox
				drawingskybox = 5; //floor/6th texture/index 5 of skybox
				ft[0] = 512/16; ft[1] = -512/-16;
				ft[2] = ((float)cosglobalang)*(1.f/2147483648.f);
				ft[3] = ((float)singlobalang)*(1.f/2147483648.f);
				gdx = 0;
				gdy = gxyaspect*(-1.f/4194304.f);
				gdo = -ghoriz*gdy;
				gux = (double)ft[3]*((double)viewingrange)/-65536.0;
				gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
				guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
				guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
				guo += (double)(ft[2]-gux)*ghalfx;
				gvo -= (double)(ft[3]+gvx)*ghalfx;
				domost(x0,fy0,x1,fy1);

				skyclamphack = 0;
				drawingskybox = 0;
			}
#ifdef USE_OPENGL
			if (rendmode == 3)
			{
				skyclamphack = 0;
				if (!nofog) {
				bglEnable(GL_FOG);
				//bglFogf(GL_FOG_DENSITY,((float)globalvisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE);
				}
			}
#endif
		}

		globalpicnum = sec->ceilingpicnum; globalshade = sec->ceilingshade; globalpal = (long)((unsigned char)sec->ceilingpal);
		globalorientation = sec->ceilingstat;
		if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,sectnum);
		if (!(globalorientation&1))
		{
			if (!(globalorientation&64))
				{ ft[0] = globalposx; ft[1] = globalposy; ft[2] = cosglobalang; ft[3] = singlobalang; }
			else
			{
					//relative alignment
				fx = (double)(wall[wall[sec->wallptr].point2].x-wall[sec->wallptr].x);
				fy = (double)(wall[wall[sec->wallptr].point2].y-wall[sec->wallptr].y);
				r = 1.0/sqrt(fx*fx+fy*fy); fx *= r; fy *= r;
				ft[2] = cosglobalang*fx + singlobalang*fy;
				ft[3] = singlobalang*fx - cosglobalang*fy;
				ft[0] = ((double)(globalposx-wall[sec->wallptr].x))*fx + ((double)(globalposy-wall[sec->wallptr].y))*fy;
				ft[1] = ((double)(globalposy-wall[sec->wallptr].y))*fx - ((double)(globalposx-wall[sec->wallptr].x))*fy;
				if (!(globalorientation&4)) globalorientation ^= 32; else globalorientation ^= 16;
			}
			gdx = 0;
			gdy = gxyaspect;
			if (!(globalorientation&2)) gdy /= (double)(sec->ceilingz-globalposz);
			gdo = -ghoriz*gdy;
			if (globalorientation&8) { ft[0] /= 8; ft[1] /= -8; ft[2] /= 2097152; ft[3] /= 2097152; }
									  else { ft[0] /= 16; ft[1] /= -16; ft[2] /= 4194304; ft[3] /= 4194304; }
			gux = (double)ft[3]*((double)viewingrange)/-65536.0;
			gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
			guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
			guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
			guo += (double)(ft[2]-gux)*ghalfx;
			gvo -= (double)(ft[3]+gvx)*ghalfx;

				//Texture flipping
			if (globalorientation&4)
			{
				r = gux; gux = gvx; gvx = r;
				r = guy; guy = gvy; gvy = r;
				r = guo; guo = gvo; gvo = r;
			}
			if (globalorientation&16) { gux = -gux; guy = -guy; guo = -guo; }
			if (globalorientation&32) { gvx = -gvx; gvy = -gvy; gvo = -gvo; }

				//Texture panning
			fx = (float)sec->ceilingxpanning*((float)(1<<(picsiz[globalpicnum]&15)))/256.0;
			fy = (float)sec->ceilingypanning*((float)(1<<(picsiz[globalpicnum]>>4)))/256.0;
			if ((globalorientation&(2+64)) == (2+64)) //Hack for panning for slopes w/ relative alignment
			{
				r = (float)sec->ceilingheinum / 4096.0; r = 1.0/sqrt(r*r+1);
				if (!(globalorientation&4)) fy *= r; else fx *= r;
			}
			guy += gdy*fx; guo += gdo*fx;
			gvy += gdy*fy; gvo += gdo*fy;

			if (globalorientation&2) //slopes
			{
				px[0] = x0; py[0] = ryp0 + ghoriz;
				px[1] = x1; py[1] = ryp1 + ghoriz;

					//Pick some point guaranteed to be not collinear to the 1st two points
				ox = nx0 + (ny1-ny0);
				oy = ny0 + (nx0-nx1);
				ox2 = (double)(oy-globalposy)*gcosang  - (double)(ox-globalposx)*gsinang ;
				oy2 = (double)(ox-globalposx)*gcosang2 + (double)(oy-globalposy)*gsinang2;
				oy2 = 1.0/oy2;
				px[2] = ghalfx*ox2*oy2 + ghalfx; oy2 *= gyxscale;
				py[2] = oy2 + ghoriz;

				for(i=0;i<3;i++)
				{
					dd[i] = px[i]*gdx + py[i]*gdy + gdo;
					uu[i] = px[i]*gux + py[i]*guy + guo;
					vv[i] = px[i]*gvx + py[i]*gvy + gvo;
				}

				py[0] = cy0;
				py[1] = cy1;
				py[2] = (getceilzofslope(sectnum,ox,oy)-globalposz)*oy2 + ghoriz;

				ox = py[1]-py[2]; oy = py[2]-py[0]; oz = py[0]-py[1];
				r = 1.0 / (ox*px[0] + oy*px[1] + oz*px[2]);
				gdx = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				gux = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				gvx = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				ox = px[2]-px[1]; oy = px[0]-px[2]; oz = px[1]-px[0];
				gdy = (ox*dd[0] + oy*dd[1] + oz*dd[2])*r;
				guy = (ox*uu[0] + oy*uu[1] + oz*uu[2])*r;
				gvy = (ox*vv[0] + oy*vv[1] + oz*vv[2])*r;
				gdo = dd[0] - px[0]*gdx - py[0]*gdy;
				guo = uu[0] - px[0]*gux - py[0]*guy;
				gvo = vv[0] - px[0]*gvx - py[0]*gvy;

				if (globalorientation&64) //Hack for relative alignment on slopes
				{
					r = (float)sec->ceilingheinum / 4096.0;
					r = sqrt(r*r+1);
					if (!(globalorientation&4)) { gvx *= r; gvy *= r; gvo *= r; }
												  else { gux *= r; guy *= r; guo *= r; }
				}
			}
			pow2xsplit = 0; domost(x1,cy1,x0,cy0); //ceil
		}
		else if ((nextsectnum < 0) || (!(sector[nextsectnum].ceilingstat&1)))
		{
#ifdef USE_OPENGL
			if (rendmode == 3)
			{
				if (!nofog) {
				bglDisable(GL_FOG);
				//r = ((float)globalpisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE;
				//r *= ((double)xdimscale*(double)viewingrange*gdo) / (65536.0*65536.0);
				//bglFogf(GL_FOG_DENSITY,r);
				}

					//Use clamping for tiled sky textures
				for(i=(1<<pskybits)-1;i>0;i--)
					if (pskyoff[i] != pskyoff[i-1])
						{ skyclamphack = 1; break; }
			}
#endif
				//Parallaxing sky...
			if (!hicfindsubst(globalpicnum,globalpal,1))
			{
					//Render for parallaxtype == 0 / paper-sky
				dd[0] = (float)xdimen*.0000001; //Adjust sky depth based on screen size!
				t = (double)((1<<(picsiz[globalpicnum]&15))<<pskybits);
				vv[1] = dd[0]*((double)xdimscale*(double)viewingrange)/(65536.0*65536.0);
				vv[0] = dd[0]*((double)((tilesizy[globalpicnum]>>1)+parallaxyoffs)) - vv[1]*ghoriz;
				i = (1<<(picsiz[globalpicnum]>>4)); if (i != tilesizy[globalpicnum]) i += i;
				vv[0] += dd[0]*((double)sec->ceilingypanning)*((double)i)/256.0;

					//Hack to draw black rectangle below sky when looking down...
				gdx = 0; gdy = gxyaspect / -262144.0; gdo = -ghoriz*gdy;
				gux = 0; guy = 0; guo = 0;
				gvx = 0; gvy = 0; gvo = 0;
				oy = -vv[0]/vv[1];
				if ((oy < cy0) && (oy < cy1)) domost(x1,oy,x0,oy);
				else if ((oy < cy0) != (oy < cy1))
				{      /*         cy1        cy0
						//        /             \
						//oy----------      oy---------
						//    /                    \
						//  cy0                     cy1
						*/
					ox = (oy-cy0)*(x1-x0)/(cy1-cy0) + x0;
					if (oy < cy0) { domost(ox,oy,x0,oy); domost(x1,cy1,ox,oy); }
								else { domost(ox,oy,x0,cy0); domost(x1,oy,ox,oy); }
				} else domost(x1,cy1,x0,cy0);

				gdx = 0; gdy = 0; gdo = dd[0];
				gux = gdo*(t*((double)xdimscale)*((double)yxaspect)*((double)viewingrange))/(16384.0*65536.0*65536.0*5.0*1024.0);
				guy = 0; //guo calculated later
				gvx = 0; gvy = vv[1]; gvo = vv[0];

				i = globalpicnum; r = (cy1-cy0)/(x1-x0); //slope of line
				oy = ((double)viewingrange)/(ghalfx*256.0); oz = 1/oy;

				y = ((((long)((x0-ghalfx)*oy))+globalang)>>(11-pskybits));
				fx = x0;
				do
				{
					globalpicnum = pskyoff[y&((1<<pskybits)-1)]+i;
					guo = gdo*(t*((double)(globalang-(y<<(11-pskybits))))/2048.0 + (double)sec->ceilingxpanning) - gux*ghalfx;
					y++;
					ox = fx; fx = ((double)((y<<(11-pskybits))-globalang))*oz+ghalfx;
					if (fx > x1) { fx = x1; i = -1; }
					pow2xsplit = 0; domost(fx,(fx-x0)*r+cy0,ox,(ox-x0)*r+cy0); //ceil
				} while (i >= 0);
			}
			else
			{     //Skybox code for parallax ceiling!
				double _xp0, _yp0, _xp1, _yp1, _oxp0, _oyp0, _t0, _t1, _nx0, _ny0, _nx1, _ny1;
				double _ryp0, _ryp1, _x0, _x1, _cy0, _fy0, _cy1, _fy1, _ox0, _ox1;
				double ncy0, ncy1;
				long skywalx[4] = {-512,512,512,-512}, skywaly[4] = {-512,-512,512,512};

				pow2xsplit = 0;
				skyclamphack = 1;

				for(i=0;i<4;i++)
				{
					x = skywalx[i&3]; y = skywaly[i&3];
					_xp0 = (double)y*gcosang  - (double)x*gsinang;
					_yp0 = (double)x*gcosang2 + (double)y*gsinang2;
					x = skywalx[(i+1)&3]; y = skywaly[(i+1)&3];
					_xp1 = (double)y*gcosang  - (double)x*gsinang;
					_yp1 = (double)x*gcosang2 + (double)y*gsinang2;

					_oxp0 = _xp0; _oyp0 = _yp0;

						//Clip to close parallel-screen plane
					if (_yp0 < SCISDIST)
					{
						if (_yp1 < SCISDIST) continue;
						_t0 = (SCISDIST-_yp0)/(_yp1-_yp0); _xp0 = (_xp1-_xp0)*_t0+_xp0; _yp0 = SCISDIST;
						_nx0 = (skywalx[(i+1)&3]-skywalx[i&3])*_t0+skywalx[i&3];
						_ny0 = (skywaly[(i+1)&3]-skywaly[i&3])*_t0+skywaly[i&3];
					}
					else { _t0 = 0.f; _nx0 = skywalx[i&3]; _ny0 = skywaly[i&3]; }
					if (_yp1 < SCISDIST)
					{
						_t1 = (SCISDIST-_oyp0)/(_yp1-_oyp0); _xp1 = (_xp1-_oxp0)*_t1+_oxp0; _yp1 = SCISDIST;
						_nx1 = (skywalx[(i+1)&3]-skywalx[i&3])*_t1+skywalx[i&3];
						_ny1 = (skywaly[(i+1)&3]-skywaly[i&3])*_t1+skywaly[i&3];
					}
					else { _t1 = 1.f; _nx1 = skywalx[(i+1)&3]; _ny1 = skywaly[(i+1)&3]; }

					_ryp0 = 1.f/_yp0; _ryp1 = 1.f/_yp1;

						//Generate screen coordinates for front side of wall
					_x0 = ghalfx*_xp0*_ryp0 + ghalfx;
					_x1 = ghalfx*_xp1*_ryp1 + ghalfx;
					if (_x1 <= _x0) continue;
					if ((_x0 >= x1) || (x0 >= _x1)) continue;

					_ryp0 *= gyxscale; _ryp1 *= gyxscale;

					_cy0 = -8192.f*_ryp0 + ghoriz;
					_fy0 =  8192.f*_ryp0 + ghoriz;
					_cy1 = -8192.f*_ryp1 + ghoriz;
					_fy1 =  8192.f*_ryp1 + ghoriz;

					_ox0 = _x0; _ox1 = _x1;

						//Make sure: x0<=_x0<_x1<=_x1
					ncy0 = cy0; ncy1 = cy1;
					if (_x0 < x0)
					{
						t = (x0-_x0)/(_x1-_x0);
						_cy0 += (_cy1-_cy0)*t;
						_fy0 += (_fy1-_fy0)*t;
						_x0 = x0;
					}
					else if (_x0 > x0) ncy0 += (_x0-x0)*(cy1-cy0)/(x1-x0);
					if (_x1 > x1)
					{
						t = (x1-_x1)/(_x1-_x0);
						_cy1 += (_cy1-_cy0)*t;
						_fy1 += (_fy1-_fy0)*t;
						_x1 = x1;
					}
					else if (_x1 < x1) ncy1 += (_x1-x1)*(cy1-cy0)/(x1-x0);

						//   (skybox ceiling)
						//(_x0,_cy0)-(_x1,_cy1)
						//   (skybox wall)
						//(_x0,_fy0)-(_x1,_fy1)
						//   (skybox floor)
						//(_x0,ncy0)-(_x1,ncy1)

						//ceiling of skybox
					drawingskybox = 5; //ceiling/5th texture/index 4 of skybox
					ft[0] = 512/16; ft[1] = -512/-16;
					ft[2] = ((float)cosglobalang)*(1.f/2147483648.f);
					ft[3] = ((float)singlobalang)*(1.f/2147483648.f);
					gdx = 0;
					gdy = gxyaspect*-(1.f/4194304.f);
					gdo = -ghoriz*gdy;
					gux = (double)ft[3]*((double)viewingrange)/-65536.0;
					gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
					guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
					guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
					guo += (double)(ft[2]-gux)*ghalfx;
					gvo -= (double)(ft[3]+gvx)*ghalfx;
					if ((_cy0 < ncy0) && (_cy1 < ncy1)) domost(_x1,_cy1,_x0,_cy0);
					else if ((_cy0 < ncy0) != (_cy1 < ncy1))
					{
							//(ox,oy) is intersection of: (_x0,_cy0)-(_x1,_cy1)
							//                            (_x0,ncy0)-(_x1,ncy1)
							//ox = _x0 + (_x1-_x0)*t
							//oy = _cy0 + (_cy1-_cy0)*t
							//oy = ncy0 + (ncy1-ncy0)*t
						t = (_cy0-ncy0)/(ncy1-ncy0-_cy1+_cy0);
						ox = _x0 + (_x1-_x0)*t;
						oy = _cy0 + (_cy1-_cy0)*t;
						if (ncy0 < _cy0) { domost(ox,oy,_x0,ncy0); domost(_x1,_cy1,ox,oy); }
										else { domost(ox,oy,_x0,_cy0); domost(_x1,ncy1,ox,oy); }
					} else domost(_x1,ncy1,_x0,ncy0);

						//wall of skybox
					drawingskybox = i+1; //i+1th texture/index i of skybox
					gdx = (_ryp0-_ryp1)*gxyaspect*(1.f/512.f) / (_ox0-_ox1);
					gdy = 0;
					gdo = _ryp0*gxyaspect*(1.f/512.f) - gdx*_ox0;
					gux = (_t0*_ryp0 - _t1*_ryp1)*gxyaspect*(64.f/512.f) / (_ox0-_ox1);
					guo = _t0*_ryp0*gxyaspect*(64.f/512.f) - gux*_ox0;
					guy = 0;
					_t0 = -8192.0*_ryp0 + ghoriz;
					_t1 = -8192.0*_ryp1 + ghoriz;
					t = ((gdx*_ox0 + gdo)*8.f) / ((_ox1-_ox0) * _ryp0 * 2048.f);
					gvx = (_t0-_t1)*t;
					gvy = (_ox1-_ox0)*t;
					gvo = -gvx*_ox0 - gvy*_t0;
					if ((_fy0 < ncy0) && (_fy1 < ncy1)) domost(_x1,_fy1,_x0,_fy0);
					else if ((_fy0 < ncy0) != (_fy1 < ncy1))
					{
							//(ox,oy) is intersection of: (_x0,_fy0)-(_x1,_fy1)
							//                            (_x0,ncy0)-(_x1,ncy1)
							//ox = _x0 + (_x1-_x0)*t
							//oy = _fy0 + (_fy1-_fy0)*t
							//oy = ncy0 + (ncy1-ncy0)*t
						t = (_fy0-ncy0)/(ncy1-ncy0-_fy1+_fy0);
						ox = _x0 + (_x1-_x0)*t;
						oy = _fy0 + (_fy1-_fy0)*t;
						if (ncy0 < _fy0) { domost(ox,oy,_x0,ncy0); domost(_x1,_fy1,ox,oy); }
										else { domost(ox,oy,_x0,_fy0); domost(_x1,ncy1,ox,oy); }
					} else domost(_x1,ncy1,_x0,ncy0);
				}

					//Floor of skybox
				drawingskybox = 6; //floor/6th texture/index 5 of skybox
				ft[0] = 512/16; ft[1] = 512/-16;
				ft[2] = ((float)cosglobalang)*(1.f/2147483648.f);
				ft[3] = ((float)singlobalang)*(1.f/2147483648.f);
				gdx = 0;
				gdy = gxyaspect*(1.f/4194304.f);
				gdo = -ghoriz*gdy;
				gux = (double)ft[3]*((double)viewingrange)/-65536.0;
				gvx = (double)ft[2]*((double)viewingrange)/-65536.0;
				guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
				guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
				guo += (double)(ft[2]-gux)*ghalfx;
				gvo -= (double)(ft[3]+gvx)*ghalfx;
				gvx = -gvx; gvy = -gvy; gvo = -gvo; //y-flip skybox floor
				domost(x1,cy1,x0,cy0);

				skyclamphack = 0;
				drawingskybox = 0;
			}
#ifdef USE_OPENGL
			if (rendmode == 3)
			{
				skyclamphack = 0;
				if (!nofog) {
				bglEnable(GL_FOG);
				//bglFogf(GL_FOG_DENSITY,((float)globalvisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE);
				}
			}
#endif
		}

			//(x0,cy0) == (u=             0,v=0,d=)
			//(x1,cy0) == (u=wal->xrepeat*8,v=0)
			//(x0,fy0) == (u=             0,v=v)
			//             u = (gux*sx + guy*sy + guo) / (gdx*sx + gdy*sy + gdo)
			//             v = (gvx*sx + gvy*sy + gvo) / (gdx*sx + gdy*sy + gdo)
			//             0 = (gux*x0 + guy*cy0 + guo) / (gdx*x0 + gdy*cy0 + gdo)
			//wal->xrepeat*8 = (gux*x1 + guy*cy0 + guo) / (gdx*x1 + gdy*cy0 + gdo)
			//             0 = (gvx*x0 + gvy*cy0 + gvo) / (gdx*x0 + gdy*cy0 + gdo)
			//             v = (gvx*x0 + gvy*fy0 + gvo) / (gdx*x0 + gdy*fy0 + gdo)
			//sx = x0, u = t0*wal->xrepeat*8, d = yp0;
			//sx = x1, u = t1*wal->xrepeat*8, d = yp1;
			//d = gdx*sx + gdo
			//u = (gux*sx + guo) / (gdx*sx + gdo)
			//yp0 = gdx*x0 + gdo
			//yp1 = gdx*x1 + gdo
			//t0*wal->xrepeat*8 = (gux*x0 + guo) / (gdx*x0 + gdo)
			//t1*wal->xrepeat*8 = (gux*x1 + guo) / (gdx*x1 + gdo)
			//gdx*x0 + gdo = yp0
			//gdx*x1 + gdo = yp1
		gdx = (ryp0-ryp1)*gxyaspect / (x0-x1);
		gdy = 0;
		gdo = ryp0*gxyaspect - gdx*x0;

			//gux*x0 + guo = t0*wal->xrepeat*8*yp0
			//gux*x1 + guo = t1*wal->xrepeat*8*yp1
		gux = (t0*ryp0 - t1*ryp1)*gxyaspect*(float)wal->xrepeat*8.f / (x0-x1);
		guo = t0*ryp0*gxyaspect*(float)wal->xrepeat*8.f - gux*x0;
		guo += (float)wal->xpanning*gdo;
		gux += (float)wal->xpanning*gdx;
		guy = 0;
			//Derivation for u:
			//   (gvx*x0 + gvy*cy0 + gvo) / (gdx*x0 + gdy*cy0 + gdo) = 0
			//   (gvx*x1 + gvy*cy1 + gvo) / (gdx*x1 + gdy*cy1 + gdo) = 0
			//   (gvx*x0 + gvy*fy0 + gvo) / (gdx*x0 + gdy*fy0 + gdo) = v
			//   (gvx*x1 + gvy*fy1 + gvo) / (gdx*x1 + gdy*fy1 + gdo) = v
			//   (gvx*x0 + gvy*cy0 + gvo*1) = 0
			//   (gvx*x1 + gvy*cy1 + gvo*1) = 0
			//   (gvx*x0 + gvy*fy0 + gvo*1) = t
		ogux = gux; oguy = guy; oguo = guo;

		if (nextsectnum >= 0)
		{
			getzsofslope(nextsectnum,nx0,ny0,&cz,&fz);
			ocy0 = ((float)(cz-globalposz))*ryp0 + ghoriz;
			ofy0 = ((float)(fz-globalposz))*ryp0 + ghoriz;
			getzsofslope(nextsectnum,nx1,ny1,&cz,&fz);
			ocy1 = ((float)(cz-globalposz))*ryp1 + ghoriz;
			ofy1 = ((float)(fz-globalposz))*ryp1 + ghoriz;

			if ((wal->cstat&48) == 16) maskwall[maskwallcnt++] = z;

			if (((cy0 < ocy0) || (cy1 < ocy1)) && (!((sec->ceilingstat&sector[nextsectnum].ceilingstat)&1)))
			{
				globalpicnum = wal->picnum; globalshade = wal->shade; globalpal = (long)((unsigned char)wal->pal);
				if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,wallnum+16384);

				if (!(wal->cstat&4)) i = sector[nextsectnum].ceilingz; else i = sec->ceilingz;
				t0 = ((float)(i-globalposz))*ryp0 + ghoriz;
				t1 = ((float)(i-globalposz))*ryp1 + ghoriz;
				t = ((gdx*x0 + gdo) * (float)wal->yrepeat) / ((x1-x0) * ryp0 * 2048.f);
				i = (1<<(picsiz[globalpicnum]>>4)); if (i < tilesizy[globalpicnum]) i <<= 1;
				fy = (float)wal->ypanning * ((float)i) / 256.0;
				gvx = (t0-t1)*t;
				gvy = (x1-x0)*t;
				gvo = -gvx*x0 - gvy*t0 + fy*gdo; gvx += fy*gdx; gvy += fy*gdy;

				if (wal->cstat&8) //xflip
				{
					t = (float)(wal->xrepeat*8 + wal->xpanning*2);
					gux = gdx*t - gux;
					guy = gdy*t - guy;
					guo = gdo*t - guo;
				}
				if (wal->cstat&256) { gvx = -gvx; gvy = -gvy; gvo = -gvo; } //yflip

				pow2xsplit = 1; domost(x1,ocy1,x0,ocy0);

				if (wal->cstat&8) { gux = ogux; guy = oguy; guo = oguo; }
			}
			if (((ofy0 < fy0) || (ofy1 < fy1)) && (!((sec->floorstat&sector[nextsectnum].floorstat)&1)))
			{
				if (!(wal->cstat&2)) nwal = wal;
				else
				{
					nwal = &wall[wal->nextwall];
					guo += (float)(nwal->xpanning-wal->xpanning)*gdo;
					gux += (float)(nwal->xpanning-wal->xpanning)*gdx;
					guy += (float)(nwal->xpanning-wal->xpanning)*gdy;
				}
				globalpicnum = nwal->picnum; globalshade = nwal->shade; globalpal = (long)((unsigned char)nwal->pal);
				if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,wallnum+16384);

				if (!(nwal->cstat&4)) i = sector[nextsectnum].floorz; else i = sec->ceilingz;
				t0 = ((float)(i-globalposz))*ryp0 + ghoriz;
				t1 = ((float)(i-globalposz))*ryp1 + ghoriz;
				t = ((gdx*x0 + gdo) * (float)wal->yrepeat) / ((x1-x0) * ryp0 * 2048.f);
				i = (1<<(picsiz[globalpicnum]>>4)); if (i < tilesizy[globalpicnum]) i <<= 1;
				fy = (float)nwal->ypanning * ((float)i) / 256.0;
				gvx = (t0-t1)*t;
				gvy = (x1-x0)*t;
				gvo = -gvx*x0 - gvy*t0 + fy*gdo; gvx += fy*gdx; gvy += fy*gdy;

				if (wal->cstat&8) //xflip
				{
					t = (float)(wal->xrepeat*8 + nwal->xpanning*2);
					gux = gdx*t - gux;
					guy = gdy*t - guy;
					guo = gdo*t - guo;
				}
				if (nwal->cstat&256) { gvx = -gvx; gvy = -gvy; gvo = -gvo; } //yflip

				pow2xsplit = 1; domost(x0,ofy0,x1,ofy1);

				if (wal->cstat&(2+8)) { guo = oguo; gux = ogux; guy = oguy; }
			}
		}

		if ((nextsectnum < 0) || (wal->cstat&32))   //White/1-way wall
		{
			if (nextsectnum < 0) globalpicnum = wal->picnum; else globalpicnum = wal->overpicnum;
			globalshade = wal->shade; globalpal = (long)((unsigned char)wal->pal);
			if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,wallnum+16384);

			if (nextsectnum >= 0) { if (!(wal->cstat&4)) i = nextsec->ceilingz; else i = sec->ceilingz; }
								  else { if (!(wal->cstat&4)) i = sec->ceilingz;     else i = sec->floorz; }
			t0 = ((float)(i-globalposz))*ryp0 + ghoriz;
			t1 = ((float)(i-globalposz))*ryp1 + ghoriz;
			t = ((gdx*x0 + gdo) * (float)wal->yrepeat) / ((x1-x0) * ryp0 * 2048.f);
			i = (1<<(picsiz[globalpicnum]>>4)); if (i < tilesizy[globalpicnum]) i <<= 1;
			fy = (float)wal->ypanning * ((float)i) / 256.0;
			gvx = (t0-t1)*t;
			gvy = (x1-x0)*t;
			gvo = -gvx*x0 - gvy*t0 + fy*gdo; gvx += fy*gdx; gvy += fy*gdy;

			if (wal->cstat&8) //xflip
			{
				t = (float)(wal->xrepeat*8 + wal->xpanning*2);
				gux = gdx*t - gux;
				guy = gdy*t - guy;
				guo = gdo*t - guo;
			}
			if (wal->cstat&256) { gvx = -gvx; gvy = -gvy; gvo = -gvo; } //yflip
			pow2xsplit = 1; domost(x0,-10000,x1,-10000);
		}

		if (nextsectnum >= 0)
			if ((!(gotsector[nextsectnum>>3]&pow2char[nextsectnum&7])) && (testvisiblemost(x0,x1)))
				polymost_scansector(nextsectnum);
	}
}

static long wallfront(long, long);
static long polymost_bunchfront (long b1, long b2)
{
	double x1b1, x1b2, x2b1, x2b2;
	long b1f, b2f, i;

	b1f = bunchfirst[b1]; x1b1 = dxb1[b1f]; x2b2 = dxb2[bunchlast[b2]]; if (x1b1 >= x2b2) return(-1);
	b2f = bunchfirst[b2]; x1b2 = dxb1[b2f]; x2b1 = dxb2[bunchlast[b1]]; if (x1b2 >= x2b1) return(-1);

	if (x1b1 >= x1b2)
	{
		for(i=b2f;dxb2[i]<=x1b1;i=p2[i]);
		return(wallfront(b1f,i));
	}
	for(i=b1f;dxb2[i]<=x1b2;i=p2[i]);
	return(wallfront(i,b2f));
}

static void polymost_scansector (long sectnum)
{
	double d, xp1, yp1, xp2, yp2;
	walltype *wal, *wal2;
	spritetype *spr;
	long z, zz, startwall, endwall, numscansbefore, scanfirst, bunchfrst, nextsectnum;
	long xs, ys, x1, y1, x2, y2;

	if (sectnum < 0) return;
	if (automapping) show2dsector[sectnum>>3] |= pow2char[sectnum&7];

	sectorborder[0] = sectnum, sectorbordercnt = 1;
	do
	{
		sectnum = sectorborder[--sectorbordercnt];

		for(z=headspritesect[sectnum];z>=0;z=nextspritesect[z])
		{
			spr = &sprite[z];
			if ((((spr->cstat&0x8000) == 0) || (showinvisibility)) &&
				  (spr->xrepeat > 0) && (spr->yrepeat > 0) &&
				  (spritesortcnt < MAXSPRITESONSCREEN))
			{
				xs = spr->x-globalposx; ys = spr->y-globalposy;
				if ((spr->cstat&48) || (xs*gcosang+ys*gsinang > 0))
				{
					copybufbyte(spr,&tsprite[spritesortcnt],sizeof(spritetype));
					tsprite[spritesortcnt++].owner = z;
				}
			}
		}

		gotsector[sectnum>>3] |= pow2char[sectnum&7];

		bunchfrst = numbunches;
		numscansbefore = numscans;

		startwall = sector[sectnum].wallptr; endwall = sector[sectnum].wallnum+startwall;
		scanfirst = numscans;
		xp2 = 0; yp2 = 0;
		for(z=startwall,wal=&wall[z];z<endwall;z++,wal++)
		{
			wal2 = &wall[wal->point2];
			x1 = wal->x-globalposx; y1 = wal->y-globalposy;
			x2 = wal2->x-globalposx; y2 = wal2->y-globalposy;

			nextsectnum = wal->nextsector; //Scan close sectors
			if ((nextsectnum >= 0) && (!(wal->cstat&32)) && (!(gotsector[nextsectnum>>3]&pow2char[nextsectnum&7])))
			{
				d = (double)x1*(double)y2 - (double)x2*(double)y1; xp1 = (double)(x2-x1); yp1 = (double)(y2-y1);
				if (d*d <= (xp1*xp1 + yp1*yp1)*(SCISDIST*SCISDIST*260.0))
					sectorborder[sectorbordercnt++] = nextsectnum;
			}

			if ((z == startwall) || (wall[z-1].point2 != z))
			{
				xp1 = ((double)y1*(double)cosglobalang             - (double)x1*(double)singlobalang            )/64.0;
				yp1 = ((double)x1*(double)cosviewingrangeglobalang + (double)y1*(double)sinviewingrangeglobalang)/64.0;
			}
			else { xp1 = xp2; yp1 = yp2; }
			xp2 = ((double)y2*(double)cosglobalang             - (double)x2*(double)singlobalang            )/64.0;
			yp2 = ((double)x2*(double)cosviewingrangeglobalang + (double)y2*(double)sinviewingrangeglobalang)/64.0;
			if ((yp1 >= SCISDIST) || (yp2 >= SCISDIST))
				if ((double)xp1*(double)yp2 < (double)xp2*(double)yp1) //if wall is facing you...
				{
					if (yp1 >= SCISDIST)
						  dxb1[numscans] = (double)xp1*ghalfx/(double)yp1 + ghalfx;
					else dxb1[numscans] = -1e32;

					if (yp2 >= SCISDIST)
						  dxb2[numscans] = (double)xp2*ghalfx/(double)yp2 + ghalfx;
					else dxb2[numscans] = 1e32;

					if (dxb1[numscans] < dxb2[numscans])
						{ thesector[numscans] = sectnum; thewall[numscans] = z; p2[numscans] = numscans+1; numscans++; }
				}

			if ((wall[z].point2 < z) && (scanfirst < numscans))
				{ p2[numscans-1] = scanfirst; scanfirst = numscans; }
		}

		for(z=numscansbefore;z<numscans;z++)
			if ((wall[thewall[z]].point2 != thewall[p2[z]]) || (dxb2[z] > dxb1[p2[z]]))
				{ bunchfirst[numbunches++] = p2[z]; p2[z] = -1; }

		for(z=bunchfrst;z<numbunches;z++)
		{
			for(zz=bunchfirst[z];p2[zz]>=0;zz=p2[zz]);
			bunchlast[z] = zz;
		}
	} while (sectorbordercnt > 0);
}

void polymost_drawrooms ()
{
	long i, j, k, n, n2, closest;
	double ox, oy, oz, ox2, oy2, oz2, r, px[6], py[6], pz[6], px2[6], py2[6], pz2[6], sx[6], sy[6];

#ifdef USE_PMDBGKEYS
	if (keystatus[0x29])
	{
		keystatus[0x29] = 0;
		if ((keystatus[0x1d]|keystatus[0x9d])) { i = rendmode-1; if (i < 0) i = 3; }
													 else { if (rendmode == 3) i = 2; else i = 3; }
		setrendermode(i);
	}
	if (keystatus[0x2b]) // Backslash
	{
		keystatus[0x2b] = 0;
		glanisotropy >>= 1; if (!glanisotropy) glanisotropy = glinfo.maxanisotropy;
		gltexinvalidateall();
	}
#endif
	if (!rendmode) return;

	begindrawing();
	frameoffset = frameplace + windowy1*bytesperline + windowx1;

#ifdef USE_PMDBGKEYS
	if (keystatus[0x38]) gtang += (float)((keystatus[0x2a]|keystatus[0x36])*3+1)*0.01;
	if (keystatus[0xb8]) gtang -= (float)((keystatus[0x2a]|keystatus[0x36])*3+1)*0.01;
	if (keystatus[0x1d]|keystatus[0x9d]) gtang = 0;
#endif

#ifdef USE_OPENGL
	omd2tims = md2tims; md2tims = getticks();
	if (((unsigned long)(md2tims-omd2tims)) > 10000) omd2tims = md2tims;
	if (rendmode == 3)
	{
		resizeglcheck();

		//bglClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		//bglEnable(GL_TEXTURE_2D);
		//bglTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE); //default anyway
		bglEnable(GL_DEPTH_TEST);
		bglDepthFunc(GL_ALWAYS); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS

		//bglPolygonOffset(1,1); //Supposed to make sprites pasted on walls or floors not disappear
		bglDepthRange(0.00001,1.0); //<- this is more widely supported than glPolygonOffset

		 //Enable this for OpenGL red-blue glasses mode :)
		if (glredbluemode)
		{
			float m[4][4];
			static int grbfcnt = 0; grbfcnt++;
			if (redblueclearcnt < numpages) { redblueclearcnt++; bglColorMask(1,1,1,1); bglClear(GL_COLOR_BUFFER_BIT); }
			if (grbfcnt&1)
			{
				bglViewport(windowx1-16,yres-(windowy2+1),windowx2-(windowx1-16)+1,windowy2-windowy1+1);
				bglColorMask(1,0,0,1);
				globalposx += (float)singlobalang/1024.0;
				globalposy -= (float)cosglobalang/1024.0;
			}
			else
			{
				bglViewport(windowx1,yres-(windowy2+1),windowx2+16-windowx1+1,windowy2-windowy1+1);
				bglColorMask(0,1,1,1);
				globalposx -= (float)singlobalang/1024.0;
				globalposy += (float)cosglobalang/1024.0;
			}
		}
	}
#endif

		//Polymost supports true look up/down :) Here, we convert horizon to angle.
		//gchang&gshang are cos&sin of this angle (respectively)
	gyxscale = ((double)xdimenscale)/131072.0;
	gxyaspect = ((double)xyaspect*(double)viewingrange)*(5.0/(65536.0*262144.0));
	gviewxrange = ((double)viewingrange)*((double)xdimen)/(32768.0*1024.0);
	gcosang = ((double)cosglobalang)/262144.0;
	gsinang = ((double)singlobalang)/262144.0;
	gcosang2 = gcosang*((double)viewingrange)/65536.0;
	gsinang2 = gsinang*((double)viewingrange)/65536.0;
	ghalfx = (double)halfxdimen; grhalfxdown10 = 1.0/(((double)ghalfx)*1024);
	ghoriz = (double)globalhoriz;

		//global cos/sin height angle
	r = (double)((ydimen>>1)-ghoriz);
	gshang = r/sqrt(r*r+ghalfx*ghalfx);
	gchang = sqrt(1.0-gshang*gshang);
	ghoriz = (double)(ydimen>>1);

	  //global cos/sin tilt angle
	gctang = cos(gtang);
	gstang = sin(gtang);
	if (fabs(gstang) < .001) //This hack avoids nasty precision bugs in domost()
		{ gstang = 0; if (gctang > 0) gctang = 1.0; else gctang = -1.0; }

	if (inpreparemirror)
		gstang = -gstang;

		//Generate viewport trapezoid (for handling screen up/down)
	px[0] = px[3] = 0-1; px[1] = px[2] = windowx2+1-windowx1+2;
	py[0] = py[1] = 0-1; py[2] = py[3] = windowy2+1-windowy1+2; n = 4;
	for(i=0;i<n;i++)
	{
		ox = px[i]-ghalfx; oy = py[i]-ghoriz; oz = ghalfx;

			//Tilt rotation (backwards)
		ox2 = ox*gctang + oy*gstang;
		oy2 = oy*gctang - ox*gstang;
		oz2 = oz;

			//Up/down rotation (backwards)
		px[i] = ox2;
		py[i] = oy2*gchang + oz2*gshang;
		pz[i] = oz2*gchang - oy2*gshang;
	}

		//Clip to SCISDIST plane
	n2 = 0;
	for(i=0;i<n;i++)
	{
		j = i+1; if (j >= n) j = 0;
		if (pz[i] >= SCISDIST) { px2[n2] = px[i]; py2[n2] = py[i]; pz2[n2] = pz[i]; n2++; }
		if ((pz[i] >= SCISDIST) != (pz[j] >= SCISDIST))
		{
			r = (SCISDIST-pz[i])/(pz[j]-pz[i]);
			px2[n2] = (px[j]-px[i])*r + px[i];
			py2[n2] = (py[j]-py[i])*r + py[i];
			pz2[n2] = SCISDIST; n2++;
		}
	}
	if (n2 < 3) { enddrawing(); return; }
	for(i=0;i<n2;i++)
	{
		r = ghalfx / pz2[i];
		sx[i] = px2[i]*r + ghalfx;
		sy[i] = py2[i]*r + ghoriz;
	}
	initmosts(sx,sy,n2);

	if (searchit == 2)
	{
		short hitsect, hitwall, hitsprite;
		long vx, vy, vz, hitx, hity, hitz;

		ox2 = searchx-ghalfx; oy2 = searchy-ghoriz; oz2 = ghalfx;

			//Tilt rotation
		ox = ox2*gctang + oy2*gstang;
		oy = oy2*gctang - ox2*gstang;
		oz = oz2;

			//Up/down rotation
		ox2 = oz*gchang - oy*gshang;
		oy2 = ox;
		oz2 = oy*gchang + oz*gshang;

			//Standard Left/right rotation
		vx = ox2*((float)cosglobalang) - oy2*((float)singlobalang);
		vy = ox2*((float)singlobalang) + oy2*((float)cosglobalang);
		vz = oz2*16384.0;

		hitallsprites = 1;
		hitscan(globalposx,globalposy,globalposz,globalcursectnum, //Start position
			vx>>12,vy>>12,vz>>8,&hitsect,&hitwall,&hitsprite,&hitx,&hity,&hitz,0xffff0030);
		hitallsprites = 0;

		searchsector = hitsect;
		if (hitwall >= 0)
		{
			searchwall = hitwall; searchstat = 0;
			if (wall[hitwall].nextwall >= 0)
			{
				long cz, fz;
				getzsofslope(wall[hitwall].nextsector,hitx,hity,&cz,&fz);
				if (hitz > fz)
				{
					if (wall[hitwall].cstat&2) //'2' bottoms of walls
						searchwall = wall[hitwall].nextwall;
				}
				else if ((hitz > cz) && (wall[hitwall].cstat&(16+32))) //masking or 1-way
					searchstat = 4;
			}
		}
		else if (hitsprite >= 0) { searchwall = hitsprite; searchstat = 3; }
		else
		{
			long cz, fz;
			getzsofslope(hitsect,hitx,hity,&cz,&fz);
			if ((hitz<<1) < cz+fz) searchstat = 1; else searchstat = 2;
			//if (vz < 0) searchstat = 1; else searchstat = 2; //Won't work for slopes :/
		}
		searchit = 0;
	}

	numscans = numbunches = 0;

	if (globalcursectnum >= MAXSECTORS)
		globalcursectnum -= MAXSECTORS;
	else
	{
		i = globalcursectnum;
		updatesector(globalposx,globalposy,&globalcursectnum);
		if (globalcursectnum < 0) globalcursectnum = i;
	}

	polymost_scansector(globalcursectnum);

	if (inpreparemirror)
	{
		grhalfxdown10x = -grhalfxdown10;
		inpreparemirror = 0;
		polymost_drawalls(0);
		numbunches--;
		bunchfirst[0] = bunchfirst[numbunches];
		bunchlast[0] = bunchlast[numbunches];
	}
	else
		grhalfxdown10x = grhalfxdown10;

	while (numbunches > 0)
	{
		memset(tempbuf,0,numbunches+3); tempbuf[0] = 1;

		closest = 0;              //Almost works, but not quite :(
		for(i=1;i<numbunches;i++)
		{
			j = polymost_bunchfront(i,closest); if (j < 0) continue;
			tempbuf[i] = 1;
			if (!j) { tempbuf[closest] = 1; closest = i; }
		}
		for(i=0;i<numbunches;i++) //Double-check
		{
			if (tempbuf[i]) continue;
			j = polymost_bunchfront(i,closest); if (j < 0) continue;
			tempbuf[i] = 1;
			if (!j) { tempbuf[closest] = 1; closest = i; i = 0; }
		}

		polymost_drawalls(closest);

		numbunches--;
		bunchfirst[closest] = bunchfirst[numbunches];
		bunchlast[closest] = bunchlast[numbunches];
	}
#ifdef USE_OPENGL
	if (rendmode == 3)
	{
		bglDepthFunc(GL_LEQUAL); //NEVER,LESS,(,L)EQUAL,GREATER,(NOT,G)EQUAL,ALWAYS

		//bglPolygonOffset(0,0);
		bglDepthRange(0.0,0.99999); //<- this is more widely supported than glPolygonOffset
	}
#endif

	enddrawing();
}

void polymost_drawmaskwall (long damaskwallcnt)
{
	double dpx[8], dpy[8], dpx2[8], dpy2[8];
	float fx, fy, x0, x1, sx0, sy0, sx1, sy1, xp0, yp0, xp1, yp1, oxp0, oyp0, ryp0, ryp1;
	float f, r, t, t0, t1, nx0, ny0, nx1, ny1, py[4], csy[4], fsy[4];
	long i, j, k, n, n2, x, z, sectnum, z1, z2, lx, rx, cz[4], fz[4], method;
	sectortype *sec, *nsec;
	walltype *wal, *wal2;

	z = maskwall[damaskwallcnt];
	wal = &wall[thewall[z]]; wal2 = &wall[wal->point2];
	sectnum = thesector[z]; sec = &sector[sectnum];
	nsec = &sector[wal->nextsector];
	z1 = max(nsec->ceilingz,sec->ceilingz);
	z2 = min(nsec->floorz,sec->floorz);

	globalpicnum = wal->overpicnum; if ((unsigned long)globalpicnum >= MAXTILES) globalpicnum = 0;
	if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,(short)thewall[z]+16384);
	globalshade = (long)wal->shade;
	globalpal = (long)((unsigned char)wal->pal);
	globalorientation = (long)wal->cstat;

	sx0 = (float)(wal->x-globalposx); sx1 = (float)(wal2->x-globalposx);
	sy0 = (float)(wal->y-globalposy); sy1 = (float)(wal2->y-globalposy);
	yp0 = sx0*gcosang2 + sy0*gsinang2;
	yp1 = sx1*gcosang2 + sy1*gsinang2;
	if ((yp0 < SCISDIST) && (yp1 < SCISDIST)) return;
	xp0 = sy0*gcosang - sx0*gsinang;
	xp1 = sy1*gcosang - sx1*gsinang;

		//Clip to close parallel-screen plane
	oxp0 = xp0; oyp0 = yp0;
	if (yp0 < SCISDIST) { t0 = (SCISDIST-yp0)/(yp1-yp0); xp0 = (xp1-xp0)*t0+xp0; yp0 = SCISDIST; }
						  else t0 = 0.f;
	if (yp1 < SCISDIST) { t1 = (SCISDIST-oyp0)/(yp1-oyp0); xp1 = (xp1-oxp0)*t1+oxp0; yp1 = SCISDIST; }
						else { t1 = 1.f; }

	getzsofslope(sectnum,(wal2->x-wal->x)*t0+wal->x,(wal2->y-wal->y)*t0+wal->y,&cz[0],&fz[0]);
	getzsofslope(wal->nextsector,(wal2->x-wal->x)*t0+wal->x,(wal2->y-wal->y)*t0+wal->y,&cz[1],&fz[1]);
	getzsofslope(sectnum,(wal2->x-wal->x)*t1+wal->x,(wal2->y-wal->y)*t1+wal->y,&cz[2],&fz[2]);
	getzsofslope(wal->nextsector,(wal2->x-wal->x)*t1+wal->x,(wal2->y-wal->y)*t1+wal->y,&cz[3],&fz[3]);

	ryp0 = 1.f/yp0; ryp1 = 1.f/yp1;

		//Generate screen coordinates for front side of wall
	x0 = ghalfx*xp0*ryp0 + ghalfx;
	x1 = ghalfx*xp1*ryp1 + ghalfx;
	if (x1 <= x0) return;

	ryp0 *= gyxscale; ryp1 *= gyxscale;

	gdx = (ryp0-ryp1)*gxyaspect / (x0-x1);
	gdy = 0;
	gdo = ryp0*gxyaspect - gdx*x0;

		//gux*x0 + guo = t0*wal->xrepeat*8*yp0
		//gux*x1 + guo = t1*wal->xrepeat*8*yp1
	gux = (t0*ryp0 - t1*ryp1)*gxyaspect*(float)wal->xrepeat*8.f / (x0-x1);
	guo = t0*ryp0*gxyaspect*(float)wal->xrepeat*8.f - gux*x0;
	guo += (float)wal->xpanning*gdo;
	gux += (float)wal->xpanning*gdx;
	guy = 0;

	if (!(wal->cstat&4)) i = z1; else i = z2;
	i -= globalposz;
	t0 = ((float)i)*ryp0 + ghoriz;
	t1 = ((float)i)*ryp1 + ghoriz;
	t = ((gdx*x0 + gdo) * (float)wal->yrepeat) / ((x1-x0) * ryp0 * 2048.f);
	i = (1<<(picsiz[globalpicnum]>>4)); if (i < tilesizy[globalpicnum]) i <<= 1;
	fy = (float)wal->ypanning * ((float)i) / 256.0;
	gvx = (t0-t1)*t;
	gvy = (x1-x0)*t;
	gvo = -gvx*x0 - gvy*t0 + fy*gdo; gvx += fy*gdx; gvy += fy*gdy;

	if (wal->cstat&8) //xflip
	{
		t = (float)(wal->xrepeat*8 + wal->xpanning*2);
		gux = gdx*t - gux;
		guy = gdy*t - guy;
		guo = gdo*t - guo;
	}
	if (wal->cstat&256) { gvx = -gvx; gvy = -gvy; gvo = -gvo; } //yflip

	method = 1; pow2xsplit = 1;
	if (wal->cstat&128) { if (!(wal->cstat&512)) method = 2; else method = 3; }

#ifdef USE_OPENGL
	if (!nofog) {
	if (rendmode == 3)
		bglFogf(GL_FOG_DENSITY,((float)globalvisibility)*((float)((unsigned char)(sec->visibility+16)))*FOGSCALE);
	}
#endif

	for(i=0;i<2;i++)
	{
		csy[i] = ((float)(cz[i]-globalposz))*ryp0 + ghoriz;
		fsy[i] = ((float)(fz[i]-globalposz))*ryp0 + ghoriz;
		csy[i+2] = ((float)(cz[i+2]-globalposz))*ryp1 + ghoriz;
		fsy[i+2] = ((float)(fz[i+2]-globalposz))*ryp1 + ghoriz;
	}

		//Clip 2 quadrilaterals
		//               /csy3
		//             /   |
		// csy0------/----csy2
		//   |     /xxxxxxx|
		//   |   /xxxxxxxxx|
		// csy1/xxxxxxxxxxx|
		//   |xxxxxxxxxxx/fsy3
		//   |xxxxxxxxx/   |
		//   |xxxxxxx/     |
		// fsy0----/------fsy2
		//   |   /
		// fsy1/

	dpx[0] = x0; dpy[0] = csy[1];
	dpx[1] = x1; dpy[1] = csy[3];
	dpx[2] = x1; dpy[2] = fsy[3];
	dpx[3] = x0; dpy[3] = fsy[1];
	n = 4;

		//Clip to (x0,csy[0])-(x1,csy[2])
	n2 = 0; t1 = -((dpx[0]-x0)*(csy[2]-csy[0]) - (dpy[0]-csy[0])*(x1-x0));
	for(i=0;i<n;i++)
	{
		j = i+1; if (j >= n) j = 0;

		t0 = t1; t1 = -((dpx[j]-x0)*(csy[2]-csy[0]) - (dpy[j]-csy[0])*(x1-x0));
		if (t0 >= 0) { dpx2[n2] = dpx[i]; dpy2[n2] = dpy[i]; n2++; }
		if ((t0 >= 0) != (t1 >= 0))
		{
			r = t0/(t0-t1);
			dpx2[n2] = (dpx[j]-dpx[i])*r + dpx[i];
			dpy2[n2] = (dpy[j]-dpy[i])*r + dpy[i];
			n2++;
		}
	}
	if (n2 < 3) return;

		//Clip to (x1,fsy[2])-(x0,fsy[0])
	n = 0; t1 = -((dpx2[0]-x1)*(fsy[0]-fsy[2]) - (dpy2[0]-fsy[2])*(x0-x1));
	for(i=0;i<n2;i++)
	{
		j = i+1; if (j >= n2) j = 0;

		t0 = t1; t1 = -((dpx2[j]-x1)*(fsy[0]-fsy[2]) - (dpy2[j]-fsy[2])*(x0-x1));
		if (t0 >= 0) { dpx[n] = dpx2[i]; dpy[n] = dpy2[i]; n++; }
		if ((t0 >= 0) != (t1 >= 0))
		{
			r = t0/(t0-t1);
			dpx[n] = (dpx2[j]-dpx2[i])*r + dpx2[i];
			dpy[n] = (dpy2[j]-dpy2[i])*r + dpy2[i];
			n++;
		}
	}
	if (n < 3) return;

	drawpoly(dpx,dpy,n,method);
}

void polymost_drawsprite (long snum)
{
	double px[6], py[6];
	float f, r, c, s, fx, fy, sx0, sy0, sx1, sy1, xp0, yp0, xp1, yp1, oxp0, oyp0, ryp0, ryp1, ft[4];
	float x0, y0, x1, y1, sc0, sf0, sc1, sf1, px2[6], py2[6], xv, yv, t0, t1;
	long i, j, spritenum, xoff, yoff, method, npoints;
	spritetype *tspr;

	tspr = tspriteptr[snum];
	globalpicnum      = tspr->picnum;
	globalshade       = tspr->shade;
	globalpal         = tspr->pal;
	globalorientation = tspr->cstat;
	spritenum         = tspr->owner;
	if (picanm[globalpicnum]&192) globalpicnum += animateoffs(globalpicnum,spritenum+32768);

	xoff = (long)((signed char)((picanm[globalpicnum]>>8)&255))+((long)tspr->xoffset);
	yoff = (long)((signed char)((picanm[globalpicnum]>>16)&255))+((long)tspr->yoffset);

	method = 1+4;
	if (tspr->cstat&2) { if (!(tspr->cstat&512)) method = 2+4; else method = 3+4; }

#ifdef USE_OPENGL
	if (!nofog) {
	if (rendmode == 3)
		bglFogf(GL_FOG_DENSITY,((float)globalvisibility)*((float)((unsigned char)(sector[tspr->sectnum].visibility+16)))*FOGSCALE);
	}
#endif

	switch((globalorientation>>4)&3)
	{
		case 0: //Face sprite

#ifdef USE_OPENGL
			if (rendmode == 3 && usemodels && !(spriteext[tspr->owner].flags&SPREXT_NOTMD2))
			{
				if (tiletomodel[tspr->picnum].modelid >= 0 &&
					 tiletomodel[tspr->picnum].framenum >= 0) {
					md2draw(0,tspr);
					return;
				}
			}
#endif
			
				//Project 3D to 2D
			sx0 = (float)(tspr->x-globalposx);
			sy0 = (float)(tspr->y-globalposy);
			xp0 = sy0*gcosang  - sx0*gsinang;
			yp0 = sx0*gcosang2 + sy0*gsinang2;
			if (yp0 <= SCISDIST) return;
			ryp0 = 1/yp0;
			sx0 = ghalfx*xp0*ryp0 + ghalfx;
			sy0 = ((float)(tspr->z-globalposz))*gyxscale*ryp0 + ghoriz;

			f = ryp0*(float)xdimen/160.0;
			fx = ((float)tspr->xrepeat)*f;
			fy = ((float)tspr->yrepeat)*f*((float)yxaspect/65536.0);
			sx0 -= fx*(float)xoff; if (tilesizx[globalpicnum]&1) sx0 += fx*.5;
			sy0 -= fy*(float)yoff;
			fx *= ((float)tilesizx[globalpicnum]);
			fy *= ((float)tilesizy[globalpicnum]);

			px[0] = px[3] = sx0-fx*.5; px[1] = px[2] = sx0+fx*.5;
			if (!(globalorientation&128)) { py[0] = py[1] = sy0-fy; py[2] = py[3] = sy0; }
											 else { py[0] = py[1] = sy0-fy*.5; py[2] = py[3] = sy0+fy*.5; }

			gdx = gdy = guy = gvx = 0; gdo = ryp0*gviewxrange;
			if (!(globalorientation&4))
				  { gux = (float)tilesizx[globalpicnum]*gdo/(px[1]-px[0]+.002); guo = -gux*(px[0]-.001); }
			else { gux = (float)tilesizx[globalpicnum]*gdo/(px[0]-px[1]-.002); guo = -gux*(px[1]+.001); }
			if (!(globalorientation&8))
				  { gvy = (float)tilesizy[globalpicnum]*gdo/(py[3]-py[0]+.002); gvo = -gvy*(py[0]-.001); }
			else { gvy = (float)tilesizy[globalpicnum]*gdo/(py[0]-py[3]-.002); gvo = -gvy*(py[3]+.001); }

			//Clip sprites to ceilings/floors when no parallaxing and not sloped
			if (!(sector[tspr->sectnum].ceilingstat&3))
			{
				sy0 = ((float)(sector[tspr->sectnum].ceilingz-globalposz))*gyxscale*ryp0 + ghoriz;
				if (py[0] < sy0) py[0] = py[1] = sy0;
			}
			if (!(sector[tspr->sectnum].floorstat&3))
			{
				sy0 = ((float)(sector[tspr->sectnum].floorz-globalposz))*gyxscale*ryp0 + ghoriz;
				if (py[2] > sy0) py[2] = py[3] = sy0;
			}

			pow2xsplit = 0; drawpoly(px,py,4,method);
			break;
		case 1: //Wall sprite
#ifdef USE_OPENGL
			if (rendmode == 3 && usemodels && !(spriteext[tspr->owner].flags&SPREXT_NOTMD2))
			{
				if (tiletomodel[tspr->picnum].modelid >= 0 &&
					 tiletomodel[tspr->picnum].framenum >= 0) {
					md2draw(0,tspr);
					return;
				}
			}
#endif
			
				//Project 3D to 2D
			if (globalorientation&4) xoff = -xoff;
			if (globalorientation&8) yoff = -yoff;

			xv = (float)tspr->xrepeat * (float)sintable[(tspr->ang     )&2047] / 65536.0;
			yv = (float)tspr->xrepeat * (float)sintable[(tspr->ang+1536)&2047] / 65536.0;
			f = (float)(tilesizx[globalpicnum]>>1) + (float)xoff;
			x0 = (float)(tspr->x-globalposx) - xv*f; x1 = xv*(float)tilesizx[globalpicnum] + x0;
			y0 = (float)(tspr->y-globalposy) - yv*f; y1 = yv*(float)tilesizx[globalpicnum] + y0;

			yp0 = x0*gcosang2 + y0*gsinang2;
			yp1 = x1*gcosang2 + y1*gsinang2;
			if ((yp0 <= SCISDIST) && (yp1 <= SCISDIST)) return;
			xp0 = y0*gcosang - x0*gsinang;
			xp1 = y1*gcosang - x1*gsinang;

				//Clip to close parallel-screen plane
			oxp0 = xp0; oyp0 = yp0;
			if (yp0 < SCISDIST) { t0 = (SCISDIST-yp0)/(yp1-yp0); xp0 = (xp1-xp0)*t0+xp0; yp0 = SCISDIST; }
								else { t0 = 0.f; }
			if (yp1 < SCISDIST) { t1 = (SCISDIST-oyp0)/(yp1-oyp0); xp1 = (xp1-oxp0)*t1+oxp0; yp1 = SCISDIST; }
								else { t1 = 1.f; }

			f = ((float)tspr->yrepeat) * (float)tilesizy[globalpicnum] * 4;

			ryp0 = 1.0/yp0;
			ryp1 = 1.0/yp1;
			sx0 = ghalfx*xp0*ryp0 + ghalfx;
			sx1 = ghalfx*xp1*ryp1 + ghalfx;
			ryp0 *= gyxscale;
			ryp1 *= gyxscale;

			tspr->z -= ((yoff*tspr->yrepeat)<<2);
			if (globalorientation&128)
			{
				tspr->z += ((tilesizy[globalpicnum]*tspr->yrepeat)<<1);
				if (tilesizy[globalpicnum]&1) tspr->z += (tspr->yrepeat<<1); //Odd yspans
			}

			sc0 = ((float)(tspr->z-globalposz-f))*ryp0 + ghoriz;
			sc1 = ((float)(tspr->z-globalposz-f))*ryp1 + ghoriz;
			sf0 = ((float)(tspr->z-globalposz))*ryp0 + ghoriz;
			sf1 = ((float)(tspr->z-globalposz))*ryp1 + ghoriz;

			gdx = (ryp0-ryp1)*gxyaspect / (sx0-sx1);
			gdy = 0;
			gdo = ryp0*gxyaspect - gdx*sx0;

				//Original equations:
				//(gux*sx0 + guo)/(gdx*sx1 + gdo) = tilesizx[globalpicnum]*t0
				//(gux*sx1 + guo)/(gdx*sx1 + gdo) = tilesizx[globalpicnum]*t1
				//
				// gvx*sx0 + gvy*sc0 + gvo = 0
				// gvy*sx1 + gvy*sc1 + gvo = 0
				//(gvx*sx0 + gvy*sf0 + gvo)/(gdx*sx0 + gdo) = tilesizy[globalpicnum]
				//(gvx*sx1 + gvy*sf1 + gvo)/(gdx*sx1 + gdo) = tilesizy[globalpicnum]

				//gux*sx0 + guo = t0*tilesizx[globalpicnum]*yp0
				//gux*sx1 + guo = t1*tilesizx[globalpicnum]*yp1
			if (globalorientation&4) { t0 = 1.f-t0; t1 = 1.f-t1; }
			gux = (t0*ryp0 - t1*ryp1)*gxyaspect*(float)tilesizx[globalpicnum] / (sx0-sx1);
			guy = 0;
			guo = t0*ryp0*gxyaspect*(float)tilesizx[globalpicnum] - gux*sx0;

				//gvx*sx0 + gvy*sc0 + gvo = 0
				//gvx*sx1 + gvy*sc1 + gvo = 0
				//gvx*sx0 + gvy*sf0 + gvo = tilesizy[globalpicnum]*(gdx*sx0 + gdo)
			f = ((float)tilesizy[globalpicnum])*(gdx*sx0 + gdo) / ((sx0-sx1)*(sc0-sf0));
			if (!(globalorientation&8))
			{
				gvx = (sc0-sc1)*f;
				gvy = (sx1-sx0)*f;
				gvo = -gvx*sx0 - gvy*sc0;
			}
			else
			{
				gvx = (sf1-sf0)*f;
				gvy = (sx0-sx1)*f;
				gvo = -gvx*sx0 - gvy*sf0;
			}

			//Clip sprites to ceilings/floors when no parallaxing
			if (!(sector[tspr->sectnum].ceilingstat&1))
			{
				f = ((float)tspr->yrepeat) * (float)tilesizy[globalpicnum] * 4;
				if (sector[tspr->sectnum].ceilingz > tspr->z-f)
				{
					sc0 = ((float)(sector[tspr->sectnum].ceilingz-globalposz))*ryp0 + ghoriz;
					sc1 = ((float)(sector[tspr->sectnum].ceilingz-globalposz))*ryp1 + ghoriz;
				}
			}
			if (!(sector[tspr->sectnum].floorstat&1))
			{
				if (sector[tspr->sectnum].floorz < tspr->z)
				{
					sf0 = ((float)(sector[tspr->sectnum].floorz-globalposz))*ryp0 + ghoriz;
					sf1 = ((float)(sector[tspr->sectnum].floorz-globalposz))*ryp1 + ghoriz;
				}
			}
			
			if (sx0 > sx1)
			{
				if (globalorientation&64) return; //1-sided sprite
				f = sx0; sx0 = sx1; sx1 = f;
				f = sc0; sc0 = sc1; sc1 = f;
				f = sf0; sf0 = sf1; sf1 = f;
			}

			px[0] = sx0; py[0] = sc0;
			px[1] = sx1; py[1] = sc1;
			px[2] = sx1; py[2] = sf1;
			px[3] = sx0; py[3] = sf0;
			pow2xsplit = 0; drawpoly(px,py,4,method);
			break;
		case 2: //Floor sprite
			/*
#ifdef USE_OPENGL
			if (rendmode == 3 && usemodels && !(spriteext[tspr->owner].flags&SPREXT_NOTMD2))
			{
				if (tiletomodel[tspr->picnum].modelid >= 0 &&
					 tiletomodel[tspr->picnum].framenum >= 0) {
					md2draw(0,tspr);
					return;
				}
			}
#endif
			*/
			
			if ((globalorientation&64) != 0)
				if ((globalposz > tspr->z) == (!(globalorientation&8)))
					return;
			if ((globalorientation&4) > 0) xoff = -xoff;
			if ((globalorientation&8) > 0) yoff = -yoff;

			i = (tspr->ang&2047);
			c = sintable[(i+512)&2047]/65536.0;
			s = sintable[i]/65536.0;
			x0 = ((tilesizx[globalpicnum]>>1)-xoff)*tspr->xrepeat;
			y0 = ((tilesizy[globalpicnum]>>1)-yoff)*tspr->yrepeat;
			x1 = ((tilesizx[globalpicnum]>>1)+xoff)*tspr->xrepeat;
			y1 = ((tilesizy[globalpicnum]>>1)+yoff)*tspr->yrepeat;

				//Project 3D to 2D
			for(j=0;j<4;j++)
			{
				sx0 = (float)(tspr->x-globalposx);
				sy0 = (float)(tspr->y-globalposy);
				if ((j+0)&2) { sy0 -= s*y0; sx0 -= c*y0; } else { sy0 += s*y1; sx0 += c*y1; }
				if ((j+1)&2) { sx0 -= s*x0; sy0 += c*x0; } else { sx0 += s*x1; sy0 -= c*x1; }
				px[j] = sy0*gcosang  - sx0*gsinang;
				py[j] = sx0*gcosang2 + sy0*gsinang2;
			}

			if (tspr->z < globalposz) //if floor sprite is above you, reverse order of points
			{
				f = px[0]; px[0] = px[1]; px[1] = f;
				f = py[0]; py[0] = py[1]; py[1] = f;
				f = px[2]; px[2] = px[3]; px[3] = f;
				f = py[2]; py[2] = py[3]; py[3] = f;
			}

				//Clip to SCISDIST plane
			npoints = 0;
			for(i=0;i<4;i++)
			{
				j = ((i+1)&3);
				if (py[i] >= SCISDIST) { px2[npoints] = px[i]; py2[npoints] = py[i]; npoints++; }
				if ((py[i] >= SCISDIST) != (py[j] >= SCISDIST))
				{
					f = (SCISDIST-py[i])/(py[j]-py[i]);
					px2[npoints] = (px[j]-px[i])*f + px[i];
					py2[npoints] = (py[j]-py[i])*f + py[i]; npoints++;
				}
			}
			if (npoints < 3) return;

				//Project rotated 3D points to screen
			f = ((float)(tspr->z-globalposz))*gyxscale;
			for(j=0;j<npoints;j++)
			{
				ryp0 = 1/py2[j];
				px[j] = ghalfx*px2[j]*ryp0 + ghalfx;
				py[j] = f*ryp0 + ghoriz;
			}

				//gd? Copied from floor rendering code
			gdx = 0;
			gdy = gxyaspect / (double)(tspr->z-globalposz);
			gdo = -ghoriz*gdy;
				//copied&modified from relative alignment
			xv = (float)tspr->x + s*x1 + c*y1; fx = (double)-(x0+x1)*s;
			yv = (float)tspr->y + s*y1 - c*x1; fy = (double)+(x0+x1)*c;
			f = 1.0/sqrt(fx*fx+fy*fy); fx *= f; fy *= f;
			ft[2] = singlobalang*fy + cosglobalang*fx;
			ft[3] = singlobalang*fx - cosglobalang*fy;
			ft[0] = ((double)(globalposy-yv))*fy + ((double)(globalposx-xv))*fx;
			ft[1] = ((double)(globalposx-xv))*fy - ((double)(globalposy-yv))*fx;
			gux = (double)ft[3]*((double)viewingrange)/(-65536.0*262144.0);
			gvx = (double)ft[2]*((double)viewingrange)/(-65536.0*262144.0);
			guy = (double)ft[0]*gdy; gvy = (double)ft[1]*gdy;
			guo = (double)ft[0]*gdo; gvo = (double)ft[1]*gdo;
			guo += (double)(ft[2]/262144.0-gux)*ghalfx;
			gvo -= (double)(ft[3]/262144.0+gvx)*ghalfx;
			f = 4.0/(float)tspr->xrepeat; gux *= f; guy *= f; guo *= f;
			f =-4.0/(float)tspr->yrepeat; gvx *= f; gvy *= f; gvo *= f;
			if (globalorientation&4)
			{
				gux = ((float)tilesizx[globalpicnum])*gdx - gux;
				guy = ((float)tilesizx[globalpicnum])*gdy - guy;
				guo = ((float)tilesizx[globalpicnum])*gdo - guo;
			}

			pow2xsplit = 0; drawpoly(px,py,npoints,method);
			break;

		//case 3: //Voxel sprite
		//   break;
	}
}

	//sx,sy       center of sprite; screen coods*65536
	//z           zoom*65536. > is zoomed in
	//a           angle (0 is default)
	//dastat&1    1:translucence
	//dastat&2    1:auto-scale mode (use 320*200 coordinates)
	//dastat&4    1:y-flip
	//dastat&8    1:don't clip to startumost/startdmost
	//dastat&16   1:force point passed to be top-left corner, 0:Editart center
	//dastat&32   1:reverse translucence
	//dastat&64   1:non-masked, 0:masked
	//dastat&128  1:draw all pages (permanent)
	//cx1,...     clip window (actual screen coords)
void polymost_dorotatesprite (long sx, long sy, long z, short a, short picnum,
	signed char dashade, char dapalnum, char dastat, long cx1, long cy1, long cx2, long cy2)
{
	long i, n, nn, x, zz, xoff, yoff, xsiz, ysiz, method;
	long ogpicnum, ogshade, ogpal, ofoffset, oxdimen, oydimen;
	double ogchang, ogshang, ogctang, ogstang, oghalfx, oghoriz, fx, fy, x1, y1, x2, y2;
	double ogrhalfxdown10, ogrhalfxdown10x;
	double d, cosang, sinang, cosang2, sinang2, px[8], py[8], px2[8], py2[8];

	ogpicnum = globalpicnum; globalpicnum = picnum;
	ogshade  = globalshade;  globalshade  = dashade;
	ogpal    = globalpal;    globalpal    = (long)((unsigned char)dapalnum);
	oghalfx  = ghalfx;       ghalfx       = (double)(xdim>>1);
	ogrhalfxdown10 = grhalfxdown10;    grhalfxdown10 = 1.0/(((double)ghalfx)*1024);
	ogrhalfxdown10x = grhalfxdown10x;  grhalfxdown10x = grhalfxdown10;
	oghoriz  = ghoriz;       ghoriz       = (double)(ydim>>1);
	ofoffset = frameoffset;  frameoffset  = frameplace;
	oxdimen  = xdimen;       xdimen       = xdim;
	oydimen  = ydimen;       ydimen       = ydim;
	ogchang = gchang; gchang = 1.0;
	ogshang = gshang; gshang = 0.0;
	ogctang = gctang; gctang = 1.0;
	ogstang = gstang; gstang = 0.0;

#ifdef USE_OPENGL
	if (rendmode == 3)
	{
		resizeglcheck();
		if (1) //dastat&(2+8))
		{
			float m[4][4];
			bglViewport(0,0,xdim,ydim); glox1 = -1; //Force fullscreen (glox1=-1 forces it to restore)
			bglMatrixMode(GL_PROJECTION);
			memset(m,0,sizeof(m));
			m[0][0] = m[2][3] = 1.0; m[1][1] = ((float)xdim)/((float)ydim); m[2][2] = 1.0001; m[3][2] = 1-m[2][2];
			bglLoadMatrixf(&m[0][0]);
			bglMatrixMode(GL_MODELVIEW);
			bglLoadIdentity();
		}
		bglDisable(GL_DEPTH_TEST);
	}
#endif

	method = 0;
	if (!(dastat&64))
	{
		 method = 1;
		 if (dastat&1) { if (!(dastat&32)) method = 2; else method = 3; }
	}
	method |= 4; //Use OpenGL clamping - dorotatesprite never repeats 

	xsiz = tilesizx[globalpicnum]; ysiz = tilesizy[globalpicnum];
	if (dastat&16) { xoff = 0; yoff = 0; }
	else
	{
		xoff = (long)((signed char)((picanm[globalpicnum]>>8)&255))+(xsiz>>1);
		yoff = (long)((signed char)((picanm[globalpicnum]>>16)&255))+(ysiz>>1);
	}
	if (dastat&4) yoff = ysiz-yoff;

	if (dastat&2)  //Auto window size scaling
	{
		if (!(dastat&8))
		{
			x = xdimenscale;   //= scale(xdimen,yxaspect,320);
			sx = ((cx1+cx2+2)<<15)+scale(sx-(320<<15),oxdimen,320);
			sy = ((cy1+cy2+2)<<15)+mulscale16(sy-(200<<15),x);
		}
		else
		{
				//If not clipping to startmosts, & auto-scaling on, as a
				//hard-coded bonus, scale to full screen instead
			x = scale(xdim,yxaspect,320);
			sx = (xdim<<15)+32768+scale(sx-(320<<15),xdim,320);
			sy = (ydim<<15)+32768+mulscale16(sy-(200<<15),x);
		}
		z = mulscale16(z,x);
	}

	d = (double)z/(65536.0*16384.0);
	cosang2 = cosang = (double)sintable[(a+512)&2047]*d;
	sinang2 = sinang = (double)sintable[a&2047]*d;
	if ((dastat&2) || (!(dastat&8))) //Don't aspect unscaled perms
		{ d = (double)xyaspect/65536.0; cosang2 *= d; sinang2 *= d; }
	px[0] = (double)sx/65536.0 - (double)xoff*cosang2+ (double)yoff*sinang2;
	py[0] = (double)sy/65536.0 - (double)xoff*sinang - (double)yoff*cosang;
	px[1] = px[0] + (double)xsiz*cosang2;
	py[1] = py[0] + (double)xsiz*sinang;
	px[3] = px[0] - (double)ysiz*sinang2;
	py[3] = py[0] + (double)ysiz*cosang;
	px[2] = px[1]+px[3]-px[0];
	py[2] = py[1]+py[3]-py[0];
	n = 4;

	gdx = 0; gdy = 0; gdo = 1.0;
		//px[0]*gux + py[0]*guy + guo = 0
		//px[1]*gux + py[1]*guy + guo = xsiz-.0001
		//px[3]*gux + py[3]*guy + guo = 0
	d = 1.0/(px[0]*(py[1]-py[3]) + px[1]*(py[3]-py[0]) + px[3]*(py[0]-py[1]));
	gux = (py[3]-py[0])*((double)xsiz-.0001)*d;
	guy = (px[0]-px[3])*((double)xsiz-.0001)*d;
	guo = 0 - px[0]*gux - py[0]*guy;

	if (!(dastat&4))
	{     //px[0]*gvx + py[0]*gvy + gvo = 0
			//px[1]*gvx + py[1]*gvy + gvo = 0
			//px[3]*gvx + py[3]*gvy + gvo = ysiz-.0001
		gvx = (py[0]-py[1])*((double)ysiz-.0001)*d;
		gvy = (px[1]-px[0])*((double)ysiz-.0001)*d;
		gvo = 0 - px[0]*gvx - py[0]*gvy;
	}
	else
	{     //px[0]*gvx + py[0]*gvy + gvo = ysiz-.0001
			//px[1]*gvx + py[1]*gvy + gvo = ysiz-.0001
			//px[3]*gvx + py[3]*gvy + gvo = 0
		gvx = (py[1]-py[0])*((double)ysiz-.0001)*d;
		gvy = (px[0]-px[1])*((double)ysiz-.0001)*d;
		gvo = (double)ysiz-.0001 - px[0]*gvx - py[0]*gvy;
	}

	cx2++; cy2++;
		//Clippoly4 (converted from long to double)
	nn = z = 0;
	do
	{
		zz = z+1; if (zz == n) zz = 0;
		x1 = px[z]; x2 = px[zz]-x1; if ((cx1 <= x1) && (x1 <= cx2)) { px2[nn] = x1; py2[nn] = py[z]; nn++; }
		if (x2 <= 0) fx = cx2; else fx = cx1;  d = fx-x1;
		if ((d < x2) != (d < 0)) { px2[nn] = fx; py2[nn] = (py[zz]-py[z])*d/x2 + py[z]; nn++; }
		if (x2 <= 0) fx = cx1; else fx = cx2;  d = fx-x1;
		if ((d < x2) != (d < 0)) { px2[nn] = fx; py2[nn] = (py[zz]-py[z])*d/x2 + py[z]; nn++; }
		z = zz;
	} while (z);
	if (nn >= 3)
	{
		n = z = 0;
		do
		{
			zz = z+1; if (zz == nn) zz = 0;
			y1 = py2[z]; y2 = py2[zz]-y1; if ((cy1 <= y1) && (y1 <= cy2)) { py[n] = y1; px[n] = px2[z]; n++; }
			if (y2 <= 0) fy = cy2; else fy = cy1;  d = fy-y1;
			if ((d < y2) != (d < 0)) { py[n] = fy; px[n] = (px2[zz]-px2[z])*d/y2 + px2[z]; n++; }
			if (y2 <= 0) fy = cy1; else fy = cy2;  d = fy-y1;
			if ((d < y2) != (d < 0)) { py[n] = fy; px[n] = (px2[zz]-px2[z])*d/y2 + px2[z]; n++; }
			z = zz;
		} while (z);
		pow2xsplit = 0; drawpoly(px,py,n,method);
	}

	globalpicnum = ogpicnum;
	globalshade  = ogshade;
	globalpal    = ogpal;
	ghalfx       = oghalfx;
	grhalfxdown10 = ogrhalfxdown10;
	grhalfxdown10x = ogrhalfxdown10x;
	ghoriz       = oghoriz;
	frameoffset  = ofoffset;
	xdimen       = oxdimen;
	ydimen       = oydimen;
	gchang = ogchang;
	gshang = ogshang;
	gctang = ogctang;
	gstang = ogstang;
}

long polymost_drawtilescreen (long tilex, long tiley, long wallnum, long dimen)
{
#ifdef USE_OPENGL
	float xdime, ydime, xdimepad, ydimepad, scx, scy;
	long i;
	pthtyp *pth;

	if ((rendmode != 3) || (qsetmode != 200)) return(-1);

	i = (1<<(picsiz[wallnum]&15)); if (i < tilesizx[wallnum]) i += i; xdimepad = (float)i;
	i = (1<<(picsiz[wallnum]>>4)); if (i < tilesizy[wallnum]) i += i; ydimepad = (float)i;
	xdime = (float)tilesizx[wallnum]; xdimepad = xdime/xdimepad;
	ydime = (float)tilesizy[wallnum]; ydimepad = ydime/ydimepad;

	if ((xdime <= dimen) && (ydime <= dimen))
	{
		scx = xdime;
		scy = ydime;
	}
	else
	{
		scx = (float)dimen;
		scy = (float)dimen;
		if (xdime < ydime) scx *= xdime/ydime; else scy *= ydime/xdime;
	}

	pth = gltexcache(wallnum,0,4);
	bglBindTexture(GL_TEXTURE_2D,pth ? pth->glpic : 0);

	bglDisable(GL_TEXTURE_2D);
	bglBegin(GL_TRIANGLE_FAN);
	bglColor4f((float)curpalette[255].r/255.0,(float)curpalette[255].g/255.0,(float)curpalette[255].b/255.0,1);
	bglVertex2f((float)tilex    ,(float)tiley    );
	bglVertex2f((float)tilex+scx,(float)tiley    );
	bglVertex2f((float)tilex+scx,(float)tiley+scy);
	bglVertex2f((float)tilex    ,(float)tiley+scy);
	bglEnd();

	bglColor4f(1,1,1,1);
	bglEnable(GL_TEXTURE_2D);
	bglEnable(GL_BLEND);
	bglBegin(GL_TRIANGLE_FAN);
	bglTexCoord2f(       0,       0); bglVertex2f((float)tilex    ,(float)tiley    );
	bglTexCoord2f(xdimepad,       0); bglVertex2f((float)tilex+scx,(float)tiley    );
	bglTexCoord2f(xdimepad,ydimepad); bglVertex2f((float)tilex+scx,(float)tiley+scy);
	bglTexCoord2f(       0,ydimepad); bglVertex2f((float)tilex    ,(float)tiley+scy);
	bglEnd();
	
	return(0);
#else
	return -1;
#endif
}

// Console commands by JBF
#ifdef USE_OPENGL
static int gltexturemode(const osdfuncparm_t *parm)
{
	int m;
	const char *p;

	if (parm->numparms != 1) {
		OSD_Printf("Current texturing mode is %s\n", glfiltermodes[gltexfiltermode].name);
		OSD_Printf("  Vaild modes are:\n");
		for (m = 0; m < (int)numglfiltermodes; m++)
			OSD_Printf("     %d - %s\n",m,glfiltermodes[m].name);

		return OSDCMD_OK;
	}

	m = Bstrtoul(parm->parms[0], (char **)&p, 10);
	if (p == parm->parms[0]) {
		// string
		for (m = 0; m < (int)numglfiltermodes; m++) {
			if (!Bstrcasecmp(parm->parms[0], glfiltermodes[m].name)) break;
		}
		if (m == numglfiltermodes) m = gltexfiltermode;   // no change
	} else {
		if (m < 0) m = 0;
		else if (m >= (int)numglfiltermodes) m = numglfiltermodes - 1;
	}

	if (m != gltexfiltermode) {
		gltexfiltermode = m;
		gltexapplyprops();
	}

	OSD_Printf("Texture filtering mode changed to %s\n", glfiltermodes[gltexfiltermode].name );

	return OSDCMD_OK;
}

static int gltextureanisotropy(const osdfuncparm_t *parm)
{
	long l;
	const char *p;

	if (parm->numparms != 1) {
		OSD_Printf("Current texture anisotropy is %d\n", glanisotropy);
		OSD_Printf("  Maximum is %d\n", (long)glinfo.maxanisotropy);

		return OSDCMD_OK;
	}

	l = Bstrtoul(parm->parms[0], (char **)&p, 10);
	if (l < 0 || l > (long)glinfo.maxanisotropy) l = 0;

	if (l != gltexfiltermode) {
		glanisotropy = l;
		gltexapplyprops();
	}

	OSD_Printf("Texture anisotropy changed to %d\n", glanisotropy );

	return OSDCMD_OK;
}
#endif

void polymost_initosdfuncs(void)
{
#ifdef USE_OPENGL
	OSD_RegisterVariable("glusetexcompr", OSDVAR_INTEGER, &glusetexcompr, 0, osd_internal_validate_boolean);
	OSD_RegisterVariable("glredbluemode", OSDVAR_INTEGER, &glredbluemode, 1, osd_internal_validate_boolean);
	OSD_RegisterFunction("gltexturemode", 0, "gltexturemode: changes the texture filtering settings", gltexturemode);
	OSD_RegisterFunction("gltextureanisotropy", 0, "gltextureanisotropy: changes the texture anisotropy setting", gltextureanisotropy);
	OSD_RegisterVariable("gltexturemaxsize", OSDVAR_INTEGER, &gltexmaxsize, 1, osd_internal_validate_integer);
#endif
	OSD_RegisterVariable("usemodels", OSDVAR_INTEGER, &usemodels, 0, osd_internal_validate_boolean);
}

void polymost_precache(long dapicnum, long dapalnum, long datype)
{
#ifdef USE_OPENGL
	// dapicnum and dapalnum are like you'd expect
	// datype is 0 for a wall/floor/ceiling and 1 for a sprite
	//    basically this just means walls are repeating
	//    while sprites are clamped

	if (rendmode < 3) return;
	
	if (!palookup[dapalnum]) dapalnum = 0;

	//OSD_Printf("precached %d (%d=%d) type %d\n", dapicnum, dapalnum, theglobalpal, datype);
	gltexcache(dapicnum, dapalnum, (datype & 1) << 2);
#endif
}

// vim:ts=4:sw=4:tw=0: