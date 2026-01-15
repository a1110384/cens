//Make sure you link to emcscripten.h (emsdk/upstream/emscripten/system/include) and sokol_audio.h.
//^^^Will have to download both if you dont already have them

//COMPILE: emcc main.c -o audio_module.js -s NO_EXIT_RUNTIME=1 -s "EXPORTED_FUNCTIONS=['_main', '_htmlInput']"

#define SOKOL_AUDIO_IMPL
#include "sokol_audio.h"
#include <emscripten.h>
#include <stdlib.h>

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "UT.h"

static bool running = false;
static bool simple = false;



int SAMPLE_RATE = 44100;
#define TWO_PI 6.283185
#define CHUNK_SIZE 512
#define SINE_LENGTH 1024

#define resShift 0
const unsigned int res = 1 << resShift;
#define oscAmt (128 << resShift)
const short oscs2 = oscAmt * 2;

int CPS;
float cpsInv;

double lengthMult;
const float byteMult = 1.0f / 256.0f;
const float activeMult = 1.0f / 6.0f; 
const float shortInv = 1.0f / 0x7FFF;

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

struct adsr {
	float l;
	float ap;
	float dp;
	float s;
	float r;
	float c;
};

int k2m(float note7, int key[]) {
	float oct = note7 / 7.0f;
	int note = (int)note7 - ((int)oct * 7.0f);
	return min(((int)oct * 12 + key[note]), oscAmt - 1);
}
float osc(float t, float amt) { return 1.0f - (sineWave[(int)(t * SINE_LENGTH) % SINE_LENGTH] / (float)0x7FFF + 1.0f) * 0.5f * amt; }


int getFormant(int vowel, int formant) {
	switch (vowel) {
	case 0: return fAh[formant] << resShift;
	case 1: return fEe[formant] << resShift;
	case 2: return fEh[formant] << resShift;
	case 3: return fOh[formant] << resShift;
	case 4: return fOo[formant] << resShift;
	}
	return 0;
}


const int cKey[] = { 0, 2, 4, 5, 7, 9, 11 };

void setFV(int offStep, int freq, float val) {
	if (freq >= oscAmt || freq < 0) { return; }
	
	int pos = (cStep + offStep) % bufferLength;
	unsigned short v = clampf(val, 0.0f, 1.0f) * 255;

	buffer[pos][freq][0] = clamp((unsigned short)buffer[pos][freq][0] + v, 0, 255); 
	buffer[pos][freq][1] = clamp((unsigned short)buffer[pos][freq][1] + v, 0, 255);
}

//Same as above but using float frequency
void setFFV(int offStep, float freq, float val, int channel) {
	if (freq >= oscAmt - 1 || freq < 0) { return; }

	int f1 = (int)freq;
	int f2 = f1 + 1;
	float diff = freq - f1;

	int pos = (cStep + offStep) % bufferLength;
	unsigned short v1 = clampf(val * sqrtf(1.0f - diff), 0.0f, 1.0f) * 255;
	unsigned short v2 = clampf(val * sqrtf(diff), 0.0f, 1.0f) * 255;

	if (channel == 2) {
		buffer[pos][f1][0] = clamp((unsigned short)buffer[pos][f1][0] + v1, 0, 255);
		buffer[pos][f1][1] = clamp((unsigned short)buffer[pos][f1][1] + v1, 0, 255);

		buffer[pos][f2][0] = clamp((unsigned short)buffer[pos][f2][0] + v2, 0, 255);
		buffer[pos][f2][1] = clamp((unsigned short)buffer[pos][f2][1] + v2, 0, 255);
	}
	else {
		buffer[pos][f1][channel] = clamp((unsigned short)buffer[pos][f1][channel] + v1, 0, 255);
		buffer[pos][f2][channel] = clamp((unsigned short)buffer[pos][f2][channel] + v2, 0, 255);
	}

}

static void synthesize(
	struct adsr vEnv,
	int freq,
	struct adsr pEnv,
	float pEnvAmt,
	float gain,

	//Filter
	float loMin,
	float hiMin,
	int center,
	char fKeyTrack,
	int distance,
	int minDistance,
	float fc,
	float falloffTime,

	//Eqs
	int eq1Freq,
	float eq1Amt,
	int eq2Freq,
	float eq2Amt,
	int eq3Freq,
	float eq3Amt,
	int eqDist,
	float eqMin,
	
	//Modulation
	float vOscRate,
	float vOscAmt,
	float pOscRate,
	float pOscAmt,

	//Timbre
	int harDist,
	float noiseAmt,
	float noiseCurve

	) {


	float a = vEnv.l * vEnv.ap;
	float d = vEnv.l * vEnv.dp;
	int stepLength = (int)((vEnv.l + vEnv.r) * CPS);

	float pa = pEnv.l * pEnv.ap;
	float pd = pEnv.l * pEnv.dp;

	int rCenter = center * res;
	if (fKeyTrack == 1) { rCenter = freq * res; }
	int rDistance = distance * res;
	int rMinDistance = minDistance * res;
	float distInv = 1.0f / rDistance;
	float rFalloffTime = 1.0f / falloffTime;

	static float eqs[oscAmt];

	static float harValues[oscAmt][2];

	//Frequency Filters
	for (int f = 0; f < oscAmt; f++) {

		harValues[f][0] = ranf();
		harValues[f][1] = ranf();

		eqs[f] += envEq(f, eq1Freq, eqDist) * eq1Amt;
		eqs[f] += envEq(f, eq2Freq, eqDist) * eq2Amt;
		eqs[f] += envEq(f, eq3Freq, eqDist) * eq3Amt;
		eqs[f] = lerp(eqs[f], 1.0f, eqMin);
	}

	
	
	//Rendering pass
	int i;
	//#pragma omp parallel for num_threads(activeThreads)
	for (i = 0; i < stepLength; i++) {
		float time = i * cpsInv;
		float v = envADSR(time, vEnv.l, a, d, vEnv.s, vEnv.r, vEnv.c) * gain; //Volume envelope
		float p = envADSR(time, pEnv.l, pa, pd, pEnv.s, pEnv.r, pEnv.c) * pEnvAmt * res; //Pitch envelope
		if (time >= pEnv.l) { p = 0.0f; }

		//Harmonics
		for (int h = 0; h < 100; h++) {

			float index = harmonic(freq, h * harDist) * res;
			if (index >= oscAmt) { break; } //If the harmonic is out of bounds, stop

			//Made the cutoff distance relative to the volume of the note?
			int cDistance = lerp(rMinDistance, rDistance, v);
			
			//FILTERING
			float lowHighPass = 1.0f;
			//Low pass
			if (index >= rCenter && index < rCenter + cDistance) {
				lowHighPass = lerp(powf(1.0f - ((index - rCenter) * distInv), fc), 1.0f, hiMin);
			}
			//High pass
			if (index < rCenter && index > rCenter - cDistance) {
				lowHighPass = lerp(powf((index - (rCenter - cDistance)) * distInv, fc), 1.0f, loMin);
			}
			//Outside of ranges
			if (index >= rCenter + cDistance) { lowHighPass = hiMin; }
			if (index <= rCenter - cDistance) { lowHighPass = loMin; }

			lowHighPass *= eqs[(int)index];


			
			

			setFFV(i, index + p + osc(time * pOscRate, pOscAmt), v * lowHighPass * osc(time * vOscRate, vOscAmt) * harValues[(int)index][0], 0);
			setFFV(i, index + p + osc(time * pOscRate, pOscAmt), v * lowHighPass * osc(time * vOscRate, vOscAmt) * harValues[(int)index][1], 1);

			//Octave down render for no reason?
			setFFV(i, index + p + osc(time * pOscRate, pOscAmt) - 12.0f * res, v * lowHighPass * osc(time * vOscRate, vOscAmt) * harValues[(int)index][0], 0);
			setFFV(i, index + p + osc(time * pOscRate, pOscAmt) - 12.0f * res, v * lowHighPass * osc(time * vOscRate, vOscAmt) * harValues[(int)index][1], 1);
		}

	}
}


void generate() {

	//Init Seeding
	for (int t = 0; t < time(NULL) % 256; t++) { ranf(); }
	
	
	bool nTrigger = false;

	int cFor = 0;
	if (ranf() < 0.04f && cStep % 4 == 0) { nTrigger = true; cFor = rani(0, 1); }

	

	for (int note = 0; note < rani(1, 6); note++) {

		//If note isnt triggered, goto next note
		if (!nTrigger) { continue; }
		//else...

		//MAKE ATTACK/DECAY 1 VALUE (DECAY = 1f - AP)
		struct adsr s1v = { 8.0f, ranfIn(0.0f, 0.5f), 0.5f, 0.5f, 5.0f, 1.9f};
		struct adsr s1p = { 1.9f, 0.0f, 0.9f, 0.0f, 0.0f, 1.9f };
		synthesize(
			s1v, //vEnv
			k2m(rani(29, 41) + note * 2, cKey), //Freq
			s1p, //pEnv
			-0.5f, //pEnvAmt
			ranfIn(0.8f, 0.99f), //Gain

			1.0f, //loMin
			0.0f, //hiMin
			40, //Filter center
			0, //Filter key tracking
			100, //Filter distance
			10, //Filter min distance
			2.7f, //Filter curve
			2.0f, //Filter Falloff time

			//EQs
			getFormant(cFor, 0),
			1.0f,
			getFormant(cFor, 1),
			1.0f,
			getFormant(cFor, 2),
			0.5f,
			9 * res, //eqDist
			0.9f, //eqMin

			rani(2, 5), //vOscRate
			ranfIn(0.0f, 0.1f), //vOscAmt

			ranfIn(18.0f, 19.0f), //pOscRate
			ranfIn(0.0f, 0.05f) * res, //pOscAmt

			rani(1, 5), //harDist
			ranfIn(0.0f, 0.02f), //Noise amount
			0.5f //Noise curve
		);
		
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














void process(float* fbuffer, int num_frames, int num_channels) {
    int num_samples = num_frames * num_channels;
    short sbuffer[num_samples];

    if (running) {

        int samples = num_frames;
        int andVal = SINE_LENGTH - 1;
        float timeMult = 1.0f / samples;

        generate();
        cVols = getVols();

        for (int i = 0; i < samples; i++) {

            sbuffer[i * 2] = 0;
            sbuffer[i * 2 + 1] = 0;

            float time = i * timeMult;

            for (int osc = 0; osc < oscAmt; osc++) {

                if (cVols[osc * 2] == 0 && cVols[osc * 2 + 1] == 0 && cVols[osc * 2 + oscs2] == 0 && cVols[osc * 2 + 1 + oscs2] == 0) { continue; }


                unsigned int index = (unsigned int)((i + cSample) * mtfs[osc] + sineStarts[osc]) & andVal;
                
                short volL = lerp(cVols[osc * 2 + oscs2], cVols[osc * 2], time);
                short volR = lerp(cVols[osc * 2 + 1 + oscs2], cVols[osc * 2 + 1], time);

                sbuffer[i * 2] += ((int)sineWave[index] * volL >> 16) * volume >> 16;
                sbuffer[i * 2 + 1] += ((int)sineWave[index] * volR >> 16) * volume >> 16;
            }
            
        }
        cSample += samples;


        for (int i = 0; i < num_samples; i++) {
            fbuffer[i] = (float)(sbuffer[i]) * shortInv;
        }



    } else {
        for (int i = 0; i < num_samples; i++) {
            fbuffer[i] = 0.0f;
        }
    }
}

void initAudioData() {
    float oscMult = sqrt(1.0 / oscAmt);
	CPS = (SAMPLE_RATE / CHUNK_SIZE);
	cpsInv = 1.0f / (float)CPS;
	lengthMult = SINE_LENGTH / (double)SAMPLE_RATE;

    for (int i = 0; i < SINE_LENGTH; i++) {
        sineWave[i] = (short)(sinf(i * TWO_PI / SINE_LENGTH) * 0x7FFF);
    }
    for (int osc = 0; osc < oscAmt; osc++) {
        mtfs[osc] = mtf(osc / (float)res) * lengthMult;
        sineStarts[osc] = rand() & 0xFFFF;
    }
}


EMSCRIPTEN_KEEPALIVE
void htmlInput(int toggle, float vol) {
	if (toggle == 1) { running = !running; }
	if (toggle == 2) { simple = !simple; }
	if (vol >= 0.0f) { volume = vol * 0xFFFF; }
}

int main() {

    //CREATE A GENERAL INPUT HANDLER FUNCTION FOR CONTROLLING THE SYNTH (jam every functionality into it)
	//Have a toggle to switch to simple single sine osc mode and see if mobile is still bad

    saudio_setup(&(saudio_desc){
        .stream_cb = process,
		//.sample_rate = SAMPLE_RATE, //GET RID OF THIS
        .num_channels = 2,
        .buffer_frames = CHUNK_SIZE
    });
	SAMPLE_RATE = saudio_sample_rate();

	initAudioData();
	printf("samplerate: %i\n", SAMPLE_RATE);
    return 0;
}