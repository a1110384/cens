experimental additive synth.
Generates chords with varying timbres in real time. currently all cpu based with a single audio thread doing the composing + rendering.


Uses sokol_audio.h for the javascript audioworklet initialization/connection. automatically compiles using emcc to create the audio_module .js and .wasm
