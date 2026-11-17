/*
 * info.c - track information: metadata, song message, sample list.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

#define INFO_LINES 6
#define SAMPLE_ROWS 15

static char msg_lines[96][44];
static int msg_count = 0;
static char msg_path[MAX_PATH];

static void wrap_message(const char *src)
{
	char ascii[4096];
	const char *p = ascii;
	int line = 0;

	util_ascii(src ? src : "", ascii, sizeof(ascii));
	msg_count = 0;
	if (!ascii[0])
		return;

	while (*p && line < 96) {
		char *dst = msg_lines[line];
		int col = 0;

		while (*p == '\r')
			p++;
		if (*p == '\n') {
			p++;
			dst[0] = 0;
			line++;
			continue;
		}
		while (*p && *p != '\n' && *p != '\r' && col < 42) {
			dst[col++] = *p++;
		}
		dst[col] = 0;
		/* soft-wrap long lines at the last space */
		if (col == 42 && *p && *p != ' ' && *p != '\n') {
			int k = col - 1;
			while (k > 12 && dst[k] != ' ')
				k--;
			if (k > 12) {
				int rest = col - k - 1;
				char tail[44];
				memcpy(tail, dst + k + 1, (size_t)rest);
				tail[rest] = 0;
				dst[k] = 0;
				line++;
				if (line < 96)
					util_copy(msg_lines[line], sizeof(msg_lines[0]),
						  tail);
				line++;
				continue;
			}
		}
		line++;
	}
	msg_count = line;
}

static void ensure_message(void)
{
	if (!strcmp(msg_path, player.track.path))
		return;
	util_copy(msg_path, sizeof(msg_path), player.track.path);
	msg_count = 0;
	msg_lines[0][0] = 0;
	{
		char *m = audio_meta("message");
		if (m && *m)
			wrap_message(m);
		if (m)
			free(m);
	}
}

static void field(int y, const char *label, const char *value, Uint32 vc)
{
	gfx_text(6, y, label, pal.dim);
	gfx_text_ellipsis(84, y, 232, value, vc);
}

void info_update(void)
{
	int maxscroll;

	ensure_message();
	if (player.info_tab == 0)
		maxscroll = msg_count > INFO_LINES ? msg_count - INFO_LINES : 0;
	else
		maxscroll = audio_num_samples() > SAMPLE_ROWS ?
			audio_num_samples() - SAMPLE_ROWS : 0;
	if (player.info_scroll < 0)
		player.info_scroll = 0;
	if (player.info_scroll > maxscroll)
		player.info_scroll = maxscroll;
}

void info_draw(void)
{
	AudioSnap s;
	char buf[192];
	char dur[16], size[16];

	audio_snapshot(&s);
	ensure_message();
	gfx_clear(pal.bg);

	gfx_fill(0, 0, SW, 31, pal.panel);
	gfx_hline(0, 31, SW, pal.accent);
	gfx_text2(6, 5, "TRACK INFO", pal.bright);

	gfx_text(150, 10, "INFO", player.info_tab == 0 ? pal.accent : pal.dim);
	gfx_text(186, 10, "SAMPLES", player.info_tab == 1 ? pal.accent : pal.dim);
	gfx_hline(150, 18, 24, player.info_tab == 0 ? pal.accent : pal.line);
	gfx_hline(186, 18, 42, player.info_tab == 1 ? pal.accent : pal.line);

	gfx_text_ellipsis(6, 34, 234, player.title, pal.bright);
	snprintf(buf, sizeof(buf), "%s  %s", player.track.fmt,
		 player.tracker[0] ? player.tracker : audio_format_name());
	gfx_text_ellipsis(6, 42, 234, buf, pal.accent_dim);

	if (player.info_tab == 0) {
		util_time((int)(s.dur > 0 ? s.dur : 0), dur, sizeof(dur));
		util_size(player.track.size, size, sizeof(size));

		snprintf(buf, sizeof(buf), "%s  %s", player.track.fmt,
			 audio_format_name());
		field(54, "FORMAT", buf, pal.text);
		field(66, "TRACKER", player.tracker[0] ? player.tracker : "UNKNOWN",
		      pal.text);
		snprintf(buf, sizeof(buf), "%d", s.channels);
		field(78, "CHANNELS", buf, pal.text);
		snprintf(buf, sizeof(buf), "%d  /  PATTERNS %d", s.orders,
			 audio_num_patterns());
		field(90, "ORDERS", buf, pal.text);
		snprintf(buf, sizeof(buf), "%d", audio_num_samples());
		field(102, "SAMPLES", buf, pal.text);
		field(114, "LENGTH", dur, pal.text);
		snprintf(buf, sizeof(buf), "%s   SUBSONGS %d", size,
			 audio_num_subsongs());
		field(126, "FILE", buf, pal.text);

		gfx_text(6, 140, "MESSAGE", pal.accent);
		gfx_hline(6, 149, 228, pal.line);
		if (msg_count == 0) {
			gfx_text(6, 154, "NO MESSAGE IN THIS MODULE", pal.dim);
		} else {
			int i;
			for (i = 0; i < INFO_LINES; i++) {
				int n = player.info_scroll + i;
				if (n >= msg_count)
					break;
				gfx_text(6, 154 + i * 9, msg_lines[n], pal.text);
			}
			if (msg_count > INFO_LINES) {
				snprintf(buf, sizeof(buf), "%d/%d",
					 player.info_scroll + 1, msg_count);
				gfx_text_right(234, 140, buf, pal.dim);
			}
		}
	} else {
		int n = audio_num_samples();
		int i;

		snprintf(buf, sizeof(buf), "SAMPLES  %d", n);
		gfx_text(6, 54, buf, pal.accent);
		gfx_hline(6, 63, 228, pal.line);
		if (n == 0) {
			gfx_text(6, 70, "NO SAMPLE NAMES", pal.dim);
		} else {
			for (i = 0; i < SAMPLE_ROWS; i++) {
				int idx = player.info_scroll + i;
				char *nm;
				char nmbuf[128];
				if (idx >= n)
					break;
				nm = audio_sample_name(idx);
				util_ascii(nm && *nm ? nm : "(unnamed)", nmbuf,
					   sizeof(nmbuf));
				snprintf(buf, sizeof(buf), "%02d  %s", idx + 1,
					 nmbuf);
				gfx_text_ellipsis(6, 68 + i * 9, 232, buf,
						  i % 2 ? pal.text : pal.bright);
				if (nm)
					free(nm);
			}
		}
	}

	gfx_fill(0, 212, SW, SH - 212, pal.bg);
	gfx_hline(0, 212, SW, pal.line);
	{
		int off = ui_hints_offset();
		if (off < 20) {
			gfx_text(6, 214 + off,
				 "LEFT/RIGHT: TAB  UP/DOWN: SCROLL",
				 pal.dim);
			gfx_hint(6, 226 + off, 'B', pal.btn_b, "BACK", pal.text);
			gfx_text(66, 228 + off, "L/R: TRACK", pal.dim);
		}
	}
}

void info_key(SDLKey k)
{
	switch (k) {
	case SDLK_b:
		screen = player.info_back;
		break;
	case SDLK_l:
	case SDLK_r:
		player.info_tab = !player.info_tab;
		player.info_scroll = 0;
		break;
	case SDLK_m:
		player_next(-1);
		player.info_scroll = 0;
		break;
	case SDLK_n:
		player_next(1);
		player.info_scroll = 0;
		break;
	case SDLK_u:
		player.info_scroll--;
		break;
	case SDLK_d:
		player.info_scroll++;
		break;
	default:
		break;
	}
}
