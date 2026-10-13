import type { Candle } from '../feed'
import { fmtN, px2 } from '../format'
import { Panel } from './Panel'

interface Props {
  candles: Candle[] // oldest first
  last: number
  slots: number // how many candles fit across
}

const GRID_LINES = 5

export function Chart({ candles, last, slots }: Props) {
  const shown = candles.slice(-slots)
  // Newest candle sits on the right, empty slots fill in on the left.
  const cells: (Candle | null)[] = [...Array(slots - shown.length).fill(null), ...shown]

  let lo = Math.min(...shown.map((c) => c.l), last)
  let hi = Math.max(...shown.map((c) => c.h), last)
  const pad = Math.max((hi - lo) * 0.08, 5)
  lo -= pad
  hi += pad
  const y = (px: number) => ((hi - px) / (hi - lo)) * 100 // % from the top
  const maxVol = Math.max(1, ...shown.map((c) => c.v))

  const tail = shown[shown.length - 1]
  const ohlc = tail ? `O ${px2(tail.o)}  H ${px2(tail.h)}  L ${px2(tail.l)}  C ${px2(tail.c)}` : ''
  const grid = Array.from({ length: GRID_LINES }, (_, i) => hi - ((hi - lo) * (i + 0.5)) / GRID_LINES)

  return (
    <Panel
      title=""
      className="chart"
      header={
        <div className="panel-head mono chart-head">
          <span className="head-title">STAG · 3s candles</span>
          <span>{ohlc}</span>
        </div>
      }
    >
      <div className="chart-body">
        {shown.length === 0 && <div className="chart-empty">Waiting for trades…</div>}
        <div className="plot">
          {grid.map((px) => (
            <div key={px}>
              <div className="gridline" style={{ top: `${y(px)}%` }} />
              <div className="y-label mono" style={{ top: `${y(px)}%` }}>{px2(px)}</div>
            </div>
          ))}
          <div className="candles">
            {cells.map((c, i) => {
              if (!c) return <div className="candle" key={i} />
              const up = c.c >= c.o
              const color = up ? 'var(--bid)' : 'var(--ask)'
              const bodyTop = y(Math.max(c.o, c.c))
              return (
                <div className="candle" key={c.k}>
                  <div className="wick" style={{ top: `${y(c.h)}%`, height: `${y(c.l) - y(c.h)}%`, background: color }} />
                  <div className="body" style={{ top: `${bodyTop}%`, height: `${y(Math.min(c.o, c.c)) - bodyTop}%`, background: color }} />
                </div>
              )
            })}
          </div>
          <div className="last-line" style={{ top: `${y(last)}%` }} />
          <div className="last-tag mono" style={{ top: `${y(last)}%` }}>{px2(last)}</div>
        </div>
        <div className="volume">
          {cells.map((c, i) => (
            <div className="vol-slot" key={c ? c.k : i} title={c ? `${fmtN(c.v)} shares` : undefined}>
              {c && <div style={{ height: `${(c.v / maxVol) * 100}%`, background: c.c >= c.o ? 'var(--bid)' : 'var(--ask)' }} />}
            </div>
          ))}
        </div>
      </div>
    </Panel>
  )
}
