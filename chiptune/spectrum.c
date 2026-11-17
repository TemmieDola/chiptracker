/*
 * spectrum.c - 512-point FFT visualizer (32 log-spaced bars).
 */
#include <math.h>
#include <string.h>

#include "app.h"

#define FFT_N 512

static float re[FFT_N];
static float im[FFT_N];
static float window[FFT_N];
static float bars[SPECTRUM_BARS];
static float peaks[SPECTRUM_BARS];
static int inited = 0;

static void init_tables(void)
{
	int i;

	if (inited)
		return;
	for (i = 0; i < FFT_N; i++)
		window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * i / (FFT_N - 1));
	inited = 1;
}

void spectrum_reset(void)
{
	memset(bars, 0, sizeof(bars));
	memset(peaks, 0, sizeof(peaks));
}

void spectrum_update(const int16_t *samples, int count, int rate)
{
	int i, j, len, k;
	float mean = 0.0f;

	init_tables();
	if (count <= 0) {
		for (i = 0; i < SPECTRUM_BARS; i++) {
			bars[i] *= 0.86f;
			peaks[i] -= 0.012f;
			if (peaks[i] < 0)
				peaks[i] = 0;
		}
		return;
	}
	if (count > FFT_N)
		count = FFT_N;
	if (count < FFT_N) {
		int off = FFT_N - count;
		for (i = 0; i < off; i++) {
			re[i] = 0.0f;
			im[i] = 0.0f;
		}
		for (i = 0; i < count; i++) {
			re[off + i] = samples[i] / 32768.0f;
			im[off + i] = 0.0f;
		}
	} else {
		for (i = 0; i < count; i++) {
			re[i] = samples[i] / 32768.0f;
			im[i] = 0.0f;
		}
	}
	for (i = 0; i < FFT_N; i++)
		mean += re[i];
	mean /= FFT_N;
	for (i = 0; i < FFT_N; i++)
		re[i] = (re[i] - mean) * window[i];

	/* bit reversal */
	for (i = 1, j = 0; i < FFT_N; i++) {
		int bit = FFT_N >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			float t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (len = 2; len <= FFT_N; len <<= 1) {
		float ang = -2.0f * 3.14159265f / (float)len;
		float wr = cosf(ang), wi = sinf(ang);
		for (i = 0; i < FFT_N; i += len) {
			float cr = 1.0f, ci = 0.0f;
			for (k = 0; k < len / 2; k++) {
				int a = i + k;
				int b = a + len / 2;
				float tr = re[b] * cr - im[b] * ci;
				float ti = re[b] * ci + im[b] * cr;
				float nr;
				re[b] = re[a] - tr;
				im[b] = im[a] - ti;
				nr = re[a] + tr;
				im[a] = im[a] + ti;
				re[a] = nr;
				{
					float ncr = cr * wr - ci * wi;
					ci = cr * wi + ci * wr;
					cr = ncr;
				}
			}
		}
	}

	for (i = 0; i < SPECTRUM_BARS; i++) {
		float f0 = 40.0f * powf(16000.0f / 40.0f, (float)i / SPECTRUM_BARS);
		float f1 = 40.0f * powf(16000.0f / 40.0f, (float)(i + 1) / SPECTRUM_BARS);
		int b0 = (int)(f0 * FFT_N / rate);
		int b1 = (int)(f1 * FFT_N / rate);
		float mag = 0.0f;
		float v;

		if (b0 < 1)
			b0 = 1;
		if (b1 <= b0)
			b1 = b0 + 1;
		if (b1 > FFT_N / 2)
			b1 = FFT_N / 2;
		for (k = b0; k < b1; k++) {
			float m = sqrtf(re[k] * re[k] + im[k] * im[k]) / (FFT_N / 4.0f);
			if (m > mag)
				mag = m;
		}
		v = log10f(1.0f + mag * 90.0f) / 1.9f;
		v *= 1.0f + (float)i / (float)SPECTRUM_BARS * 0.35f;
		if (v > 1.0f)
			v = 1.0f;
		if (v < 0.0f)
			v = 0.0f;

		if (v > bars[i])
			bars[i] = bars[i] * 0.30f + v * 0.70f;
		else
			bars[i] = bars[i] * 0.84f + v * 0.16f;
		if (bars[i] > peaks[i])
			peaks[i] = bars[i];
		else {
			peaks[i] -= 0.010f;
			if (peaks[i] < 0)
				peaks[i] = 0;
		}
	}
}

float spectrum_bar(int i)
{
	if (i < 0 || i >= SPECTRUM_BARS)
		return 0.0f;
	return bars[i];
}

float spectrum_peak(int i)
{
	if (i < 0 || i >= SPECTRUM_BARS)
		return 0.0f;
	return peaks[i];
}
