/*
 * gfx.h - tiny software renderer for the 240x240 framebuffer.
 */
#ifndef CHIPTRACKER_GFX_H
#define CHIPTRACKER_GFX_H

#include "app.h"

#define FONT5X7_ADV 6

typedef struct {
	Uint32 bg, panel, panel2, line, line2;
	Uint32 dim, text, bright;
	Uint32 accent, accent_dim;
	Uint32 note, instr, vol, fx;
	Uint32 btn_a, btn_b, btn_x, btn_y, btn_s;
	Uint32 sel_bg, cur_bg;
	Uint32 warn, ok;
} Pal;

extern Pal pal;
extern SDL_Surface *gfx_screen;

int  gfx_init(void);
void gfx_quit(void);
int  gfx_reinit(void);
void gfx_theme(int idx);
void gfx_clear(Uint32 c);
Uint32 gfx_black(void);

void gfx_px(int x, int y, Uint32 c);
void gfx_fill(int x, int y, int w, int h, Uint32 c);
void gfx_fill_blend(int x, int y, int w, int h, Uint32 c, int alpha);
void gfx_hline(int x, int y, int w, Uint32 c);
void gfx_vline(int x, int y, int h, Uint32 c);
void gfx_rect(int x, int y, int w, int h, Uint32 c);
void gfx_line(int x0, int y0, int x1, int y1, Uint32 c);
void gfx_blit_indexed(const unsigned char *idx, int w, int h, int scale,
		      const Uint32 *pal);

void gfx_glyph(int x, int y, int ch, Uint32 c, int scale);
void gfx_text(int x, int y, const char *s, Uint32 c);
void gfx_text2(int x, int y, const char *s, Uint32 c);
int  gfx_text_w(const char *s);
int  gfx_text2_w(const char *s);
void gfx_text_lim(int x, int y, int maxpx, const char *s, Uint32 c);
void gfx_text2_lim(int x, int y, int maxpx, const char *s, Uint32 c);
void gfx_text_ellipsis(int x, int y, int maxpx, const char *s, Uint32 c);
void gfx_text_right(int right, int y, const char *s, Uint32 c);

void gfx_badge(int x, int y, const char *s, Uint32 bgc, Uint32 fgc);
void gfx_hint(int x, int y, char letter, Uint32 color, const char *label, Uint32 labelcol);
void gfx_circle(int cx, int cy, int r, Uint32 c);
void gfx_scanlines(void);
void gfx_dump_ppm(const char *path);

#endif
