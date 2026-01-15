all:
		source ~/dev/libs/emsdk/emsdk_env.sh
		emcc main.c -o audio_module.js -s NO_EXIT_RUNTIME=1 -s "EXPORTED_FUNCTIONS=['_main', '_htmlInput']"

serve:
		python -m http.server 8000
