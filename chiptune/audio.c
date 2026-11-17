/*
 * audio.c - libopenmpt + SDL 1.2 audio output (mono, 16 bit).
 *
 * The module is rendered in the SDL audio callback; the UI only reads a
 * small snapshot under SDL_LockAudio().  Loading/swapping a module happens
 * with the audio callback paused so the pointer is never used after free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <SDL/SDL.h>

#include <libopenmpt/libopenmpt.h>

#include "app.h"

static openmpt_module *mod = NULL;
static SDL_AudioSpec spec;
static int audio_ready = 0;
static volatile int have_mod = 0;
static volatile int want_play = 0;
static volatile int render_on = 0;
static volatile int ended = 0;
static float gain = 0.0f;
static char errbuf[200];
static double duration = 0.0;
static int nchannels = 0;
static int norders = 0;
static char type_name[48] = "";

static volatile float vu[64];
static volatile float master_vu = 0.0f;
static int16_t scope[512];
static int scope_pos = 0;

static int amp_on = 0;
static unsigned int amp_off_at = 0;

static void alock(void)
{
	if (audio_ready)
		SDL_LockAudio();
}

static void aunlock(void)
{
	if (audio_ready)
		SDL_UnlockAudio();
}

static void amp_set(int on)
{
	if (on == amp_on)
		return;
	amp_on = on;
	if (system(on ? "audio_amp on >/dev/null 2>&1" :
			 "audio_amp off >/dev/null 2>&1") != 0) {
		/* service may not exist in the preview environment */
	}
}

static Sint16 clamp16(int v)
{
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return (Sint16)v;
}

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
	int frames = len / (2 * (spec.channels ? spec.channels : 1));
	Sint16 *out = (Sint16 *)stream;
	int i;
	size_t got;

	(void)userdata;

	if (!have_mod || !render_on) {
		memset(stream, 0, (size_t)len);
		return;
	}

	if (spec.channels == 1) {
		got = openmpt_module_read_mono(mod, spec.freq, (size_t)frames, out);
	} else {
		/* We requested mono, but be safe if the driver gave us stereo. */
		static Sint16 tmp[4096];
		int want = frames;
		int k;

		if (want > (int)(sizeof(tmp) / sizeof(tmp[0]) / 2))
			want = (int)(sizeof(tmp) / sizeof(tmp[0]) / 2);
		got = openmpt_module_read_interleaved_stereo(mod, spec.freq,
							     (size_t)want, tmp);
		for (k = 0; k < (int)got; k++) {
			Sint16 v = (Sint16)((tmp[k * 2] + tmp[k * 2 + 1]) / 2);
			out[k * 2] = v;
			out[k * 2 + 1] = v;
		}
	}

	if (got == 0) {
		ended = 1;
		want_play = 0;
		render_on = 0;
		memset(stream, 0, (size_t)len);
		return;
	}

	{
		float target = want_play ? 1.0f : 0.0f;
		int peak = 0;
		int n = (int)got * spec.channels;

		for (i = 0; i < n; i++) {
			int v;
			gain += (target - gain) * 0.012f;
			v = (int)(out[i] * gain);
			out[i] = clamp16(v);
			{
				int a = out[i] < 0 ? -out[i] : out[i];
				if (a > peak)
					peak = a;
			}
		}
		if ((int)got < frames)
			memset(out + got * spec.channels, 0,
			       (size_t)(frames - got) * 2 * spec.channels);

		master_vu = master_vu * 0.80f + (peak / 32768.0f) * 0.20f;

		/* last 512 mono samples for the scope / FFT */
		{
			int count = (int)got;
			int start = count > 512 ? count - 512 : 0;
			int k;
			for (k = start; k < count; k++) {
				int v = out[k * spec.channels];
				scope[scope_pos] = (int16_t)v;
				scope_pos = (scope_pos + 1) & 511;
			}
		}

		{
			int n = nchannels > 64 ? 64 : nchannels;
			int k;
			for (k = 0; k < n; k++)
				vu[k] = (float)openmpt_module_get_current_channel_vu_mono(mod, k);
			for (; k < 64; k++)
				vu[k] = 0.0f;
		}
	}

	if (!want_play && gain < 0.002f) {
		gain = 0.0f;
		render_on = 0;
	}
}

int audio_open(void)
{
	SDL_AudioSpec want;
	int cand[8];
	int n = 0, i, ch;

	if (audio_ready)
		return 0;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		snprintf(errbuf, sizeof(errbuf), "audio init: %s", SDL_GetError());
		return -1;
	}
	memset(&want, 0, sizeof(want));
	want.format = AUDIO_S16SYS;
	want.samples = 2048;
	want.callback = audio_cb;

	if (cfg.samplerate > 0)
		cand[n++] = cfg.samplerate;
	{
		static const int extra[] = { 44100, 48000, 32000, 22050 };
		for (i = 0; i < 4; i++) {
			int j, dup = 0;
			for (j = 0; j < n; j++)
				if (cand[j] == extra[i])
					dup = 1;
			if (!dup)
				cand[n++] = extra[i];
		}
	}

	for (i = 0; i < n; i++) {
		for (ch = 1; ch <= 2; ch++) {
			want.freq = cand[i];
			want.channels = (Uint8)ch;
			if (SDL_OpenAudio(&want, &spec) == 0) {
				if (spec.freq < 22050) {
					SDL_CloseAudio();
					continue;
				}
				audio_ready = 1;
				SDL_PauseAudio(0);
				return 0;
			}
		}
	}

	snprintf(errbuf, sizeof(errbuf), "audio: %s", SDL_GetError());
	return -1;
}

int audio_rate(void)
{
	return audio_ready ? spec.freq : (cfg.samplerate > 0 ? cfg.samplerate : 44100);
}

int audio_available(void)
{
	return audio_ready;
}

void audio_close(void)
{
	audio_unload();
	if (audio_ready) {
		SDL_CloseAudio();
		audio_ready = 0;
	}
	amp_set(0);
}

static void free_module_locked(void)
{
	if (mod) {
		openmpt_module_destroy(mod);
		mod = NULL;
	}
	have_mod = 0;
	render_on = 0;
	want_play = 0;
	ended = 0;
	gain = 0.0f;
	scope_pos = 0;
	memset((void *)vu, 0, sizeof(vu));
	master_vu = 0.0f;
	duration = 0.0;
	nchannels = 0;
	norders = 0;
	type_name[0] = 0;
}

void audio_unload(void)
{
	if (!audio_ready) {
		free_module_locked();
		return;
	}
	alock();
	free_module_locked();
	aunlock();
	amp_set(0);
}

static void no_log(const char *message, void *user)
{
	(void)message;
	(void)user;
}

static int quality_interp(void)
{
	switch (cfg.quality) {
	case QUAL_FAST:
		return 2;
	case QUAL_HIGH:
		return 8;
	default:
		return 4;
	}
}

void audio_apply_quality(void)
{
	int interp = quality_interp();

	alock();
	if (mod) {
		openmpt_module_set_render_param(mod,
			OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, interp);
		openmpt_module_set_render_param(mod,
			OPENMPT_MODULE_RENDER_VOLUMERAMPING_STRENGTH, 5);
	}
	aunlock();
}

int audio_load(const char *path, int start_paused, double seek_sec)
{
	unsigned char *data = NULL;
	size_t size = 0;
	openmpt_module *newmod;
	double dur;
	int interp;

	errbuf[0] = 0;
	if (!audio_ready)
		audio_open();	/* silent mode is fine (headless preview) */

	if (util_read_file(path, &data, &size) != 0) {
		snprintf(errbuf, sizeof(errbuf), "cannot read file");
		return -1;
	}

	newmod = openmpt_module_create_from_memory2(data, size, no_log, NULL,
						    NULL, NULL, NULL, NULL, NULL);
	free(data);
	if (!newmod) {
		snprintf(errbuf, sizeof(errbuf), "unsupported or broken module");
		return -1;
	}

	interp = quality_interp();
	openmpt_module_set_render_param(newmod,
		OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, interp);
	openmpt_module_set_render_param(newmod,
		OPENMPT_MODULE_RENDER_VOLUMERAMPING_STRENGTH, 5);
	openmpt_module_set_render_param(newmod,
		OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL, 0);
	openmpt_module_set_repeat_count(newmod, 0);

	dur = openmpt_module_get_duration_seconds(newmod);

	if (seek_sec > 0.0) {
		double d = dur;
		if (d > 0.5 && seek_sec > d - 0.5)
			seek_sec = d - 0.5;
		if (seek_sec < 0)
			seek_sec = 0;
		openmpt_module_set_position_seconds(newmod, seek_sec);
	}

	alock();
	free_module_locked();
	mod = newmod;
	have_mod = 1;
	ended = 0;
	gain = 0.0f;
	duration = dur > 0 ? dur : 0.0;
	nchannels = openmpt_module_get_num_channels(mod);
	norders = openmpt_module_get_num_orders(mod);
	{
		const char *s = openmpt_module_get_metadata(mod, "type_long");
		if (!s || !*s) {
			if (s)
				openmpt_free_string(s);
			s = openmpt_module_get_metadata(mod, "type");
		}
		util_copy(type_name, sizeof(type_name), s ? s : "");
		if (s)
			openmpt_free_string(s);
	}
	if (start_paused || !audio_ready) {
		want_play = 0;
		render_on = 0;
	} else {
		want_play = 1;
		render_on = 1;
	}
	aunlock();

	if (!start_paused && audio_ready)
		amp_set(1);
	amp_off_at = 0;
	return 0;
}

void audio_set_paused(int paused)
{
	if (!have_mod)
		return;
	if (paused) {
		want_play = 0;
		amp_off_at = SDL_GetTicks() + 180;
	} else {
		want_play = 1;
		render_on = 1;
		ended = 0;
		amp_off_at = 0;
		amp_set(1);
	}
}

int audio_paused(void)
{
	return !want_play;
}

int audio_have_track(void)
{
	return have_mod;
}

void audio_tick(void)
{
	if (amp_off_at && SDL_GetTicks() >= amp_off_at) {
		amp_off_at = 0;
		if (!want_play)
			amp_set(0);
	}
}

void audio_seek_seconds(double sec)
{
	if (!have_mod)
		return;
	if (sec < 0)
		sec = 0;
	if (duration > 1.0 && sec > duration - 0.25)
		sec = duration - 0.25;
	alock();
	openmpt_module_set_position_seconds(mod, sec);
	ended = 0;
	aunlock();
}

void audio_seek_order(int delta)
{
	int order;

	if (!have_mod)
		return;
	alock();
	order = (int)openmpt_module_get_current_order(mod) + delta;
	if (order < 0)
		order = 0;
	if (norders > 0 && order >= norders)
		order = norders - 1;
	openmpt_module_set_position_order_row(mod, order, 0);
	ended = 0;
	aunlock();
}

void audio_set_position(double sec)
{
	audio_seek_seconds(sec);
}

double audio_position(void)
{
	double p;

	if (!have_mod)
		return 0.0;
	alock();
	p = openmpt_module_get_position_seconds(mod);
	aunlock();
	return p;
}

double audio_duration(void)
{
	return duration;
}

int audio_channels(void)
{
	return nchannels;
}

int audio_orders(void)
{
	return norders;
}

int audio_pattern(void)
{
	int p;

	if (!have_mod)
		return -1;
	alock();
	p = (int)openmpt_module_get_current_pattern(mod);
	aunlock();
	return p;
}

int audio_row(void)
{
	int r;

	if (!have_mod)
		return 0;
	alock();
	r = (int)openmpt_module_get_current_row(mod);
	aunlock();
	return r;
}

int audio_take_ended(void)
{
	int e;

	if (!have_mod)
		return 0;
	alock();
	e = ended;
	ended = 0;
	aunlock();
	return e;
}

const char *audio_error(void)
{
	return errbuf;
}

int audio_pattern_rows(int pattern)
{
	int n;

	if (!have_mod)
		return 0;
	alock();
	n = (int)openmpt_module_get_pattern_num_rows(mod, pattern);
	aunlock();
	return n;
}

int audio_num_patterns(void)
{
	int n;

	if (!have_mod)
		return 0;
	alock();
	n = (int)openmpt_module_get_num_patterns(mod);
	aunlock();
	return n;
}

int audio_format_cell(int pattern, int row, int channel, char *out, int outsz)
{
	const char *s;

	if (outsz <= 0)
		return 0;
	out[0] = 0;
	if (!have_mod)
		return 0;
	alock();
	s = openmpt_module_format_pattern_row_channel(mod, pattern, row,
						      channel, 0, 0);
	if (!s) {
		aunlock();
		return 0;
	}
	util_copy(out, (size_t)outsz, s);
	openmpt_free_string(s);
	aunlock();
	return 1;
}

char *audio_meta(const char *key)
{
	const char *s;
	char *ret = NULL;

	if (!have_mod)
		return NULL;
	alock();
	s = openmpt_module_get_metadata(mod, key);
	if (s) {
		ret = strdup(s);
		openmpt_free_string(s);
	}
	aunlock();
	return ret;
}

int audio_num_samples(void)
{
	int n;

	if (!have_mod)
		return 0;
	alock();
	n = (int)openmpt_module_get_num_samples(mod);
	aunlock();
	return n;
}

char *audio_sample_name(int i)
{
	const char *s;
	char *ret = NULL;

	if (!have_mod)
		return NULL;
	alock();
	s = openmpt_module_get_sample_name(mod, i);
	if (s) {
		ret = strdup(s);
		openmpt_free_string(s);
	}
	aunlock();
	return ret;
}

int audio_num_subsongs(void)
{
	int n;

	if (!have_mod)
		return 0;
	alock();
	n = (int)openmpt_module_get_num_subsongs(mod);
	aunlock();
	return n;
}

const char *audio_format_name(void)
{
	return type_name;
}

void audio_snapshot(AudioSnap *s)
{
	memset(s, 0, sizeof(*s));
	s->channels = nchannels;
	s->orders = norders;
	s->dur = duration;
	if (!have_mod)
		return;

	alock();
	s->valid = 1;
	s->playing = want_play;
	s->order = (int)openmpt_module_get_current_order(mod);
	s->pattern = (int)openmpt_module_get_current_pattern(mod);
	s->row = (int)openmpt_module_get_current_row(mod);
	s->speed = (int)openmpt_module_get_current_speed(mod);
	s->tempo = (int)(openmpt_module_get_current_tempo2(mod) + 0.5);
	s->pos = openmpt_module_get_position_seconds(mod);
	memcpy(s->vu, (const void *)vu, sizeof(s->vu));
	s->master = master_vu;
	{
		int i;
		for (i = 0; i < 512; i++)
			s->scope[i] = scope[(scope_pos + i) & 511];
	}
	aunlock();
}
