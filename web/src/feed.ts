// Market-data feed: decoding and the book state machine.
// No sockets in here, so it can be tested on its own.

export const REF_PX = 14250 // 142.50, in ticks

export type Status = 'connecting' | 'sync' | 'recover' | 'down'
export type Side = 'B' | 'S'

export interface Trade {
  seq: number
  ts: number // ms since epoch
  px: number
  qty: number
  side: Side // aggressor
}

type MdMsg =
  | { kind: 'delta'; seq: number; side: Side; px: number; size: number }
  | { kind: 'trade'; seq: number; trade: Trade }
  | { kind: 'snapshot'; seq: number; bids: [number, number][]; asks: [number, number][] }

type Reply = { kind: 'ack'; ack: Ack } | { kind: 'fill'; fill: Fill }
type Msg = MdMsg | Reply

// Replies to our own orders. Their seq is separate from the market-data seq.
export interface Ack {
  clOrdId: number
  id: number
  status: 'new' | 'canceled' | 'rejected'
  reason: number // engine Status code
  seq: number
}

export interface Fill {
  id: number
  px: number
  qty: number
  role: 'maker' | 'taker'
  seq: number
  tradeSeq: number // seq of the matching trade on the market-data feed
}

const DELTA = 20
const TRADE = 21
const SNAPSHOT = 22
const ACK = 10
const FILL = 11
export const SNAPSHOT_REQ = 23

const MAX_TRADES = 60
const MAX_CANDLES = 120
export const CANDLE_MS = 3000
const GAP_SHOWN_MS = 600

// One WebSocket message: type byte, then the body. Little-endian.
export function decode(buf: ArrayBuffer): Msg | null {
  const v = new DataView(buf)
  const type = v.getUint8(0)
  if (type === ACK) {
    return {
      kind: 'ack',
      ack: {
        clOrdId: Number(v.getBigUint64(1, true)),
        id: Number(v.getBigUint64(9, true)),
        status: (['new', 'canceled', 'rejected'] as const)[v.getUint8(17)] ?? 'rejected',
        reason: v.getUint8(18),
        seq: Number(v.getBigUint64(19, true)),
      },
    }
  }
  if (type === FILL) {
    return {
      kind: 'fill',
      fill: {
        id: Number(v.getBigUint64(1, true)),
        px: Number(v.getBigInt64(9, true)),
        qty: v.getUint32(17, true),
        role: v.getUint8(21) === 0 ? 'maker' : 'taker',
        seq: Number(v.getBigUint64(22, true)),
        tradeSeq: Number(v.getBigUint64(30, true)),
      },
    }
  }
  const seq = Number(v.getBigUint64(1, true))
  switch (type) {
    case DELTA:
      return {
        kind: 'delta',
        seq,
        side: v.getUint8(9) === 0 ? 'B' : 'S',
        px: Number(v.getBigInt64(10, true)),
        size: v.getUint32(18, true),
      }
    case TRADE:
      return {
        kind: 'trade',
        seq,
        trade: {
          seq,
          ts: Number(v.getBigUint64(9, true)) / 1e6,
          px: Number(v.getBigInt64(17, true)),
          qty: v.getUint32(25, true),
          side: v.getUint8(29) === 0 ? 'B' : 'S',
        },
      }
    case SNAPSHOT: {
      const nb = v.getUint16(9, true)
      const na = v.getUint16(11, true)
      let at = 13
      const read = (n: number) => {
        const out: [number, number][] = []
        for (let i = 0; i < n; i++, at += 12) out.push([Number(v.getBigInt64(at, true)), v.getUint32(at + 8, true)])
        return out
      }
      const bids = read(nb)
      return { kind: 'snapshot', seq, bids, asks: read(na) }
    }
    default:
      return null
  }
}

export interface Candle {
  k: number // bucket number, ts / CANDLE_MS
  o: number
  h: number
  l: number
  c: number
  v: number
}

export class MarketState {
  bids = new Map<number, number>()
  asks = new Map<number, number>()
  trades: Trade[] = [] // newest first
  candles: Candle[] = [] // oldest first
  last = REF_PX
  seq = 0
  count = 0 // messages received, for msg/s
  status: Status = 'connecting'
  gap: { from: number; to: number; at: number } | null = null
  private buffered: MdMsg[] = []
  private gapOpen = false

  // Replies to our orders are handed to this instead of touching the book.
  onReply: (m: Reply) => void = () => {}
  onEvent: (kind: 'ok' | 'gap', text: string) => void = () => {}

  constructor(
    private requestSnapshot: () => void,
    private now: () => number = Date.now,
  ) {}

  // The gateway sends a snapshot right after connecting.
  onOpen() {
    this.status = 'recover'
    this.buffered = []
    this.gap = null
    this.gapOpen = false
    this.onEvent('ok', 'Connected · waiting for snapshot')
  }

  onClose() {
    this.status = 'down'
  }

  onMessage(buf: ArrayBuffer) {
    const m = decode(buf)
    if (!m) return
    if (m.kind === 'ack' || m.kind === 'fill') return this.onReply(m)
    this.count++
    if (m.kind === 'snapshot') return this.onSnapshot(m)

    if (this.status === 'sync' && m.seq !== this.seq + 1) {
      // Missed something. Freeze the book and ask for a snapshot.
      this.gap = { from: this.seq + 1, to: m.seq - 1, at: this.now() }
      this.gapOpen = true
      this.status = 'recover'
      this.requestSnapshot()
      this.onEvent('gap', `GAP detected · missing seq ${fmtRange(this.gap.from, this.gap.to)} · book frozen`)
      this.onEvent('gap', 'Snapshot requested · replaying buffered deltas')
    }
    if (this.status === 'sync') this.apply(m)
    else if (this.status === 'recover') this.buffered.push(m)
  }

  private onSnapshot(s: Extract<MdMsg, { kind: 'snapshot' }>) {
    this.bids = new Map(s.bids)
    this.asks = new Map(s.asks)
    this.seq = s.seq
    // Replay what arrived while we waited, skipping what the snapshot already covers.
    const pending = this.buffered.filter((m) => m.seq > s.seq).sort((a, b) => a.seq - b.seq)
    this.buffered = []
    this.status = 'sync'
    for (const m of pending) {
      if (m.seq !== this.seq + 1) {
        // Still a hole, so ask again.
        this.status = 'recover'
        this.buffered = pending.filter((p) => p.seq > this.seq)
        this.requestSnapshot()
        return
      }
      this.apply(m)
    }
    if (this.gapOpen) this.onEvent('ok', `Recovered · in sync at seq ${this.seq.toLocaleString('en-US')}`)
    this.gapOpen = false
  }

  private apply(m: MdMsg) {
    if (m.kind === 'snapshot') return
    this.seq = m.seq
    if (m.kind === 'delta') {
      const book = m.side === 'B' ? this.bids : this.asks
      if (m.size) book.set(m.px, m.size)
      else book.delete(m.px)
    } else {
      this.last = m.trade.px
      this.trades.unshift(m.trade)
      if (this.trades.length > MAX_TRADES) this.trades.pop()
      this.addToCandle(m.trade)
    }
  }

  private addToCandle(t: Trade) {
    const k = Math.floor(t.ts / CANDLE_MS)
    let c = this.candles[this.candles.length - 1]
    if (!c || c.k !== k) {
      c = { k, o: t.px, h: t.px, l: t.px, c: t.px, v: 0 }
      this.candles.push(c)
      if (this.candles.length > MAX_CANDLES) this.candles.shift()
    }
    c.h = Math.max(c.h, t.px)
    c.l = Math.min(c.l, t.px)
    c.c = t.px
    c.v += t.qty
  }

  // Best-first levels for one side.
  levels(side: Side, n: number): [number, number][] {
    const book = side === 'B' ? this.bids : this.asks
    const px = [...book.keys()].sort((a, b) => (side === 'B' ? b - a : a - b)).slice(0, n)
    return px.map((p) => [p, book.get(p)!])
  }
}

// What the header chip shows. A gap is kept on screen briefly, since recovery is fast.
export function chipState(s: MarketState, now: number) {
  if (s.status === 'down') return { label: 'DISCONNECTED', ok: false, detail: 'retrying' }
  if (s.status === 'connecting') return { label: 'CONNECTING', ok: false, detail: '' }
  if (s.gap && now - s.gap.at < GAP_SHOWN_MS) {
    const { from, to } = s.gap
    return { label: 'SEQ GAP', ok: false, detail: `missing ${fmtRange(from, to)}` }
  }
  if (s.status === 'recover') return { label: 'RECOVERING', ok: false, detail: 'snapshot → deltas' }
  return { label: 'IN SYNC', ok: true, detail: `seq ${s.seq.toLocaleString('en-US')}` }
}

function fmtRange(from: number, to: number) {
  const a = from.toLocaleString('en-US')
  return from === to ? a : `${a}–${to.toLocaleString('en-US')}`
}
