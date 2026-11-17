/*
 * vis.c - fullscreen audio visualizers.
 *
 * Seven styles, all rendered on the CPU at 240x240:
 *   SPECTRUM  - big mirrored bar spectrum
 *   RADIAL    - circular spectrum / pulsing rings
 *   SCOPE     - phosphor oscilloscope with persistence
 *   WATERFALL - scrolling spectrogram
 *   PLASMA    - low-res psychedelic plasma (MilkDrop-ish)
 *   TUNNEL    - rotating audio-reactive tunnel
 *   STARS     - warp starfield with trails
 *
 * Heavy effects render into a 120x120 (or 60x60) indexed buffer and are
 * scaled up, which keeps the CPU cost low while looking deliberately
 * retro on the 240x240 LCD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "gfx.h"

#define VW 120
#define VH 120
#define PW 60
#define PH 60

static const char *style_names[VIS_STYLE_COUNT] = {
	"SPECTRUM", "RADIAL", "SCOPE", "WATERFALL", "PLASMA", "TUNNEL",
	"STARS", "PATTERN"
};

typedef struct {
	float x, y, z;
} Star;

static int style = 0;
static unsigned int anim_ms = 0;
static AudioSnap snap;
static float bass = 0.0f, mid = 0.0f, treb = 0.0f;

static unsigned char vbuf[VW * VH];
static unsigned char pbuf[PW * PH];
static unsigned char tun_a[VW * VH];
static unsigned char tun_d[VW * VH];
static unsigned char tun_dark[VW * VH];
static int tun_ready = 0;

static Star stars[96];
static int stars_ready = 0;
static int wf_started = 0;

static void fx_reset(void);

static int sin8[256], cos8[256];
static int tables_ready = 0;

static Uint32 vpal[256];
static int pal_style = -1;
static int pal_theme = -1;

static char last_path[MAX_PATH];

#define VP_CH 9
#define VP_ROWS 1024
static char vpat[VP_ROWS][VP_CH][4];
static int vpat_pat = -2;
static int vpat_ch0 = -1;
static int vpat_rows = 0;
static int vpat_page = 0;
static int vpat_cur_pat = -2;
static float vpat_pos = 0.0f;

const char *vis_style_name(int i)
{
	if (i < 0 || i >= VIS_STYLE_COUNT)
		return "?";
	return style_names[i];
}

/* Per-style knob, adjusted with the D-pad right in the visualizer. */
static int vp_count(int s)
{
	switch (s) {
	case 0: return 4;	/* SPECTRUM colour */
	case 1: return 4;	/* RADIAL colour */
	case 2: return 3;	/* SCOPE gain */
	case 3: return 4;	/* WATERFALL flow */
	case 4: return 3;	/* PLASMA speed */
	case 5: return 3;	/* TUNNEL speed */
	case 6: return 4;	/* STARS speed */
	}
	return 0;
}

static int vp(int s)
{
	int n;

	if (s < 0 || s >= VIS_STYLE_COUNT)
		return 0;
	n = vp_count(s);
	if (n <= 1)
		return 0;
	return cfg.vis_p[s] % n;
}

static const char *color_value(int v)
{
	switch (v) {
	case 1: return "WARM";
	case 2: return "COOL";
	case 3: return "MONO";
	}
	return "TEAL";
}

static const char *speed_value(int v, int warp)
{
	if (warp) {
		switch (v) {
		case 1: return "MID";
		case 2: return "FAST";
		case 3: return "WARP";
		}
		return "SLOW";
	}
	return v == 0 ? "SLOW" : (v == 1 ? "MID" : "FAST");
}

static const char *vp_name(int s)
{
	switch (s) {
	case 0:
	case 1: return "COLOUR";
	case 2: return "GAIN";
	case 3: return "FLOW";
	case 4:
	case 5:
	case 6: return "SPEED";
	}
	return "";
}

static const char *vp_value(int s, int v)
{
	switch (s) {
	case 0:
	case 1:
		return color_value(v);
	case 2:
		return v == 0 ? "LOW" : (v == 1 ? "MID" : "HIGH");
	case 3:
		switch (v) {
		case 1: return "UP";
		case 2: return "LEFT";
		case 3: return "DOWN";
		}
		return "RIGHT";
	case 6:
		return speed_value(v, 1);
	}
	return speed_value(v, 0);
}

/* Second knob, adjusted with the D-pad left in the visualizer. */
static int vp2_count(int s)
{
	switch (s) {
	case 0: return 6;	/* SPECTRUM extra effect */
	case 1: return 3;	/* RADIAL spin */
	case 2: return 4;	/* SCOPE mode */
	case 3: return 3;	/* WATERFALL speed */
	case 4: return 4;	/* PLASMA mode */
	case 5: return 4;	/* TUNNEL mode */
	case 6: return 3;	/* STARS mode */
	}
	return 0;
}

static int vp2(int s)
{
	int n;

	if (s < 0 || s >= VIS_STYLE_COUNT)
		return 0;
	n = vp2_count(s);
	if (n <= 1)
		return 0;
	return cfg.vis_p2[s] % n;
}

static const char *vp2_name(int s)
{
	switch (s) {
	case 0: return "FX";
	case 1:
	case 3: return "SPEED";
	case 2:
	case 4:
	case 5:
	case 6: return "MODE";
	}
	return "";
}

static const char *vp2_value(int s, int v)
{
	static const char *fx[] = { "OFF", "SAKURA", "RAIN", "SPARK",
				    "FIRE", "PULSE" };
	static const char *scope_modes[] = { "VECTOR", "XY", "DOTS",
					     "MIRROR" };
	static const char *plasma_modes[] = { "CLASSIC", "VORTEX", "MARBLE",
					      "CAUSTIC" };
	static const char *tunnel_modes[] = { "CLASSIC", "RINGS", "ZIGZAG",
					      "PLASMA" };
	static const char *star_modes[] = { "TRAILS", "DOTS", "PLANETS" };

	switch (s) {
	case 0: return fx[v % 6];
	case 1:
	case 3: return speed_value(v, 0);
	case 2: return scope_modes[v % 4];
	case 4: return plasma_modes[v % 4];
	case 5: return tunnel_modes[v % 4];
	case 6: return star_modes[v % 3];
	}
	return "";
}

/* ------------------------------------------------------------------ util --- */

static void vis_init_tables(void)
{
	int i;

	if (tables_ready)
		return;
	for (i = 0; i < 256; i++) {
		sin8[i] = (int)(sin((double)i * 2.0 * 3.14159265358979 / 256.0) * 256.0);
		cos8[i] = (int)(cos((double)i * 2.0 * 3.14159265358979 / 256.0) * 256.0);
	}
	tables_ready = 1;
}

static void vis_init_tunnel(void)
{
	int x, y, i;

	if (tun_ready)
		return;
	vis_init_tables();
	for (y = 0; y < VH; y++) {
		for (x = 0; x < VW; x++) {
			double dx = (double)x - 59.5;
			double dy = (double)y - 59.5;
			double r = sqrt(dx * dx + dy * dy);
			double a = atan2(dy, dx);
			int ang = (int)(a * 40.7436654) & 255;
			int dis = (int)(2600.0 / (r + 4.0)) & 255;

			i = y * VW + x;
			tun_a[i] = (unsigned char)ang;
			tun_d[i] = (unsigned char)dis;
			tun_dark[i] = r < 9.0 ? 1 : 0;
		}
	}
	tun_ready = 1;
}

static void stars_init(void)
{
	int i;

	for (i = 0; i < (int)(sizeof(stars) / sizeof(stars[0])); i++) {
		stars[i].x = ((float)(rand() % 2000) / 1000.0f) - 1.0f;
		stars[i].y = ((float)(rand() % 2000) / 1000.0f) - 1.0f;
		stars[i].z = 0.05f + (float)(rand() % 1000) / 1000.0f;
	}
	stars_ready = 1;
}

static void vis_reset_buffers(void)
{
	memset(vbuf, 0, sizeof(vbuf));
	memset(pbuf, 0, sizeof(pbuf));
	stars_init();
	wf_started = 0;
	anim_ms = 0;
	fx_reset();
	vpat_pat = -2;
	vpat_ch0 = -1;
	vpat_cur_pat = -2;
	vpat_page = 0;
	vpat_pos = 0.0f;
}

static void ensure_palette(void)
{
	const ThemeDef *t = &themes[cfg.theme];
	uint32_t stops[7];
	int n = 0;
	int cyclic = 0;
	int i;

	if (pal_style == style && pal_theme == cfg.theme)
		return;

	switch (style) {
	case 2:	/* SCOPE */
		stops[0] = t->bg;
		stops[1] = t->accent_dim;
		stops[2] = t->accent;
		stops[3] = t->text;
		n = 4;
		break;
	case 3:	/* WATERFALL */
		stops[0] = t->bg;
		stops[1] = t->accent_dim;
		stops[2] = t->accent;
		stops[3] = t->instr;
		stops[4] = t->fx;
		stops[5] = t->bright;
		n = 6;
		break;
	case 4:	/* PLASMA */
		stops[0] = t->accent_dim;
		stops[1] = t->accent;
		stops[2] = t->vol;
		stops[3] = t->fx;
		stops[4] = t->instr;
		stops[5] = t->accent;
		n = 6;
		cyclic = 1;
		break;
	case 5:	/* TUNNEL */
		stops[0] = t->bg;
		stops[1] = t->accent_dim;
		stops[2] = t->accent;
		stops[3] = t->vol;
		stops[4] = t->bright;
		stops[5] = t->accent_dim;
		n = 6;
		cyclic = 1;
		break;
	case 6:	/* STARS */
		stops[0] = t->bg;
		stops[1] = t->accent_dim;
		stops[2] = t->accent;
		stops[3] = t->text;
		n = 4;
		break;
	default:
		return;
	}

	for (i = 0; i < 256; i++) {
		float f;
		int s, nx;
		float fr;
		uint32_t a, b, c;

		if (cyclic) {
			f = (float)i / 255.0f * (float)n;
			s = (int)f % n;
			nx = (s + 1) % n;
			fr = f - floorf(f);
		} else {
			f = (float)i / 255.0f * (float)(n - 1);
			s = (int)f;
			if (s >= n - 1) {
				s = n - 2;
				fr = 1.0f;
			} else {
				fr = f - (float)s;
			}
			nx = s + 1;
		}
		a = stops[s];
		b = stops[nx];
		c = 0;
		c |= (uint32_t)(((a >> 16) & 255) +
				((int)((b >> 16) & 255) -
				 (int)((a >> 16) & 255)) * fr) << 16;
		c |= (uint32_t)(((a >> 8) & 255) +
				((int)((b >> 8) & 255) -
				 (int)((a >> 8) & 255)) * fr) << 8;
		c |= (uint32_t)((a & 255) +
				((int)(b & 255) - (int)(a & 255)) * fr);
		vpal[i] = SDL_MapRGB(gfx_screen->format,
				     (Uint8)((c >> 16) & 255),
				     (Uint8)((c >> 8) & 255),
				     (Uint8)(c & 255));
	}
	pal_style = style;
	pal_theme = cfg.theme;
}

static void bplot(int x, int y, int v)
{
	if (x < 0 || y < 0 || x >= VW || y >= VH)
		return;
	vbuf[y * VW + x] = (unsigned char)(v > 255 ? 255 : v);
}

static void bline(int x0, int y0, int x1, int y1, int v)
{
	int dx = x1 - x0, dy = y1 - y0;
	int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
	int err, e2;

	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	err = dx - dy;
	for (;;) {
		bplot(x0, y0, v);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = err * 2;
		if (e2 > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static void text_sh(int x, int y, const char *s, Uint32 c)
{
	gfx_text(x + 1, y + 1, s, gfx_black());
	gfx_text(x, y, s, c);
}

static void text2_sh(int x, int y, const char *s, Uint32 c)
{
	gfx_text2(x + 1, y + 1, s, gfx_black());
	gfx_text2(x, y, s, c);
}

static void text_right_sh(int right, int y, const char *s, Uint32 c)
{
	gfx_text(right - gfx_text_w(s) + 1, y + 1, s, gfx_black());
	gfx_text_right(right, y, s, c);
}

static Uint32 fmt_color(const char *f)
{
	if (!strcmp(f, "XM"))
		return pal.note;
	if (!strcmp(f, "IT"))
		return pal.instr;
	if (!strcmp(f, "MOD"))
		return pal.vol;
	if (!strcmp(f, "S3M"))
		return pal.fx;
	if (!strcmp(f, "MO3"))
		return pal.accent;
	return pal.dim;
}

/* --------------------------------------------------------------- styles --- */

/* --------------------------------------------------- spectrum extras --- */

enum { SPX_OFF = 0, SPX_SAKURA, SPX_RAIN, SPX_SPARK, SPX_FIRE, SPX_PULSE };

#define FX_PET 20
#define FX_DROP 10
#define FX_RIP 8
#define FX_MOTE 40
#define FX_FW 64
#define FX_PULSE 4

typedef struct {
	float x, y, vy, ph;
	unsigned char on;
} FxParticle;

static FxParticle petals[FX_PET];
static FxParticle motes[FX_MOTE];
static struct {
	float x, y, ty, vy;
	unsigned char on;
} drops[FX_DROP];
static struct {
	int x, y;
	unsigned int t;
	unsigned char on;
} rips[FX_RIP];
static struct {
	float x, y, vx, vy;
	unsigned int born;
	unsigned char on;
} fw[FX_FW];
static struct {
	unsigned int t;
	unsigned char on;
} pulses[FX_PULSE];

static float fx_last_bass = 0.0f;
static unsigned int fx_pulse_cool = 0;
static unsigned int fx_spawn = 0;
static int fx_seeded = 0;
static Uint32 fx_petal_c = 0, fx_petal2_c = 0;
static Uint32 fx_rain_c = 0, fx_mote_c = 0;
static Uint32 fx_fw_c = 0, fx_pulse_c = 0;

static void fx_colors(void)
{
	if (fx_petal_c)
		return;
	fx_petal_c = SDL_MapRGB(gfx_screen->format, 255, 128, 170);
	fx_petal2_c = SDL_MapRGB(gfx_screen->format, 255, 190, 210);
	fx_rain_c = SDL_MapRGB(gfx_screen->format, 130, 200, 255);
	fx_mote_c = SDL_MapRGB(gfx_screen->format, 225, 235, 255);
	fx_fw_c = SDL_MapRGB(gfx_screen->format, 255, 220, 140);
	fx_pulse_c = SDL_MapRGB(gfx_screen->format, 120, 235, 255);
}

static void fx_spawn_petal(FxParticle *p)
{
	p->on = 1;
	p->x = (float)(rand() % SW);
	p->y = -4.0f;
	p->vy = 0.020f + (float)(rand() % 30) / 1000.0f;
	p->ph = (float)(rand() % 628) / 100.0f;
}

static void fx_spawn_mote(FxParticle *p)
{
	p->on = 1;
	p->x = (float)(rand() % SW);
	p->y = (float)(60 + rand() % 140);
	p->vy = -(0.004f + (float)(rand() % 10) / 2000.0f);
	p->ph = (float)(rand() % 628) / 100.0f;
}

static void fx_spawn_drop(void)
{
	int i;

	for (i = 0; i < FX_DROP; i++) {
		if (drops[i].on)
			continue;
		drops[i].on = 1;
		drops[i].x = (float)(8 + rand() % (SW - 16));
		drops[i].y = -8.0f;
		drops[i].vy = 0.42f + (float)(rand() % 30) / 100.0f;
		drops[i].ty = (float)(150 + rand() % 60);
		return;
	}
}

static void fx_burst(void)
{
	int i, n = 0;
	int bx = 26 + rand() % (SW - 52);
	int by = 40 + rand() % 90;
	float base = (float)(rand() % 628) / 100.0f;

	for (i = 0; i < FX_FW && n < 22; i++) {
		float ang, sp;

		if (fw[i].on)
			continue;
		ang = base + (float)n * 0.349f;
		sp = 0.050f + (float)(rand() % 55) / 1000.0f;
		fw[i].on = 1;
		fw[i].x = (float)bx;
		fw[i].y = (float)by;
		fw[i].vx = cosf(ang) * sp;
		fw[i].vy = sinf(ang) * sp;
		fw[i].born = anim_ms;
		n++;
	}
}

static void fx_pulse_spawn(void)
{
	int i;

	for (i = 0; i < FX_PULSE; i++) {
		if (pulses[i].on)
			continue;
		pulses[i].on = 1;
		pulses[i].t = anim_ms;
		return;
	}
}

static void fx_reset(void)
{
	memset(petals, 0, sizeof(petals));
	memset(motes, 0, sizeof(motes));
	memset(drops, 0, sizeof(drops));
	memset(rips, 0, sizeof(rips));
	memset(fw, 0, sizeof(fw));
	memset(pulses, 0, sizeof(pulses));
	fx_last_bass = 0.0f;
	fx_pulse_cool = 0;
	fx_spawn = 0;
	fx_seeded = 0;
}

static void fx_seed(void)
{
	int i;
	int fx = vp2(0);

	if (fx == SPX_SAKURA) {
		for (i = 0; i < 8; i++) {
			fx_spawn_petal(&petals[i]);
			petals[i].y = (float)(rand() % SH);
		}
	} else if (fx == SPX_RAIN) {
		for (i = 0; i < 4; i++) {
			fx_spawn_drop();
			drops[i].y = (float)(rand() % (int)drops[i].ty);
		}
	} else if (fx == SPX_SPARK) {
		for (i = 0; i < 14; i++)
			fx_spawn_mote(&motes[i]);
	} else if (fx == SPX_FIRE) {
		fx_burst();
		fx_burst();
	} else if (fx == SPX_PULSE) {
		fx_pulse_spawn();
	}
	fx_seeded = 1;
}

static void fx_update(unsigned int dt)
{
	int i;
	int fx = vp2(0);

	if (!dt)
		return;
	if (fx == SPX_OFF) {
		if (petals[0].on || motes[0].on || drops[0].on ||
		    fw[0].on || pulses[0].on)
			fx_reset();
		return;
	}
	if (!fx_seeded)
		fx_seed();
	fx_spawn += dt;

	if (fx == SPX_SAKURA) {
		if (fx_spawn >= 320) {
			fx_spawn = 0;
			for (i = 0; i < FX_PET; i++)
				if (!petals[i].on) {
					fx_spawn_petal(&petals[i]);
					break;
				}
		}
		for (i = 0; i < FX_PET; i++) {
			FxParticle *p = &petals[i];

			if (!p->on)
				continue;
			p->y += p->vy * (float)dt;
			p->ph += 0.0022f * (float)dt;
			p->x += sinf(p->ph * 2.0f) * 0.020f * (float)dt;
			if (p->y > SH + 4 || p->x < -6 || p->x > SW + 6)
				p->on = 0;
		}
	} else if (fx == SPX_RAIN) {
		if (fx_spawn >= 110) {
			fx_spawn = 0;
			fx_spawn_drop();
		}
		for (i = 0; i < FX_DROP; i++) {
			int j;

			if (!drops[i].on)
				continue;
			drops[i].y += drops[i].vy * (float)dt;
			if (drops[i].y < drops[i].ty)
				continue;
			drops[i].on = 0;
			for (j = 0; j < FX_RIP; j++) {
				if (rips[j].on)
					continue;
				rips[j].on = 1;
				rips[j].x = (int)drops[i].x;
				rips[j].y = (int)drops[i].ty;
				rips[j].t = anim_ms;
				break;
			}
		}
		for (i = 0; i < FX_RIP; i++)
			if (rips[i].on && anim_ms - rips[i].t > 800)
				rips[i].on = 0;
	} else if (fx == SPX_SPARK) {
		if (fx_spawn >= 90) {
			fx_spawn = 0;
			for (i = 0; i < FX_MOTE; i++)
				if (!motes[i].on) {
					fx_spawn_mote(&motes[i]);
					break;
				}
		}
		for (i = 0; i < FX_MOTE; i++) {
			FxParticle *p = &motes[i];

			if (!p->on)
				continue;
			p->y += p->vy * (float)dt;
			p->ph += 0.004f * (float)dt;
			p->x += sinf(p->ph) * 0.008f * (float)dt;
			if (p->y < -4)
				p->on = 0;
		}
	} else if (fx == SPX_FIRE) {
		if (fx_spawn >= 750) {
			fx_spawn = 0;
			fx_burst();
		}
		for (i = 0; i < FX_FW; i++) {
			unsigned int age;

			if (!fw[i].on)
				continue;
			age = anim_ms - fw[i].born;
			if (age > 1300) {
				fw[i].on = 0;
				continue;
			}
			fw[i].vy += 0.00012f * (float)dt;
			fw[i].x += fw[i].vx * (float)dt;
			fw[i].y += fw[i].vy * (float)dt;
		}
	} else {	/* SPX_PULSE */
		if (bass > 0.18f && bass > fx_last_bass + 0.015f &&
		    anim_ms - fx_pulse_cool > 240) {
			fx_pulse_spawn();
			fx_pulse_cool = anim_ms;
		}
		fx_last_bass = bass;
		for (i = 0; i < FX_PULSE; i++)
			if (pulses[i].on && anim_ms - pulses[i].t > 900)
				pulses[i].on = 0;
	}
}

static void circle_blend(int cx, int cy, int r, Uint32 c, int a)
{
	int i;

	if (r <= 0 || r > 200)
		return;
	for (i = 0; i < 128; i++) {
		int ang = i * 2;
		int x = cx + sin8[ang & 255] * r / 256;
		int y = cy - cos8[ang & 255] * r / 256;

		gfx_fill_blend(x, y, 1, 1, c, a);
	}
}

static void fx_draw(void)
{
	int i;
	int fx = vp2(0);

	if (fx == SPX_OFF)
		return;
	fx_colors();

	if (fx == SPX_SAKURA) {
		for (i = 0; i < FX_PET; i++) {
			FxParticle *p = &petals[i];
			int x, y;

			if (!p->on)
				continue;
			x = (int)p->x;
			y = (int)p->y;
			gfx_fill(x, y, 2, 1, fx_petal_c);
			gfx_px(x + (sinf(p->ph) > 0 ? 2 : -1), y + 1,
				fx_petal2_c);
			gfx_px(x + 1, y + 1, fx_petal_c);
		}
	} else if (fx == SPX_RAIN) {
		for (i = 0; i < FX_DROP; i++) {
			if (!drops[i].on)
				continue;
			gfx_fill((int)drops[i].x, (int)drops[i].y, 1, 6,
				 fx_rain_c);
		}
		for (i = 0; i < FX_RIP; i++) {
			unsigned int age;
			int w, a;

			if (!rips[i].on)
				continue;
			age = anim_ms - rips[i].t;
			if (age > 800)
				continue;
			w = 4 + (int)age / 24;
			a = 190 - (int)age * 190 / 800;
			gfx_fill_blend(rips[i].x - w / 2, rips[i].y, w, 1,
				       fx_rain_c, a);
			gfx_fill_blend(rips[i].x - w / 3, rips[i].y + 2,
				       w * 2 / 3, 1, fx_rain_c, a / 2);
		}
	} else if (fx == SPX_SPARK) {
		for (i = 0; i < FX_MOTE; i++) {
			FxParticle *p = &motes[i];
			int x, y, a;

			if (!p->on)
				continue;
			x = (int)p->x;
			y = (int)p->y;
			a = 110 + (int)((sinf(p->ph) * 0.5f + 0.5f) * 145.0f);
			gfx_fill_blend(x, y, 2, 2, fx_mote_c, a);
			gfx_fill_blend(x - 1, y, 1, 1, fx_mote_c, a / 4);
			gfx_fill_blend(x + 2, y, 1, 1, fx_mote_c, a / 4);
		}
	} else if (fx == SPX_FIRE) {
		for (i = 0; i < FX_FW; i++) {
			unsigned int age;
			int a;

			if (!fw[i].on)
				continue;
			age = anim_ms - fw[i].born;
			if (age > 1300)
				continue;
			a = 255 - (int)(age * 200 / 1300);
			gfx_fill_blend((int)fw[i].x, (int)fw[i].y, 3, 3,
				       fx_fw_c, a);
			gfx_fill_blend((int)(fw[i].x - fw[i].vx * 30.0f),
				       (int)(fw[i].y - fw[i].vy * 30.0f),
				       2, 2, fx_fw_c, a / 3);
			gfx_fill_blend((int)(fw[i].x - fw[i].vx * 55.0f),
				       (int)(fw[i].y - fw[i].vy * 55.0f),
				       1, 1, fx_fw_c, a / 6);
		}
	} else {	/* SPX_PULSE */
		for (i = 0; i < FX_PULSE; i++) {
			unsigned int age;
			int r, a;

			if (!pulses[i].on)
				continue;
			age = anim_ms - pulses[i].t;
			if (age > 900)
				continue;
			r = 8 + (int)age / 8;
			a = 210 - (int)age * 210 / 900;
			circle_blend(120, 126, r, fx_pulse_c, a);
			circle_blend(120, 126, r * 2 / 3, fx_pulse_c, a / 2);
		}
	}
}

static void draw_spectrum_full(void)
{
	int i;
	int cy = 126;
	int up = cy - 4;
	int dn = SH - cy - 4;
	Uint32 ca = pal.accent, cb = pal.accent_dim;
	Uint32 ct = pal.text, cp = pal.instr;

	switch (vp(0)) {
	case 1:	/* warm */
		ca = pal.fx;
		cb = pal.vol;
		cp = pal.note;
		break;
	case 2:	/* cool */
		ca = pal.instr;
		cb = pal.note;
		ct = pal.bright;
		cp = pal.accent;
		break;
	case 3:	/* mono */
		ca = pal.bright;
		cb = pal.dim;
		cp = pal.text;
		break;
	}

	for (i = 0; i < 6; i++)
		gfx_fill(0, i * 40, SW, 40,
			 (i == 2 || i == 3) ? pal.panel : pal.bg);
	gfx_hline(0, cy, SW, pal.line);

	for (i = 0; i < 40; i++) {
		int band = i * SPECTRUM_BARS / 40;
		float v = spectrum_bar(band);
		float p = spectrum_peak(band);
		int x = i * 6 + 1;
		int hu = (int)(v * (float)up);
		int hd = (int)(v * (float)dn * 0.72f);
		int wd;

		if (hu > up)
			hu = up;
		if (hd > dn)
			hd = dn;
		if (hd < 0)
			hd = 0;
		/* the reflection narrows as it goes away from the centre */
		wd = 5 - (hd * 3) / (dn > 0 ? dn : 1);
		if (wd < 2)
			wd = 2;

		if (hu > 0) {
			gfx_fill(x, cy - hu, 5, hu, ca);
			gfx_hline(x, cy - hu, 5, ct);
		}
		if (hd > 0) {
			int xo = x + (5 - wd) / 2;

			gfx_fill(xo, cy + 1, wd, hd, cb);
		}
		if (p > 0.02f) {
			int ph = (int)(p * (float)up);

			gfx_hline(x, cy - ph - 2, 5, cp);
		}
	}

	/* The mirrored half fades towards the bottom, like a reflection. */
	{
		int y0 = cy + 1;
		int total = SH - y0;
		int b, bands = 10;

		if (total > 0) {
			for (b = 0; b < bands; b++) {
				int ya = y0 + total * b / bands;
				int yb = y0 + total * (b + 1) / bands;
				int a = 36 + (b * 190) / (bands - 1);

				gfx_fill_blend(0, ya, SW, yb - ya, pal.bg, a);
			}
		}
	}

	if (bass > 0.55f) {
		gfx_hline(0, 0, SW, cb);
		gfx_hline(0, SH - 1, SW, cb);
	}

	fx_draw();
}

static void circle_outline(int cx, int cy, int r, Uint32 c)
{
	int i;

	if (r <= 0)
		return;
	for (i = 0; i < 128; i++) {
		int a = i * 2;
		int x = cx + sin8[a & 255] * r / 256;
		int y = cy - cos8[a & 255] * r / 256;
		gfx_px(x, y, c);
	}
}

static void draw_radial(void)
{
	int i, r;
	int cx = 120, cy = 116;
	int rot = (int)((anim_ms / (46 - vp2(1) * 14)) & 255);
	Uint32 vinyl = SDL_MapRGB(gfx_screen->format, 14, 15, 18);
	Uint32 groove = SDL_MapRGB(gfx_screen->format, 33, 35, 41);
	Uint32 groove2 = SDL_MapRGB(gfx_screen->format, 23, 25, 30);
	Uint32 col, col_dim;

	switch (vp(1)) {
	case 1:	/* warm */
		col = pal.fx;
		col_dim = pal.vol;
		break;
	case 2:	/* cool */
		col = pal.instr;
		col_dim = pal.note;
		break;
	case 3:	/* mono */
		col = pal.bright;
		col_dim = pal.dim;
		break;
	default:
		col = pal.accent;
		col_dim = pal.accent_dim;
		break;
	}

	/* turntable deck */
	gfx_circle(cx, cy, 118, pal.panel2);

	/* vinyl disc and grooves */
	gfx_circle(cx, cy, 114, vinyl);
	for (r = 108; r > 40; r -= 4)
		circle_outline(cx, cy, r, ((r / 4) & 1) ? groove : groove2);

	/* light sweep across the disc */
	for (i = 0; i < 48; i++) {
		int a = (i * 2 + 40) & 255;
		int x0 = cx + sin8[a] * 40 / 256;
		int y0 = cy - cos8[a] * 40 / 256;
		int x1 = cx + sin8[a] * 112 / 256;
		int y1 = cy - cos8[a] * 112 / 256;

		gfx_line(x0, y0, x1, y1, groove2);
	}

	/* spectrum drawn as grooves around the label */
	for (i = 0; i < 64; i++) {
		int a = (i * 4 + rot) & 255;
		int band = i * SPECTRUM_BARS / 64;
		float v = spectrum_bar(band);
		float p = spectrum_peak(band);
		int r0 = 38;
		int len = 6 + (int)(v * 58.0f);
		int x0 = cx + sin8[a] * r0 / 256;
		int y0 = cy - cos8[a] * r0 / 256;
		int x1 = cx + sin8[a] * (r0 + len) / 256;
		int y1 = cy - cos8[a] * (r0 + len) / 256;
		Uint32 c = v > 0.82f ? pal.bright : col;

		gfx_line(x0, y0, x1, y1, c);
		gfx_line(x0 + 1, y0, x1 + 1, y1, c);
		if (p > 0.04f) {
			int pr = r0 + (int)(p * 58.0f) + 3;
			int px = cx + sin8[a] * pr / 256;
			int py = cy - cos8[a] * pr / 256;

			gfx_fill(px, py, 2, 2, col_dim);
		}
	}

	/* record label: battery percentage */
	gfx_circle(cx, cy, 34, vinyl);
	circle_outline(cx, cy, 34, col);
	circle_outline(cx, cy, 30, col_dim);
	{
		char bat[16];
		int pct = util_battery();
		Uint32 pc = col;

		if (pct < 0)
			util_copy(bat, sizeof(bat), "N/A");
		else
			snprintf(bat, sizeof(bat), "%d%%", pct);
		if (pct >= 0 && pct <= 15)
			pc = pal.warn;
		else if (util_usb_present())
			pc = pal.ok;
		gfx_text(cx - gfx_text_w(bat) / 2, cy - 8, bat, pc);
		gfx_text(cx - gfx_text_w("BAT") / 2, cy + 2, "BAT", col_dim);
	}

	/* tonearm */
	{
		int px = 216, py = 24;
		int tx = cx + 74, ty = cy - 58;

		gfx_fill(px - 6, py - 6, 13, 13, pal.panel2);
		circle_outline(px, py, 6, pal.dim);
		gfx_fill(px - 2, py - 2, 5, 5, col);
		gfx_line(px, py, tx, ty, pal.dim);
		gfx_line(px + 1, py, tx + 1, ty, pal.dim);
		gfx_fill(tx - 4, ty - 4, 9, 9, col);
		gfx_fill(tx - 2, ty - 2, 5, 5, vinyl);
	}
}

static void scope_update(void)
{
	int i, x;
	int amp = 26 + vp(2) * 16 + (int)(bass * 26.0f);
	int drift = (int)((anim_ms / 4) & 511);
	int mode = vp2(2);

	for (i = 0; i < VW * VH; i++)
		vbuf[i] = (unsigned char)((vbuf[i] * 7) >> 3);
	if (mode != 1) {
		for (x = 0; x < VW; x++)
			if (vbuf[60 * VW + x] < 20)
				vbuf[60 * VW + x] = 20;
	}

	if (mode == 1) {
		/* XY: signal against a delayed copy -> Lissajous figure. */
		int lx = 0, ly = 0;

		for (i = 0; i < 256; i++) {
			int a = (i * 2) & 511;
			int sx = snap.scope[a];
			int sy = snap.scope[(a + 64) & 511];
			int px = 60 + (sx * 50) / 32768;
			int py = 60 + (sy * 50) / 32768;

			if (i)
				bline(lx, ly, px, py, 210);
			lx = px;
			ly = py;
		}
		return;
	}
	if (mode == 2) {
		/* DOTS: sparse glowing dots with persistence. */
		for (x = 0; x < VW; x += 4) {
			int idx = (x * 512 / VW + drift) & 511;
			int s = snap.scope[idx];
			int y = 60 - (s * amp) / 32768;

			bplot(x, y, 255);
			bplot(x + 1, y, 200);
			bplot(x, y - 1, 90);
			bplot(x, y + 1, 90);
		}
		return;
	}
	if (mode == 3) {
		/* MIRROR: waveform plus a dim reflection below. */
		for (x = 0; x < VW; x++) {
			int idx = (x * 512 / VW + drift) & 511;
			int s = snap.scope[idx];
			int y = 60 - (s * amp) / 32768;
			int m = 60 + (60 - y);

			bplot(x, y, 255);
			bplot(x, y - 1, 120);
			bplot(x, m, 150);
		}
		return;
	}

	/* VECTOR: classic phosphor trace. */
	for (x = 0; x < VW; x++) {
		int idx = (x * 512 / VW + drift) & 511;
		int s = snap.scope[idx];
		int y = 60 - (s * amp) / 32768;

		bplot(x, y, 255);
		bplot(x, y - 1, 170);
		bplot(x, y + 1, 170);
		bplot(x, y - 2, 70);
		bplot(x, y + 2, 70);
	}
}

static unsigned char waterfall_level(float v)
{
	int idx;

	if (v <= 0.015f)
		return 0;
	idx = 32 + (int)(v * 223.0f);
	if (idx > 255)
		idx = 255;
	return (unsigned char)idx;
}

static void waterfall_column(int x)
{
	int y;

	for (y = 0; y < VH; y++) {
		int band = ((VH - 1 - y) * SPECTRUM_BARS) / VH;

		vbuf[y * VW + x] = waterfall_level(spectrum_bar(band));
	}
}

static void waterfall_row(int y)
{
	int x;

	for (x = 0; x < VW; x++) {
		int band = (x * SPECTRUM_BARS) / VW;

		vbuf[y * VW + x] = waterfall_level(spectrum_bar(band));
	}
}

static void waterfall_update(void)
{
	int x, y;
	int steps = 1 + vp2(3);
	int dir = vp(3);

	if (dir == 1 || dir == 3) {
		/* Vertical: UP = newest on top, DOWN = newest at the bottom. */
		if (!wf_started) {
			for (y = 0; y < VH; y++)
				waterfall_row(y);
			wf_started = 1;
			return;
		}
		if (dir == 1) {
			memmove(vbuf + steps * VW, vbuf,
				(size_t)(VH - steps) * VW);
			for (y = 0; y < steps; y++)
				waterfall_row(y);
		} else {
			memmove(vbuf, vbuf + steps * VW,
				(size_t)(VH - steps) * VW);
			for (y = VH - steps; y < VH; y++)
				waterfall_row(y);
		}
		return;
	}

	/* Horizontal: RIGHT = newest on the right, LEFT = newest on the left. */
	if (!wf_started) {
		for (x = 0; x < VW; x++)
			waterfall_column(x);
		wf_started = 1;
		return;
	}
	if (dir == 0) {
		for (y = 0; y < VH; y++)
			memmove(vbuf + y * VW, vbuf + y * VW + steps,
				VW - steps);
		for (x = VW - steps; x < VW; x++)
			waterfall_column(x);
	} else {
		for (y = 0; y < VH; y++)
			memmove(vbuf + y * VW + steps, vbuf + y * VW,
				VW - steps);
		for (x = 0; x < steps; x++)
			waterfall_column(x);
	}
}

static unsigned char pla_ang[PW * PH];
static unsigned char pla_rad[PW * PH];
static int pla_ready = 0;

static void plasma_init_table(void)
{
	int x, y;

	if (pla_ready)
		return;
	vis_init_tables();
	for (y = 0; y < PH; y++) {
		for (x = 0; x < PW; x++) {
			double dx = (double)x - 29.5;
			double dy = (double)y - 29.5;
			double r = sqrt(dx * dx + dy * dy);
			double a = atan2(dy, dx);

			pla_ang[y * PW + x] =
				(unsigned char)((int)(a * 40.7436654) & 255);
			pla_rad[y * PW + x] =
				(unsigned char)((int)(r * 6.0) & 255);
		}
	}
	pla_ready = 1;
}

static void plasma_update(void)
{
	int x, y;
	int t = (int)(anim_ms / (24 - vp(4) * 8));
	int shift = (int)(bass * 60.0f + mid * 25.0f);
	int mode = vp2(4);

	if (mode == 1)
		plasma_init_table();

	for (y = 0; y < PH; y++) {
		for (x = 0; x < PW; x++) {
			int v;

			if (mode == 1) {
				int i = y * PW + x;
				int u = pla_ang[i];
				int r = pla_rad[i];

				v = sin8[(u + r + t) & 255] +
				    sin8[(r * 2 - t * 2) & 255] +
				    sin8[(u * 2 - t) & 255] + 256;
			} else if (mode == 2) {
				int w = sin8[(y * 3 + t) & 255] >> 5;

				v = sin8[(x * 3 + w + t) & 255] +
				    sin8[(y * 5 - w - t * 2) & 255] +
				    sin8[(x * 2 + y * 3 + t) & 255] + 400;
			} else if (mode == 3) {
				v = sin8[((x * x) / 3 + t) & 255] +
				    sin8[((y * y) / 4 - t * 2) & 255] +
				    sin8[((x * y) / 6 + t) & 255] + 400;
			} else {
				v = sin8[(x * 4 + t) & 255] +
				    sin8[(y * 5 - t * 2) & 255] +
				    sin8[((x + y) * 3 + t) & 255] +
				    sin8[((x * x + y * y) / 8 + t) & 255];
			}
			pbuf[y * PW + x] = (unsigned char)
				(((v >> 2) + 128 + shift) & 255);
		}
	}
}

static void tunnel_update(void)
{
	int i;
	int t = (int)(anim_ms / 8);
	int zoom = t * (1 + vp(5)) + (int)(bass * 60.0f);
	int hue = (int)(treb * 40.0f);
	int mode = vp2(5);

	for (i = 0; i < VW * VH; i++) {
		int u, v, idx;

		if (tun_dark[i]) {
			vbuf[i] = 0;
			continue;
		}
		u = (tun_a[i] + t / 4) & 255;
		v = (tun_d[i] + zoom) & 255;

		if (mode == 1) {
			/* RINGS: only the distance matters. */
			idx = (v * 4 + hue) & 255;
		} else if (mode == 2) {
			/* ZIGZAG: folded chevrons around the wall. */
			int z = (u > 128 ? 255 - u : u) * 2;

			idx = (v * 2 + z + (v >> 4) * 16 + hue) & 255;
		} else if (mode == 3) {
			/* PLASMA: interference of angle and depth. */
			int s = (sin8[u] + sin8[v]) / 2;

			idx = (s + 256 + t * 3 + hue) & 255;
		} else {
			idx = (v * 2 + (sin8[u] >> 1) + hue) & 255;
		}
		vbuf[i] = (unsigned char)idx;
	}
}

static void stars_planets(void)
{
	int p;

	for (p = 0; p < 2; p++) {
		int px = (int)(fmodf(40.0f + (float)p * 70.0f +
				    (float)anim_ms *
				    (0.010f + (float)p * 0.006f),
				    200.0f)) - 40;
		int py = 30 + p * 46 +
			 (int)(sinf((float)anim_ms * 0.0006f +
				    (float)p * 2.0f) * 9.0f);
		int r = (p == 0) ? 10 : 6;
		int a, x;

		for (a = -r; a <= r; a++) {
			int w = (int)(sqrt((double)(r * r - a * a)) + 0.3);
			int yy = py + a;
			unsigned char val;

			if (yy < 0 || yy >= VH)
				continue;
			val = (unsigned char)(225 - (abs(a) * 55) / r);
			for (x = px - w; x <= px + w; x++) {
				if (x < 0 || x >= VW)
					continue;
				vbuf[yy * VW + x] = val;
			}
		}
		if (p == 0) {
			for (a = 0; a < 128; a++) {
				int ang = (a * 2) & 255;
				int rx = px + sin8[ang] * (r * 2) / 256;
				int ry = py + cos8[ang] * (r / 2) / 256;

				bplot(rx, ry, 150);
			}
		}
	}
}

static void stars_update(void)
{
	int i;
	int mode = vp2(6);
	float speed = 0.0008f + (float)vp(6) * 0.0037f + bass * 0.020f;

	if (mode == 0) {
		for (i = 0; i < VW * VH; i++)
			vbuf[i] = (unsigned char)((vbuf[i] * 5) >> 3);
	} else {
		memset(vbuf, 0, sizeof(vbuf));
	}

	for (i = 0; i < (int)(sizeof(stars) / sizeof(stars[0])); i++) {
		Star *st = &stars[i];
		float pz = st->z;
		int x0, y0, x1, y1, b;

		st->z -= speed;
		if (st->z < 0.04f) {
			st->x = ((float)(rand() % 2000) / 1000.0f) - 1.0f;
			st->y = ((float)(rand() % 2000) / 1000.0f) - 1.0f;
			st->z = 1.0f;
			continue;
		}
		x0 = 60 + (int)(st->x / (pz + speed * 5.0f) * 48.0f);
		y0 = 60 + (int)(st->y / (pz + speed * 5.0f) * 48.0f);
		x1 = 60 + (int)(st->x / st->z * 48.0f);
		y1 = 60 + (int)(st->y / st->z * 48.0f);
		b = 140 + (int)((1.0f - st->z) * 115.0f);

		if (mode == 0) {
			bline(x0, y0, x1, y1, b);
		} else {
			bplot(x1, y1, b);
			bplot(x1 + 1, y1, b / 2);
			bplot(x1, y1 + 1, b / 2);
		}
	}
	if (mode == 2)
		stars_planets();
	else if (mode == 0)
		bplot(60, 60, 255);
}

/* ------------------------------------------------------------------ HUD --- */

static void vpat_refresh(void)
{
	int pages = (snap.channels + VP_CH - 1) / VP_CH;
	int ch0, rows, r, c;

	if (pages < 1)
		pages = 1;
	if (vpat_page < 0)
		vpat_page = 0;
	if (vpat_page >= pages)
		vpat_page = pages - 1;
	ch0 = vpat_page * VP_CH;
	if (snap.pattern == vpat_pat && ch0 == vpat_ch0)
		return;
	vpat_pat = snap.pattern;
	vpat_ch0 = ch0;
	rows = audio_pattern_rows(snap.pattern);
	if (rows < 0)
		rows = 0;
	if (rows > VP_ROWS)
		rows = VP_ROWS;
	vpat_rows = rows;
	for (r = 0; r < rows; r++) {
		for (c = 0; c < VP_CH; c++) {
			char buf[32];
			if (audio_format_cell(snap.pattern, r, ch0 + c, buf,
					      sizeof(buf))) {
				vpat[r][c][0] = buf[0];
				vpat[r][c][1] = buf[1];
				vpat[r][c][2] = buf[2];
				vpat[r][c][3] = 0;
			} else {
				util_copy(vpat[r][c], sizeof(vpat[r][c]), "...");
			}
		}
	}
}

static void vpat_update(unsigned int dt_ms)
{
	float d;
	float f;

	vpat_refresh();
	if (snap.pattern != vpat_cur_pat) {
		vpat_cur_pat = snap.pattern;
		vpat_pos = (float)snap.row;
		return;
	}
	d = (float)snap.row - vpat_pos;
	if (d > 8.0f || d < -8.0f) {
		vpat_pos = (float)snap.row;
		return;
	}
	f = 1.0f - expf(-(float)dt_ms / 55.0f);
	if (f < 0.05f)
		f = 0.05f;
	if (f > 0.95f)
		f = 0.95f;
	vpat_pos += d * f;
	if (fabsf(vpat_pos - (float)snap.row) < 0.03f)
		vpat_pos = (float)snap.row;
}

static void draw_title_line(int y)
{
	char title[24];
	const char *fmt;
	int len, moff = 0, n = 0, i;

	len = (int)strlen(player.title);
	if (len > 16)
		moff = (int)((anim_ms / 200) % (unsigned)(len - 16 + 4));
	for (i = moff; i < len && n < 16; i++)
		title[n++] = player.title[i];
	title[n] = 0;
	text2_sh(6, y, title, pal.bright);

	fmt = player.track.fmt[0] ? player.track.fmt : "??";
	{
		int bw = (int)strlen(fmt) * FONT5X7_ADV + 6;
		gfx_badge(234 - bw, y + 1, fmt, fmt_color(fmt), pal.bg);
	}
}

#define VP_TITLE_H 15

static void draw_pattern_vis(void)
{
	int c, i, r;
	float top;
	int first;
	int off = ui_hints_offset();
	int pad = off >= VP_TITLE_H ? 0 : VP_TITLE_H - off;
	int head = pad;		/* channel header row */
	int grid = pad + 11;	/* first pattern row */
	int vis_rows = (SH - grid) / 9;

	vpat_refresh();

	/* The title behaves like in every other style: it slides in with the
	 * HUD and slides away when the hints hide. */
	if (pad > 0) {
		gfx_fill(0, 0, SW, pad, pal.panel2);
		draw_title_line(1 - off);
	}
	gfx_fill(0, head, SW, 11, pal.panel2);
	gfx_hline(0, head + 10, SW, pal.line);
	for (c = 0; c < VP_CH; c++) {
		int ch = vpat_page * VP_CH + c;
		char lab[16];
		int x = 20 + c * 24;
		Uint32 cc = ch < snap.channels ? pal.accent : pal.line2;
		float v = 0.0f;

		snprintf(lab, sizeof(lab), "%02d", ch + 1);
		gfx_text(x + 6, head + 1, lab, cc);
		gfx_fill(x + 2, head + 8, 20, 1, pal.line);
		if (ch < snap.channels && ch < 64) {
			v = snap.vu[ch];
			if (v > 1.0f)
				v = 1.0f;
			if (v > 0.01f)
				gfx_fill(x + 2, head + 8, (int)(20.0f * v),
					 1, v > 0.85f ? pal.fx :
					 (v > 0.6f ? pal.instr : pal.accent));
		}
	}

	gfx_vline(19, grid, SH - grid, pal.line);

	if (vpat_rows <= 0) {
		gfx_text(60, 100, "NO PATTERN DATA", pal.dim);
		return;
	}

	if (vis_rows < 1)
		vis_rows = 1;
	if (vpat_rows <= vis_rows) {
		top = 0.0f;
	} else {
		top = vpat_pos - (float)(vis_rows / 2);
		if (top < 0.0f)
			top = 0.0f;
		if (top > (float)(vpat_rows - vis_rows))
			top = (float)(vpat_rows - vis_rows);
	}
	first = (int)floorf(top) - 1;

	for (i = 0; i < vis_rows + 3; i++) {
		int y;
		r = first + i;
		if (r < 0 || r >= vpat_rows)
			continue;
		y = grid + (int)(((float)r - top) * 9.0f);
		if (y >= SH || y + 9 <= grid)
			continue;
		if (r == snap.row) {
			gfx_fill(0, y, SW, 9, pal.cur_bg);
			gfx_fill(2, y, 3, 9, pal.accent);
		} else if (r % 4 == 3) {
			gfx_hline(1, y + 8, SW - 2, pal.line);
		}
		{
			char rn[16];
			int len, x;
			snprintf(rn, sizeof(rn), "%02X", r);
			len = (int)strlen(rn);
			x = 18 - len * FONT5X7_ADV;
			gfx_text(x, y + 1, rn,
				 r == snap.row ? pal.bright : pal.dim);
		}
		for (c = 0; c < VP_CH; c++) {
			const char *note = vpat[r][c];
			Uint32 col;
			if (note[0] == '.' && note[1] == '.' && note[2] == '.')
				col = pal.dim;
			else if (note[0] == '^' || note[0] == '=')
				col = pal.accent_dim;
			else
				col = pal.note;
			gfx_text(21 + c * 24, y + 1, note, col);
		}
	}
}

static void draw_hud(const AudioSnap *s)
{
	int off = ui_hints_offset();
	char t1[16], t2[16], line[64];
	int y, by;

	if (off >= 20)
		return;

	if (!s->playing) {
		gfx_fill(6, 228, 2, 8, pal.warn);
		gfx_fill(11, 228, 2, 8, pal.warn);
	}

	if (style != 7) {
		y = 4 - off;
		draw_title_line(y);
		if (!s->playing)
			text_sh((SW - gfx_text_w("PAUSED")) / 2, 24 - off,
				"PAUSED", pal.warn);
	}

	by = 206 + off;
	util_time((int)s->pos, t1, sizeof(t1));
	util_time((int)(s->dur > 0 ? s->dur : 0), t2, sizeof(t2));
	text_sh(6, by, t1, pal.text);
	text_right_sh(234, by, t2, pal.dim);

	gfx_fill(48, by + 3, 142, 3, pal.line2);
	if (s->dur > 0.1) {
		int fw = (int)(s->pos / s->dur * 142.0);
		if (fw < 0)
			fw = 0;
		if (fw > 142)
			fw = 142;
		gfx_fill(48, by + 3, fw, 3, pal.accent);
	}
	snprintf(line, sizeof(line), "Y:%s B:BACK",
		 vis_style_name(style));
	text_sh(6, by + 13, line, pal.text);
	if (style == 7) {
		int pages = (snap.channels + VP_CH - 1) / VP_CH;

		if (pages < 1)
			pages = 1;
		snprintf(line, sizeof(line), "CH:%d/%d", vpat_page + 1,
			 pages);
	} else if (vp_count(style) > 1 && vp2_count(style) > 1) {
		snprintf(line, sizeof(line), "L:%s R:%s",
			 vp2_value(style, vp2(style)),
			 vp_value(style, vp(style)));
	} else if (vp_count(style) > 1) {
		snprintf(line, sizeof(line), "%s:%s", vp_name(style),
			 vp_value(style, vp(style)));
	} else {
		util_copy(line, sizeof(line), "");
	}
	if (line[0])
		text_right_sh(234, by + 13, line, pal.accent_dim);
}

/* ----------------------------------------------------------------- API --- */

void vis_enter(void)
{
	vis_init_tables();
	style = cfg.vis_style;
	if (style < 0 || style >= VIS_STYLE_COUNT)
		style = 0;
	vis_reset_buffers();
	util_copy(last_path, sizeof(last_path), player.track.path);
	audio_snapshot(&snap);
	if (snap.playing)
		spectrum_update(snap.scope, 512, audio_rate());
	ui_hints_show();
	screen = SCR_VIS;
}

void vis_update(unsigned int dt_ms)
{
	int i;
	float b = 0.0f, m = 0.0f, t = 0.0f;

	audio_snapshot(&snap);
	if (strcmp(last_path, player.track.path)) {
		vis_reset_buffers();
		util_copy(last_path, sizeof(last_path), player.track.path);
	}
	if (snap.playing) {
		spectrum_update(snap.scope, 512, audio_rate());
		anim_ms += dt_ms;
	}

	for (i = 0; i < 7; i++)
		b += spectrum_bar(i);
	for (i = 7; i < 18; i++)
		m += spectrum_bar(i);
	for (i = 18; i < SPECTRUM_BARS; i++)
		t += spectrum_bar(i);
	bass = bass * 0.70f + (b / 7.0f) * 0.30f;
	mid = mid * 0.70f + (m / 11.0f) * 0.30f;
	treb = treb * 0.70f + (t / 14.0f) * 0.30f;

	if (style == 7) {
		vpat_update(dt_ms);
		return;
	}
	if (!snap.playing)
		return;

	if (style == 0)
		fx_update(dt_ms);

	switch (style) {
	case 2:
		scope_update();
		break;
	case 3:
		waterfall_update();
		break;
	case 4:
		plasma_update();
		break;
	case 5:
		vis_init_tunnel();
		tunnel_update();
		break;
	case 6:
		if (!stars_ready)
			stars_init();
		stars_update();
		break;
	default:
		break;
	}
}

void vis_draw(void)
{
	gfx_clear(pal.bg);
	ensure_palette();

	switch (style) {
	case 0:
		draw_spectrum_full();
		break;
	case 1:
		draw_radial();
		break;
	case 2:
		gfx_blit_indexed(vbuf, VW, VH, 2, vpal);
		break;
	case 3:
		gfx_blit_indexed(vbuf, VW, VH, 2, vpal);
		break;
	case 4:
		gfx_blit_indexed(pbuf, PW, PH, 4, vpal);
		break;
	case 5:
		gfx_blit_indexed(vbuf, VW, VH, 2, vpal);
		break;
	case 6:
		gfx_blit_indexed(vbuf, VW, VH, 2, vpal);
		break;
	case 7:
		draw_pattern_vis();
		break;
	}

	draw_hud(&snap);
}

static void vis_param_step(int dir)
{
	int n = vp_count(style);
	int v;
	char msg[48];

	if (n <= 1)
		return;
	v = cfg.vis_p[style] % n;
	v = (v + (dir > 0 ? 1 : n - 1)) % n;
	cfg.vis_p[style] = v;
	config_save();
	if (style == 3)
		wf_started = 0;
	snprintf(msg, sizeof(msg), "%s: %s", vp_name(style),
		 vp_value(style, v));
	app_toast(msg, 1200);
	ui_hints_show();
}

static void vis_param2_step(int dir)
{
	int n = vp2_count(style);
	int v;
	char msg[48];

	if (n <= 1)
		return;
	v = cfg.vis_p2[style] % n;
	v = (v + (dir > 0 ? 1 : n - 1)) % n;
	cfg.vis_p2[style] = v;
	config_save();
	if (style == 3)
		wf_started = 0;
	if (style == 0)
		fx_reset();
	pal_style = -1;		/* force the palette to rebuild */
	snprintf(msg, sizeof(msg), "%s: %s", vp2_name(style),
		 vp2_value(style, v));
	app_toast(msg, 1200);
	ui_hints_show();
}

void vis_key(SDLKey k)
{
	switch (k) {
	case SDLK_y:
		style = (style + 1) % VIS_STYLE_COUNT;
		cfg.vis_style = style;
		config_save();
		vis_reset_buffers();
		app_toast(vis_style_name(style), 1000);
		break;
	case SDLK_b:
		screen = SCR_PLAYER;
		break;
	case SDLK_a:
		player_toggle();
		break;
	case SDLK_m:
		player_next(-1);
		if (player.loaded)
			screen = SCR_VIS;
		break;
	case SDLK_n:
		player_next(1);
		if (player.loaded)
			screen = SCR_VIS;
		break;
	case SDLK_l:
		if (style == 7) {
			if (vpat_page > 0) {
				vpat_page--;
				vpat_pat = -2;
			}
		} else {
			vis_param2_step(-1);
		}
		break;
	case SDLK_r:
		if (style == 7) {
			int pages = (snap.channels + VP_CH - 1) / VP_CH;

			if (vpat_page + 1 < pages) {
				vpat_page++;
				vpat_pat = -2;
			}
		} else {
			vis_param_step(1);
		}
		break;
	case SDLK_u:
		audio_seek_order(-1);
		break;
	case SDLK_d:
		audio_seek_order(1);
		break;
	case SDLK_x:
		player.info_tab = 0;
		player.info_scroll = 0;
		player.info_back = SCR_VIS;
		screen = SCR_INFO;
		break;
	case SDLK_s:
		screen = SCR_SETTINGS;
		break;
	default:
		break;
	}
}
