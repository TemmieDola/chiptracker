/*
 * help.c - controls reference.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

typedef struct {
	const char *key;
	const char *text;
	unsigned char header;
} HelpLine;

static const HelpLine help_lines[] = {
	{ "PLAYER", "", 1 },
	{ "A", "PLAY / PAUSE", 0 },
	{ "B", "LIBRARY", 0 },
	{ "X", "TRACK INFO", 0 },
	{ "Y", "FULLSCREEN VISUALIZER", 0 },
	{ "L / R", "PREV / NEXT TRACK", 0 },
	{ "UP / DOWN", "PREV / NEXT ORDER", 0 },
	{ "LEFT / RIGHT", "CHANNEL PAGE", 0 },
	{ "START", "SETTINGS", 0 },
	{ "HOLD S/SEL", "LOCK SCREEN", 0 },
	{ "LIBRARY", "", 1 },
	{ "A", "PLAY / OPEN", 0 },
	{ "B", "BACK / UP", 0 },
	{ "X", "SORT / REFRESH", 0 },
	{ "Y", "RANDOM / USE FOLDER", 0 },
	{ "START", "RESCAN / PLAY FIRST", 0 },
	{ "VISUALIZER", "", 1 },
	{ "Y", "NEXT VISUAL STYLE", 0 },
	{ "B", "BACK TO PLAYER", 0 },
	{ "LEFT/RIGHT", "PARAM / PATTERN PAGE", 0 },
	{ "A", "PLAY / PAUSE", 0 }
};

#define HELP_N (int)(sizeof(help_lines) / sizeof(help_lines[0]))

void help_draw(void)
{
	int i;
	int y = 36;

	gfx_clear(pal.bg);
	gfx_fill(0, 0, SW, 31, pal.panel);
	gfx_hline(0, 31, SW, pal.accent);
	gfx_text2(6, 5, "CONTROLS", pal.bright);
	gfx_text_right(234, 10, "CHIPTRACKER", pal.dim);

	for (i = 0; i < HELP_N; i++) {
		const HelpLine *l = &help_lines[i];
		if (l->header) {
			gfx_text(6, y, l->key, pal.accent);
			gfx_hline(6, y + 7, 228, pal.line);
			y += 10;
		} else {
			gfx_text(8, y, l->key, pal.instr);
			gfx_text(92, y, l->text, pal.text);
			y += 8;
		}
	}

	gfx_fill(0, 212, SW, SH - 212, pal.bg);
	gfx_hline(0, 212, SW, pal.line);
	gfx_hint(6, 215, 'B', pal.btn_b, "BACK", pal.text);
	gfx_text(56, 217, "MENU: EXIT   FN: STATS OVERLAY", pal.dim);
}

void help_key(SDLKey k)
{
	switch (k) {
	case SDLK_b:
		screen = SCR_SETTINGS;
		break;
	default:
		break;
	}
}
