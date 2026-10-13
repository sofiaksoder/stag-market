// How the page talks to an exchange: a WebSocket to the C++ gateway, or the
// same engine compiled to WebAssembly and running in the page. Both carry the
// same messages: a type byte then the body.

export interface Handlers {
  onOpen: () => void
  onMessage: (buf: ArrayBuffer) => void
  onClose: () => void
}

export interface Transport {
  // False if it can't send right now.
  send: (bytes: Uint8Array<ArrayBuffer>) => boolean
  close: () => void
}

export function connectWebSocket(url: string, h: Handlers): Transport {
  const ws = new WebSocket(url)
  ws.binaryType = 'arraybuffer'
  ws.onopen = h.onOpen
  ws.onmessage = (e) => h.onMessage(e.data as ArrayBuffer)
  ws.onclose = h.onClose
  return {
    send: (bytes) => {
      if (ws.readyState !== WebSocket.OPEN) return false
      ws.send(bytes)
      return true
    },
    close: () => ws.close(),
  }
}

// The WebAssembly outbox is protocol frames back to back: u16 length, then
// that many bytes (type byte and body). Returns each one without its length.
export function splitFrames(bytes: Uint8Array): ArrayBuffer[] {
  const out: ArrayBuffer[] = []
  let at = 0
  while (at + 2 <= bytes.length) {
    const len = bytes[at] | (bytes[at + 1] << 8)
    if (at + 2 + len > bytes.length) break
    out.push(bytes.slice(at + 2, at + 2 + len).buffer)
    at += 2 + len
  }
  return out
}

const STEP_MS = 100 // how often the simulated bots trade
const STEPS_PER_TICK = 3
const WARMUP_STEPS = 300

export function connectWasm(h: Handlers): Transport {
  let closed = false
  let timer: number | undefined
  let engine: Awaited<ReturnType<typeof loadEngine>> | null = null

  const drain = () => {
    if (!engine) return
    const size = engine._stag_outbox_size()
    if (size === 0) return
    const ptr = engine._stag_outbox()
    const bytes = engine.HEAPU8.slice(ptr, ptr + size)
    engine._stag_outbox_clear()
    for (const msg of splitFrames(bytes)) h.onMessage(msg)
  }

  loadEngine().then((m) => {
    if (closed) return
    engine = m
    m._stag_init(Math.floor(Math.random() * 1e9), WARMUP_STEPS)
    h.onOpen()
    m._stag_snapshot()
    drain()
    timer = window.setInterval(() => {
      m._stag_step(STEPS_PER_TICK)
      drain()
    }, STEP_MS)
  })

  return {
    send: (bytes) => {
      if (!engine) return false
      const m = engine
      const ptr = m._malloc(bytes.length)
      m.HEAPU8.set(bytes, ptr)
      const rc = m._stag_submit(ptr, bytes.length)
      m._free(ptr)
      // Replies go out on the next tick of the event loop, like a real socket.
      if (rc === 0) queueMicrotask(drain)
      return rc === 0
    },
    close: () => {
      closed = true
      window.clearInterval(timer)
    },
  }
}

async function loadEngine() {
  const { default: createStag } = await import('./wasm/stag_engine.js')
  return createStag()
}
