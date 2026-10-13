import type { Side } from './feed'
import type { OrderRequest } from './protocol'

export const MAX_QTY = 50_000

export interface Ticket {
  side: Side
  type: 'limit' | 'market'
  px: string // dollars, as typed
  qty: string
}

type Order = Omit<OrderRequest, 'clOrdId'>

// Checks the ticket the same way the gateway will, so mistakes show up before sending.
export function checkTicket(t: Ticket, bestBid?: number, bestAsk?: number): { order?: Order; error?: string } {
  const qty = Number(t.qty)
  if (!/^\d+$/.test(t.qty) || qty < 1) return { error: 'Quantity must be a whole number above 0.' }
  if (qty > MAX_QTY) return { error: `Quantity can't be more than ${MAX_QTY.toLocaleString('en-US')}.` }

  if (t.type === 'market') {
    const touch = t.side === 'B' ? bestAsk : bestBid
    if (touch === undefined) return { error: 'No liquidity on the other side of the book.' }
    return { order: { side: t.side, type: 'market', px: 0, qty } }
  }

  const dollars = Number(t.px)
  if (t.px.trim() === '' || !Number.isFinite(dollars) || dollars <= 0) return { error: 'Enter a price above 0.' }
  const px = Math.round(dollars * 100)
  if (px < 1) return { error: 'Price is below one tick (0.01).' }
  return { order: { side: t.side, type: 'limit', px, qty } }
}

// Rough value of the order in dollars. Market orders use the touch on the other side.
export function notional(t: Ticket, bestBid?: number, bestAsk?: number): number | null {
  const qty = Number(t.qty)
  if (!/^\d+$/.test(t.qty) || qty < 1) return null
  const ticks = t.type === 'limit' ? Math.round(Number(t.px) * 100) : t.side === 'B' ? bestAsk : bestBid
  if (!ticks || !Number.isFinite(ticks)) return null
  return (qty * ticks) / 100
}

export function stepPx(px: string, dir: 1 | -1, mid: number): string {
  const base = Number.isFinite(Number(px)) && px.trim() !== '' ? Number(px) : mid / 100
  return Math.max(0.01, Math.round(base * 100 + dir) / 100).toFixed(2)
}
