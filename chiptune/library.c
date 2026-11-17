/*
 * library.c - scanned track list.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

#define LIST_TOP 34
#define ROW_H 13
#define ROWS 14
#define FOOT_LIB 216

static int probe_sel = -1;
static unsigned int probe_at = 0;

void lib_sel_reset(void)
{
	lib.sel = 0;
	lib.top = 0;
	probe_sel = -1;
}

void lib_sel_clamp(void)
{
	if (lib.sel < 0)
		lib.sel = 0;
	if (lib.sel >= track_count)
		lib.sel = track_count > 0 ? track_count - 1 : 0;
	if (lib.top < 0)
		lib.top = 0;
	if (lib.top > lib.sel)
		lib.top = lib.sel;
	if (lib.sel >= lib.top + ROWS)
		lib.top = lib.sel - ROWS + 1;
}

int library_index_of(const char *path)
{
	int i;

	if (!path)
		return -1;
	for (i = 0; i < track_count; i++)
		if (!strcmp(tracks[i].path, path))
			return i;
	return -1;
}

void library_play_selected(void)
{
	if (track_count <= 0)
		return;
	lib_sel_clamp();
	player_start_index(lib.sel, 0);
	/* Stay in the list so the next track can be picked without a round
	 * trip through the player screen.  B still returns to the player. */
	if (player.loaded)
		screen = SCR_LIBRARY;
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

static void ensure_visible(void)
{
	if (lib.sel < lib.top)
		lib.top = lib.sel;
	if (lib.sel >= lib.top + ROWS)
		lib.top = lib.sel - ROWS + 1;
	if (lib.top < 0)
		lib.top = 0;
}

void library_update(void)
{
	unsigned int now = SDL_GetTicks();

	lib_sel_clamp();
	if (track_count <= 0)
		return;
	if (lib.sel != probe_sel) {
		probe_sel = lib.sel;
		probe_at = now + 160;
		return;
	}
	if (now >= probe_at && tracks[lib.sel].meta == 0) {
		probe_track(&tracks[lib.sel]);
		probe_at = now + 1000;
	}
}

static void draw_scan_bar(void)
{
	int x, w = 38;
	unsigned int t = SDL_GetTicks() / 4;

	x = (int)(t % (SW + w)) - w;
	gfx_fill(0, 30, SW, 1, pal.line);
	if (x + w > 0) {
		int x0 = x < 0 ? 0 : x;
		int x1 = x + w;
		if (x1 > SW)
			x1 = SW;
		gfx_fill(x0, 30, x1 - x0, 1, pal.accent);
	}
}

static void draw_empty(void)
{
	const char *msg1 = "NO TRACKS";
	const char *msg2;
	int y = 88;

	gfx_text2((SW - gfx_text2_w(msg1)) / 2, y, msg1, pal.dim);
	y += 24;

	if (scan.active) {
		char line[48];
		snprintf(line, sizeof(line), "SCANNING  %d FOUND", scan.files);
		gfx_text((SW - gfx_text_w(line)) / 2, y, line, pal.accent);
		return;
	}
	if (!cfg.music_dir[0])
		msg2 = "NO MUSIC FOLDER SELECTED";
	else
		msg2 = "FOLDER IS EMPTY OR UNREADABLE";
	gfx_text((SW - gfx_text_w(msg2)) / 2, y, msg2, pal.text);
	y += 14;
	{
		char pbuf[MAX_PATH];
		util_ascii(cfg.music_dir[0] ? cfg.music_dir : "", pbuf,
			   sizeof(pbuf));
		gfx_text_ellipsis(6, y, 234, pbuf, pal.dim);
	}
	y += 24;
	gfx_text((SW - gfx_text_w("A: CHOOSE FOLDER")) / 2, y,
		 "A: CHOOSE FOLDER", pal.accent);
	gfx_text((SW - gfx_text_w("START: SETTINGS")) / 2, y + 14,
		 "START: SETTINGS", pal.dim);
}

static void library_header_path(void)
{
	char buf[MAX_PATH];

	util_ascii(cfg.music_dir[0] ? cfg.music_dir : "NO MUSIC FOLDER", buf,
		   sizeof(buf));
	gfx_text_ellipsis(6, 21, 234, buf, pal.dim);
}

void library_draw(void)
{
	char line[64];
	char tmp[64];
	Track *t;
	int i;
	int scan_state = scan.active;

	gfx_clear(pal.bg);
	gfx_fill(0, 0, SW, 31, pal.panel);
	gfx_hline(0, 31, SW, pal.accent);
	gfx_text2(6, 5, "LIBRARY", pal.bright);

	if (scan_state) {
		snprintf(tmp, sizeof(tmp), "SCAN %d", scan.files);
		gfx_text_right(234, 8, tmp, pal.accent);
	} else if (player.loaded && player.ctx_count > 0) {
		snprintf(tmp, sizeof(tmp), "%d/%d", player.ctx_pos + 1,
			 player.ctx_count);
		gfx_text_right(234, 8, tmp, pal.accent);
	} else {
		snprintf(tmp, sizeof(tmp), "%d TRACKS", track_count);
		gfx_text_right(234, 8, tmp, pal.dim);
	}
	library_header_path();

	if (track_count == 0) {
		draw_empty();
	} else {
		for (i = 0; i < ROWS; i++) {
			int idx = lib.top + i;
			int y;
			Uint32 fc;

			if (idx >= track_count)
				break;
			t = &tracks[idx];
			y = LIST_TOP + i * ROW_H;
			fc = fmt_color(t->fmt);

			if (idx == lib.sel) {
				gfx_fill(0, y, 236, ROW_H, pal.sel_bg);
				gfx_fill(2, y, 3, ROW_H, pal.accent);
			} else {
				gfx_hline(8, y + ROW_H - 1, 226, pal.line);
			}

			gfx_fill(9, y + 3, 3, 7, fc);
			gfx_text(15, y + 3, t->fmt, fc);
			{
				char disp[192];
				util_ascii(t->stem, disp, sizeof(disp));
				gfx_text_ellipsis(43, y + 3, 230, disp,
						  idx == lib.sel ? pal.bright :
						  pal.text);
			}

			if (!strcmp(t->path, player.track.path)) {
				gfx_fill(231, y + 4, 1, 5, pal.accent);
				gfx_fill(232, y + 5, 1, 3, pal.accent);
				gfx_fill(233, y + 6, 1, 1, pal.accent);
			}
		}

		/* scrollbar */
		gfx_fill(238, LIST_TOP, 2, ROWS * ROW_H, pal.line);
		if (track_count > ROWS) {
			int h = ROWS * ROW_H * ROWS / track_count;
			int y = LIST_TOP + (ROWS * ROW_H - h) * lib.top /
				(track_count - ROWS);
			if (h < 6)
				h = 6;
			gfx_fill(238, y, 2, h, pal.accent_dim);
		}
	}

	if (scan_state)
		draw_scan_bar();

	/* footer */
	gfx_fill(0, FOOT_LIB, SW, SH - FOOT_LIB, pal.bg);
	gfx_hline(0, FOOT_LIB, SW, pal.line);

	if (track_count > 0) {
		t = &tracks[lib.sel];
		{
			char dur[16], size[16];
			int w;
			Uint32 fc = fmt_color(t->fmt);

			if (t->meta == 1 && t->duration > 0)
				util_time(t->duration, dur, sizeof(dur));
			else
				util_copy(dur, sizeof(dur), "--:--");
			util_size(t->size, size, sizeof(size));
			gfx_text(6, FOOT_LIB + 2, t->fmt, fc);
			w = gfx_text_w(t->fmt);
			snprintf(line, sizeof(line), "  %dCH  %s  %s",
				 t->channels, dur, size);
			gfx_text(6 + w, FOOT_LIB + 2, line, pal.dim);
			if (t->tracker[0])
				gfx_text_ellipsis(6 + w + gfx_text_w(line) + 6,
						  FOOT_LIB + 2, 234, t->tracker,
						  pal.accent_dim);
		}
	} else {
		gfx_text(6, FOOT_LIB + 2, "SELECT A FOLDER TO SCAN", pal.dim);
	}

	gfx_hint(6, FOOT_LIB + 10, 'A', pal.btn_a, "PLAY", pal.text);
	gfx_hint(51, FOOT_LIB + 10, 'B', pal.btn_b, "BACK", pal.text);
	gfx_hint(96, FOOT_LIB + 10, 'X', pal.btn_x, "SORT", pal.text);
	gfx_hint(141, FOOT_LIB + 10, 'Y', pal.btn_y, "RAND", pal.text);
	gfx_hint(186, FOOT_LIB + 10, 'S', pal.btn_s, "SCAN", pal.text);
	(void)scan_state;
}

static void library_step(int dir)
{
	int idx;

	if (!player.loaded)
		return;
	player_next(dir);
	idx = library_index_of(player.track.path);
	if (idx >= 0) {
		lib.sel = idx;
		ensure_visible();
	}
}

void library_key(SDLKey k)
{
	switch (k) {
	case SDLK_u:
		if (track_count > 0) {
			lib.sel = (lib.sel + track_count - 1) % track_count;
			ensure_visible();
		}
		break;
	case SDLK_d:
		if (track_count > 0) {
			lib.sel = (lib.sel + 1) % track_count;
			ensure_visible();
		}
		break;
	case SDLK_l:
		if (track_count > 0) {
			lib.sel -= 10;
			if (lib.sel < 0)
				lib.sel = 0;
			ensure_visible();
		}
		break;
	case SDLK_r:
		if (track_count > 0) {
			lib.sel += 10;
			if (lib.sel >= track_count)
				lib.sel = track_count - 1;
			ensure_visible();
		}
		break;
	case SDLK_m:
		library_step(-1);
		break;
	case SDLK_n:
		library_step(1);
		break;
	case SDLK_a:
		if (track_count == 0)
			browser_open(cfg.music_dir[0] ? cfg.music_dir : "/mnt",
				     1, 0);
		else
			library_play_selected();
		break;
	case SDLK_b:
		if (player.loaded)
			screen = SCR_PLAYER;
		else
			screen = SCR_SETTINGS;
		break;
	case SDLK_x: {
		char keep[MAX_PATH];
		keep[0] = 0;
		if (track_count > 0)
			util_copy(keep, sizeof(keep), tracks[lib.sel].path);
		tracks_sort((cfg.sort + 1) % 3);
		if (keep[0]) {
			int idx = library_index_of(keep);
			if (idx >= 0)
				lib.sel = idx;
		}
		lib_sel_clamp();
		app_toast(cfg.sort == 0 ? "SORT: NAME" :
			  (cfg.sort == 1 ? "SORT: FORMAT" : "SORT: SIZE"),
			  1200);
		config_save();
		break;
	}
	case SDLK_y:
		if (track_count > 0) {
			lib.sel = rand() % track_count;
			ensure_visible();
			library_play_selected();
		}
		break;
	case SDLK_s:
		if (cfg.music_dir[0]) {
			if (!scan.active) {
				scan_begin(cfg.music_dir, cfg.recursive);
				app_toast("SCANNING...", 1200);
			}
		} else {
			browser_open("/mnt", 1, 0);
		}
		break;
	default:
		break;
	}
}
