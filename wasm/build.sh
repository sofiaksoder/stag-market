#!/bin/sh
# Builds the engine to WebAssembly for the web demo. Needs Emscripten (emcc) on the PATH.
# The output goes into web/src/wasm so Vite bundles it.
set -e
cd "$(dirname "$0")/.."

em++ wasm/stag_wasm.cpp \
  -std=c++20 -O2 -DNDEBUG \
  -I engine -I gateway -I tools \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createStag \
  -sENVIRONMENT=web \
  -sSINGLE_FILE=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_stag_init,_stag_step,_stag_submit,_stag_snapshot,_stag_outbox,_stag_outbox_size,_stag_outbox_clear \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8 \
  -o web/src/wasm/stag_engine.js

echo "built web/src/wasm/stag_engine.js"
