/*
 * lock.c - pocket screen lock.
 *
 * The lock is a single-button affair: hold Start to lock, tap Start to see
 * the hint, hold Start to unlock.  The music keeps playing.
 *
 * Backlight handling notes:
 *  - FunKey-OS ships a "brightness" helper whose "set 0" maps to kernel
 *    level 1, i.e. the darkest level rather than off.  We drive the kernel
 *    device directly.
 *  - The device name is not guaranteed to be "backlight", so the node is
 *    discovered by scanning /sys/class/backlight at runtime.
 *  - Two off modes: MIN keeps the PWM running at level 1 (dark on both PWM
 *    polarities, and the mode that avoids pwm_disable()); OFF writes 0 and
 *    bl_power=4 (FB_BLANK_POWERDOWN).  The setting exists because the
 *    correct one depends on how the panel LED driver is wired.
 *  - fb0/blank is deliberately NOT used.  Blanking the console while SDL
 *    owns the framebuffer leaves the panel dark even after unblanking,
 *    which is exactly the "unlocked but still black" symptom.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include <SDL/SDL.h>

#include "app.h"
#include "gfx.h"

#define BL_ROOT "/sys/class/backlight"
#define LOCK_HOLD_MS 1500
#define HINT_MS 1500

static int locked = 0;
static int hint = 0;
static Screen saved_screen;
static int saved_bright = 6;

#define MAX_BL 4

typedef struct {
	char bright[256];
	char power[256];
	int max;
	int saved;
} BlDev;

static BlDev bls[MAX_BL];
static int bl_n = 0;
static int bl_off_state = 2;	/* 0 driven, 2 unavailable */

static unsigned int start_down = 0;
static unsigned int hint_until = 0;

static int lock_key_part(SDLKey k)
{
	if (cfg.lock_key == 1)
		return k == SDLK_k;
	if (cfg.lock_key == 2)
		return k == SDLK_s || k == SDLK_k;
	return k == SDLK_s;
}

static int lock_key_held(void)
{
	Uint8 *ks = SDL_GetKeyState(NULL);

	if (!ks)
		return 0;
	if (cfg.lock_key == 1)
		return ks[SDLK_k] != 0;
	if (cfg.lock_key == 2)
		return ks[SDLK_s] != 0 && ks[SDLK_k] != 0;
	return ks[SDLK_s] != 0;
}

static const char *hold_label(void)
{
	if (cfg.lock_key == 1)
		return "HOLD SELECT";
	if (cfg.lock_key == 2)
		return "HOLD START+SEL";
	return "HOLD START";
}

static int read_int(const char *path, int *v)
{
	FILE *f;
	int n;

	if (!path[0])
		return -1;
	f = fopen(path, "r");
	if (!f)
		return -1;
	n = fscanf(f, "%d", v);
	fclose(f);
	return n == 1 ? 0 : -1;
}

static int write_int(const char *path, int v)
{
	FILE *f;
	int rc;

	if (!path[0])
		return -1;
	f = fopen(path, "w");
	if (!f)
		return -1;
	rc = fprintf(f, "%d\n", v) > 0 ? 0 : -1;
	fclose(f);
	return rc;
}

static int bl_get_dev(int i)
{
	int v = -1;

	if (i < 0 || i >= bl_n)
		return -1;
	read_int(bls[i].bright, &v);
	return v;
}

/* Enumerate every backlight device: depending on the hardware revision the
 * panel may be exposed under a name other than "backlight". */
static void bl_probe(void)
{
	DIR *d;
	struct dirent *e;

	bl_n = 0;
	d = opendir(BL_ROOT);
	if (d) {
		while ((e = readdir(d)) != NULL && bl_n < MAX_BL) {
			char p[320];
			BlDev *b;
			int m;

			if (e->d_name[0] == '.')
				continue;
			snprintf(p, sizeof(p), BL_ROOT "/%s/brightness",
				 e->d_name);
			if (access(p, F_OK) != 0)
				continue;
			b = &bls[bl_n];
			memset(b, 0, sizeof(*b));
			util_copy(b->bright, sizeof(b->bright), p);
			m = 11;
			snprintf(p, sizeof(p), BL_ROOT "/%s/max_brightness",
				 e->d_name);
			if (read_int(p, &m) != 0 || m < 1)
				m = 11;
			b->max = m;
			snprintf(p, sizeof(p), BL_ROOT "/%s/bl_power",
				 e->d_name);
			if (access(p, F_OK) == 0)
				util_copy(b->power, sizeof(b->power), p);
			b->saved = bl_get_dev(bl_n);
			bl_n++;
		}
		closedir(d);
	}

	if (bl_n == 0 && access(BL_ROOT "/backlight/brightness", F_OK) == 0) {
		BlDev *b = &bls[0];

		memset(b, 0, sizeof(*b));
		util_copy(b->bright, sizeof(b->bright),
			  BL_ROOT "/backlight/brightness");
		if (access(BL_ROOT "/backlight/bl_power", F_OK) == 0)
			util_copy(b->power, sizeof(b->power),
				  BL_ROOT "/backlight/bl_power");
		b->max = 11;
		read_int(BL_ROOT "/backlight/max_brightness", &b->max);
		if (b->max < 1)
			b->max = 11;
		bl_n = 1;
		b->saved = bl_get_dev(0);
	}
}

static int bl_get(void)
{
	return bl_n > 0 ? bl_get_dev(0) : -1;
}

static void backlight_off(void)
{
	int i, ok = 0;
	/* Minimum level with the PWM still running.  Writing 0 (or
	 * bl_power=4) did not darken the RG Nano panel at all, and on some
	 * PWM polarities a disabled output actually lights the LEDs. */
	for (i = 0; i < bl_n; i++) {
		write_int(bls[i].power, 0);	/* FB_BLANK_UNBLANK */
		if (write_int(bls[i].bright, 1) == 0)
			ok = 1;
	}
	bl_off_state = ok ? 0 : 2;
}

/* Diagnostics: appended next to config.ini so it can be inspected on the
 * device when the backlight behaves unexpectedly. */
static void bl_log(const char *tag)
{
	const char *cf = config_file();
	char path[MAX_PATH];
	char *slash;
	struct stat st;
	FILE *f;
	const char *mode = "a";
	int i;

	util_copy(path, sizeof(path), cf);
	slash = strrchr(path, '/');
	if (slash)
		util_copy(slash + 1,
			  sizeof(path) - (size_t)(slash + 1 - path), "lock.log");
	else
		util_copy(path, sizeof(path), "/tmp/chip-lock.log");
	if (stat(path, &st) == 0 && st.st_size > 32768)
		mode = "w";
	f = fopen(path, mode);
	if (!f)
		return;
	fprintf(f, "%s state=%d devs=%d", tag, bl_off_state, bl_n);
	for (i = 0; i < bl_n; i++)
		fprintf(f, " [%s max=%d saved=%d now=%d]", bls[i].bright,
			bls[i].max, bls[i].saved, bl_get_dev(i));
	fprintf(f, "\n");
	fclose(f);
}

static void backlight_on(int level)
{
	int i;

	for (i = 0; i < bl_n; i++) {
		int l = level;

		if (l < 1)
			l = 1;
		if (l > bls[i].max)
			l = bls[i].max;
		write_int(bls[i].power, 0);	/* FB_BLANK_UNBLANK */
		write_int(bls[i].bright, l);
	}
}

static int hint_level(void)
{
	int l = saved_bright * 2 / 3;

	if (l < 3)
		l = 3;
	if (l > 8)
		l = 8;
	return l;
}

static void draw_blank(void)
{
	SDL_LockSurface(gfx_screen);
	gfx_clear(gfx_black());
	SDL_UnlockSurface(gfx_screen);
	SDL_Flip(gfx_screen);
}

/* Last line of defence: never leave the backlight off after a normal exit. */
static void lock_atexit(void)
{
	if (locked)
		backlight_on(saved_bright);
}

static void draw_lock_screen(void)
{
	unsigned int now = SDL_GetTicks();
	int prog = 0;
	int w;

	SDL_LockSurface(gfx_screen);
	gfx_clear(pal.bg);

	gfx_text((SW - gfx_text_w("SCREEN LOCKED")) / 2, 52,
		 "SCREEN LOCKED", pal.dim);

	/* padlock */
	gfx_vline(116, 76, 11, pal.accent);
	gfx_vline(124, 76, 11, pal.accent);
	gfx_hline(116, 76, 9, pal.accent);
	gfx_fill(111, 84, 18, 17, pal.accent_dim);
	gfx_rect(111, 84, 18, 17, pal.accent);
	gfx_fill(119, 89, 2, 5, pal.bg);
	gfx_fill(117, 93, 6, 2, pal.bg);

	if (start_down) {
		prog = (int)((now - start_down) * 96 / LOCK_HOLD_MS);
		gfx_text((SW - gfx_text_w("KEEP HOLDING")) / 2, 128,
			 "KEEP HOLDING", pal.bright);
	} else {
		gfx_text((SW - gfx_text_w(hold_label())) / 2, 128,
			 hold_label(), pal.bright);
	}

	gfx_fill(72, 142, 96, 3, pal.line);
	if (prog > 96)
		prog = 96;
	if (prog > 0)
		gfx_fill(72, 142, prog, 3, pal.btn_s);

	w = gfx_text_w("TO UNLOCK");
	gfx_text((SW - w) / 2, 152, "TO UNLOCK", pal.dim);

	if (bl_off_state == 2)
		gfx_text((SW - gfx_text_w("BL: NO ACCESS")) / 2, 206,
			 "BL: NO ACCESS", pal.warn);

	SDL_UnlockSurface(gfx_screen);
	SDL_Flip(gfx_screen);
}

void lock_enter(void)
{
	static int atexit_done = 0;

	if (locked)
		return;
	saved_screen = screen;
	bl_probe();
	if (!atexit_done) {
		atexit(lock_atexit);
		atexit_done = 1;
	}
	saved_bright = 6;
	{
		int v = bl_get();
		/* Value 1 is our own dim level: a previous run may have been
		 * killed while locked, so do not make that permanent. */
		if (v >= 2)
			saved_bright = v;
	}
	if (saved_bright < 1)
		saved_bright = 6;
	locked = 1;
	hint = 0;
	start_down = 0;
	hint_until = 0;

	/* Never leave the previous frame visible if the backlight refuses to
	 * switch off. */
	draw_blank();
	backlight_off();
	bl_log("lock");
}

int lock_active(void)
{
	return locked;
}

void lock_tick(void)
{
	unsigned int now = SDL_GetTicks();

	if (!locked || !hint)
		return;

	if (lock_key_held()) {
		if (!start_down)
			start_down = now;
		if (now - start_down >= LOCK_HOLD_MS) {
			lock_restore();
			return;
		}
	} else {
		start_down = 0;
	}

	if (!start_down && (int)(now - hint_until) >= 0) {
		hint = 0;
		draw_blank();
		backlight_off();
		return;
	}
	draw_lock_screen();
}

int lock_event(SDL_Event *e)
{
	unsigned int now;

	if (!locked)
		return 0;
	if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP)
		return 1;
	now = SDL_GetTicks();

	if (e->type == SDL_KEYUP) {
		if (lock_key_part(e->key.keysym.sym))
			start_down = 0;
		return 1;
	}

	if (!hint) {
		/* Wake only when the configured gesture is complete: for
		 * BOTH this means the second key of the pair going down. */
		if (lock_key_part(e->key.keysym.sym) && lock_key_held()) {
			hint = 1;
			backlight_on(hint_level());
			start_down = now;
			hint_until = now + HINT_MS;
			draw_lock_screen();
		}
		return 1;
	}

	if (lock_key_part(e->key.keysym.sym) && !start_down)
		start_down = now;
	hint_until = now + HINT_MS;
	return 1;
}

void lock_restore(void)
{
	if (!locked)
		return;
	backlight_on(saved_bright);
	bl_log("unlock");
	locked = 0;
	hint = 0;
	start_down = 0;
	screen = saved_screen;
	ui_hints_show();
}

/* Used by the screenshot generator only. */
void lock_preview(void)
{
	if (!locked)
		return;
	bl_off_state = 0;
	hint = 1;
	if (getenv("CHIP_LOCK_STATIC"))
		start_down = 0;
	else
		start_down = SDL_GetTicks() - 620;
	hint_until = SDL_GetTicks() + HINT_MS;
	draw_lock_screen();
}
