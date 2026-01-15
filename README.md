experimental additive synth.
Generates chords with varying timbres in real time. currently all cpu based with a single audio thread doing the composing + rendering.


Uses sokol_audio.h for the javascript audioworklet initialization/connection. automatically compiles using emcc to create the audio_module .js and .wasm

some details:

  -generates (128 * res) sine waves every block and applys correct volumes to each. for example playing a single frequency note would have all the volumes be 0 except for the (note * res) index. 
  
  -Uses a looping buffer of bytes to store current and future frequency volumes. the buffer is about 24 seconds long, meaning it can only compose notes that are less than 24 seconds long. 
