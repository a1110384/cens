//compile: emcc hello.c -o script.js -s USE_SDL=2 -s EXPORTED_RUNTIME_METHODS=['ccall'] -s NO_EXIT_RUNTIME=1

#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <emscripten/emscripten.h>

#define SAMPLE_RATE 44100
#define TWO_PI 6.283185
#define CHUNK_SIZE 1024
#define SINE_LENGTH 1024

#define resShift 3
const unsigned int res = 1 << resShift;
#define oscAmt (128 << resShift)
const short oscs2 = oscAmt * 2;

const int CPS = (SAMPLE_RATE / CHUNK_SIZE);
const float cpsInv = 1.0f / (float)CPS;

const double lengthMult = SINE_LENGTH / (double)SAMPLE_RATE;
const float byteMult = 1.0f / 256.0f;
const float activeMult = 1.0f / 6.0f; 

unsigned short volume = 0.1f * 0xFFFF;
int paused = 0;
char started = 0;
int cSample = 0;

short sineWave[SINE_LENGTH];
float mtfs[oscAmt];

unsigned short sineStarts[oscAmt];
unsigned short* cVols;

#define bufferLength 512
static unsigned char buffer[bufferLength][oscAmt][2];

static int cStep = 0;



float mtf(float m) { return 440 * powf(2.0f, (m - 69) / 12.0f); }
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
float lerp(float a, float b, float t) { return a + t * (b - a); }
int min(int a, int b) { return (a < b) ? a : b; }
int k2m(float note7, int key[]) {
	float oct = note7 / 7.0f;
	int note = (int)note7 - ((int)oct * 7.0f);
	return min(((int)oct * 12 + key[note]), oscAmt - 1);
}
int rani(int min, int max) { return (rand() % (max - min)) + min; }


const int cKey[] = { 0, 2, 4, 5, 7, 9, 11 };

void setFV(int offStep, int freq, float val) {
	if (freq >= oscAmt || freq < 0) { return; }
	
	int pos = (cStep + offStep) % bufferLength;
	unsigned short v = clampf(val, 0.0f, 1.0f) * 255;

	buffer[pos][freq][0] = clamp((unsigned short)buffer[pos][freq][0] + v, 0, 255); 
	buffer[pos][freq][1] = clamp((unsigned short)buffer[pos][freq][1] + v, 0, 255);
}

void generate() {

    if (rand() < (RAND_MAX * 0.05f)) {

        float length = 2.5f;

        int stepLength = length * CPS;
        //int pitch = (rand() % 10 + 70) << resShift;
        int pitch = k2m(rani(27, 39), cKey) << resShift;

        for (int i = 0; i < stepLength; i++) {
            float time = i * cpsInv;

            float v = lerp(1.0f, 0.0f, time / length);
            setFV(i, pitch, v);
        }

    }
}




unsigned short* getVols() {
    static unsigned short vols[oscAmt * 4];

    int prev = cStep - 1;
	if (prev < 0) { prev = bufferLength - 1; }

    for (int osc = 0; osc < oscAmt; osc++) {
		//Current step
		short l = buffer[cStep][osc][0];
		short r = buffer[cStep][osc][1];
		vols[osc * 2] = l * l * activeMult;
		vols[osc * 2 + 1] = r * r * activeMult;

		//Previous step
		l = buffer[prev][osc][0];
		r = buffer[prev][osc][1];
		vols[osc * 2 + oscAmt * 2] = l * l * activeMult;
		vols[osc * 2 + 1 + oscAmt * 2] = r * r * activeMult;

		//Clear previous step
		buffer[prev][osc][0] = 0;
		buffer[prev][osc][1] = 0;
	}

    cStep++; if (cStep >= bufferLength) { cStep -= bufferLength; }
    return vols;
}






EMSCRIPTEN_KEEPALIVE
void process(void *userdata, Uint8 *stream, int len) {
    short *buffer = (short *)stream;
    int samples = len / sizeof(short) / 2;
    int andVal = SINE_LENGTH - 1;

    float timeMult = 1.0f / samples;

    generate();
    cVols = getVols();

    for (int i = 0; i < samples; i++) {

        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;

        float time = i * timeMult;

        for (int osc = 0; osc < oscAmt; osc++) {

            if (cVols[osc * 2] == 0 && cVols[osc * 2 + 1] == 0 && cVols[osc * 2 + oscs2] == 0 && cVols[osc * 2 + 1 + oscs2] == 0) { continue; }


            unsigned int index = (unsigned int)((i + cSample) * mtfs[osc] + sineStarts[osc]) & andVal;
            
            short volL = lerp(cVols[osc * 2 + oscs2], cVols[osc * 2], time);
            short volR = lerp(cVols[osc * 2 + 1 + oscs2], cVols[osc * 2 + 1], time);

            buffer[i * 2] += ((int)sineWave[index] * volL >> 16) * volume >> 16;
            buffer[i * 2 + 1] += ((int)sineWave[index] * volR >> 16) * volume >> 16;
        }
        
    }
    cSample += samples;
}
 
void initAudioData() {
    float oscMult = sqrt(1.0 / oscAmt);

    for (int i = 0; i < SINE_LENGTH; i++) {
        sineWave[i] = (short)(sinf(i * TWO_PI / SINE_LENGTH) * 0x7FFF);
    }
    for (int osc = 0; osc < oscAmt; osc++) {
        mtfs[osc] = mtf(osc / (float)res) * lengthMult;
        sineStarts[osc] = rand() & 0xFFFF;
    }
}

EMSCRIPTEN_KEEPALIVE
void setVol(float v) { volume = v * 0.01f * 0xFFFF; }

EMSCRIPTEN_KEEPALIVE
void init_audio() {
    if (!started) {

        if (SDL_GetAudioStatus() == SDL_AUDIO_PLAYING) {
            printf("Audio already playing.\n");
            return;
        }

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
            return;
        }

        SDL_AudioSpec want;
        SDL_zero(want);

        want.freq = SAMPLE_RATE;
        want.format = AUDIO_S16; // Use floating point audio
        want.channels = 2;       // Mono
        want.samples = CHUNK_SIZE;
        want.callback = process;

        initAudioData();

        if (SDL_OpenAudio(&want, NULL) < 0) {
            printf("Failed to open audio: %s\n", SDL_GetError());
        } else {
            SDL_PauseAudio(0); // Start playing
            printf("Audio started successfully\n");
            started = 1;
        }
    } else {
        SDL_PauseAudio(!paused);
        paused = !paused;
    }
}

EMSCRIPTEN_KEEPALIVE
void stop_audio() {
    if (started) {
        SDL_CloseAudio();
        SDL_Quit();
        started = 0;
        printf("Audio closed.\n");
    }
}