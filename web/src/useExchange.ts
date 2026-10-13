import { useCallback, useEffect, useRef, useState } from 'react'
import { Account, type FillRecord, type LogEntry, type OpenOrder } from './account'
import { MarketState, SNAPSHOT_REQ, chipState, type Candle, type Side, type Trade } from './feed'
import { unrealized, type Position } from './pnl'
import { encodeCancel, encodeNewOrder, type OrderRequest } from './protocol'
import { connectWasm, connectWebSocket, type Transport } from './transport'

const URL = import.meta.env.VITE_WS_URL ?? 'ws://127.0.0.1:9002'

// A demo build runs the engine in the page instead of talking to a server.
export const DEMO = import.meta.env.VITE_DEMO === 'wasm'
const TICK_MS = 100 // the UI redraws at 10 Hz, not once per message
const RETRY_MS = 1000
const TOAST_MS = 2400
const EQUITY_MS = 1200
const EQUITY_SAMPLES = 180

export interface View {
  chip: ReturnType<typeof chipState>
  bids: [number, number][]
  asks: [number, number][]
  trades: Trade[]
  candles: Candle[]
  last: number
  msgPerSec: number
  orders: OpenOrder[]
  fills: FillRecord[]
  position: Position
  equity: number[] // net P&L in dollars, sampled over time
  log: LogEntry[]
  mineBids: Map<number, number>
  mineAsks: Map<number, number>
  myTradeSeqs: Set<number>
  toast: FillRecord | null
  reject: { text: string; at: number } | null
}

export interface Exchange {
  view: View | null
  // Sends an order. False if we're not connected.
  place: (o: Omit<OrderRequest, 'clOrdId'>) => boolean
  cancel: (id: number) => void
  cancelAll: () => void
}

export function useExchange(depth: number): Exchange {
  const [view, setView] = useState<View | null>(null)
  const transportRef = useRef<Transport | null>(null)
  const accountRef = useRef<Account>(new Account())

  useEffect(() => {
    let retry: number | undefined
    let closed = false
    const account = accountRef.current

    const state = new MarketState(() => transportRef.current?.send(new Uint8Array([SNAPSHOT_REQ])))
    state.onEvent = (kind, text) => account.addLog(kind, text)
    state.onReply = (m) => (m.kind === 'ack' ? account.onAck(m.ack) : account.onFill(m.fill))

    const connect = () => {
      const handlers = {
        onOpen: () => state.onOpen(),
        onMessage: (buf: ArrayBuffer) => state.onMessage(buf),
        onClose: () => {
          state.onClose()
          if (!closed && !DEMO) retry = window.setTimeout(connect, RETRY_MS)
        },
      }
      transportRef.current = DEMO ? connectWasm(handlers) : connectWebSocket(URL, handlers)
    }
    connect()

    let lastCount = 0
    let lastT = performance.now()
    let rate = 0
    let toast: FillRecord | null = null
    let toastUntil = 0
    let lastSample = 0
    const equity: number[] = []
    const timer = window.setInterval(() => {
      const t = performance.now()
      if (t - lastT >= 1000) {
        rate = Math.round(((state.count - lastCount) * 1000) / (t - lastT))
        lastCount = state.count
        lastT = t
      }
      if (account.lastFill && account.lastFill !== toast) {
        toast = account.lastFill
        toastUntil = performance.now() + TOAST_MS
      }
      if (t - lastSample >= EQUITY_MS) {
        lastSample = t
        equity.push((account.position.realized + unrealized(account.position, state.last)) / 100)
        if (equity.length > EQUITY_SAMPLES) equity.shift()
      }
      const side = (s: Side) => state.levels(s, depth)
      setView({
        chip: chipState(state, Date.now()),
        bids: side('B'),
        asks: side('S'),
        trades: state.trades.slice(0, 40),
        candles: state.candles.map((c) => ({ ...c })),
        last: state.last,
        msgPerSec: rate,
        orders: [...account.orders.values()].sort((a, b) => b.id - a.id),
        fills: account.fills.slice(0, 100),
        position: account.position,
        equity: equity.slice(),
        log: account.log.slice(0, 100),
        mineBids: account.mine('B'),
        mineAsks: account.mine('S'),
        myTradeSeqs: new Set(account.tradeSeqs),
        toast: toast && performance.now() < toastUntil ? toast : null,
        reject: account.lastReject,
      })
    }, TICK_MS)

    return () => {
      closed = true
      window.clearTimeout(retry)
      window.clearInterval(timer)
      transportRef.current?.close()
    }
  }, [depth])

  const send = (bytes: Uint8Array<ArrayBuffer>) => transportRef.current?.send(bytes) ?? false

  const place = useCallback((o: Omit<OrderRequest, 'clOrdId'>) => {
    const req = accountRef.current.stage(o)
    return send(encodeNewOrder(req))
  }, [])

  const cancel = useCallback((id: number) => send(encodeCancel(id)), [])
  const cancelAll = useCallback(() => {
    for (const o of accountRef.current.orders.values()) send(encodeCancel(o.id))
  }, [])

  return { view, place, cancel, cancelAll }
}
