import type { Trade } from '../feed'
import { fmtN, fmtTime, px2 } from '../format'
import { Panel } from './Panel'

export function Tape({ trades, mine = new Set(), title = 'Trades', right = '● = yours', utc = false }: { trades: Trade[]; mine?: Set<number>; title?: string; right?: string; utc?: boolean }) {
  return (
    <Panel title={title} right={right} className="tape">
      <div className="tape-cols mono">
        <span>Seq</span>
        <span>Time</span>
        <span style={{ textAlign: 'right' }}>Price</span>
        <span style={{ textAlign: 'right' }}>Qty</span>
      </div>
      <div className="tape-body mono">
        {trades.map((t) => (
          <div className={`tape-row${mine.has(t.seq) ? ' mine' : ''}`} key={t.seq}>
            <span className="muted">{mine.has(t.seq) && '● '}{fmtN(t.seq)}</span>
            <span className="muted">{fmtTime(t.ts, utc)}</span>
            <span style={{ textAlign: 'right', color: t.side === 'B' ? 'var(--bid-text)' : 'var(--ask-text)' }}>{px2(t.px)}</span>
            <span style={{ textAlign: 'right' }}>{fmtN(t.qty)}</span>
          </div>
        ))}
      </div>
    </Panel>
  )
}
