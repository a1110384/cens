#include <math.h>

float clampf(float v, float lo, float hi) {
	if (v > hi) { return hi; }
	if (v < lo) { return lo; }
	return v;
}
int clamp(int v, int lo, int hi) {
	if (v > hi) { return hi; }
	if (v < lo) { return lo; }
	return v;
}
int min(int a, int b) { return (a < b) ? a : b; }

float mtf(float m) { return 440 * powf(2.0f, (m - 69) / 12.0f); }
float ftm(float f) { return 12 * (logf(f / 220) / logf(2.0f)) + 57; }
float harmonic(float n, int har) { return ftm(mtf(n) * (har + 1)); }

int rani(int min, int max) { return (rand() % (max - min)) + min; }
float ranf() { return rand() / (float)RAND_MAX; }
float ranfIn(float min, float max) { return rand() / (float)RAND_MAX * (max - min) + min; }

float lerp(float a, float b, float t) { return a + t * (b - a); }
unsigned char lerpByte(unsigned char a, unsigned char b, float t) {
	if (t < 0.0f) { return a; }
	if (t > 1.0f) { return b; }
	return a + t * (b - a);
}


float envADSR(float t, float l, float a, float d, float s, float r, float c) {
	if (t < a) { return powf(t / a, c); } //Attack
	if (t > a && t < a + d) { return powf(1.0f - (((t - a) / d) * (1.0f - s)), c); } //Decay
	if (t > l) { return powf(s - (t - l) / r, 1.5f); } //Release
	if (t > l + r || t < 0.0f) { return 0.0f; } //Before/After
	return s; //Sustain
}
float envEq(float t, float center, float distance) {
	if (t < center - distance || t > center + distance) { return 0.0f; } //Outside bounds
	if (t < center) { return (t - (center - distance)) / distance; } //Lower slope
	return (t - center) / distance; //Upper slope
}

static int fAh[] = { 76, 85, 100 };
static int fEe[] = { 62, 94, 101 };
static int fEh[] = { 67, 92, 99 };
static int fOh[] = { 67, 79, 99 };
static int fOo[] = { 65, 74, 100 };

