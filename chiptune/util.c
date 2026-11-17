/*
 * util.c - paths, strings, UTF-8 transliteration, small helpers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libopenmpt/libopenmpt.h>

#include "app.h"

void util_copy(char *dst, size_t n, const char *src)
{
	if (n == 0)
		return;
	if (!src) {
		dst[0] = 0;
		return;
	}
	while (--n && *src)
		*dst++ = *src++;
	*dst = 0;
}

void util_join(char *dst, size_t n, const char *dir, const char *name)
{
	size_t len;

	if (!dir || !*dir) {
		util_copy(dst, n, name);
		return;
	}
	util_copy(dst, n, dir);
	len = strlen(dst);
	if (len && dst[len - 1] == '/') {
		util_copy(dst + len, n - len, name);
	} else {
		util_copy(dst + len, n - len, "/");
		len = strlen(dst);
		util_copy(dst + len, n - len, name);
	}
}

const char *util_basename(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

void util_dirname(const char *p, char *out, size_t n)
{
	const char *s = strrchr(p, '/');

	if (!s) {
		util_copy(out, n, ".");
		return;
	}
	if (s == p) {
		util_copy(out, n, "/");
		return;
	}
	{
		size_t len = (size_t)(s - p);
		if (len >= n)
			len = n - 1;
		memcpy(out, p, len);
		out[len] = 0;
	}
}

void util_stem(char *dst, size_t n, const char *name)
{
	const char *dot;
	size_t len;

	util_copy(dst, n, name);
	dot = strrchr(dst, '.');
	if (!dot || dot == dst)
		return;
	len = (size_t)(dot - dst);
	dst[len] = 0;
}

void util_upper(char *dst, size_t n, const char *src)
{
	size_t i;

	for (i = 0; i + 1 < n && src[i]; i++)
		dst[i] = (char)toupper((unsigned char)src[i]);
	dst[i] = 0;
}

void util_lower(char *dst, size_t n, const char *src)
{
	size_t i;

	for (i = 0; i + 1 < n && src[i]; i++)
		dst[i] = (char)tolower((unsigned char)src[i]);
	dst[i] = 0;
}

void util_time(int sec, char *out, size_t n)
{
	if (sec < 0)
		sec = 0;
	if (sec >= 3600)
		snprintf(out, n, "%d:%02d:%02d", sec / 3600,
			 (sec / 60) % 60, sec % 60);
	else
		snprintf(out, n, "%02d:%02d", sec / 60, sec % 60);
}

void util_size(unsigned int bytes, char *out, size_t n)
{
	if (bytes >= 1024u * 1024u)
		snprintf(out, n, "%.1fM", bytes / (1024.0 * 1024.0));
	else if (bytes >= 1024u)
		snprintf(out, n, "%uK", (bytes + 512) / 1024);
	else
		snprintf(out, n, "%uB", bytes);
}

int util_ci_cmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		int ca = tolower((unsigned char)*a);
		int cb = tolower((unsigned char)*b);
		if (ca != cb)
			return ca - cb;
		if (!ca)
			return 0;
	}
}

int util_nat_cmp(const char *a, const char *b)
{
	while (*a && *b) {
		if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
			unsigned long va = 0, vb = 0;
			while (isdigit((unsigned char)*a))
				va = va * 10 + (unsigned long)(*a++ - '0');
			while (isdigit((unsigned char)*b))
				vb = vb * 10 + (unsigned long)(*b++ - '0');
			if (va != vb)
				return va < vb ? -1 : 1;
			continue;
		}
		{
			int ca = tolower((unsigned char)*a);
			int cb = tolower((unsigned char)*b);
			if (ca != cb)
				return ca - cb;
		}
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

/*
 * UTF-8 (Cyrillic) -> readable ASCII.  The tracker scene is Latin, but SD
 * cards often contain Russian file names.  A compact transliteration keeps
 * the pixel font usable without shipping a second alphabet.
 */
void util_ascii(const char *in, char *out, size_t n)
{
	static const char *cyr_upper[] = {
		"A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L",
		"M", "N", "O", "P", "R", "S", "T", "U", "F", "Kh", "Ts", "Ch",
		"Sh", "Shch", "", "Y", "", "E", "Yu", "Ya"
	};
	static const char *cyr_lower[] = {
		"a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l",
		"m", "n", "o", "p", "r", "s", "t", "u", "f", "kh", "ts", "ch",
		"sh", "shch", "", "y", "", "e", "yu", "ya"
	};
	size_t o = 0;

	while (*in && o + 1 < n) {
		unsigned char c = (unsigned char)*in;

		if (c < 0x80) {
			out[o++] = (char)c;
			in++;
			continue;
		}
		if ((c == 0xD0 || c == 0xD1) && in[1]) {
			unsigned char d = (unsigned char)in[1];
			int idx = -1;
			int upper = (c == 0xD0 && d <= 0xAF);

			if (c == 0xD0) {
				if (d >= 0x90 && d <= 0xAF)
					idx = d - 0x90;
				else if (d == 0x81) {	/* Ё */
					if (o + 3 < n) {
						out[o++] = 'Y';
						out[o++] = 'o';
					}
					in += 2;
					continue;
				} else if (d >= 0xB0 && d <= 0xBF)
					idx = d - 0xB0;
			} else {
				if (d >= 0x80 && d <= 0x8F)
					idx = 16 + (d - 0x80);
				else if (d == 0x91) {	/* ё */
					if (o + 3 < n) {
						out[o++] = 'y';
						out[o++] = 'o';
					}
					in += 2;
					continue;
				}
			}
			if (idx >= 0) {
				const char *tr = upper ? cyr_upper[idx] : cyr_lower[idx];
				while (*tr && o + 1 < n)
					out[o++] = *tr++;
				in += 2;
				continue;
			}
		}
		/* common punctuation */
		if (c == 0xE2 && in[1] == 0x80) {
			if ((unsigned char)in[2] == 0xA6) {	/* … */
				if (o + 4 < n) { memcpy(out + o, "...", 3); o += 3; }
				in += 3;
				continue;
			}
			in += 3;
			continue;
		}
		if (c == 0xC2 && in[1] == 0xAB) {	/* « */
			out[o++] = '"';
			in += 2;
			continue;
		}
		if (c == 0xC2 && in[1] == 0xBB) {	/* » */
			out[o++] = '"';
			in += 2;
			continue;
		}
		in++;
	}
	out[o] = 0;
	{
		char *p = out, *q = out;
		int space = 0;
		while (*p == ' ')
			p++;
		while (*p) {
			if (*p == ' ') {
				if (space) {
					p++;
					continue;
				}
				space = 1;
			} else {
				space = 0;
			}
			*q++ = *p++;
		}
		while (q > out && q[-1] == ' ')
			q--;
		*q = 0;
	}
}

int util_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

int util_isdir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int util_isfile(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int util_mkdirs(const char *path)
{
	char tmp[MAX_PATH];
	size_t i, len;

	util_copy(tmp, sizeof(tmp), path);
	len = strlen(tmp);
	for (i = 1; i < len; i++) {
		if (tmp[i] == '/') {
			tmp[i] = 0;
			mkdir(tmp, 0755);
			tmp[i] = '/';
		}
	}
	return mkdir(tmp, 0755) == 0 || util_isdir(tmp);
}

int util_read_file(const char *path, unsigned char **buf, size_t *size)
{
	FILE *f;
	long len;

	*buf = NULL;
	*size = 0;
	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	len = ftell(f);
	if (len < 0 || len > 32 * 1024 * 1024) {
		fclose(f);
		return -1;
	}
	rewind(f);
	*buf = (unsigned char *)malloc((size_t)len + 1);
	if (!*buf) {
		fclose(f);
		return -1;
	}
	if (fread(*buf, 1, (size_t)len, f) != (size_t)len) {
		free(*buf);
		*buf = NULL;
		fclose(f);
		return -1;
	}
	(*buf)[len] = 0;
	*size = (size_t)len;
	fclose(f);
	return 0;
}

const char *util_ext(const char *name)
{
	const char *dot = strrchr(name, '.');

	return dot ? dot + 1 : "";
}

int util_supported_ext(const char *ext)
{
	char low[16];

	if (!ext || !*ext)
		return 0;
	util_lower(low, sizeof(low), ext);
	return openmpt_is_extension_supported(low) != 0;
}

static int util_read_int(const char *path, int *v)
{
	FILE *f;
	int n;

	f = fopen(path, "r");
	if (!f)
		return -1;
	n = fscanf(f, "%d", v);
	fclose(f);
	return n == 1 ? 0 : -1;
}

static int bat_cached = -2;
static unsigned int bat_at = 0;

int util_battery(void)
{
	unsigned int now = SDL_GetTicks();
	const char *e = getenv("CHIP_BATTERY");
	int v = -1;

	if (e && *e)
		return atoi(e);
	if (bat_cached != -2 && now - bat_at < 2000)
		return bat_cached;
	bat_at = now;
	if (util_read_int("/tmp/current_battery_percentage", &v) != 0 &&
	    util_read_int("/sys/class/power_supply/axp20x-battery/capacity",
			  &v) != 0 &&
	    util_read_int("/sys/class/power_supply/battery/capacity", &v) != 0)
		v = -1;
	if (v > 100)
		v = 100;
	if (v < 0)
		v = -1;
	bat_cached = v;
	return v;
}

static int usb_cached = -1;
static unsigned int usb_at = 0;

int util_usb_present(void)
{
	unsigned int now = SDL_GetTicks();
	int v = 0;

	if (usb_cached != -1 && now - usb_at < 2000)
		return usb_cached;
	usb_at = now;
	if (util_read_int("/sys/class/power_supply/axp20x-usb/present", &v) != 0)
		v = 0;
	usb_cached = v != 0;
	return usb_cached;
}
