import type { FillRecord } from '../account'
import { fmtN, px2 } from '../format'
import { fmtMoney, unrealized, type Position } from '../pnl'
import { FillsTable } from './FillsTable'
import { Panel } from './Panel'

interface Props {
  position: Position
  last: number
  openOrders: number
  fills: FillRecord[]
  equity: number[] // net P&L in dollars
}

const pnlColor = (v: number) => (v > 0.004 ? 'var(--bid-text)' : v < -0.004 ? 'var(--ask-text)' : undefined)

export function Portfolio({ position, last, openOrders, fills, equity }: Props) {
  const unreal = unrealized(position, last) / 100
  const real = position.realized / 100
  const net = real + unreal
  const flat = position.qty === 0
  const qtyText = flat ? '0' : `${position.qty > 0 ? '+' : '−'}${fmtN(Math.abs(position.qty))}`

  const stats: { label: string; value: string; color?: string }[] = [
    { label: 'Position', value: qtyText },
    { label: 'Avg price', value: flat ? '–' : px2(position.avg) },
    { label: 'Mark', value: px2(last) },
    { label: 'Unrealized', value: fmtMoney(unreal), color: pnlColor(unreal) },
    { label: 'Realized', value: fmtMoney(real), color: pnlColor(real) },
    { label: 'Net P&L', value: fmtMoney(net), color: pnlColor(net) },
  ]

  return (
    <div className="page">
      <div className="page-title">
        <h1>Portfolio</h1>
        <span className="muted mono small">acct SIM-01 · marked to last trade · avg-cost P&amp;L</span>
      </div>

      <div className="stats">
        {stats.map((s) => (
          <div className="stat" key={s.label}>
            <div className="label mono">{s.label}</div>
            <div className="stat-value mono" style={{ color: s.color }}>{s.value}</div>
          </div>
        ))}
      </div>

      <Panel title="Net P&L" right="every 1.2s" className="pnl-panel">
        <PnlBars samples={equity} />
      </Panel>

      <div className="portfolio-bottom">
        <Panel title="Positions" className="positions">
          <div className="pos-row cols mono">
            <span>Symbol</span><span style={{ textAlign: 'right' }}>Qty</span><span style={{ textAlign: 'right' }}>Notional</span><span style={{ textAlign: 'right' }}>Open orders</span>
          </div>
          <div className="mono">
            <div className="pos-row">
              <span>STAG</span>
              <span style={{ textAlign: 'right' }}>{qtyText}</span>
              <span style={{ textAlign: 'right' }}>{fmtMoney(Math.abs(position.qty * last) / 100, false)}</span>
              <span style={{ textAlign: 'right' }}>{openOrders}</span>
            </div>
          </div>
        </Panel>
        <Panel title="Fill history" className="history">
          <FillsTable fills={fills} maxHeight={360} />
        </Panel>
      </div>
    </div>
  )
}

// Bars hang from a zero line: green above it, red below.
function PnlBars({ samples }: { samples: number[] }) {
  const hi = Math.max(0, ...samples)
  const lo = Math.min(0, ...samples)
  const range = hi - lo
  if (samples.length === 0 || range < 0.005) {
    return <div className="pnl-chart"><div className="chart-empty">No P&amp;L yet. Place an order and get filled.</div></div>
  }
  const zero = (hi / range) * 100 // % from the top
  return (
    <div className="pnl-chart">
      <div className="pnl-zero" style={{ top: `${zero}%` }} />
      <div className="pnl-bars">
        {samples.map((v, i) => {
          const h = (Math.abs(v) / range) * 100
          return (
            <div className="pnl-slot" key={i} title={fmtMoney(v)}>
              <div
                className="pnl-bar"
                style={{
                  height: `${h}%`,
                  top: v >= 0 ? `${zero - h}%` : `${zero}%`,
                  background: v >= 0 ? 'var(--bid)' : 'var(--ask)',
                }}
              />
            </div>
          )
        })}
      </div>
    </div>
  )
}
