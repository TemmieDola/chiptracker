/*
 * main.c - CHIPTRACKER: a tracker-style chiptune player for the RG Nano.
 *
 * Screen 240x240, SDL 1.2, libopenmpt.  Menu exits, SIGUSR1 saves state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>

#include <SDL/SDL.h>
#include <libopenmpt/libopenmpt.h>

#include "app.h"
#include "gfx.h"

Config cfg;
Track tracks[MAX_TRACKS];
int track_count = 0;
PlayerState player;
LibState lib;
BrowserState browser;
SettingsState settings;
Screen screen = SCR_SPLASH;
int app_running = 1;
unsigned int app_ms = 0;

static volatile sig_atomic_t quit_requested = 0;
static unsigned int splash_start = 0;
static int splash_left = 0;
static char toast_msg[160];
static unsigned int toast_until = 0;
static int show_stats = 0;
static float fps = 30.0f;

#define SPLASH_MS 3000
#define HINTS_HIDE_PX 24

static unsigned int hints_until = 0;
static int hints_off = 0;

void ui_hints_show(void)
{
	hints_until = SDL_GetTicks() + 3500;
}

int ui_hints_offset(void)
{
	return hints_off;
}

static void ui_hints_tick(unsigned int now, unsigned int dt)
{
	int target = 0;
	int step;

	if (cfg.hints == 1) {
		hints_off = 0;
		return;
	}
	if (cfg.hints == 2) {
		hints_off = HINTS_HIDE_PX;
		return;
	}
	if ((int)(now - hints_until) >= 0)
		target = HINTS_HIDE_PX;

	if (dt > 100)
		dt = 100;
	step = (HINTS_HIDE_PX * (int)dt) / 250;
	if (step < 1)
		step = 1;
	if (hints_off < target) {
		hints_off += step;
		if (hints_off > target)
			hints_off = target;
	} else if (hints_off > target) {
		hints_off -= step + 1;
		if (hints_off < target)
			hints_off = target;
	}
}

static const SDLKey nav_keys[6] = { SDLK_u, SDLK_d, SDLK_l, SDLK_r, SDLK_m, SDLK_n };
static int nav_held[6];
static unsigned int nav_next[6];
static unsigned int start_down = 0;
static int start_pending = 0;
static unsigned int select_down = 0;
static int select_pending = 0;

static void handle_signal(int sig)
{
	(void)sig;
	quit_requested = 1;
}

void app_toast(const char *msg, int ms)
{
	util_copy(toast_msg, sizeof(toast_msg), msg ? msg : "");
	toast_until = SDL_GetTicks() + (unsigned int)ms;
}

void app_set_screen(Screen s)
{
	screen = s;
}

void app_start_scan(void)
{
	if (cfg.music_dir[0])
		scan_begin(cfg.music_dir, cfg.recursive);
}

static void draw_toast(void)
{
	unsigned int now = SDL_GetTicks();
	int w;
	char txt[160];

	if (!toast_msg[0] || now >= toast_until)
		return;
	util_ascii(toast_msg, txt, sizeof(txt));
	w = gfx_text_w(txt) + 16;
	if (w > 230)
		w = 230;
	gfx_fill((SW - w) / 2, 188, w, 17, pal.panel2);
	gfx_rect((SW - w) / 2, 188, w, 17, pal.accent);
	gfx_text_ellipsis((SW - w) / 2 + 8, 193, (SW + w) / 2 - 8, txt,
			  pal.text);
}

static void draw_stats(void)
{
	char line[64];
	int bat = util_battery();

	if (!show_stats)
		return;
	gfx_fill(4, 140, 168, 46, pal.panel2);
	gfx_rect(4, 140, 168, 46, pal.line2);
	snprintf(line, sizeof(line), "FPS %d  %dCH", (int)(fps + 0.5),
		 audio_channels());
	gfx_text(8, 144, line, pal.dim);
	snprintf(line, sizeof(line), "POS %.1f / %.1f", audio_position(),
		 audio_duration());
	gfx_text(8, 154, line, pal.dim);
	snprintf(line, sizeof(line), "LIB %d  SCAN %s", track_count,
		 scan.active ? "YES" : "NO");
	gfx_text(8, 164, line, pal.dim);
	if (bat < 0)
		util_copy(line, sizeof(line), "BAT N/A");
	else if (util_usb_present())
		snprintf(line, sizeof(line), "BAT %d%% USB", bat);
	else
		snprintf(line, sizeof(line), "BAT %d%%", bat);
	gfx_text(8, 174, line, pal.dim);
}

static Uint32 lerp_rgb(Uint32 a, Uint32 b, int t)
{
	Uint8 ar, ag, ab, br, bg, bb;
	int r, g, bl;

	if (t < 0)
		t = 0;
	if (t > 255)
		t = 255;
	SDL_GetRGB(a, gfx_screen->format, &ar, &ag, &ab);
	SDL_GetRGB(b, gfx_screen->format, &br, &bg, &bb);
	r = (int)ar + ((int)br - (int)ar) * t / 255;
	g = (int)ag + ((int)bg - (int)ag) * t / 255;
	bl = (int)ab + ((int)bb - (int)ab) * t / 255;
	return SDL_MapRGB(gfx_screen->format, (Uint8)r, (Uint8)g, (Uint8)bl);
}

/* "LOCKING..." fades in while the lock key is held. */
static void draw_locking(void)
{
	unsigned int now = SDL_GetTicks();
	unsigned int held = 0;
	int active = 0;
	int a;
	const char *msg = "LOCKING...";
	int x, cw;

	if (lock_active() || screen == SCR_SPLASH)
		return;
	if (cfg.lock_key == 2) {
		if (start_pending && select_pending) {
			held = start_down > select_down ? start_down :
				select_down;
			active = 1;
		}
	} else if (cfg.lock_key == 1) {
		if (select_pending) {
			held = select_down;
			active = 1;
		}
	} else if (start_pending) {
		held = start_down;
		active = 1;
	}
	if (!active || now - held < 90)
		return;
	a = (int)(now - held - 90) * 255 / 500;
	if (a > 255)
		a = 255;

	cw = gfx_text2_w(msg);
	x = (SW - cw) / 2;
	gfx_fill_blend(0, 96, SW, 36, pal.bg, a * 3 / 4);
	gfx_text2(x + 1, 106, msg, gfx_black());
	gfx_text2(x, 105, msg, lerp_rgb(pal.bg, pal.bright, a));
}

static void draw_current(void)
{
	SDL_LockSurface(gfx_screen);
	switch (screen) {
	case SCR_SPLASH:
		splash_draw();
		break;
	case SCR_PLAYER:
		player_draw();
		break;
	case SCR_LIBRARY:
		library_draw();
		break;
	case SCR_BROWSER:
		browser_draw();
		break;
	case SCR_INFO:
		info_draw();
		break;
	case SCR_SETTINGS:
		settings_draw();
		break;
	case SCR_VIS:
		vis_draw();
		break;
	case SCR_HELP:
		help_draw();
		break;
	}
	draw_toast();
	draw_stats();
	draw_locking();
	if (cfg.scanlines)
		gfx_scanlines();
	gfx_rect(0, 0, SW, SH, gfx_black());
	SDL_UnlockSurface(gfx_screen);
	SDL_Flip(gfx_screen);
}

static void leave_splash(void)
{
	if (splash_left)
		return;
	splash_left = 1;
	ui_hints_show();
	if (!cfg.music_dir[0]) {
		browser_open(util_isdir("/mnt/Audio") ? "/mnt/Audio" : "/mnt",
			     1, 1);
		return;
	}
	if (cfg.start_player && cfg.last_valid && cfg.last_path[0] &&
	    util_isfile(cfg.last_path)) {
		player_load_last();
		if (player.loaded) {
			screen = SCR_PLAYER;
			if (getenv("CHIP_AUTOPLAY"))
				player_toggle();
			return;
		}
	}
	screen = SCR_LIBRARY;
	if (cfg.last_valid && cfg.last_path[0]) {
		int idx = library_index_of(cfg.last_path);
		if (idx >= 0) {
			lib.sel = idx;
			lib.top = idx > 7 ? idx - 7 : 0;
		}
	}
}

static void save_state(void)
{
	if (player.loaded && audio_have_track()) {
		double p = audio_position();
		if (p > 0) {
			cfg.last_pos = p;
			util_copy(cfg.last_path, sizeof(cfg.last_path),
				  player.track.path);
			cfg.last_valid = 1;
		}
	}
	config_save();
}

static void handle_key(SDLKey k)
{
	if (screen == SCR_SPLASH) {
		leave_splash();
		return;
	}
	ui_hints_show();
	if (k == SDLK_q || k == SDLK_ESCAPE) {
		quit_requested = 1;
		return;
	}
	if (k == SDLK_k) {
		show_stats = !show_stats;
		return;
	}
	switch (screen) {
	case SCR_PLAYER:
		player_key(k);
		break;
	case SCR_LIBRARY:
		library_key(k);
		break;
	case SCR_BROWSER:
		browser_key(k);
		break;
	case SCR_INFO:
		info_key(k);
		break;
	case SCR_SETTINGS:
		settings_key(k);
		break;
	case SCR_VIS:
		vis_key(k);
		break;
	case SCR_HELP:
		help_key(k);
		break;
	default:
		break;
	}
}

static void handle_nav_repeat(unsigned int now)
{
	Uint8 *ks = SDL_GetKeyState(NULL);
	int i;

	if (screen == SCR_SPLASH)
		return;
	for (i = 0; i < 6; i++) {
		if (ks[nav_keys[i]]) {
			if (!nav_held[i]) {
				/* the initial press is handled by the event loop */
				nav_held[i] = 1;
				nav_next[i] = now + 330;
			} else if ((int)(now - nav_next[i]) >= 0) {
				nav_next[i] = now + 70;
				handle_key(nav_keys[i]);
			}
		} else {
			nav_held[i] = 0;
			nav_next[i] = 0;
		}
	}
}

static int nav_index(SDLKey k)
{
	int i;

	for (i = 0; i < 6; i++)
		if (nav_keys[i] == k)
			return i;
	return -1;
}

/* ---------------------------------------------------------------- bench --- */

static int bench_render(const char *path, int seconds)
{
	unsigned char *data;
	size_t size;
	openmpt_module *m;
	int16_t buf[4096];
	size_t total = 0, target;
	unsigned int t0, t1;
	double ratio;
	int ch;
	int peak = 0;

	if (util_read_file(path, &data, &size) != 0) {
		fprintf(stderr, "bench: cannot read %s\n", path);
		return 1;
	}
	m = openmpt_module_create_from_memory2(data, size, NULL, NULL, NULL,
					       NULL, NULL, NULL, NULL);
	free(data);
	if (!m) {
		fprintf(stderr, "bench: cannot load %s\n", path);
		return 1;
	}
	openmpt_module_set_render_param(m,
		OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, 4);
	openmpt_module_set_render_param(m,
		OPENMPT_MODULE_RENDER_VOLUMERAMPING_STRENGTH, 5);
	ch = openmpt_module_get_num_channels(m);
	target = (size_t)seconds * 44100;
	t0 = SDL_GetTicks();
	while (total < target) {
		size_t got = openmpt_module_read_mono(m, 44100, 4096, buf);
		size_t i;
		if (got == 0)
			break;
		for (i = 0; i < got; i++) {
			int a = buf[i] < 0 ? -buf[i] : buf[i];
			if (a > peak)
				peak = a;
		}
		total += got;
	}
	t1 = SDL_GetTicks();
	openmpt_module_destroy(m);
	ratio = (t1 > t0) ? ((double)total / 44100.0) / ((t1 - t0) / 1000.0) : 0.0;
	fprintf(stderr, "bench: %dCH  %.2fs audio in %ums  realtime x%.2f  peak %d\n",
		ch, total / 44100.0, t1 - t0, ratio, peak);
	return 0;
}

/* ---------------------------------------------------------------- test --- */

static int test_all_modules(const char *dir)
{
	FILE *out = stderr;
	int i;
	int ok = 0, fail = 0;

	scan_begin(dir, 1);
	while (!scan_poll(120))
		;
	fprintf(out, "== %d files\n", track_count);
	for (i = 0; i < track_count; i++) {
		unsigned char *data;
		size_t size;
		openmpt_module *m;
		int ch, ord;
		double dur;
		int16_t buf[4096];
		size_t total = 0;

		if (util_read_file(tracks[i].path, &data, &size) != 0) {
			fprintf(out, "FAIL read   %s\n", tracks[i].file);
			fail++;
			continue;
		}
		m = openmpt_module_create_from_memory2(data, size, NULL, NULL,
						       NULL, NULL, NULL, NULL, NULL);
		free(data);
		if (!m) {
			fprintf(out, "FAIL load   %s\n", tracks[i].file);
			fail++;
			continue;
		}
		ch = openmpt_module_get_num_channels(m);
		ord = openmpt_module_get_num_orders(m);
		dur = openmpt_module_get_duration_seconds(m);
		{
			size_t got;
			while (total < 22050) {
				got = openmpt_module_read_mono(m, 44100, 4096, buf);
				if (got == 0)
					break;
				total += got;
			}
		}
		if (total == 0) {
			fprintf(out, "FAIL audio  %s\n", tracks[i].file);
			fail++;
		} else {
			fprintf(out, "OK %-5s %2dCH %3dORD %5.1fs  %s\n",
				tracks[i].fmt, ch, ord, dur, tracks[i].file);
			ok++;
		}
		openmpt_module_destroy(m);
	}
	fprintf(out, "== summary: %d ok, %d failed\n", ok, fail);
	return fail ? 1 : 0;
}

/* ------------------------------------------------------------- preview --- */

static void preview_setup(void)
{
	const char *mdir = getenv("CHIP_MUSIC_DIR");
	const char *trk = getenv("CHIP_TRACK");
	const char *scr = getenv("CHIP_SCREEN");
	const char *theme = getenv("CHIP_THEME");
	const char *pos = getenv("CHIP_POS");
	const char *order = getenv("CHIP_ORDER");
	const char *tab = getenv("CHIP_TAB");
	const char *vstyle = getenv("CHIP_VIS_STYLE");
	const char *hints = getenv("CHIP_HINTS");
	int need_track = 0;

	if (mdir && *mdir)
		util_copy(cfg.music_dir, sizeof(cfg.music_dir), mdir);
	if (hints && *hints) {
		cfg.hints = atoi(hints);
		if (cfg.hints == 2)
			hints_off = HINTS_HIDE_PX;
	}
	if (theme && *theme) {
		int i;
		for (i = 0; i < THEME_COUNT; i++)
			if (!strcasecmp(theme, themes[i].name))
				cfg.theme = i;
	}
	if (vstyle && *vstyle) {
		cfg.vis_style = atoi(vstyle);
		if (cfg.vis_style < 0 || cfg.vis_style >= VIS_STYLE_COUNT)
			cfg.vis_style = 0;
	}
	if (getenv("CHIP_VIS_P")) {
		cfg.vis_p[cfg.vis_style] = atoi(getenv("CHIP_VIS_P"));
		if (cfg.vis_p[cfg.vis_style] < 0)
			cfg.vis_p[cfg.vis_style] = 0;
	}
	if (getenv("CHIP_VIS_P2")) {
		cfg.vis_p2[cfg.vis_style] = atoi(getenv("CHIP_VIS_P2"));
		if (cfg.vis_p2[cfg.vis_style] < 0)
			cfg.vis_p2[cfg.vis_style] = 0;
	}
	if (getenv("CHIP_LOCKKEY"))
		cfg.lock_key = atoi(getenv("CHIP_LOCKKEY"));
	if (getenv("CHIP_STATS"))
		show_stats = 1;
	if (getenv("CHIP_BOTTOM_VIS"))
		cfg.vis = atoi(getenv("CHIP_BOTTOM_VIS"));
	if (getenv("CHIP_LOCKING_PREVIEW")) {
		cfg.lock_key = 0;
		start_pending = 1;
		start_down = SDL_GetTicks() - 380;
	}
	if (!scr || !*scr)
		scr = "player";
	if (!strcmp(scr, "library") || !strcmp(scr, "info") ||
	    !strcmp(scr, "player") || !strcmp(scr, "settings") ||
	    !strcmp(scr, "vis"))
		need_track = (!strcmp(scr, "player") || !strcmp(scr, "info") ||
			      !strcmp(scr, "vis") ||
			      (!strcmp(scr, "library") && trk && *trk));

	if (cfg.music_dir[0]) {
		scan_begin(cfg.music_dir, cfg.recursive);
		while (!scan_poll(200))
			;
	}

	if (need_track) {
		if (trk && *trk) {
			const char *slash = strrchr(trk, '/');
			const char *base = slash ? slash + 1 : trk;
			int idx = -1;
			int i;
			for (i = 0; i < track_count; i++) {
				if (!strcmp(tracks[i].file, base) ||
				    !strcmp(tracks[i].stem, trk)) {
					idx = i;
					break;
				}
			}
			if (idx >= 0)
				player_start_index(idx, 1);
			else
				player_start_path(trk, 1);
		} else if (track_count > 0) {
			player_start_index(0, 1);
		}
		if (pos && *pos)
			audio_seek_seconds(atof(pos));
		if (order && *order)
			audio_seek_order(atoi(order));
		player_update(33);
		if ((!strcmp(scr, "player") || !strcmp(scr, "vis")) &&
		    player.loaded) {
			audio_set_paused(0);
			SDL_Delay(500);
			if (!strcmp(scr, "player"))
				player_update(33);
		}
		{
			const char *page = getenv("CHIP_PAGE");
			if (page && *page && player.loaded) {
				player_draw();
				player_set_volume_page(atoi(page));
				if (cfg.page_slide)
					player.page_anim_start =
						SDL_GetTicks() - 90;
			}
		}
	}

	if (!strcmp(scr, "library")) {
		screen = SCR_LIBRARY;
		if (track_count > 0) {
			const char *ls = getenv("CHIP_LIB_SEL");
			lib.sel = ls && *ls ? atoi(ls) : 3;
			if (player.loaded) {
				int idx = library_index_of(player.track.path);
				if (idx >= 0)
					lib.sel = idx;
			}
			if (lib.sel >= track_count)
				lib.sel = track_count - 1;
			lib.top = lib.sel > 7 ? lib.sel - 7 : 0;
			probe_track(&tracks[lib.sel]);
			library_update();
		}
	} else if (!strcmp(scr, "settings")) {
		screen = SCR_SETTINGS;
		settings.sel = getenv("CHIP_SET_SEL") ?
			atoi(getenv("CHIP_SET_SEL")) : 9;	/* theme */
		settings_update();
		if (getenv("CHIP_SET_DESC"))
			settings_desc_preview();
	} else if (!strcmp(scr, "browser") || !strcmp(scr, "firstrun")) {
		int first = !strcmp(scr, "firstrun");
		browser_open(cfg.music_dir[0] ? cfg.music_dir :
			     (util_isdir("/mnt/Audio") ? "/mnt/Audio" : "/mnt"),
			     first ? 1 : 0, first);
	} else if (!strcmp(scr, "info")) {
		screen = SCR_INFO;
		player.info_back = SCR_PLAYER;
		player.info_tab = tab && atoi(tab) ? 1 : 0;
	} else if (!strcmp(scr, "vis")) {
		int i;
		vis_enter();
		for (i = 0; i < 25; i++) {
			SDL_Delay(12);
			vis_update(33);
		}
		screen = SCR_VIS;
	} else if (!strcmp(scr, "help")) {
		screen = SCR_HELP;
	} else if (!strcmp(scr, "splash")) {
		screen = SCR_SPLASH;
	} else {
		screen = player.loaded ? SCR_PLAYER : SCR_LIBRARY;
	}
	if (!strcmp(scr, "player") && player.loaded)
		screen = SCR_PLAYER;
}

/* ----------------------------------------------------------------- main --- */

int main(int argc, char **argv)
{
	const char *shot = getenv("CHIP_SHOT");
	const char *testall = getenv("CHIP_TEST_ALL");
	unsigned int last_frame = 0;
	unsigned int last_tick = 0;
	SDL_Event ev;

	(void)argc;
	(void)argv;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGUSR1, handle_signal);

	config_load();
	if (getenv("CHIP_MUSIC_DIR") && *getenv("CHIP_MUSIC_DIR"))
		util_copy(cfg.music_dir, sizeof(cfg.music_dir),
			  getenv("CHIP_MUSIC_DIR"));
	if (getenv("CHIP_LOCKKEY"))
		cfg.lock_key = atoi(getenv("CHIP_LOCKKEY"));

	if (gfx_init() != 0)
		return 1;
	gfx_theme(cfg.theme);
	srand((unsigned int)SDL_GetTicks());

	if (getenv("CHIP_BENCH")) {
		const char *f = getenv("CHIP_BENCH_FILE");
		int sec = atoi(getenv("CHIP_BENCH"));
		int rc;

		if (sec <= 0)
			sec = 20;
		if (!f || !*f)
			f = cfg.music_dir;
		rc = bench_render(f, sec);
		gfx_quit();
		return rc;
	}

	if (testall) {
		int rc = test_all_modules(cfg.music_dir[0] ? cfg.music_dir :
					  (getenv("CHIP_MUSIC_DIR") ?
					   getenv("CHIP_MUSIC_DIR") : "."));
		gfx_quit();
		return rc;
	}

	if (audio_open() != 0)
		fprintf(stderr, "audio: %s\n", audio_error());

	if (getenv("CHIP_LOCK_TEST")) {
		lock_enter();
		if (shot) {
			lock_preview();
			gfx_dump_ppm(shot);
		} else {
			SDL_Delay(200);
		}
		lock_restore();
		gfx_quit();
		return 0;
	}

	if (getenv("CHIP_SWAP_TEST")) {
		int i;

		if (cfg.music_dir[0]) {
			scan_begin(cfg.music_dir, cfg.recursive);
			while (!scan_poll(200))
				;
		}
		if (track_count > 0)
			player_start_index(0, 0);
		for (i = 0; i < 5 && track_count > 1 && player.loaded; i++) {
			SDL_Delay(350);
			player_next(1);
			draw_current();
			fprintf(stderr, "swap %d: %s\n", i + 1,
				player.track.file);
		}
		audio_close();
		gfx_quit();
		return 0;
	}

	if (shot) {
		preview_setup();
		draw_current();
		gfx_dump_ppm(shot);
		audio_close();
		gfx_quit();
		return 0;
	}

	memset(&player, 0, sizeof(player));
	memset(&settings, 0, sizeof(settings));
	splash_start = SDL_GetTicks();
	last_tick = splash_start;
	last_frame = splash_start;

	if (cfg.music_dir[0])
		scan_begin(cfg.music_dir, cfg.recursive);
	if (!cfg.show_splash)
		leave_splash();

	while (!quit_requested) {
		unsigned int now = SDL_GetTicks();
		unsigned int dt = now - last_tick;

		last_tick = now;
		app_ms = now;

		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				quit_requested = 1;
				continue;
			}
			if (lock_active()) {
				lock_event(&ev);
				continue;
			}
			if (ev.type == SDL_KEYDOWN) {
				SDLKey k = ev.key.keysym.sym;
				if (screen == SCR_SPLASH) {
					leave_splash();
				} else if (k == SDLK_s) {
					start_down = now;
					start_pending = 1;
				} else if (k == SDLK_k) {
					select_down = now;
					select_pending = 1;
				} else {
					int ni;

					handle_key(k);
					ni = nav_index(k);
					if (ni >= 0) {
						nav_held[ni] = 1;
						nav_next[ni] = now + 330;
					}
				}
			} else if (ev.type == SDL_KEYUP) {
				SDLKey k = ev.key.keysym.sym;

				if (k == SDLK_s && start_pending) {
					start_pending = 0;
					handle_key(SDLK_s);
				} else if (k == SDLK_k && select_pending) {
					select_pending = 0;
					handle_key(SDLK_k);
				}
			}
		}

		if (!lock_active()) {
			if (cfg.lock_key == 2) {
				/* Both buttons must be held together. */
				if (start_pending && select_pending) {
					unsigned int t0 = start_down > select_down ?
						start_down : select_down;

					if (now - t0 >= 700) {
						start_pending = 0;
						select_pending = 0;
						lock_enter();
					}
				}
			} else if (cfg.lock_key == 1) {
				if (select_pending && now - select_down >= 700) {
					select_pending = 0;
					lock_enter();
				}
			} else {
				if (start_pending && now - start_down >= 700) {
					start_pending = 0;
					lock_enter();
				}
			}
		}

		if (lock_active()) {
			audio_tick();
			if (audio_take_ended())
				player_on_end();
			if (scan.active)
				scan_poll(5);
			lock_tick();
			if (lock_active()) {
				SDL_Delay(30);
				last_tick = SDL_GetTicks();
				continue;
			}
		}

		if (screen == SCR_SPLASH && now - splash_start > SPLASH_MS)
			leave_splash();

		handle_nav_repeat(now);
		ui_hints_tick(now, dt);

		if (scan.active)
			scan_poll(5);

		audio_tick();
		if (audio_take_ended())
			player_on_end();

		switch (screen) {
		case SCR_PLAYER:
			player_update(dt);
			break;
		case SCR_LIBRARY:
			library_update();
			break;
		case SCR_SETTINGS:
			settings_update();
			break;
		case SCR_INFO:
			info_update();
			break;
		case SCR_VIS:
			vis_update(dt);
			break;
		default:
			break;
		}

		draw_current();

		if (now - last_frame > 0) {
			fps = fps * 0.9f + (1000.0f / (float)(now - last_frame)) * 0.1f;
			last_frame = now;
		}

		{
			unsigned int frame_ms = 1000u / (unsigned int)
				(cfg.fps > 0 ? cfg.fps : 30);
			unsigned int elapsed = SDL_GetTicks() - now;
			if (elapsed < frame_ms)
				SDL_Delay(frame_ms - elapsed);
		}
	}

	if (lock_active())
		lock_restore();
	save_state();
	audio_close();
	gfx_quit();
	return 0;
}
