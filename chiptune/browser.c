/*
 * browser.c - directory / file manager.
 *
 * mode 0: browse and play modules
 * mode 1: pick the music folder (Y = use current folder)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "app.h"
#include "gfx.h"

#define BR_TOP 34
#define BR_ROW 13
#define BR_ROWS 13
#define BR_FOOT 204

static void ensure_visible(void)
{
	if (browser.sel < 0)
		browser.sel = 0;
	if (browser.sel >= browser.count)
		browser.sel = browser.count > 0 ? browser.count - 1 : 0;
	if (browser.sel < browser.top)
		browser.top = browser.sel;
	if (browser.sel >= browser.top + BR_ROWS)
		browser.top = browser.sel - BR_ROWS + 1;
	if (browser.top < 0)
		browser.top = 0;
}

static void add_entry(const char *name, const char *path, int isdir)
{
	if (browser.count >= MAX_BROWSER)
		return;
	util_copy(browser.names[browser.count], sizeof(browser.names[0]), name);
	util_copy(browser.paths[browser.count], sizeof(browser.paths[0]), path);
	browser.isdir[browser.count] = (unsigned char)(isdir ? 1 : 0);
	browser.count++;
}

void browser_reload(void)
{
	DIR *d;
	struct dirent *de;
	char path[MAX_PATH];
	int i, j;

	browser.count = 0;
	browser.error = 0;
	browser.sel = 0;
	browser.top = 0;

	if (strcmp(browser.path, "/") != 0) {
		char parent[MAX_PATH];
		util_dirname(browser.path, parent, sizeof(parent));
		add_entry("..", parent, 1);
	}

	d = opendir(browser.path);
	if (!d) {
		browser.error = 1;
		return;
	}
	while ((de = readdir(d)) != NULL) {
		struct stat st;
		const char *ext;

		if (de->d_name[0] == '.')
			continue;
		util_join(path, sizeof(path), browser.path, de->d_name);
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			add_entry(de->d_name, path, 1);
		} else if (S_ISREG(st.st_mode) && browser.mode == 0) {
			ext = util_ext(de->d_name);
			if (util_supported_ext(ext))
				add_entry(de->d_name, path, 0);
		}
	}
	closedir(d);

	/* directories first, then natural name */
	for (i = 0; i < browser.count; i++) {
		int best = i;
		for (j = i + 1; j < browser.count; j++) {
			int better;
			if (browser.isdir[j] != browser.isdir[best]) {
				better = browser.isdir[j] > browser.isdir[best];
			} else {
				better = util_nat_cmp(browser.names[j],
						      browser.names[best]) < 0;
			}
			if (better)
				best = j;
		}
		if (best != i) {
			char tn[192], tp[MAX_PATH];
			unsigned char td;
			util_copy(tn, sizeof(tn), browser.names[i]);
			util_copy(tp, sizeof(tp), browser.paths[i]);
			td = browser.isdir[i];
			util_copy(browser.names[i], sizeof(browser.names[0]),
				  browser.names[best]);
			util_copy(browser.paths[i], sizeof(browser.paths[0]),
				  browser.paths[best]);
			browser.isdir[i] = browser.isdir[best];
			util_copy(browser.names[best], sizeof(browser.names[0]), tn);
			util_copy(browser.paths[best], sizeof(browser.paths[0]), tp);
			browser.isdir[best] = td;
		}
	}
}

void browser_open(const char *path, int mode, int first_run)
{
	char start[MAX_PATH];

	if (path && *path && util_isdir(path))
		util_copy(start, sizeof(start), path);
	else if (util_isdir("/mnt/Audio"))
		util_copy(start, sizeof(start), "/mnt/Audio");
	else if (util_isdir("/mnt"))
		util_copy(start, sizeof(start), "/mnt");
	else
		util_copy(start, sizeof(start), "/");

	util_copy(browser.path, sizeof(browser.path), start);
	browser.mode = mode;
	browser.first_run = first_run;
	browser_reload();
	screen = SCR_BROWSER;
}

static void browse_to(const char *path)
{
	if (!util_isdir(path))
		return;
	util_copy(browser.path, sizeof(browser.path), path);
	browser_reload();
}

static void go_up(void)
{
	char parent[MAX_PATH];

	if (!strcmp(browser.path, "/")) {
		if (browser.first_run)
			app_toast("CHOOSE A MUSIC FOLDER", 1800);
		else
			screen = SCR_SETTINGS;
		return;
	}
	util_dirname(browser.path, parent, sizeof(parent));
	browse_to(parent);
}

static void choose_current(void)
{
	util_copy(cfg.music_dir, sizeof(cfg.music_dir), browser.path);
	cfg.last_valid = 0;
	cfg.last_path[0] = 0;
	config_save();
	scan_begin(cfg.music_dir, cfg.recursive);
	app_toast("MUSIC FOLDER SET", 1600);
	screen = SCR_LIBRARY;
}

static void activate_selected(void)
{
	if (browser.count <= 0)
		return;
	if (browser.isdir[browser.sel]) {
		browse_to(browser.paths[browser.sel]);
		return;
	}
	player_start_path(browser.paths[browser.sel], 0);
}

void browser_key(SDLKey k)
{
	switch (k) {
	case SDLK_u:
		if (browser.count > 0) {
			browser.sel--;
			if (browser.sel < 0)
				browser.sel = 0;
			ensure_visible();
		}
		break;
	case SDLK_d:
		if (browser.count > 0) {
			browser.sel++;
			if (browser.sel >= browser.count)
				browser.sel = browser.count - 1;
			ensure_visible();
		}
		break;
	case SDLK_l:
	case SDLK_m:
		if (browser.count > 0) {
			browser.sel -= 10;
			if (browser.sel < 0)
				browser.sel = 0;
			ensure_visible();
		}
		break;
	case SDLK_r:
	case SDLK_n:
		if (browser.count > 0) {
			browser.sel += 10;
			if (browser.sel >= browser.count)
				browser.sel = browser.count - 1;
			ensure_visible();
		}
		break;
	case SDLK_a:
		activate_selected();
		break;
	case SDLK_b:
		go_up();
		break;
	case SDLK_y:
		if (browser.mode == 1)
			choose_current();
		break;
	case SDLK_x:
		if (browser.mode == 0)
			browser_reload();
		break;
	case SDLK_s:
		if (browser.mode == 0) {
			int i;
			for (i = 0; i < browser.count; i++) {
				if (!browser.isdir[i]) {
					player_start_path(browser.paths[i], 0);
					break;
				}
			}
		}
		break;
	default:
		break;
	}
}

static void draw_folder_icon(int x, int y, Uint32 c)
{
	gfx_fill(x + 1, y + 1, 3, 2, c);
	gfx_fill(x, y + 3, 9, 5, c);
	gfx_fill(x + 1, y + 3, 7, 1, pal.bg);
}

void browser_draw(void)
{
	int i;

	gfx_clear(pal.bg);
	gfx_fill(0, 0, SW, 31, pal.panel);
	gfx_hline(0, 31, SW, pal.accent);
	gfx_text2(6, 5, "BROWSE", pal.bright);

	if (browser.mode == 1) {
		gfx_text_right(234, 8, "CHOOSE FOLDER", pal.instr);
	} else {
		gfx_text_right(234, 8, "PLAY FILE", pal.accent);
	}
	{
		char pbuf[MAX_PATH];
		util_ascii(browser.path, pbuf, sizeof(pbuf));
		gfx_text_ellipsis(6, 21, 234, pbuf, pal.dim);
	}

	if (browser.error) {
		char pbuf[MAX_PATH];
		util_ascii(browser.path, pbuf, sizeof(pbuf));
		gfx_text2(24, 90, "CANNOT OPEN", pal.fx);
		gfx_text_ellipsis(6, 120, 234, pbuf, pal.dim);
	} else {
		for (i = 0; i < BR_ROWS; i++) {
			int idx = browser.top + i;
			int y;
			char name[192];

			if (idx >= browser.count)
				break;
			y = BR_TOP + i * BR_ROW;
			util_ascii(browser.names[idx], name, sizeof(name));

			if (idx == browser.sel) {
				gfx_fill(0, y, 236, BR_ROW, pal.sel_bg);
				gfx_fill(2, y, 3, BR_ROW, pal.accent);
			} else {
				gfx_hline(8, y + BR_ROW - 1, 226, pal.line);
			}

			if (browser.isdir[idx]) {
				draw_folder_icon(8, y + 3,
						 idx == browser.sel ? pal.instr :
						 pal.accent_dim);
				gfx_text_ellipsis(24, y + 3, 226, name,
						  idx == browser.sel ?
						  pal.bright : pal.text);
			} else {
				const char *ext = util_ext(browser.names[idx]);
				char fmt[8];
				Uint32 fc;

				util_upper(fmt, sizeof(fmt), ext);
				if (!strcmp(fmt, "XM"))
					fc = pal.note;
				else if (!strcmp(fmt, "IT"))
					fc = pal.instr;
				else if (!strcmp(fmt, "MOD"))
					fc = pal.vol;
				else if (!strcmp(fmt, "S3M"))
					fc = pal.fx;
				else
					fc = pal.accent;
				gfx_fill(9, y + 3, 3, 7, fc);
				gfx_text(15, y + 3, fmt, fc);
				gfx_text_ellipsis(43, y + 3, 226, name,
						  idx == browser.sel ?
						  pal.bright : pal.text);
				if (!strcmp(browser.paths[idx], player.track.path))
					gfx_fill(233, y + 5, 3, 3, pal.accent);
			}
		}
	}

	if (!browser.error && browser.mode == 1 && browser.count <= 1) {
		const char *a = "NO SUBFOLDERS";
		const char *b = "PRESS Y TO USE THIS FOLDER";
		gfx_text2((SW - gfx_text2_w(a)) / 2, 100, a, pal.dim);
		gfx_text((SW - gfx_text_w(b)) / 2, 124, b, pal.accent);
	}

	gfx_fill(0, BR_FOOT, SW, SH - BR_FOOT, pal.bg);
	gfx_hline(0, BR_FOOT, SW, pal.line);

	if (browser.mode == 1) {
		gfx_hint(6, BR_FOOT + 2, 'A', pal.btn_a, "OPEN", pal.text);
		gfx_hint(56, BR_FOOT + 2, 'B', pal.btn_b, "UP", pal.text);
		gfx_hint(96, BR_FOOT + 2, 'Y', pal.btn_y, "USE THIS FOLDER",
			 pal.text);
		gfx_text(6, BR_FOOT + 20, "NAVIGATE TO YOUR MODULE FOLDER",
			 pal.dim);
	} else {
		gfx_hint(6, BR_FOOT + 2, 'A', pal.btn_a, "PLAY", pal.text);
		gfx_hint(56, BR_FOOT + 2, 'B', pal.btn_b, "UP", pal.text);
		gfx_text(6, BR_FOOT + 20, "SUPPORTED: XM IT MOD S3M MO3",
			 pal.dim);
	}
	{
		char cnt[32];
		if (browser.count == 1)
			snprintf(cnt, sizeof(cnt), "1 ITEM");
		else
			snprintf(cnt, sizeof(cnt), "%d ITEMS", browser.count);
		gfx_text_right(234, BR_FOOT + 20, cnt, pal.accent_dim);
	}
}
