/*
 * gfx.c - direct software drawing into the SDL 1.2 framebuffer.
 *
 * The FunKey screen is 240x240x32.  We keep the pixel loops simple and
 * assume the common 32 bpp path; 16 bpp is supported as a fallback.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL/SDL.h>

#include "app.h"
#include "gfx.h"
#include "font5x7.h"

SDL_Surface *gfx_screen = NULL;
Pal pal;

static Uint8 *fb = NULL;
static int fb_pitch = 0;
static int fb_bpp = 0;
static Uint32 fb_rmask, fb_gmask, fb_bmask, fb_amask;
static int fb_rshift, fb_gshift, fb_bshift;
static Uint32 black_px;

static Uint32 map_rgb(int r, int g, int b)
{
	return SDL_MapRGB(gfx_screen->format, (Uint8)r, (Uint8)g, (Uint8)b);
}

static Uint32 rgb(Uint32 v)
{
	return map_rgb((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

int gfx_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return -1;
	}
	gfx_screen = SDL_SetVideoMode(SW, SH, 32,
				      SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
	if (!gfx_screen) {
		fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
		SDL_Quit();
		return -1;
	}
	SDL_ShowCursor(0);
	SDL_EnableKeyRepeat(0, 0);
	SDL_WM_SetCaption("Chiptracker", NULL);

	fb = (Uint8 *)gfx_screen->pixels;
	fb_pitch = gfx_screen->pitch;
	fb_bpp = gfx_screen->format->BytesPerPixel;
	fb_rmask = gfx_screen->format->Rmask;
	fb_gmask = gfx_screen->format->Gmask;
	fb_bmask = gfx_screen->format->Bmask;
	fb_amask = gfx_screen->format->Amask;
	fb_rshift = gfx_screen->format->Rshift;
	fb_gshift = gfx_screen->format->Gshift;
	fb_bshift = gfx_screen->format->Bshift;
	black_px = map_rgb(0, 0, 0);
	return 0;
}

int gfx_reinit(void)
{
	gfx_screen = SDL_SetVideoMode(SW, SH, 32,
				      SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
	if (!gfx_screen)
		return -1;
	fb = (Uint8 *)gfx_screen->pixels;
	fb_pitch = gfx_screen->pitch;
	fb_bpp = gfx_screen->format->BytesPerPixel;
	fb_rmask = gfx_screen->format->Rmask;
	fb_gmask = gfx_screen->format->Gmask;
	fb_bmask = gfx_screen->format->Bmask;
	fb_amask = gfx_screen->format->Amask;
	fb_rshift = gfx_screen->format->Rshift;
	fb_gshift = gfx_screen->format->Gshift;
	fb_bshift = gfx_screen->format->Bshift;
	black_px = map_rgb(0, 0, 0);
	return 0;
}

void gfx_quit(void)
{
	SDL_Quit();
}

Uint32 gfx_black(void)
{
	return black_px;
}

void gfx_theme(int idx)
{
	const ThemeDef *t;

	if (idx < 0 || idx >= THEME_COUNT)
		idx = 0;
	t = &themes[idx];

	pal.bg = rgb(t->bg);
	pal.panel = rgb(t->panel);
	pal.panel2 = rgb(t->panel2);
	pal.line = rgb(t->line);
	pal.line2 = rgb(t->line2);
	pal.dim = rgb(t->dim);
	pal.text = rgb(t->text);
	pal.bright = rgb(t->bright);
	pal.accent = rgb(t->accent);
	pal.accent_dim = rgb(t->accent_dim);
	pal.note = rgb(t->note);
	pal.instr = rgb(t->instr);
	pal.vol = rgb(t->vol);
	pal.fx = rgb(t->fx);
	/* Physical RG Nano face buttons: these do not change with the shell. */
	pal.btn_a = map_rgb(224, 68, 60);	/* A - red */
	pal.btn_b = map_rgb(232, 194, 74);	/* B - yellow */
	pal.btn_x = map_rgb(76, 125, 255);	/* X - blue */
	pal.btn_y = map_rgb(86, 184, 92);	/* Y - green */
	pal.btn_s = map_rgb(230, 238, 240);	/* Start - white */
	pal.sel_bg = rgb(t->sel_bg);
	pal.cur_bg = rgb(t->cur_bg);
	pal.warn = rgb(t->warn);
	pal.ok = rgb(t->ok);
}

void gfx_px(int x, int y, Uint32 c)
{
	if (x < 0 || y < 0 || x >= SW || y >= SH)
		return;
	if (fb_bpp == 4)
		((Uint32 *)(fb + y * fb_pitch))[x] = c;
	else if (fb_bpp == 2)
		((Uint16 *)(fb + y * fb_pitch))[x] = (Uint16)c;
	else
		fb[y * fb_pitch + x] = (Uint8)c;
}

void gfx_fill(int x, int y, int w, int h, Uint32 c)
{
	int j;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SW) w = SW - x;
	if (y + h > SH) h = SH - y;
	if (w <= 0 || h <= 0)
		return;

	if (fb_bpp == 4) {
		for (j = 0; j < h; j++) {
			Uint32 *row = (Uint32 *)(fb + (y + j) * fb_pitch) + x;
			int i;
			for (i = 0; i < w; i++)
				row[i] = c;
		}
	} else if (fb_bpp == 2) {
		for (j = 0; j < h; j++) {
			Uint16 *row = (Uint16 *)(fb + (y + j) * fb_pitch) + x;
			int i;
			for (i = 0; i < w; i++)
				row[i] = (Uint16)c;
		}
	} else {
		for (j = 0; j < h; j++)
			memset(fb + (y + j) * fb_pitch + x, (int)c, (size_t)w);
	}
}

void gfx_fill_blend(int x, int y, int w, int h, Uint32 c, int alpha)
{
	int j, i;

	if (alpha <= 0)
		return;
	if (alpha >= 255 || fb_bpp != 4) {
		gfx_fill(x, y, w, h, c);
		return;
	}
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SW) w = SW - x;
	if (y + h > SH) h = SH - y;
	if (w <= 0 || h <= 0)
		return;

	{
		Uint32 sr = ((c & fb_rmask) >> fb_rshift) & 255;
		Uint32 sg = ((c & fb_gmask) >> fb_gshift) & 255;
		Uint32 sb = ((c & fb_bmask) >> fb_bshift) & 255;
		Uint32 ia = (Uint32)(255 - alpha);

		for (j = 0; j < h; j++) {
			Uint32 *row = (Uint32 *)(fb + (y + j) * fb_pitch) + x;
			for (i = 0; i < w; i++) {
				Uint32 v = row[i];
				Uint32 dr = ((v & fb_rmask) >> fb_rshift) & 255;
				Uint32 dg = ((v & fb_gmask) >> fb_gshift) & 255;
				Uint32 db = ((v & fb_bmask) >> fb_bshift) & 255;

				dr = (sr * (Uint32)alpha + dr * ia) / 255;
				dg = (sg * (Uint32)alpha + dg * ia) / 255;
				db = (sb * (Uint32)alpha + db * ia) / 255;
				row[i] = (dr << fb_rshift) | (dg << fb_gshift) |
					 (db << fb_bshift) | (v & fb_amask);
			}
		}
	}
}

void gfx_hline(int x, int y, int w, Uint32 c)
{
	gfx_fill(x, y, w, 1, c);
}

void gfx_vline(int x, int y, int h, Uint32 c)
{
	gfx_fill(x, y, 1, h, c);
}

void gfx_rect(int x, int y, int w, int h, Uint32 c)
{
	gfx_hline(x, y, w, c);
	gfx_hline(x, y + h - 1, w, c);
	gfx_vline(x, y, h, c);
	gfx_vline(x + w - 1, y, h, c);
}

void gfx_line(int x0, int y0, int x1, int y1, Uint32 c)
{
	int dx = x1 - x0;
	int dy = y1 - y0;
	int sx = dx < 0 ? -1 : 1;
	int sy = dy < 0 ? -1 : 1;
	int err, e2;

	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	err = dx - dy;
	for (;;) {
		gfx_px(x0, y0, c);
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

void gfx_blit_indexed(const unsigned char *idx, int w, int h, int scale,
		      const Uint32 *pal)
{
	int x, y, sx, sy;

	if (fb_bpp != 4 || scale < 1) {
		for (y = 0; y < h * scale; y++)
			for (x = 0; x < w * scale; x++)
				gfx_px(x, y, pal[idx[(y / scale) * w +
						    (x / scale)]]);
		return;
	}
	for (y = 0; y < h; y++) {
		const unsigned char *src = idx + y * w;
		for (sy = 0; sy < scale; sy++) {
			Uint32 *row = (Uint32 *)(fb + (y * scale + sy) * fb_pitch);
			for (x = 0; x < w; x++) {
				Uint32 c = pal[src[x]];
				int dx = x * scale;
				for (sx = 0; sx < scale; sx++)
					row[dx + sx] = c;
			}
		}
	}
}

void gfx_clear(Uint32 c)
{
	gfx_fill(0, 0, SW, SH, c);
}

void gfx_glyph(int x, int y, int ch, Uint32 c, int scale)
{
	const unsigned char *g;
	int col, row;

	if (ch < 32 || ch > 126)
		ch = '?';
	g = font5x7[ch - 32];

	for (col = 0; col < 5; col++) {
		unsigned char bits = g[col];
		for (row = 0; row < 7; row++) {
			if (bits & (1u << row))
				gfx_fill(x + col * scale, y + row * scale,
					 scale, scale, c);
		}
	}
}

void gfx_text(int x, int y, const char *s, Uint32 c)
{
	for (; *s; s++, x += FONT5X7_ADV)
		gfx_glyph(x, y, (unsigned char)*s, c, 1);
}

void gfx_text2(int x, int y, const char *s, Uint32 c)
{
	for (; *s; s++, x += FONT5X7_ADV * 2)
		gfx_glyph(x, y, (unsigned char)*s, c, 2);
}

int gfx_text_w(const char *s)
{
	return (int)strlen(s) * FONT5X7_ADV;
}

int gfx_text2_w(const char *s)
{
	return (int)strlen(s) * FONT5X7_ADV * 2;
}

void gfx_text_lim(int x, int y, int x_end, const char *s, Uint32 c)
{
	for (; *s && x + FONT5X7_ADV <= x_end; s++, x += FONT5X7_ADV)
		gfx_glyph(x, y, (unsigned char)*s, c, 1);
}

void gfx_text2_lim(int x, int y, int x_end, const char *s, Uint32 c)
{
	for (; *s && x + FONT5X7_ADV * 2 <= x_end; s++, x += FONT5X7_ADV * 2)
		gfx_glyph(x, y, (unsigned char)*s, c, 2);
}

void gfx_text_right(int right, int y, const char *s, Uint32 c)
{
	gfx_text(right - gfx_text_w(s), y, s, c);
}

void gfx_text_ellipsis(int x, int y, int x_end, const char *s, Uint32 c)
{
	int maxc = (x_end - x) / FONT5X7_ADV;
	int n = (int)strlen(s);

	if (maxc <= 0)
		return;
	if (n <= maxc) {
		gfx_text(x, y, s, c);
		return;
	}
	if (maxc <= 2) {
		gfx_text_lim(x, y, x_end, s, c);
		return;
	}
	for (int i = 0; i < maxc - 2; i++)
		gfx_glyph(x + i * FONT5X7_ADV, y, (unsigned char)s[i], c, 1);
	gfx_text(x + (maxc - 2) * FONT5X7_ADV, y, "..", c);
}

void gfx_badge(int x, int y, const char *s, Uint32 bgc, Uint32 fgc)
{
	int w = gfx_text_w(s) + 6;

	gfx_fill(x, y, w, 9, bgc);
	gfx_text(x + 3, y + 1, s, fgc);
}

static const unsigned char circle5[11] = { 1, 7, 9, 9, 9, 11, 9, 9, 9, 7, 1 };

void gfx_circle(int cx, int cy, int r, Uint32 c)
{
	int dy;

	for (dy = -r; dy <= r; dy++) {
		int w = (int)(sqrt((double)(r * r - dy * dy)) + 0.3) * 2 + 1;
		gfx_fill(cx - w / 2, cy + dy, w, 1, c);
	}
	(void)circle5;
}

void gfx_hint(int x, int y, char letter, Uint32 color, const char *label, Uint32 labelcol)
{
	int dy;
	int cx = x + 5;
	int cy = y + 5;

	/* 11 px circle */
	for (dy = -5; dy <= 5; dy++) {
		int w = circle5[dy + 5];
		gfx_fill(cx - w / 2, cy + dy, w, 1, color);
	}
	gfx_glyph(cx - 2, cy - 3, letter, pal.bg, 1);
	gfx_text(x + 13, y + 2, label, labelcol);
}

void gfx_scanlines(void)
{
	int x, y;

	if (fb_bpp != 4)
		return;
	for (y = 1; y < SH; y += 2) {
		Uint32 *row = (Uint32 *)(fb + y * fb_pitch);
		for (x = 0; x < SW; x++) {
			Uint32 v = row[x];
			Uint32 r = (v & fb_rmask) >> fb_rshift;
			Uint32 g = (v & fb_gmask) >> fb_gshift;
			Uint32 b = (v & fb_bmask) >> fb_bshift;
			r = (r * 7) / 8;
			g = (g * 7) / 8;
			b = (b * 7) / 8;
			row[x] = (r << fb_rshift) | (g << fb_gshift) |
				 (b << fb_bshift) | (v & fb_amask);
		}
	}
}

void gfx_dump_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	int x, y;

	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", SW, SH);
	SDL_LockSurface(gfx_screen);
	for (y = 0; y < SH; y++) {
		Uint8 *row = (Uint8 *)gfx_screen->pixels + y * gfx_screen->pitch;
		for (x = 0; x < SW; x++) {
			Uint8 r, g, b;
			if (gfx_screen->format->BytesPerPixel == 4)
				SDL_GetRGB(((Uint32 *)row)[x], gfx_screen->format,
					   &r, &g, &b);
			else
				SDL_GetRGB(((Uint16 *)row)[x], gfx_screen->format,
					   &r, &g, &b);
			fputc(r, f);
			fputc(g, f);
			fputc(b, f);
		}
	}
	SDL_UnlockSurface(gfx_screen);
	fclose(f);
	fprintf(stderr, "[gfx] screenshot: %s\n", path);
	(void)black_px;
}
