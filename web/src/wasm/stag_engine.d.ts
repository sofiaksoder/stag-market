// Types for the module em++ builds from wasm/stag_wasm.cpp.
export interface StagEngine {
  HEAPU8: Uint8Array
  _malloc: (n: number) => number
  _free: (ptr: number) => void
  _stag_init: (seed: number, warmup: number) => void
  _stag_step: (n: number) => void
  _stag_submit: (ptr: number, len: number) => number
  _stag_snapshot: () => void
  _stag_outbox: () => number
  _stag_outbox_size: () => number
  _stag_outbox_clear: () => void
}

export default function createStag(): Promise<StagEngine>
