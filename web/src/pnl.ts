// Average-cost position accounting. Prices are in ticks (1 tick = $0.01).

export interface Position {
  qty: number // signed, long is positive
  avg: number // average entry price, 0 when flat
  realized: number // in ticks times shares
}

export const FLAT: Position = { qty: 0, avg: 0, realized: 0 }

export function applyFill(p: Position, side: 'B' | 'S', px: number, qty: number): Position {
  const signed = side === 'B' ? qty : -qty
  if (p.qty === 0 || Math.sign(p.qty) === Math.sign(signed)) {
    // Opening or adding: the average moves.
    const total = Math.abs(p.qty) + qty
    return { ...p, qty: p.qty + signed, avg: (Math.abs(p.qty) * p.avg + qty * px) / total }
  }
  // Closing some or all, and maybe flipping to the other side.
  const closed = Math.min(qty, Math.abs(p.qty))
  const realized = p.realized + closed * (px - p.avg) * Math.sign(p.qty)
  const next = p.qty + signed
  if (next === 0) return { qty: 0, avg: 0, realized }
  const flipped = Math.sign(next) !== Math.sign(p.qty)
  return { qty: next, avg: flipped ? px : p.avg, realized }
}

export const unrealized = (p: Position, mark: number) => p.qty * (mark - p.avg)

// Dollars, like +$1,234.56 or −$12.00.
export function fmtMoney(dollars: number, signed = true) {
  const abs = Math.abs(dollars).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
  if (Math.abs(dollars) < 0.005) return `$${abs}`
  const sign = dollars < 0 ? '−' : signed ? '+' : ''
  return `${sign}$${abs}`
}
