/*
 * settings.c - grouped configuration screen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

enum {
	S_FOLDER = 0,
	S_RECURSIVE,
	S_RESCAN,
	S_REPEAT,
	S_SHUFFLE,
	S_RESUME,
	S_START,
	S_QUALITY,
	S_RATE,
	S_THEME,
	S_VIS,
	S_FPS,
	S_SPLASH,
	S_HINTS,
	S_SCANLINES,
	S_SMOOTH,
	S_PAGE,
	S_LOCKKEY,
	S_HELP,
	S_COUNT
};

enum {
	G_LIBRARY = 0,
	G_PLAYBACK,
	G_DISPLAY,
	G_ANIMATION,
	G_SYSTEM
};

typedef struct {
	unsigned char group;
	unsigned char id;
} SetItem;

static const SetItem items[S_COUNT] = {
	{ G_LIBRARY, S_FOLDER },
	{ G_LIBRARY, S_RECURSIVE },
	{ G_LIBRARY, S_RESCAN },
	{ G_PLAYBACK, S_REPEAT },
	{ G_PLAYBACK, S_SHUFFLE },
	{ G_PLAYBACK, S_RESUME },
	{ G_PLAYBACK, S_START },
	{ G_PLAYBACK, S_QUALITY },
	{ G_PLAYBACK, S_RATE },
	{ G_DISPLAY, S_THEME },
	{ G_DISPLAY, S_VIS },
	{ G_DISPLAY, S_FPS },
	{ G_DISPLAY, S_SPLASH },
	{ G_DISPLAY, S_HINTS },
	{ G_DISPLAY, S_SCANLINES },
	{ G_ANIMATION, S_SMOOTH },
	{ G_ANIMATION, S_PAGE },
	{ G_SYSTEM, S_LOCKKEY },
	{ G_SYSTEM, S_HELP }
};

static const char *group_names[] = {
	"LIBRARY", "PLAYBACK", "DISPLAY", "ANIMATION", "SYSTEM"
};

static const char *lock_key_name(int k)
{
	if (k == 1)
		return "SELECT";
	if (k == 2)
		return "BOTH";
	return "START";
}

/* Short description shown while L/R are held. */
static const char *row_desc(int id)
{
	switch (id) {
	case S_FOLDER: return "PICK THE FOLDER WITH YOUR MODULES";
	case S_RECURSIVE: return "ALSO SCAN SUBFOLDERS (SLOWER)";
	case S_RESCAN: return "REBUILD THE TRACK LIST";
	case S_REPEAT: return "OFF / REPEAT ONE / REPEAT ALL";
	case S_SHUFFLE: return "RANDOM ORDER WHEN A TRACK STARTS";
	case S_RESUME: return "CONTINUE FROM THE LAST POSITION";
	case S_START: return "PLAYER OR LIBRARY AT STARTUP";
	case S_QUALITY: return "AUDIO INTERPOLATION, APPLIED LIVE";
	case S_RATE: return "SAMPLE RATE, NEEDS A RESTART";
	case S_THEME: return "COLOUR PALETTE OF THE UI";
	case S_VIS: return "SPECTRUM/SCOPE/WATERFALL/PEAKS/DOTS";
	case S_FPS: return "FRAME RATE, 15 SAVES BATTERY";
	case S_SPLASH: return "3 SECOND SPLASH ON STARTUP";
	case S_HINTS: return "AUTO HIDE / ALWAYS / OFF";
	case S_SCANLINES: return "CRT STYLE DARK LINES";
	case S_SMOOTH: return "ANIMATE THE PATTERN SCROLL";
	case S_PAGE: return "SLIDE WHEN CHANGING CHANNEL PAGE";
	case S_LOCKKEY: return "WHICH BUTTON LOCKS THE SCREEN";
	case S_HELP: return "FULL LIST OF CONTROLS";
	}
	return "";
}

#define LIST_TOP 34
#define LIST_BOTTOM 204
#define ITEM_H 13
#define HEAD_H 13
#define GROUP_GAP 6

static const char *theme_name(int i)
{
	if (i < 0 || i >= THEME_COUNT)
		return "?";
	return themes[i].name;
}

static const char *repeat_name(int r)
{
	if (r == REPEAT_ONE)
		return "ONE";
	if (r == REPEAT_ALL)
		return "ALL";
	return "OFF";
}

static const char *vis_name(int v)
{
	switch (v) {
	case VIS_SCOPE: return "SCOPE";
	case VIS_WATERFALL: return "WATERFALL";
	case VIS_PEAKS: return "PEAKS";
	case VIS_DOTS: return "DOTS";
	case VIS_OFF: return "OFF";
	}
	return "SPECTRUM";
}

static const char *quality_name(int q)
{
	if (q == QUAL_FAST)
		return "FAST";
	if (q == QUAL_HIGH)
		return "HIGH";
	return "NORMAL";
}

static const char *row_label(int id)
{
	switch (id) {
	case S_FOLDER: return "MUSIC FOLDER";
	case S_RECURSIVE: return "SCAN SUBFOLDERS";
	case S_RESCAN: return "RESCAN LIBRARY";
	case S_REPEAT: return "REPEAT";
	case S_SHUFFLE: return "SHUFFLE";
	case S_RESUME: return "RESUME POSITION";
	case S_START: return "START SCREEN";
	case S_QUALITY: return "PLAYBACK QUALITY";
	case S_RATE: return "SAMPLE RATE";
	case S_THEME: return "COLOUR THEME";
	case S_VIS: return "BOTTOM VISUALIZER";
	case S_FPS: return "FRAME RATE";
	case S_SPLASH: return "SHOW SPLASH";
	case S_HINTS: return "BUTTON HINTS";
	case S_SCANLINES: return "CRT SCANLINES";
	case S_SMOOTH: return "SMOOTH SCROLL";
	case S_PAGE: return "PAGE SLIDE";
	case S_LOCKKEY: return "LOCK BUTTON";
	case S_HELP: return "HELP / CONTROLS";
	}
	return "";
}

static void row_value(int id, char *out, size_t n)
{
	out[0] = 0;
	switch (id) {
	case S_FOLDER:
		util_copy(out, n, cfg.music_dir[0] ? cfg.music_dir : "(not set)");
		break;
	case S_RECURSIVE:
		util_copy(out, n, cfg.recursive ? "ON" : "OFF");
		break;
	case S_RESCAN:
		if (scan.active)
			snprintf(out, n, "SCAN %d", scan.files);
		else
			util_copy(out, n, "PRESS A");
		break;
	case S_REPEAT:
		util_copy(out, n, repeat_name(cfg.repeat));
		break;
	case S_SHUFFLE:
		util_copy(out, n, cfg.shuffle ? "ON" : "OFF");
		break;
	case S_RESUME:
		util_copy(out, n, cfg.resume ? "ON" : "OFF");
		break;
	case S_START:
		util_copy(out, n, cfg.start_player ? "PLAYER" : "LIBRARY");
		break;
	case S_QUALITY:
		util_copy(out, n, quality_name(cfg.quality));
		break;
	case S_RATE:
		snprintf(out, n, "%d", cfg.samplerate);
		break;
	case S_THEME:
		util_copy(out, n, theme_name(cfg.theme));
		break;
	case S_VIS:
		util_copy(out, n, vis_name(cfg.vis));
		break;
	case S_FPS:
		snprintf(out, n, "%d", cfg.fps);
		break;
	case S_SPLASH:
		util_copy(out, n, cfg.show_splash ? "ON" : "OFF");
		break;
	case S_HINTS:
		util_copy(out, n, cfg.hints == 0 ? "AUTO" :
			  (cfg.hints == 1 ? "ON" : "OFF"));
		break;
	case S_SCANLINES:
		util_copy(out, n, cfg.scanlines ? "ON" : "OFF");
		break;
	case S_SMOOTH:
		util_copy(out, n, cfg.smooth ? "ON" : "OFF");
		break;
	case S_PAGE:
		util_copy(out, n, cfg.page_slide ? "ON" : "OFF");
		break;
	case S_LOCKKEY:
		util_copy(out, n, lock_key_name(cfg.lock_key));
		break;
	case S_HELP:
		util_copy(out, n, "OPEN");
		break;
	}
}

static void cycle(int dir)
{
	int id = items[settings.sel].id;

	switch (id) {
	case S_RECURSIVE:
		cfg.recursive = !cfg.recursive;
		break;
	case S_REPEAT:
		cfg.repeat = (cfg.repeat + (dir > 0 ? 1 : 2)) % 3;
		break;
	case S_SHUFFLE:
		cfg.shuffle = !cfg.shuffle;
		break;
	case S_RESUME:
		cfg.resume = !cfg.resume;
		break;
	case S_START:
		cfg.start_player = !cfg.start_player;
		break;
	case S_QUALITY:
		cfg.quality = (cfg.quality + (dir > 0 ? 1 : 2)) % 3;
		audio_apply_quality();
		break;
	case S_RATE:
		cfg.samplerate = cfg.samplerate == 44100 ? 32000 : 44100;
		break;
	case S_THEME:
		cfg.theme = (cfg.theme + (dir > 0 ? 1 : THEME_COUNT - 1)) %
			    THEME_COUNT;
		gfx_theme(cfg.theme);
		break;
	case S_VIS:
		cfg.vis = (cfg.vis + (dir > 0 ? 1 : VIS_MODE_COUNT - 1)) %
			  VIS_MODE_COUNT;
		break;
	case S_FPS:
		if (cfg.fps == 15)
			cfg.fps = dir > 0 ? 30 : 60;
		else if (cfg.fps == 30)
			cfg.fps = dir > 0 ? 45 : 15;
		else if (cfg.fps == 45)
			cfg.fps = dir > 0 ? 60 : 30;
		else
			cfg.fps = dir > 0 ? 15 : 45;
		break;
	case S_SPLASH:
		cfg.show_splash = !cfg.show_splash;
		break;
	case S_HINTS:
		cfg.hints = (cfg.hints + (dir > 0 ? 1 : 2)) % 3;
		break;
	case S_SCANLINES:
		cfg.scanlines = !cfg.scanlines;
		break;
	case S_SMOOTH:
		cfg.smooth = !cfg.smooth;
		break;
	case S_PAGE:
		cfg.page_slide = !cfg.page_slide;
		break;
	case S_LOCKKEY:
		cfg.lock_key = (cfg.lock_key + (dir > 0 ? 1 : 2)) % 3;
		break;
	default:
		return;
	}
	config_save();
}

static void activate(void)
{
	int id = items[settings.sel].id;

	switch (id) {
	case S_FOLDER:
		browser_open(cfg.music_dir[0] ? cfg.music_dir : "/mnt", 1, 0);
		break;
	case S_RESCAN:
		if (cfg.music_dir[0] && !scan.active) {
			scan_begin(cfg.music_dir, cfg.recursive);
			app_toast("SCANNING...", 1200);
		}
		break;
	case S_HELP:
		screen = SCR_HELP;
		break;
	default:
		cycle(1);
		break;
	}
}

static void draw_tail_right(int right, int y, const char *s, Uint32 c)
{
	char ascii[192];
	int maxc;
	int n;

	util_ascii(s, ascii, sizeof(ascii));
	s = ascii;
	maxc = (right - 110) / FONT5X7_ADV;
	n = (int)strlen(s);
	if (maxc <= 0)
		return;
	if (n <= maxc) {
		gfx_text(right - gfx_text_w(s), y, s, c);
		return;
	}
	gfx_text(right - maxc * FONT5X7_ADV, y, "..", c);
	gfx_text(right - (maxc - 2) * FONT5X7_ADV, y,
		 s + n - (maxc - 2), c);
}

static int item_y(int top, int sel)
{
	int y = LIST_TOP;
	int i;
	int prev = top > 0 ? items[top - 1].group : -1;

	for (i = top; i <= sel && i < S_COUNT; i++) {
		if (items[i].group != prev) {
			if (i != top)
				y += GROUP_GAP;
			y += HEAD_H;
			prev = items[i].group;
		}
		if (i == sel)
			return y;
		y += ITEM_H;
	}
	return y;
}

static void fix_top(void)
{
	if (settings.sel < 0)
		settings.sel = 0;
	if (settings.sel >= S_COUNT)
		settings.sel = S_COUNT - 1;
	if (settings.top < 0)
		settings.top = 0;
	if (settings.top > settings.sel)
		settings.top = settings.sel;
	/* Scroll only when the cursor would leave the window: moving inside
	 * the visible page must not shift the whole list. */
	while (settings.top < settings.sel &&
	       item_y(settings.top, settings.sel) + ITEM_H > LIST_BOTTOM)
		settings.top++;
}

static int desc_id = -1;
static int desc_sel = -1;
static unsigned int desc_until = 0;

void settings_update(void)
{
	fix_top();
}

/* Used by the screenshot generator only. */
void settings_desc_preview(void)
{
	desc_id = items[settings.sel].id;
	desc_sel = settings.sel;
	desc_until = SDL_GetTicks() + 60000;
}

void settings_draw(void)
{
	int i;
	int y = LIST_TOP;
	char val[64];
	int prev_group = settings.top > 0 ? items[settings.top - 1].group : -1;

	gfx_clear(pal.bg);
	gfx_fill(0, 0, SW, 31, pal.panel);
	gfx_hline(0, 31, SW, pal.accent);
	gfx_text2(6, 5, "SETTINGS", pal.bright);
	gfx_text_right(234, 10, "CHIPTRACKER 1.0", pal.dim);

	for (i = settings.top; i < S_COUNT; i++) {
		int id = items[i].id;
		int selected = i == settings.sel;
		const char *label;
		Uint32 lc;

		if (items[i].group != prev_group) {
			if (i != settings.top)
				y += GROUP_GAP;
			if (y + HEAD_H > LIST_BOTTOM)
				break;
			gfx_text(6, y, group_names[items[i].group], pal.accent);
			gfx_hline(6, y + 9, 228, pal.line);
			y += HEAD_H;
			prev_group = items[i].group;
		}
		if (y + ITEM_H > LIST_BOTTOM)
			break;

		label = row_label(id);
		row_value(id, val, sizeof(val));
		lc = selected ? pal.bright : pal.text;
		if (selected) {
			gfx_fill(0, y, 236, ITEM_H, pal.sel_bg);
			gfx_fill(2, y, 3, ITEM_H, pal.accent);
		} else {
			gfx_hline(8, y + ITEM_H - 1, 226, pal.line);
		}
		gfx_text(8, y + 3, label, lc);

		if (id == S_FOLDER) {
			draw_tail_right(232, y + 3, val,
					selected ? pal.accent : pal.accent_dim);
		} else if (id == S_RESCAN) {
			gfx_text_right(232, y + 3, val,
				       scan.active ? pal.accent : pal.warn);
		} else if (id == S_HELP) {
			/* action rows: nothing to choose, just press A */
			gfx_text_right(232, y + 3, val, pal.warn);
		} else {
			gfx_text_right(232, y + 3, val,
				       selected ? pal.accent : pal.accent_dim);
		}
		y += ITEM_H;
	}

	if (S_COUNT > 1) {
		int track = LIST_BOTTOM - LIST_TOP;
		int h = track * 10 / S_COUNT;
		int max = S_COUNT - 1;
		int yy;

		if (h < 10)
			h = 10;
		gfx_fill(237, LIST_TOP, 2, track, pal.line);
		yy = LIST_TOP + (track - h) * settings.sel / (max ? max : 1);
		gfx_fill(237, yy, 2, h, pal.accent_dim);
	}

	if (desc_id >= 0) {
		unsigned int now = SDL_GetTicks();
		Uint8 *ks = SDL_GetKeyState(NULL);
		int held = ks && (ks[SDLK_m] || ks[SDLK_n]);

		if (desc_sel != settings.sel)
			desc_id = -1;
		else if (!held && (int)(now - desc_until) >= 0)
			desc_id = -1;
		if (desc_id >= 0) {
			gfx_fill(0, 34, SW, 13, pal.panel2);
			gfx_rect(0, 34, SW, 13, pal.accent_dim);
			gfx_text_ellipsis(6, 37, 232, row_desc(desc_id),
					  pal.text);
		}
	}

	gfx_fill(0, 204, SW, SH - 204, pal.bg);
	gfx_hline(0, 204, SW, pal.line);
	gfx_text(6, 210, "L/R: HELP  LEFT/RIGHT: CHANGE", pal.dim);
	gfx_text(6, 219, "UP/DOWN: ITEM  AUTOSAVE", pal.accent_dim);
	if (cfg.samplerate != 44100)
		gfx_text_right(234, 219, "RESTART RATE", pal.warn);
	gfx_hint(6, 226, 'A', pal.btn_a, "CHANGE", pal.text);
	gfx_hint(66, 226, 'B', pal.btn_b, "BACK", pal.text);
	gfx_hint(116, 226, 'X', pal.btn_x, "SCAN", pal.text);
}

void settings_key(SDLKey k)
{
	switch (k) {
	case SDLK_u:
		settings.sel = (settings.sel + S_COUNT - 1) % S_COUNT;
		fix_top();
		break;
	case SDLK_d:
		settings.sel = (settings.sel + 1) % S_COUNT;
		fix_top();
		break;
	case SDLK_l:
		cycle(-1);
		break;
	case SDLK_r:
		cycle(1);
		break;
	case SDLK_m:
	case SDLK_n:
		desc_id = items[settings.sel].id;
		desc_sel = settings.sel;
		desc_until = SDL_GetTicks() + 4000;
		break;
	case SDLK_a:
		activate();
		break;
	case SDLK_x:
		if (cfg.music_dir[0] && !scan.active) {
			scan_begin(cfg.music_dir, cfg.recursive);
			app_toast("SCANNING...", 1200);
		}
		break;
	case SDLK_b:
	case SDLK_s:
		screen = player.loaded ? SCR_PLAYER : SCR_LIBRARY;
		break;
	default:
		break;
	}
}
