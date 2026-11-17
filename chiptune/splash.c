/*
 * splash.c - boot screen.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

static void equalizer(void)
{
	static const Uint32 colors[5] = { 0, 0, 0, 0, 0 };
	int i;
	unsigned int t = SDL_GetTicks();
	Uint32 cols[5];

	cols[0] = pal.note;
	cols[1] = pal.instr;
	cols[2] = pal.vol;
	cols[3] = pal.fx;
	cols[4] = pal.accent;

	for (i = 0; i < 15; i++) {
		int h = 4 + (int)(8.0f + 8.0f *
			sinf((float)t / 220.0f + (float)i * 0.7f));
		int x = 45 + i * 10;
		int y = 154 - h;

		if (h < 3)
			h = 3;
		gfx_fill(x, y, 5, h, cols[i % 5]);
		gfx_fill(x, y - 2, 5, 1, cols[i % 5]);
	}
	(void)colors;
}

void splash_draw(void)
{
	char status[64];
	const char *sub = "XM  IT  MOD  S3M  MO3";
	const char *sub2 = "KEYGEN / DEMOSCENE MODULE PLAYER";
	const char *logo = "CHIPTRACKER";
	int w;

	gfx_clear(pal.bg);

	/* case-coloured frame */
	gfx_rect(18, 48, 204, 120, pal.accent_dim);
	gfx_hline(18, 48, 204, pal.accent);
	gfx_hline(18, 167, 204, pal.accent);
	gfx_fill(18, 48, 2, 2, pal.bg);
	gfx_fill(220, 48, 2, 2, pal.bg);
	gfx_fill(18, 165, 2, 2, pal.bg);
	gfx_fill(220, 165, 2, 2, pal.bg);

	w = gfx_text2_w(logo);
	gfx_text2((SW - w) / 2, 66, logo, pal.bright);
	gfx_hline((SW - w) / 2, 86, w, pal.accent);

	gfx_text((SW - gfx_text_w(sub)) / 2, 96, sub, pal.accent);

	w = gfx_text_w(sub2);
	gfx_text((SW - w) / 2, 108, sub2, pal.dim);

	equalizer();

	if (scan.active) {
		snprintf(status, sizeof(status), "INDEXING  %d", scan.files);
		gfx_text((SW - gfx_text_w(status)) / 2, 182, status, pal.accent);
		{
			unsigned int t = SDL_GetTicks() / 3;
			int x = (int)(t % (SW + 50)) - 50;
			int x0 = x < 0 ? 0 : x;
			int x1 = x + 50;
			if (x1 > SW)
				x1 = SW;
			gfx_hline(0, 194, SW, pal.line);
			gfx_fill(x0, 194, x1 - x0, 1, pal.accent);
		}
	} else if (track_count > 0) {
		snprintf(status, sizeof(status), "%d TRACKS INDEXED", track_count);
		gfx_text((SW - gfx_text_w(status)) / 2, 182, status, pal.text);
	} else if (!cfg.music_dir[0]) {
		gfx_text((SW - gfx_text_w("NO MUSIC FOLDER")) / 2, 182,
			 "NO MUSIC FOLDER", pal.warn);
	} else {
		gfx_text((SW - gfx_text_w("READY")) / 2, 182, "READY", pal.text);
	}

	gfx_text_right(232, 232, "V1.0", pal.accent_dim);
}

void splash_update(void)
{
}

void splash_key(SDLKey k)
{
	(void)k;
}
