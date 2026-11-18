/*
 * app.h - shared state for CHIPTRACKER (RG Nano / FunKey OS).
 */
#ifndef CHIPTRACKER_APP_H
#define CHIPTRACKER_APP_H

#include <stdint.h>
#include <stddef.h>
#include <SDL/SDL.h>

#define SW 240
#define SH 240

#define VIS_STYLE_COUNT 8

/* The UI always runs at 60 FPS; the frame loop is cheap enough that the
 * difference in battery life versus 15/30 FPS was not measurable. */
#define APP_FPS 60

#define MAX_PATH 512
#define MAX_TRACKS 4096
#define MAX_BROWSER 300

/* Player screen geometry */
#define HDR_TOP 2
#define HDR_H 27
#define PAT_TOP 40
#define PAT_ROWS 14
#define PAT_ROW_H 9
#define PAT_BOTTOM (PAT_TOP + PAT_ROWS * PAT_ROW_H)
#define ORD_TOP (PAT_BOTTOM + 1)
#define ORD_H 10
#define VIS_TOP (ORD_TOP + ORD_H + 1)
#define VIS_H 28
#define PROG_TOP (VIS_TOP + VIS_H + 1)
#define PROG_H 15
#define FOOT_TOP (PROG_TOP + PROG_H)
#define STATUS_Y (SH - 8)
#define HINT_Y (SH - 14)

typedef struct {
	char path[MAX_PATH];
	char file[192];
	char stem[192];
	char fmt[8];
	char tracker[40];
	unsigned int size;
	int channels;
	int orders;
	int duration;
	unsigned char meta;	/* 0 unknown, 1 probed, 2 failed */
} Track;

typedef enum {
	SCR_SPLASH = 0,
	SCR_PLAYER,
	SCR_LIBRARY,
	SCR_BROWSER,
	SCR_INFO,
	SCR_SETTINGS,
	SCR_VIS,
	SCR_HELP
} Screen;

typedef enum {
	REPEAT_OFF = 0,
	REPEAT_ONE,
	REPEAT_ALL
} RepeatMode;

typedef enum {
	VIS_SPECTRUM = 0,
	VIS_SCOPE,
	VIS_WATERFALL,
	VIS_PEAKS,
	VIS_DOTS,
	VIS_OFF
} VisMode;

#define VIS_MODE_COUNT 6

typedef enum {
	QUAL_FAST = 0,
	QUAL_NORMAL,
	QUAL_HIGH
} Quality;

typedef struct {
	char music_dir[MAX_PATH];
	char last_path[MAX_PATH];
	double last_pos;
	int last_valid;

	int recursive;
	int repeat;
	int shuffle;
	int theme;
	int vis;
	int smooth;
	int quality;
	int samplerate;
	int start_player;
	int resume;
	int scanlines;
	int sort;		/* 0 name, 1 format, 2 size */
	int show_splash;
	int hints;		/* 0 auto, 1 on, 2 off */
	int page_slide;		/* horizontal channel page slide */
	int lock_key;		/* 0 start, 1 select, 2 both */
	int vis_style;		/* fullscreen visualizer */
	int vis_p[VIS_STYLE_COUNT];	/* per-style knob: right arrow */
	int vis_p2[VIS_STYLE_COUNT];	/* per-style knob: left arrow */
} Config;

typedef struct {
	int active;
	int done;
	int files;
	int dirs;
	int errors;
	int recursive;
	unsigned int elapsed_ms;
	char root[MAX_PATH];
} ScanState;

typedef struct {
	int loaded;
	Track track;
	char title[192];
	char tracker[64];
	char err[160];

	int ctx[MAX_TRACKS];
	int ctx_count;
	int ctx_pos;

	double pos;
	int paused;
	int page;
	int cur_pattern;
	int vis_row;		/* smoothed integer row */
	double vis_frac;	/* 0..1 sub-row */
	unsigned int marquee_ms;
	int marquee_off;

	int info_tab;
	int info_scroll;
	Screen info_back;

	unsigned int page_anim_start;
	int page_anim_dir;
	int vis_pat_page;
} PlayerState;

typedef struct {
	int sel;
	int top;
} LibState;

typedef struct {
	char path[MAX_PATH];
	int mode;		/* 0 play file, 1 choose folder */
	int first_run;
	int sel;
	int top;
	int count;
	int error;
	char names[MAX_BROWSER][192];
	char paths[MAX_BROWSER][MAX_PATH];
	unsigned char isdir[MAX_BROWSER];
} BrowserState;

typedef struct {
	int sel;
	int top;
} SettingsState;

extern Config cfg;
extern Track tracks[MAX_TRACKS];
extern int track_count;
extern unsigned int lib_generation;
extern ScanState scan;
extern PlayerState player;
extern LibState lib;
extern BrowserState browser;
extern SettingsState settings;
extern Screen screen;
extern int app_running;
extern unsigned int app_ms;

/* main.c */
void app_toast(const char *msg, int ms);
void app_set_screen(Screen s);
void app_start_scan(void);
void ui_hints_show(void);
int  ui_hints_offset(void);

/* player.c */
void player_draw(void);
void player_key(SDLKey k);
void player_start_index(int idx, int paused);
void player_start_path(const char *path, int paused);
void player_next(int dir);
void player_toggle(void);
void player_on_end(void);
void player_update(unsigned int dt_ms);
void player_load_last(void);
void player_set_volume_page(int page);

/* vis.c - fullscreen visualizers */
void vis_enter(void);
void vis_draw(void);
void vis_key(SDLKey k);
void vis_update(unsigned int dt_ms);
const char *vis_style_name(int i);

/* lock.c - screen/backlight lock */
void lock_enter(void);
int  lock_active(void);
int  lock_event(SDL_Event *e);
void lock_tick(void);
void lock_restore(void);
void lock_preview(void);

/* help.c */
void help_draw(void);
void help_key(SDLKey k);

/* config.c */
void config_defaults(void);
int  config_load(void);
int  config_save(void);
const char *config_file(void);

/* util.c */
void util_copy(char *dst, size_t n, const char *src);
void util_join(char *dst, size_t n, const char *dir, const char *name);
const char *util_basename(const char *p);
void util_dirname(const char *p, char *out, size_t n);
void util_stem(char *dst, size_t n, const char *name);
void util_upper(char *dst, size_t n, const char *src);
void util_lower(char *dst, size_t n, const char *src);
void util_time(int sec, char *out, size_t n);
void util_size(unsigned int bytes, char *out, size_t n);
int  util_ci_cmp(const char *a, const char *b);
int  util_nat_cmp(const char *a, const char *b);
void util_ascii(const char *in, char *out, size_t n);
int  util_exists(const char *path);
int  util_isdir(const char *path);
int  util_isfile(const char *path);
int  util_mkdirs(const char *path);
int  util_read_file(const char *path, unsigned char **buf, size_t *size);
const char *util_ext(const char *name);
int  util_supported_ext(const char *ext);
int  util_battery(void);	/* -1 unknown, otherwise 0..100 */
int  util_usb_present(void);

/* scan.c */
void scan_begin(const char *root, int recursive);
int  scan_poll(int budget_ms);
void scan_abort(void);
void scan_sort(void);
void scan_free(void);
int  probe_track(Track *t);
void tracks_sort(int mode);

/* browser.c */
void browser_open(const char *path, int mode, int first_run);
void browser_reload(void);
void browser_draw(void);
void browser_key(SDLKey k);

/* audio.c */
int  audio_open(void);
void audio_close(void);
int  audio_load(const char *path, int start_paused, double seek_sec);
void audio_unload(void);
void audio_set_paused(int paused);
int  audio_paused(void);
int  audio_have_track(void);
void audio_seek_seconds(double sec);
void audio_seek_order(int delta);
void audio_set_position(double sec);
double audio_position(void);
double audio_duration(void);
int  audio_channels(void);
int  audio_orders(void);
int  audio_pattern(void);
int  audio_row(void);
int  audio_take_ended(void);
const char *audio_error(void);
int  audio_format_cell(int pattern, int row, int channel, char *out, int outsz);
int  audio_pattern_rows(int pattern);
int  audio_num_patterns(void);
char *audio_meta(const char *key);	/* caller frees */
int  audio_num_samples(void);
char *audio_sample_name(int i);
int  audio_num_subsongs(void);
const char *audio_format_name(void);

typedef struct {
	int valid;
	int playing;
	int order, pattern, row, speed, tempo, channels, orders;
	double pos, dur;
	float vu[64];
	float master;
	int16_t scope[512];
} AudioSnap;

void audio_snapshot(AudioSnap *s);
void audio_tick(void);
int  audio_rate(void);
int  audio_available(void);
void audio_apply_quality(void);

/* spectrum.c */
void  spectrum_reset(void);
void  spectrum_update(const int16_t *samples, int count, int rate);
float spectrum_bar(int i);
float spectrum_peak(int i);
#define SPECTRUM_BARS 32

/* library.c */
void library_draw(void);
void library_key(SDLKey k);
void library_play_selected(void);
int  library_index_of(const char *path);
void library_update(void);
void lib_sel_reset(void);
void lib_sel_clamp(void);

/* settings.c */
void settings_draw(void);
void settings_key(SDLKey k);
void settings_update(void);
void settings_desc_preview(void);

/* info.c */
void info_draw(void);
void info_key(SDLKey k);
void info_update(void);

/* splash.c */
void splash_draw(void);
void splash_key(SDLKey k);
void splash_update(void);

/* theme.c */
typedef struct {
	const char *name;
	uint32_t bg, panel, panel2, line, line2;
	uint32_t dim, text, bright;
	uint32_t accent, accent_dim;
	uint32_t note, instr, vol, fx;
	uint32_t sel_bg, cur_bg;
	uint32_t warn, ok;
} ThemeDef;

#define THEME_COUNT 6
extern const ThemeDef themes[THEME_COUNT];

#endif /* CHIPTRACKER_APP_H */
