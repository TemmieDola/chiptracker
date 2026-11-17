/*
 * scan.c - incremental directory scan + lazy metadata probing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <SDL/SDL.h>

#include <libopenmpt/libopenmpt.h>

#include "app.h"

ScanState scan;
unsigned int lib_generation = 1;

static char **stack = NULL;
static int stack_len = 0;
static int stack_cap = 0;
static DIR *cur_dir = NULL;
static char cur_path[MAX_PATH];

static int push_dir(const char *path)
{
	if (stack_len >= 8192)
		return -1;
	if (stack_len >= stack_cap) {
		int cap = stack_cap ? stack_cap * 2 : 64;
		char **p = (char **)realloc(stack, (size_t)cap * sizeof(char *));
		if (!p)
			return -1;
		stack = p;
		stack_cap = cap;
	}
	stack[stack_len] = strdup(path);
	if (!stack[stack_len])
		return -1;
	stack_len++;
	return 0;
}

static void free_stack(void)
{
	int i;

	for (i = 0; i < stack_len; i++)
		free(stack[i]);
	free(stack);
	stack = NULL;
	stack_len = 0;
	stack_cap = 0;
}

static void close_current(void)
{
	if (cur_dir) {
		closedir(cur_dir);
		cur_dir = NULL;
	}
}

static void add_file(const char *path, const char *name, const struct stat *st)
{
	Track *t;
	const char *ext = util_ext(name);

	if (track_count >= MAX_TRACKS)
		return;
	if (!util_supported_ext(ext))
		return;
	t = &tracks[track_count];
	memset(t, 0, sizeof(*t));
	util_copy(t->path, sizeof(t->path), path);
	util_copy(t->file, sizeof(t->file), name);
	util_stem(t->stem, sizeof(t->stem), name);
	util_upper(t->fmt, sizeof(t->fmt), ext);
	t->size = (unsigned int)st->st_size;
	t->duration = -1;
	t->meta = 0;
	track_count++;
	scan.files = track_count;
}

static int add_root_if_file(const char *root)
{
	struct stat st;

	if (stat(root, &st) == 0 && S_ISREG(st.st_mode)) {
		scan.root[0] = 0;
		util_copy(scan.root, sizeof(scan.root), root);
		add_file(root, util_basename(root), &st);
		scan.done = 1;
		scan.active = 0;
		return 1;
	}
	return 0;
}

void scan_begin(const char *root, int recursive)
{
	scan_abort();
	lib_generation++;
	track_count = 0;
	memset(&scan, 0, sizeof(scan));
	util_copy(scan.root, sizeof(scan.root), root ? root : "");
	scan.recursive = recursive;
	scan.active = 1;
	scan.done = 0;
	lib_sel_reset();
	player.ctx_count = 0;
	if (!root || !*root) {
		scan.active = 0;
		scan.done = 1;
		return;
	}
	if (add_root_if_file(root))
		return;
	if (!util_isdir(root)) {
		scan.active = 0;
		scan.done = 1;
		scan.errors = 1;
		return;
	}
	push_dir(root);
}

void scan_abort(void)
{
	close_current();
	free_stack();
	scan.active = 0;
}

void scan_free(void)
{
	scan_abort();
}

void scan_sort(void)
{
	tracks_sort(cfg.sort);
}

int scan_poll(int budget_ms)
{
	unsigned int start = SDL_GetTicks();

	if (!scan.active || scan.done)
		return 1;

	for (;;) {
		struct dirent *de;
		char path[MAX_PATH];
		struct stat st;

		if (!cur_dir) {
			if (stack_len == 0) {
				scan.active = 0;
				scan.done = 1;
				scan.elapsed_ms = SDL_GetTicks() - start;
				tracks_sort(cfg.sort);
				lib_sel_clamp();
				return 1;
			}
			util_copy(cur_path, sizeof(cur_path), stack[--stack_len]);
			free(stack[stack_len]);
			cur_dir = opendir(cur_path);
			if (!cur_dir) {
				scan.errors++;
				continue;
			}
			scan.dirs++;
			continue;
		}

		de = readdir(cur_dir);
		if (!de) {
			close_current();
			continue;
		}
		if (de->d_name[0] == '.')
			continue;

		util_join(path, sizeof(path), cur_path, de->d_name);
		if (stat(path, &st) != 0)
			continue;

		if (S_ISDIR(st.st_mode)) {
			if (scan.recursive)
				push_dir(path);
			continue;
		}
		if (S_ISREG(st.st_mode))
			add_file(path, de->d_name, &st);

		if (SDL_GetTicks() - start >= (unsigned int)budget_ms)
			return 0;
	}
}

static int cmp_tracks(const void *pa, const void *pb)
{
	const Track *a = (const Track *)pa;
	const Track *b = (const Track *)pb;
	int r;

	switch (cfg.sort) {
	case 1:	/* format, then name */
		r = util_ci_cmp(a->fmt, b->fmt);
		if (r)
			return r;
		break;
	case 2:	/* size, largest first */
		if (a->size != b->size)
			return (a->size < b->size) ? 1 : -1;
		break;
	default:	/* natural name */
		break;
	}
	r = util_nat_cmp(a->stem, b->stem);
	if (r)
		return r;
	return util_nat_cmp(a->path, b->path);
}

void tracks_sort(int mode)
{
	if (mode < 0 || mode > 2)
		mode = 0;
	cfg.sort = mode;
	qsort(tracks, (size_t)track_count, sizeof(Track), cmp_tracks);
	lib_generation++;
}

static void no_log(const char *message, void *user)
{
	(void)message;
	(void)user;
}

int probe_track(Track *t)
{
	unsigned char *data = NULL;
	size_t size = 0;
	openmpt_module *mod;
	double dur;

	if (!t)
		return -1;
	if (t->meta)
		return t->meta == 1 ? 0 : -1;
	if (util_read_file(t->path, &data, &size) != 0) {
		t->meta = 2;
		return -1;
	}
	mod = openmpt_module_create_from_memory2(data, size, no_log, NULL,
						 NULL, NULL, NULL, NULL, NULL);
	free(data);
	if (!mod) {
		t->meta = 2;
		return -1;
	}
	t->channels = openmpt_module_get_num_channels(mod);
	t->orders = openmpt_module_get_num_orders(mod);
	dur = openmpt_module_get_duration_seconds(mod);
	t->duration = dur > 0 ? (int)(dur + 0.5) : 0;
	if (t->duration > 24 * 3600)
		t->duration = 0;
	{
		const char *s = openmpt_module_get_metadata(mod, "type_long");
		if (!s || !*s) {
			if (s)
				openmpt_free_string(s);
			s = openmpt_module_get_metadata(mod, "type");
		}
		util_ascii(s ? s : "", t->tracker, sizeof(t->tracker));
		if (s)
			openmpt_free_string(s);
	}
	t->meta = 1;
	openmpt_module_destroy(mod);
	return 0;
}
