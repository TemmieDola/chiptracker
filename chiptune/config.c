/*
 * config.c - tiny INI config in /mnt/FunKey/ChiptunePlayer/config.ini
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"

static char cfg_dir[MAX_PATH];
static char cfg_path[MAX_PATH];

static void trim(char *s)
{
	char *p = s;
	size_t len;

	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
	len = strlen(s);
	while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
		       s[len - 1] == '\r' || s[len - 1] == '\n'))
		s[--len] = 0;
}

static void pick_dir(void)
{
	const char *env = getenv("CHIP_CONFIG_DIR");

	if (env && *env) {
		util_copy(cfg_dir, sizeof(cfg_dir), env);
	} else if (util_isdir("/mnt/FunKey")) {
		util_copy(cfg_dir, sizeof(cfg_dir), "/mnt/FunKey/ChiptunePlayer");
	} else {
		util_copy(cfg_dir, sizeof(cfg_dir), "/tmp/chiptune");
	}
	util_mkdirs(cfg_dir);
	util_join(cfg_path, sizeof(cfg_path), cfg_dir, "config.ini");
}

const char *config_file(void)
{
	if (!cfg_path[0])
		pick_dir();
	return cfg_path;
}

void config_defaults(void)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.recursive = 1;
	cfg.repeat = REPEAT_ALL;
	cfg.shuffle = 0;
	cfg.theme = 0;
	cfg.vis = VIS_SPECTRUM;
	cfg.smooth = 1;
	cfg.quality = QUAL_NORMAL;
	cfg.samplerate = 44100;
	cfg.start_player = 1;
	cfg.resume = 1;
	cfg.scanlines = 0;
	cfg.sort = 0;
	cfg.last_valid = 0;
	cfg.show_splash = 1;
	cfg.hints = 0;
	cfg.fps = 30;
	cfg.page_slide = 1;
	cfg.lock_key = 0;
	cfg.vis_style = 0;
	cfg.vis_p[3] = 3;	/* WATERFALL: top -> bottom */
	cfg.vis_p2[1] = 1;	/* RADIAL: mid spin */
}

static int geti(const char *v)
{
	return atoi(v);
}

int config_load(void)
{
	FILE *f;
	char line[1024];

	config_defaults();
	pick_dir();

	f = fopen(cfg_path, "r");
	if (!f)
		return 0;

	while (fgets(line, sizeof(line), f)) {
		char *eq;
		char *key = line;
		char *val;

		if (line[0] == '#' || line[0] == ';' || line[0] == '[')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		val = eq + 1;
		trim(key);
		trim(val);

		if (!strcmp(key, "music_dir"))
			util_copy(cfg.music_dir, sizeof(cfg.music_dir), val);
		else if (!strcmp(key, "last_path"))
			util_copy(cfg.last_path, sizeof(cfg.last_path), val);
		else if (!strcmp(key, "last_pos"))
			cfg.last_pos = atof(val);
		else if (!strcmp(key, "last_valid"))
			cfg.last_valid = geti(val);
		else if (!strcmp(key, "recursive"))
			cfg.recursive = geti(val);
		else if (!strcmp(key, "repeat"))
			cfg.repeat = geti(val);
		else if (!strcmp(key, "shuffle"))
			cfg.shuffle = geti(val);
		else if (!strcmp(key, "theme"))
			cfg.theme = geti(val);
		else if (!strcmp(key, "vis"))
			cfg.vis = geti(val);
		else if (!strcmp(key, "smooth"))
			cfg.smooth = geti(val);
		else if (!strcmp(key, "quality"))
			cfg.quality = geti(val);
		else if (!strcmp(key, "samplerate"))
			cfg.samplerate = geti(val);
		else if (!strcmp(key, "start_player"))
			cfg.start_player = geti(val);
		else if (!strcmp(key, "resume"))
			cfg.resume = geti(val);
		else if (!strcmp(key, "scanlines"))
			cfg.scanlines = geti(val);
		else if (!strcmp(key, "sort"))
			cfg.sort = geti(val);
		else if (!strcmp(key, "show_splash"))
			cfg.show_splash = geti(val);
		else if (!strcmp(key, "hints"))
			cfg.hints = geti(val);
		else if (!strcmp(key, "fps"))
			cfg.fps = geti(val);
		else if (!strcmp(key, "page_slide"))
			cfg.page_slide = geti(val);
		else if (!strcmp(key, "lock_key"))
			cfg.lock_key = geti(val);
		else if (!strcmp(key, "vis_style"))
			cfg.vis_style = geti(val);
		else if (!strncmp(key, "vis_p", 5) && key[5] >= '0' &&
			 key[5] <= '9') {
			int vi = key[5] - '0';

			if (vi < VIS_STYLE_COUNT)
				cfg.vis_p[vi] = geti(val);
		} else if (!strncmp(key, "vis_q", 5) && key[5] >= '0' &&
			   key[5] <= '9') {
			int vi = key[5] - '0';

			if (vi < VIS_STYLE_COUNT)
				cfg.vis_p2[vi] = geti(val);
		}
	}
	fclose(f);

	if (cfg.theme < 0 || cfg.theme >= THEME_COUNT)
		cfg.theme = 0;
	if (cfg.repeat < 0 || cfg.repeat > REPEAT_ALL)
		cfg.repeat = REPEAT_ALL;
	if (cfg.vis < 0 || cfg.vis >= VIS_MODE_COUNT)
		cfg.vis = VIS_SPECTRUM;
	if (cfg.quality < 0 || cfg.quality > QUAL_HIGH)
		cfg.quality = QUAL_NORMAL;
	if (cfg.samplerate < 22050 || cfg.samplerate > 48000)
		cfg.samplerate = 44100;
	if (cfg.sort < 0 || cfg.sort > 2)
		cfg.sort = 0;
	if (cfg.hints < 0 || cfg.hints > 2)
		cfg.hints = 0;
	if (cfg.fps != 15 && cfg.fps != 30 && cfg.fps != 45 && cfg.fps != 60)
		cfg.fps = 30;
	if (cfg.page_slide < 0 || cfg.page_slide > 1)
		cfg.page_slide = 1;
	if (cfg.lock_key < 0 || cfg.lock_key > 2)
		cfg.lock_key = 0;
	if (cfg.vis_style < 0 || cfg.vis_style >= VIS_STYLE_COUNT)
		cfg.vis_style = 0;
	{
		int i;

		for (i = 0; i < VIS_STYLE_COUNT; i++) {
			if (cfg.vis_p[i] < 0 || cfg.vis_p[i] > 15)
				cfg.vis_p[i] = 0;
			if (cfg.vis_p2[i] < 0 || cfg.vis_p2[i] > 15)
				cfg.vis_p2[i] = 0;
		}
	}
	return 1;
}

int config_save(void)
{
	FILE *f;
	char tmp[MAX_PATH + 8];

	if (!cfg_path[0])
		pick_dir();
	util_mkdirs(cfg_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", cfg_path);
	f = fopen(tmp, "w");
	if (!f)
		return -1;

	fprintf(f, "# CHIPTRACKER config\n");
	fprintf(f, "music_dir=%s\n", cfg.music_dir);
	fprintf(f, "last_path=%s\n", cfg.last_path);
	fprintf(f, "last_pos=%.2f\n", cfg.last_pos);
	fprintf(f, "last_valid=%d\n", cfg.last_valid);
	fprintf(f, "recursive=%d\n", cfg.recursive);
	fprintf(f, "repeat=%d\n", cfg.repeat);
	fprintf(f, "shuffle=%d\n", cfg.shuffle);
	fprintf(f, "theme=%d\n", cfg.theme);
	fprintf(f, "vis=%d\n", cfg.vis);
	fprintf(f, "smooth=%d\n", cfg.smooth);
	fprintf(f, "quality=%d\n", cfg.quality);
	fprintf(f, "samplerate=%d\n", cfg.samplerate);
	fprintf(f, "start_player=%d\n", cfg.start_player);
	fprintf(f, "resume=%d\n", cfg.resume);
	fprintf(f, "scanlines=%d\n", cfg.scanlines);
	fprintf(f, "sort=%d\n", cfg.sort);
	fprintf(f, "show_splash=%d\n", cfg.show_splash);
	fprintf(f, "hints=%d\n", cfg.hints);
	fprintf(f, "fps=%d\n", cfg.fps);
	fprintf(f, "page_slide=%d\n", cfg.page_slide);
	fprintf(f, "lock_key=%d\n", cfg.lock_key);
	fprintf(f, "vis_style=%d\n", cfg.vis_style);
	{
		int i;

		for (i = 0; i < VIS_STYLE_COUNT; i++)
			fprintf(f, "vis_p%d=%d\n", i, cfg.vis_p[i]);
		for (i = 0; i < VIS_STYLE_COUNT; i++)
			fprintf(f, "vis_q%d=%d\n", i, cfg.vis_p2[i]);
	}
	fclose(f);

	rename(tmp, cfg_path);
	return 0;
}
