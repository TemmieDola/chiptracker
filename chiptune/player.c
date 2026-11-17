/*
 * player.c - the tracker screen: pattern scroller, meters, transport.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "app.h"
#include "gfx.h"

#define CACHE_MAX 1024

static char cells[CACHE_MAX][3][16];
static char old_cells[CACHE_MAX][3][16];
static int old_rows = 0;
static int old_page = 0;
static int cache_pat = -999;
static int cache_ch0 = -1;
static int cache_rows = 0;
static float vis_pos = 0.0f;
static int last_order = -1;
static int last_pattern = -1;
static unsigned int last_save = 0;
static unsigned int ctx_gen = 0;

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

static void sanitize(char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 32)
			*s = ' ';
}

static void set_titles(void)
{
	char *t = audio_meta("title");
	char *tr = audio_meta("tracker");
	char tmp[256];

	if (t && *t) {
		util_ascii(t, tmp, sizeof(tmp));
		util_copy(player.title, sizeof(player.title), tmp);
	} else {
		util_ascii(player.track.stem, player.title, sizeof(player.title));
	}
	if (tr && *tr && strcasecmp(tr, "unknown") != 0) {
		util_ascii(tr, player.tracker, sizeof(player.tracker));
	} else {
		util_copy(player.tracker, sizeof(player.tracker), audio_format_name());
	}
	sanitize(player.title);
	sanitize(player.tracker);
	if (t)
		free(t);
	if (tr)
		free(tr);
}

static void build_context(int idx)
{
	int i;

	ctx_gen = lib_generation;
	player.ctx_count = 0;
	player.ctx_pos = 0;
	if (track_count <= 0)
		return;
	for (i = 0; i < track_count; i++)
		player.ctx[i] = i;
	player.ctx_count = track_count;
	for (i = 0; i < track_count; i++) {
		if (player.ctx[i] == idx) {
			player.ctx_pos = i;
			break;
		}
	}
}

static void ctx_check(void)
{
	if (ctx_gen == lib_generation)
		return;
	ctx_gen = lib_generation;
	if (!player.loaded || track_count <= 0) {
		player.ctx_count = 0;
		return;
	}
	{
		int idx = library_index_of(player.track.path);

		if (idx >= 0)
			build_context(idx);
		else
			player.ctx_count = 0;
	}
}

/* ------------------------------------------------ shuffle history --- */
#define SHUF_HIST 256

static int shuf_hist[SHUF_HIST];
static int shuf_len = 0;
static int shuf_pos = -1;
static int shuf_blast = -1;
static int shuf_bag[MAX_TRACKS];
static int shuf_bag_len = 0;
static int shuf_bag_pos = 0;
static int shuf_cycle_done = 0;
static unsigned int shuf_gen = 0;
static int shuf_on = 0;

static int current_library_index(void)
{
	return player.loaded ? library_index_of(player.track.path) : -1;
}

static void shuffle_reset(int idx)
{
	shuf_gen = lib_generation;
	shuf_bag_len = 0;
	shuf_bag_pos = 0;
	shuf_cycle_done = 0;
	shuf_len = 0;
	shuf_pos = -1;
	shuf_blast = idx;
	if (idx >= 0) {
		shuf_hist[0] = idx;
		shuf_len = 1;
		shuf_pos = 0;
	}
}

static void shuffle_check(void)
{
	if (cfg.shuffle != shuf_on) {
		shuf_on = cfg.shuffle;
		shuffle_reset(cfg.shuffle ? current_library_index() : -1);
		return;
	}
	if (shuf_gen != lib_generation)
		shuffle_reset(cfg.shuffle ? current_library_index() : -1);
}

/* Random next with a no-repeat bag; going forward after "previous" replays
 * the actual next track from the history. */
static int shuffle_next(int auto_end)
{
	int i, n;

	shuffle_check();
	if (shuf_pos + 1 < shuf_len) {
		shuf_pos++;
		shuf_blast = shuf_hist[shuf_pos];
		return shuf_hist[shuf_pos];
	}
	if (track_count <= 0)
		return -1;
	if (shuf_bag_pos >= shuf_bag_len) {
		if (auto_end && cfg.repeat == REPEAT_OFF && shuf_cycle_done)
			return -1;
		n = 0;
		for (i = 0; i < track_count; i++)
			if (i != shuf_blast)
				shuf_bag[n++] = i;
		if (n == 0) {
			shuf_bag[0] = 0;
			n = 1;
		}
		for (i = n - 1; i > 0; i--) {
			int j = rand() % (i + 1);
			int t = shuf_bag[i];

			shuf_bag[i] = shuf_bag[j];
			shuf_bag[j] = t;
		}
		shuf_bag_len = n;
		shuf_bag_pos = 0;
		shuf_cycle_done = 0;
	}
	i = shuf_bag[shuf_bag_pos++];
	if (shuf_bag_pos >= shuf_bag_len)
		shuf_cycle_done = 1;
	if (shuf_len >= SHUF_HIST) {
		memmove(shuf_hist, shuf_hist + 1,
			(size_t)(SHUF_HIST - 1) * sizeof(int));
		shuf_len = SHUF_HIST - 1;
	}
	shuf_hist[shuf_len++] = i;
	shuf_pos = shuf_len - 1;
	shuf_blast = i;
	return i;
}

static int shuffle_prev(void)
{
	shuffle_check();
	if (shuf_pos > 0) {
		shuf_pos--;
		shuf_blast = shuf_hist[shuf_pos];
		return shuf_hist[shuf_pos];
	}
	return -1;
}

static void shuffle_note_start(int idx)
{
	if (!cfg.shuffle) {
		shuf_on = 0;
		return;
	}
	shuffle_check();
	if (shuf_pos >= 0 && shuf_pos < shuf_len &&
	    shuf_hist[shuf_pos] == idx)
		return;
	shuffle_reset(idx);
}

void player_start_index(int idx, int paused)
{
	Track *t;
	double seek = 0.0;

	if (idx < 0 || idx >= track_count)
		return;
	t = &tracks[idx];
	if (player.loaded && !strcmp(player.track.path, t->path) &&
	    !paused && audio_have_track()) {
		double pos = audio_position();
		double dur = audio_duration();

		if (dur > 1.0 && pos > dur - 0.7)
			audio_seek_seconds(0.0);
		audio_set_paused(0);
		player.paused = 0;
		if (screen == SCR_SPLASH)
			screen = SCR_PLAYER;
		return;
	}
	if (paused && cfg.resume && cfg.last_valid &&
	    !strcmp(cfg.last_path, t->path) && cfg.last_pos > 0.5)
		seek = cfg.last_pos;

	if (audio_load(t->path, paused, seek) != 0) {
		app_toast(audio_error(), 2800);
		return;
	}
	player.track = *t;
	player.loaded = 1;
	player.paused = paused;
	player.page = 0;
	player.info_scroll = 0;
	player.marquee_ms = 0;
	player.marquee_off = 0;
	vis_pos = 0.0f;
	last_order = -1;
	last_pattern = -1;
	cache_pat = -999;
	cache_ch0 = -1;
	spectrum_reset();
	ui_hints_show();

	t->channels = audio_channels();
	t->orders = audio_orders();
	t->duration = (int)(audio_duration() + 0.5);
	t->meta = 1;
	player.track = *t;

	set_titles();
	build_context(idx);
	shuffle_note_start(idx);

	util_copy(cfg.last_path, sizeof(cfg.last_path), player.track.path);
	cfg.last_pos = seek;
	cfg.last_valid = 1;

	if (screen == SCR_SPLASH)
		screen = SCR_PLAYER;
}

void player_start_path(const char *path, int paused)
{
	int idx = library_index_of(path);
	double seek = 0.0;

	if (idx >= 0) {
		player_start_index(idx, paused);
		if (player.loaded)
			screen = SCR_PLAYER;
		return;
	}
	if (paused && cfg.resume && cfg.last_valid &&
	    !strcmp(cfg.last_path, path) && cfg.last_pos > 0.5)
		seek = cfg.last_pos;
	if (audio_load(path, paused, seek) != 0) {
		app_toast(audio_error(), 2800);
		return;
	}
	memset(&player.track, 0, sizeof(player.track));
	util_copy(player.track.path, sizeof(player.track.path), path);
	util_copy(player.track.file, sizeof(player.track.file), util_basename(path));
	util_stem(player.track.stem, sizeof(player.track.stem), util_basename(path));
	util_upper(player.track.fmt, sizeof(player.track.fmt), util_ext(path));
	player.track.channels = audio_channels();
	player.track.orders = audio_orders();
	player.track.duration = (int)(audio_duration() + 0.5);
	player.track.meta = 1;
	{
		struct stat st;
		if (stat(path, &st) == 0)
			player.track.size = (unsigned int)st.st_size;
	}
	player.loaded = 1;
	player.paused = paused;
	player.page = 0;
	player.ctx_count = 0;
	player.ctx_pos = 0;
	player.marquee_ms = 0;
	player.marquee_off = 0;
	vis_pos = 0.0f;
	last_order = -1;
	last_pattern = -1;
	cache_pat = -999;
	cache_ch0 = -1;
	spectrum_reset();
	ui_hints_show();
	set_titles();
	shuffle_note_start(-1);
	util_copy(cfg.last_path, sizeof(cfg.last_path), path);
	cfg.last_pos = seek;
	cfg.last_valid = 1;
	screen = SCR_PLAYER;
}

void player_load_last(void)
{
	if (cfg.last_valid && cfg.last_path[0] && util_isfile(cfg.last_path)) {
		int idx = library_index_of(cfg.last_path);
		if (idx >= 0)
			player_start_index(idx, 1);
		else
			player_start_path(cfg.last_path, 1);
	}
}

void player_next(int dir)
{
	int p;

	if (!player.loaded)
		return;
	if (cfg.shuffle) {
		int idx = dir > 0 ? shuffle_next(0) : shuffle_prev();

		if (idx >= 0)
			player_start_index(idx, 0);
		return;
	}
	ctx_check();
	if (player.ctx_count <= 0)
		return;
	p = player.ctx_pos + dir;
	if (p < 0) {
		if (cfg.repeat == REPEAT_ALL)
			p = player.ctx_count - 1;
		else
			p = 0;
	}
	if (p >= player.ctx_count) {
		if (cfg.repeat == REPEAT_ALL)
			p = 0;
		else {
			audio_set_paused(1);
			player.paused = 1;
			return;
		}
	}
	player.ctx_pos = p;
	player_start_index(player.ctx[p], 0);
}

void player_toggle(void)
{
	double pos, dur;

	if (!player.loaded || !audio_have_track())
		return;
	ui_hints_show();
	if (!audio_available()) {
		app_toast("NO AUDIO DEVICE", 2200);
		return;
	}
	pos = audio_position();
	dur = audio_duration();
	if (audio_paused()) {
		if (dur > 1.0 && pos > dur - 0.7)
			audio_seek_seconds(0.0);
		audio_set_paused(0);
		player.paused = 0;
	} else {
		audio_set_paused(1);
		player.paused = 1;
		cfg.last_pos = pos;
		config_save();
	}
}

void player_on_end(void)
{
	ui_hints_show();
	if (cfg.repeat == REPEAT_ONE) {
		audio_seek_seconds(0.0);
		audio_set_paused(0);
		player.paused = 0;
		return;
	}
	if (cfg.shuffle) {
		int idx = shuffle_next(1);

		if (idx < 0) {
			player.paused = 1;
			cfg.last_pos = 0.0;
			return;
		}
		player_start_index(idx, 0);
		return;
	}
	ctx_check();
	if (player.ctx_count > 0 && player.ctx_pos + 1 < player.ctx_count) {
		player.ctx_pos++;
		player_start_index(player.ctx[player.ctx_pos], 0);
		return;
	}
	if (cfg.repeat == REPEAT_ALL && player.ctx_count > 0) {
		player.ctx_pos = 0;
		player_start_index(player.ctx[0], 0);
		return;
	}
	player.paused = 1;
	cfg.last_pos = 0.0;
}

static void copy_old_page(void)
{
	int rows = cache_rows;

	if (rows > CACHE_MAX)
		rows = CACHE_MAX;
	if (rows > 0 && cache_ch0 >= 0) {
		memcpy(old_cells, cells, (size_t)rows * 3 * 16);
		old_rows = rows;
		old_page = cache_ch0 / 3;
	} else {
		old_rows = 0;
	}
}

void player_set_volume_page(int page)
{
	int pages;
	int old;

	if (!player.loaded)
		return;
	pages = (audio_channels() + 2) / 3;
	if (pages < 1)
		pages = 1;
	if (page < 0)
		page = 0;
	if (page >= pages)
		page = pages - 1;
	old = player.page;
	if (page == old)
		return;
	if (cfg.page_slide) {
		copy_old_page();
		player.page_anim_start = SDL_GetTicks();
		player.page_anim_dir = page > old ? 1 : -1;
	} else {
		player.page_anim_start = 0;
	}
	player.page = page;
	cache_ch0 = -1;
}

void player_update(unsigned int dt_ms)
{
	AudioSnap s;
	double d;
	int len;

	audio_snapshot(&s);
	if (s.valid) {
		if (player.loaded && track_count > 0) {
			int cur = library_index_of(player.track.path);
			if (cur >= 0 &&
			    (player.ctx_count == 0 ||
			     player.ctx_pos < 0 ||
			     player.ctx_pos >= player.ctx_count ||
			     player.ctx[player.ctx_pos] != cur))
				build_context(cur);
		}
		if (s.order != last_order || s.pattern != last_pattern) {
			last_order = s.order;
			last_pattern = s.pattern;
			vis_pos = (float)s.row;
		} else if (!cfg.smooth) {
			vis_pos = (float)s.row;
		} else {
			d = s.row - (double)vis_pos;
			if (d > 7.0 || d < -7.0) {
				vis_pos = (float)s.row;
			} else {
				float f = 1.0f - expf(-(float)dt_ms / 55.0f);
				if (f < 0.05f)
					f = 0.05f;
				if (f > 0.95f)
					f = 0.95f;
				vis_pos += (float)(d * f);
				if (fabs(vis_pos - s.row) < 0.03f)
					vis_pos = (float)s.row;
			}
		}
		player.vis_row = (int)vis_pos;
		player.vis_frac = vis_pos - (float)player.vis_row;
		player.pos = s.pos;
		if (s.playing && SDL_GetTicks() - last_save > 5000) {
			last_save = SDL_GetTicks();
			cfg.last_pos = s.pos;
			util_copy(cfg.last_path, sizeof(cfg.last_path), player.track.path);
			cfg.last_valid = 1;
		}
	}

	len = (int)strlen(player.title);
	if (len > 19) {
		player.marquee_ms += dt_ms;
		if (player.marquee_ms >= 190) {
			player.marquee_ms = 0;
			player.marquee_off++;
			if (player.marquee_off > len - 19 + 5)
				player.marquee_off = 0;
		}
	} else {
		player.marquee_off = 0;
	}
}

static void refresh_cache(AudioSnap *s)
{
	int pat = s->pattern;
	int ch0 = player.page * 3;
	int rows, r, c;

	if (pat < 0)
		pat = 0;
	if (pat == cache_pat && ch0 == cache_ch0)
		return;
	cache_pat = pat;
	cache_ch0 = ch0;
	rows = audio_pattern_rows(pat);
	if (rows <= 0)
		rows = 64;
	if (rows > CACHE_MAX)
		rows = CACHE_MAX;
	cache_rows = rows;

	for (r = 0; r < rows; r++) {
		for (c = 0; c < 3; c++) {
			char buf[32];
			if (!audio_format_cell(pat, r, ch0 + c, buf, sizeof(buf)))
				util_copy(cells[r][c], sizeof(cells[r][c]),
					  "... ..v.. ...");
			else
				util_copy(cells[r][c], sizeof(cells[r][c]), buf);
		}
	}
}

static void draw_field_clip(int x, int y, const char *s, int len, Uint32 c,
			    int x0, int x1)
{
	int i;

	for (i = 0; i < len; i++) {
		int gx = x + i * 6;
		if (gx >= x0 && gx + 5 <= x1)
			gfx_glyph(gx, y, (unsigned char)s[i], c, 1);
	}
}

static void draw_cell_clip(int x, int y, const char *cell, int x0, int x1)
{
	int note_empty = (cell[0] == '.' && cell[1] == '.' && cell[2] == '.');
	int ins_empty = (cell[4] == '.' && cell[5] == '.');
	int fx_empty = (cell[10] == '.' && cell[11] == '.' && cell[12] == '.');
	Uint32 cn = note_empty ? pal.dim : pal.note;
	Uint32 ci = ins_empty ? pal.dim : pal.instr;
	Uint32 cf = fx_empty ? pal.dim : pal.fx;

	if (!note_empty && (cell[0] == '^' || cell[0] == '='))
		cn = pal.accent_dim;
	draw_field_clip(x, y, cell, 3, cn, x0, x1);
	draw_field_clip(x + 24, y, cell + 4, 2, ci, x0, x1);
	draw_field_clip(x + 42, y, cell + 10, 3, cf, x0, x1);
}

static void draw_meter(int x, float level, int h)
{
	int fh;
	Uint32 c;

	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;
	gfx_fill(x, PAT_TOP, 4, h, pal.line);
	fh = (int)(level * h);
	if (fh > 0) {
		if (level > 0.88f)
			c = pal.fx;
		else if (level > 0.68f)
			c = pal.instr;
		else
			c = pal.accent;
		gfx_fill(x, PAT_TOP + h - fh, 4, fh, c);
		gfx_hline(x, PAT_TOP + h - fh, 4, pal.text);
	}
}

static void draw_orders(AudioSnap *s)
{
	int n = s->orders;
	int x0 = 8, x1 = 232, w = x1 - x0;
	int i;

	if (n < 1) {
		gfx_hline(x0, ORD_TOP + 5, w, pal.line);
		return;
	}
	gfx_hline(x0, ORD_TOP + 8, w, pal.line);
	if (n <= 56) {
		int step = w / n;
		if (step < 3)
			step = 3;
		for (i = 0; i < n; i++) {
			int x = x0 + i * step;
			Uint32 c = i == s->order ? pal.accent :
				   (i < s->order ? pal.accent_dim : pal.line2);
			int h = i == s->order ? 7 : 4;
			if (x + 2 > x1)
				break;
			gfx_fill(x, ORD_TOP + 8 - h + 1, 2, h, c);
		}
	} else {
		int fw = (int)((float)s->order / (float)n * w);
		gfx_fill(x0, ORD_TOP + 4, fw, 4, pal.accent_dim);
		gfx_fill(x0 + fw, ORD_TOP + 2, 2, 7, pal.accent);
	}
}

static void draw_play_icon(int x, int y, int playing)
{
	if (playing) {
		gfx_fill(x + 1, y + 1, 1, 5, pal.accent);
		gfx_fill(x + 2, y + 2, 1, 3, pal.accent);
		gfx_fill(x + 3, y + 3, 1, 1, pal.accent);
	} else {
		gfx_fill(x + 1, y + 1, 2, 5, pal.warn);
		gfx_fill(x + 4, y + 1, 2, 5, pal.warn);
	}
}

static void draw_status(AudioSnap *s)
{
	char line[64], pg[24], bpm[16];
	int pages = (s->channels + 2) / 3;
	int bpm_x, pg_x;

	snprintf(line, sizeof(line), "O%02d/%02d P%02X R%02X/%02X S%02X",
		 s->order, s->orders, s->pattern, s->row,
		 s->pattern >= 0 ? audio_pattern_rows(s->pattern) : 0, s->speed);
	snprintf(bpm, sizeof(bpm), "%dBPM", s->tempo);
	bpm_x = 236 - gfx_text_w(bpm);
	if (pages > 1) {
		snprintf(pg, sizeof(pg), "CH%d/%d", player.page + 1, pages);
		pg_x = bpm_x - 8 - gfx_text_w(pg);
		gfx_text_lim(4, STATUS_Y, pg_x - 6, line, pal.dim);
		gfx_text(pg_x, STATUS_Y, pg, pal.dim);
	} else {
		gfx_text_lim(4, STATUS_Y, bpm_x - 6, line, pal.dim);
	}
	gfx_text(bpm_x, STATUS_Y, bpm, pal.accent_dim);
}

static void draw_progress(AudioSnap *s)
{
	char t1[16], t2[16];
	int playing = s->playing;
	double dur = s->dur;
	double pos = s->pos;
	int barx = 48, barw = 142, bary = PROG_TOP + 10;
	int fw;

	gfx_fill(0, PROG_TOP, SW, PROG_H, pal.bg);

	draw_play_icon(4, PROG_TOP + 8, playing);
	util_time((int)pos, t1, sizeof(t1));
	util_time((int)(dur > 0 ? dur : 0), t2, sizeof(t2));
	gfx_text(16, PROG_TOP + 7, t1, pal.text);
	gfx_text_right(236, PROG_TOP + 7, t2, pal.dim);

	gfx_fill(barx, bary, barw, 3, pal.line);
	if (dur > 0.1) {
		fw = (int)(pos / dur * barw);
		if (fw < 0)
			fw = 0;
		if (fw > barw)
			fw = barw;
		gfx_fill(barx, bary, fw, 3, pal.accent);
		gfx_fill(barx + fw - 1, bary - 1, 2, 5, pal.text);
	}

	draw_status(s);
}

/* ------------------------------------------- bottom mini waterfall --- */

#define MF_W SW
#define MF_H (VIS_H - 2)

static unsigned char mf_buf[MF_H][MF_W];
static Uint32 mf_pal[256];
static int mf_theme = -1;
static int mf_ready = 0;
static unsigned int mf_last = 0;

static void mf_build_pal(void)
{
	const ThemeDef *t = &themes[cfg.theme];
	uint32_t stops[6];
	int n = 6;
	int i;

	stops[0] = t->bg;
	stops[1] = t->accent_dim;
	stops[2] = t->accent;
	stops[3] = t->instr;
	stops[4] = t->fx;
	stops[5] = t->bright;

	for (i = 0; i < 256; i++) {
		float f = (float)i / 255.0f * (float)(n - 1);
		int s = (int)f;
		float fr;
		uint32_t a, b;
		int r, g, bl;

		if (s >= n - 1) {
			s = n - 2;
			fr = 1.0f;
		} else {
			fr = f - (float)s;
		}
		a = stops[s];
		b = stops[s + 1];
		r = (int)((a >> 16) & 255) +
		    (int)(((int)((b >> 16) & 255) -
			   (int)((a >> 16) & 255)) * fr);
		g = (int)((a >> 8) & 255) +
		    (int)(((int)((b >> 8) & 255) -
			   (int)((a >> 8) & 255)) * fr);
		bl = (int)(a & 255) +
		     (int)(((int)(b & 255) - (int)(a & 255)) * fr);
		mf_pal[i] = SDL_MapRGB(gfx_screen->format, (Uint8)r, (Uint8)g,
				       (Uint8)bl);
	}
	mf_theme = cfg.theme;
}

static void mf_row(unsigned char *row)
{
	int x;

	for (x = 0; x < MF_W; x++) {
		int pos = x * (SPECTRUM_BARS - 1);
		int b0 = pos / (MF_W - 1);
		int rem = pos % (MF_W - 1);
		int b1 = b0 + 1;
		float fr = (float)rem / (float)(MF_W - 1);
		float v;
		int idx = 0;

		if (b1 >= SPECTRUM_BARS) {
			b1 = b0;
			fr = 0.0f;
		}
		v = spectrum_bar(b0) * (1.0f - fr) + spectrum_bar(b1) * fr;
		if (v > 0.015f) {
			idx = 40 + (int)(v * 215.0f);
			if (idx > 255)
				idx = 255;
		}
		row[x] = (unsigned char)idx;
	}
}

#define BOT_BARS 30

static void vis_cell(int i, int *x, int *w)
{
	*x = i * SW / BOT_BARS;
	*w = SW / BOT_BARS - 1;
}

/* Resample the 32 FFT bands into the 30 columns of the bottom strip. */
static float bar_value(int i, int peak)
{
	float pos = (float)i * (float)(SPECTRUM_BARS - 1) /
		    (float)(BOT_BARS - 1);
	int b0 = (int)pos;
	int b1 = b0 + 1;
	float fr = pos - (float)b0;

	if (b1 >= SPECTRUM_BARS) {
		b1 = b0;
		fr = 0.0f;
	}
	if (peak)
		return spectrum_peak(b0) * (1.0f - fr) +
		       spectrum_peak(b1) * fr;
	return spectrum_bar(b0) * (1.0f - fr) + spectrum_bar(b1) * fr;
}

static void draw_visualizer(AudioSnap *s)
{
	int i;
	int center = VIS_TOP + VIS_H / 2;
	int base = VIS_TOP + VIS_H - 4;

	gfx_fill(0, VIS_TOP, SW, VIS_H, pal.panel);
	gfx_hline(0, VIS_TOP, SW, pal.line);
	gfx_hline(0, VIS_TOP + VIS_H - 1, SW, pal.line);

	if (cfg.vis == VIS_SPECTRUM) {
		gfx_hline(0, center, SW, pal.line2);
		for (i = 0; i < BOT_BARS; i++) {
			int x, w;
			float v = bar_value(i, 0);
			float p = bar_value(i, 1);
			int h = (int)(v * 12.0f);

			vis_cell(i, &x, &w);
			if (h > 0) {
				gfx_fill(x, center - h, w, h, pal.accent);
				gfx_hline(x, center - h, w, pal.text);
			}
			if (p > 0.03f) {
				int ph = (int)(p * 12.0f);

				gfx_hline(x, center - ph - 1, w, pal.instr);
				gfx_hline(x, center + ph + 1, w, pal.instr);
			}
		}
	} else if (cfg.vis == VIS_SCOPE) {
		int py = center;

		gfx_hline(0, center, SW, pal.line2);
		for (i = 0; i < SW; i++) {
			int idx = (i * 512 / SW) & 511;
			int y = center - (s->scope[idx] * 11) / 32768;

			if (i)
				gfx_line(i - 1, py, i, y, pal.accent);
			py = y;
		}
	} else if (cfg.vis == VIS_WATERFALL) {
		unsigned int now = SDL_GetTicks();
		int y, x;

		if (mf_theme != cfg.theme)
			mf_build_pal();
		if (!mf_ready) {
			for (y = 0; y < MF_H; y++)
				mf_row(mf_buf[y]);
			mf_ready = 1;
			mf_last = now;
		} else if (s->playing && now - mf_last >= 60) {
			mf_last = now;
			memmove(mf_buf[1], mf_buf[0],
				(size_t)(MF_H - 1) * MF_W);
			mf_row(mf_buf[0]);
		}
		for (y = 0; y < MF_H; y++)
			for (x = 0; x < MF_W; x++)
				gfx_px(x, VIS_TOP + 1 + y,
				       mf_pal[mf_buf[y][x]]);
	} else if (cfg.vis == VIS_PEAKS) {
		for (i = 0; i < BOT_BARS; i++) {
			int x, w;
			float v = bar_value(i, 0);
			float p = bar_value(i, 1);
			int h = (int)(v * 21.0f);

			vis_cell(i, &x, &w);
			if (h > 21)
				h = 21;
			if (h > 0)
				gfx_fill(x, base - h, w, h, pal.accent);
			if (p > 0.03f) {
				int ph = (int)(p * 21.0f);

				if (ph > 21)
					ph = 21;
				gfx_hline(x, base - ph - 1, w, pal.instr);
			}
		}
	} else if (cfg.vis == VIS_DOTS) {
		for (i = 0; i < BOT_BARS; i++) {
			int x, w;
			int lv = (int)(bar_value(i, 0) * 5.99f);
			int k;

			vis_cell(i, &x, &w);
			x += 1;
			w = 6;
			if (lv > 5)
				lv = 5;
			for (k = 0; k < 6; k++) {
				int y = base - k * 4;

				if (k <= lv)
					gfx_fill(x, y - 1, w, 2,
						 k > 3 ? pal.fx : pal.accent);
				else
					gfx_fill(x, y - 1, w, 1, pal.line2);
			}
		}
	} else {
		const char *tr = player.tracker[0] ? player.tracker :
				 audio_format_name();
		char info[64];
		int w = gfx_text_w(tr);

		gfx_text((SW - w) / 2, VIS_TOP + 5, tr, pal.dim);
		snprintf(info, sizeof(info), "%d CHANNELS  %d ORDERS",
			 s->channels, s->orders);
		gfx_text((SW - gfx_text_w(info)) / 2, VIS_TOP + 15, info,
			 pal.line2);
	}
}

static void draw_footer(AudioSnap *s)
{
	static const char letters[5] = { 'A', 'B', 'X', 'Y', 'S' };
	static const char *labels[5] = { "PLAY", "LIST", "INFO", "VIS", "SET" };
	Uint32 colors[5];
	int x = 6;
	int i;
	int off = ui_hints_offset();
	int y = HINT_Y + off;

	if (y > SH)
		return;		/* hints gone: the status line stays visible */

	/* The hints live on the bottom row and temporarily cover the status
	 * line; when they slide away the status is revealed. */
	gfx_fill(0, y - 2, SW, 18, pal.bg);
	gfx_hline(0, y - 2, SW, pal.line);

	colors[0] = pal.btn_a;
	colors[1] = pal.btn_b;
	colors[2] = pal.btn_x;
	colors[3] = pal.btn_y;
	colors[4] = pal.btn_s;
	for (i = 0; i < 5; i++) {
		gfx_hint(x, y, letters[i], colors[i], labels[i], pal.text);
		x += 13 + gfx_text_w(labels[i]) + 8;
	}

	(void)s;
}

static void draw_header(AudioSnap *s)
{
	char bpm[16], chs[16], title[32];
	int bpm_w, ch_w, badge_w;
	int xb, xbpm, xch;
	const char *fmt = player.track.fmt[0] ? player.track.fmt : "??";
	int flen = (int)strlen(fmt);
	int tlen = (int)strlen(player.title);
	int off = player.marquee_off;

	gfx_fill(0, HDR_TOP, SW, HDR_H, pal.panel);
	gfx_hline(0, HDR_TOP + HDR_H - 1, SW, pal.accent);

	gfx_text(6, HDR_TOP + 2, "CHIPTRACKER", pal.accent);
	gfx_text_lim(78, HDR_TOP + 2, 132, player.tracker, pal.dim);

	snprintf(bpm, sizeof(bpm), "%dBPM", s->tempo);
	snprintf(chs, sizeof(chs), "%dCH", s->channels);
	bpm_w = gfx_text_w(bpm);
	ch_w = gfx_text_w(chs);
	badge_w = flen * FONT5X7_ADV + 6;
	xb = 234 - badge_w;
	xbpm = xb - 6 - bpm_w;
	xch = xbpm - 6 - ch_w;

	gfx_text_right(xch + ch_w, HDR_TOP + 2, chs, pal.dim);
	gfx_text_right(xbpm + bpm_w, HDR_TOP + 2, bpm, pal.dim);
	gfx_badge(xb, HDR_TOP + 1, fmt, fmt_color(fmt), pal.bg);

	/* 2x title, marquee when long */
	if (tlen > 19) {
		int start = off;
		int n = 0;
		if (start > tlen - 1)
			start = 0;
		for (; player.title[start + n] && n < 19; n++)
			;
		memcpy(title, player.title + start, (size_t)n);
		title[n] = 0;
	} else {
		util_copy(title, sizeof(title), player.title);
	}
	gfx_text2_lim(6, HDR_TOP + 10, 234, title, pal.bright);
}

static int page_anim_state(int *dir, float *ease)
{
	unsigned int e;
	float p;

	if (!cfg.page_slide || !player.page_anim_start)
		return 0;
	e = SDL_GetTicks() - player.page_anim_start;
	if (e >= 180)
		return 0;
	p = (float)e / 180.0f;
	*dir = player.page_anim_dir;
	*ease = 1.0f - (1.0f - p) * (1.0f - p);
	return 1;
}

static void draw_pattern_header(AudioSnap *s)
{
	int i;
	int anim, dir = 0;
	float ease = 0.0f;
	int oldx = 0, newx = 0;

	gfx_fill(0, PAT_TOP - 11, SW, 10, pal.panel2);
	gfx_text(4, PAT_TOP - 9, "CH", pal.dim);
	gfx_text(222, PAT_TOP - 9, "VU", pal.dim);

	anim = page_anim_state(&dir, &ease);
	if (anim) {
		int shift = (int)(198.0f * (1.0f - ease));
		newx = dir > 0 ? shift : -shift;
		oldx = dir > 0 ? -(198 - shift) : (198 - shift);
		for (i = 0; i < 3; i++) {
			int x = 22 + i * 66;
			int ch;
			char lab[16];

			if (old_rows > 0) {
				ch = old_page * 3 + i;
				snprintf(lab, sizeof(lab), "%02d", ch + 1);
				if (x + oldx >= 20 && x + oldx + 12 <= 220)
					gfx_text(x + oldx + 2, PAT_TOP - 9, lab,
						 ch < s->channels ? pal.accent : pal.line2);
				if (x + oldx + 18 >= 22 && x + oldx + 58 <= 220)
					gfx_fill(x + oldx + 18, PAT_TOP - 7, 40, 1,
						 ch < s->channels ? pal.accent_dim : pal.line);
			}
			ch = player.page * 3 + i;
			snprintf(lab, sizeof(lab), "%02d", ch + 1);
			if (x + newx >= 20 && x + newx + 12 <= 220)
				gfx_text(x + newx + 2, PAT_TOP - 9, lab,
					 ch < s->channels ? pal.accent : pal.line2);
			if (x + newx + 18 >= 22 && x + newx + 58 <= 220)
				gfx_fill(x + newx + 18, PAT_TOP - 7, 40, 1,
					 ch < s->channels ? pal.accent_dim : pal.line);
		}
	} else {
		for (i = 0; i < 3; i++) {
			int ch = player.page * 3 + i;
			char lab[16];
			int x = 22 + i * 66;

			snprintf(lab, sizeof(lab), "%02d", ch + 1);
			gfx_text(x + 2, PAT_TOP - 9, lab,
				 ch < s->channels ? pal.accent : pal.line2);
			gfx_fill(x + 18, PAT_TOP - 7, 40, 1,
				 ch < s->channels ? pal.accent_dim : pal.line);
		}
	}
}

static void draw_pattern(AudioSnap *s)
{
	int i, c, r;
	double top;
	int first;
	int anim, dir = 0;
	float ease = 0.0f;
	int oldx = 0, newx = 0;

	refresh_cache(s);

	anim = page_anim_state(&dir, &ease);
	if (anim) {
		int shift = (int)(198.0f * (1.0f - ease));
		newx = dir > 0 ? shift : -shift;
		oldx = dir > 0 ? -(198 - shift) : (198 - shift);
	}

	gfx_fill(0, PAT_TOP, SW, PAT_BOTTOM - PAT_TOP, pal.bg);

	top = (double)vis_pos - 6.0;
	if (top < 0.0)
		top = 0.0;
	if (cache_rows > PAT_ROWS) {
		if (top > (double)(cache_rows - PAT_ROWS))
			top = (double)(cache_rows - PAT_ROWS);
	} else {
		top = 0.0;
	}
	first = (int)floor(top) - 1;

	for (i = 0; i < PAT_ROWS + 3; i++) {
		int y, ytop;
		r = first + i;
		if (r < 0 || r >= cache_rows)
			continue;
		ytop = PAT_TOP + (int)((r - top) * PAT_ROW_H);
		if (ytop >= PAT_BOTTOM || ytop + PAT_ROW_H <= PAT_TOP)
			continue;

		if (r == s->row) {
			gfx_fill(0, ytop, 220, PAT_ROW_H, pal.cur_bg);
			gfx_fill(2, ytop, 3, PAT_ROW_H, pal.accent);
		} else if (r % 4 == 3) {
			gfx_hline(2, ytop + PAT_ROW_H - 1, 216, pal.line);
		}

		y = ytop + 1;
		{
			char rn[16];
			int len, x;

			snprintf(rn, sizeof(rn), "%02X", r);
			len = (int)strlen(rn);
			x = 20 - len * FONT5X7_ADV;
			gfx_text(x, y, rn, r == s->row ? pal.bright : pal.dim);
		}
		if (anim && r < old_rows) {
			for (c = 0; c < 3; c++)
				draw_cell_clip(22 + c * 66 + oldx, y,
					       old_cells[r][c], 22, 220);
		}
		for (c = 0; c < 3; c++)
			draw_cell_clip(22 + c * 66 + newx, y, cells[r][c],
				       22, 220);
	}

	if (anim) {
		for (i = 0; i < 3; i++) {
			int sx = 85 + i * 66;
			if (sx + oldx >= 22 && sx + oldx < 220)
				gfx_vline(sx + oldx, PAT_TOP, PAT_BOTTOM - PAT_TOP,
					  pal.line);
			if (sx + newx >= 22 && sx + newx < 220)
				gfx_vline(sx + newx, PAT_TOP, PAT_BOTTOM - PAT_TOP,
					  pal.line);
		}
	} else {
		gfx_vline(85, PAT_TOP, PAT_BOTTOM - PAT_TOP, pal.line);
		gfx_vline(151, PAT_TOP, PAT_BOTTOM - PAT_TOP, pal.line);
		gfx_vline(217, PAT_TOP, PAT_BOTTOM - PAT_TOP, pal.line);
	}
	gfx_vline(221, PAT_TOP, PAT_BOTTOM - PAT_TOP, pal.line2);

	draw_meter(223, player.page * 3 + 0 < 64 ? s->vu[player.page * 3 + 0] : 0,
		   PAT_BOTTOM - PAT_TOP);
	draw_meter(229, player.page * 3 + 1 < 64 ? s->vu[player.page * 3 + 1] : 0,
		   PAT_BOTTOM - PAT_TOP);
	draw_meter(235, player.page * 3 + 2 < 64 ? s->vu[player.page * 3 + 2] : 0,
		   PAT_BOTTOM - PAT_TOP);
}

void player_draw(void)
{
	AudioSnap s;

	audio_snapshot(&s);
	gfx_clear(pal.bg);

	if (s.playing && s.valid)
		spectrum_update(s.scope, 512, audio_rate());
	else
		spectrum_update(NULL, 0, audio_rate());

	if (!player.loaded) {
		gfx_text2(30, 90, "NO TRACK", pal.dim);
		gfx_text(42, 120, "PRESS B FOR LIBRARY", pal.text);
		return;
	}

	draw_header(&s);
	draw_pattern_header(&s);
	draw_pattern(&s);
	draw_orders(&s);
	draw_visualizer(&s);
	draw_progress(&s);
	draw_footer(&s);
}

void player_key(SDLKey k)
{
	switch (k) {
	case SDLK_a:
		player_toggle();
		break;
	case SDLK_b:
		screen = SCR_LIBRARY;
		lib.sel = library_index_of(player.track.path);
		break;
	case SDLK_x:
		player.info_tab = 0;
		player.info_scroll = 0;
		player.info_back = SCR_PLAYER;
		screen = SCR_INFO;
		break;
	case SDLK_y:
		if (player.loaded)
			vis_enter();
		else
			app_toast("NO TRACK", 1200);
		break;
	case SDLK_s:
		screen = SCR_SETTINGS;
		break;
	case SDLK_l:
		player_set_volume_page(player.page - 1);
		break;
	case SDLK_r:
		player_set_volume_page(player.page + 1);
		break;
	case SDLK_u:
		audio_seek_order(-1);
		break;
	case SDLK_d:
		audio_seek_order(1);
		break;
	case SDLK_m:
		player_next(-1);
		break;
	case SDLK_n:
		player_next(1);
		break;
	default:
		break;
	}
}
