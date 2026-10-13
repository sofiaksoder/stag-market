// What we know about our own orders and fills, built from the gateway's replies.
import type { Ack, Fill, Side } from './feed'
import { FLAT, applyFill, type Position } from './pnl'
import { REJECT_TEXT, type OrderRequest } from './protocol'

export interface OpenOrder {
  id: number
  side: Side
  px: number
  qty: number // original size
  filled: number
  ts: number
}

export interface FillRecord extends Fill {
  side: Side
  ts: number
}

export interface LogEntry {
  ts: number
  kind: 'ok' | 'gap' | 'fill' | 'ack'
  text: string
}

const MAX_FILLS = 200
const MAX_LOG = 200

export class Account {
  orders = new Map<number, OpenOrder>()
  fills: FillRecord[] = [] // newest first
  log: LogEntry[] = []
  tradeSeqs = new Set<number>() // market-data seqs of trades we were in
  position: Position = FLAT
  lastFill: FillRecord | null = null
  lastReject: { text: string; at: number } | null = null

  private nextClId = 1
  private pending = new Map<number, OrderRequest>()
  private sides = new Map<number, Side>() // every order id we've seen, for labelling fills

  constructor(private now: () => number = Date.now) {}

  // Call when an order goes out. Returns the request with its client id filled in.
  stage(o: Omit<OrderRequest, 'clOrdId'>): OrderRequest {
    const req = { ...o, clOrdId: this.nextClId++ }
    this.pending.set(req.clOrdId, req)
    return req
  }

  addLog(kind: LogEntry['kind'], text: string) {
    this.log.unshift({ ts: this.now(), kind, text })
    if (this.log.length > MAX_LOG) this.log.pop()
  }

  onAck(a: Ack) {
    const req = this.pending.get(a.clOrdId)
    this.pending.delete(a.clOrdId)
    if (a.status === 'new' && req) {
      this.sides.set(a.id, req.side)
      if (req.type === 'limit') {
        this.orders.set(a.id, { id: a.id, side: req.side, px: req.px, qty: req.qty, filled: 0, ts: this.now() })
      }
      const what = req.type === 'limit' ? `@ ${fmtPx(req.px)}` : 'at market'
      this.addLog('ack', `ACK #${a.id} NEW ${sideName(req.side)} ${req.qty} ${what} · seq ${fmtN(a.seq)}`)
    } else if (a.status === 'canceled') {
      this.orders.delete(a.id)
      this.addLog('ack', `ACK #${a.id} CANCELED · seq ${fmtN(a.seq)}`)
    } else {
      const why = REJECT_TEXT[a.reason] ?? `code ${a.reason}`
      this.lastReject = { text: why[0].toUpperCase() + why.slice(1), at: this.now() }
      this.addLog('ack', `REJECT ${a.id ? '#' + a.id + ' ' : ''}${why} · seq ${fmtN(a.seq)}`)
    }
  }

  onFill(f: Fill) {
    const side = this.sides.get(f.id) ?? 'B'
    const rec = { ...f, side, ts: this.now() }
    this.fills.unshift(rec)
    if (this.fills.length > MAX_FILLS) this.fills.pop()
    this.tradeSeqs.add(f.tradeSeq)
    this.lastFill = rec
    this.position = applyFill(this.position, side, f.px, f.qty)

    const o = this.orders.get(f.id)
    if (o) {
      o.filled += f.qty
      if (o.filled >= o.qty) this.orders.delete(f.id)
    }
    this.addLog('fill', `FILL #${f.id} ${sideName(side)} ${f.qty} @ ${fmtPx(f.px)} (${f.role}) · seq ${fmtN(f.seq)}`)
  }

  // Our resting size at each price.
  mine(side: Side): Map<number, number> {
    const out = new Map<number, number>()
    for (const o of this.orders.values())
      if (o.side === side) out.set(o.px, (out.get(o.px) ?? 0) + o.qty - o.filled)
    return out
  }
}

const sideName = (s: Side) => (s === 'B' ? 'BUY' : 'SELL')
const fmtPx = (ticks: number) => (ticks / 100).toFixed(2)
const fmtN = (n: number) => n.toLocaleString('en-US')
