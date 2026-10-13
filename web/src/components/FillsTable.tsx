import type { FillRecord } from '../account'
import { fmtN, fmtTime, px2 } from '../format'

export const sideColor = (s: 'B' | 'S') => (s === 'B' ? 'var(--bid-text)' : 'var(--ask-text)')
export const sideName = (s: 'B' | 'S') => (s === 'B' ? 'Buy' : 'Sell')

export function FillsTable({ fills, maxHeight }: { fills: FillRecord[]; maxHeight?: number }) {
  return (
    <>
      <div className="fills-row cols mono">
        <span>Seq</span><span>Order</span><span>Side</span><span>Price</span><span>Qty</span><span>Role</span><span>Time</span>
      </div>
      <div className="scroll mono" style={maxHeight ? { maxHeight } : undefined}>
        {fills.length === 0 && <div className="empty">No fills yet.</div>}
        {fills.map((f) => (
          <div className="fills-row" key={f.seq}>
            <span className="muted">{fmtN(f.seq)}</span>
            <span>#{f.id}</span>
            <span style={{ color: sideColor(f.side) }}>{sideName(f.side)}</span>
            <span>{px2(f.px)}</span>
            <span>{fmtN(f.qty)}</span>
            <span>{f.role === 'maker' ? 'Maker' : 'Taker'}</span>
            <span className="muted">{fmtTime(f.ts)}</span>
          </div>
        ))}
      </div>
    </>
  )
}
