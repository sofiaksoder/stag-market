import type { FillRecord } from '../account'
import { px2 } from '../format'

export function Toast({ fill }: { fill: FillRecord | null }) {
  if (!fill) return null
  const buy = fill.side === 'B'
  return (
    <div className="toast mono">
      <span className="dot" style={{ background: buy ? 'var(--bid)' : 'var(--ask)' }} />
      FILL #{fill.id} {buy ? 'BUY' : 'SELL'} {fill.qty} @ {px2(fill.px)} · {fill.role}
    </div>
  )
}
