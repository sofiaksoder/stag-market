import { fmtN, px2 } from '../format'
import { Panel } from './Panel'

type Levels = [number, number][]

interface Props {
  bids: Levels // best first
  asks: Levels // best first
  last: number
  levels: number
  mineBids?: Map<number, number>
  mineAsks?: Map<number, number>
  onPick?: (side: 'B' | 'S', px: number) => void // leave out for a read-only book
  title?: string
  right?: string
}

// Pads to a fixed number of rows so the ladder doesn't jump around.
function rows(l: Levels, n: number): (Levels[number] | null)[] {
  return Array.from({ length: n }, (_, i) => l[i] ?? null)
}

// Our resting size at a price, blank if none.
const mineText = (m: Map<number, number>, px: number) => (m.has(px) ? fmtN(m.get(px)!) : '')

export function Ladder({ bids, asks, last, levels, mineBids = new Map(), mineAsks = new Map(), onPick, title = 'Order book', right = `${levels} lvls` }: Props) {
  const max = Math.max(1, ...bids.map((l) => l[1]), ...asks.map((l) => l[1]))
  const bestBid = bids[0]?.[0]
  const bestAsk = asks[0]?.[0]
  const both = bestBid !== undefined && bestAsk !== undefined

  // Asks are drawn with the highest price at the top.
  const askRows = rows(asks, levels).reverse()
  const bidRows = rows(bids, levels)

  return (
    <Panel title={title} right={right} className="ladder">
      <div className="ladder-cols mono">
        <span>Mine</span>
        <span style={{ textAlign: 'right' }}>Bid</span>
        <span style={{ textAlign: 'center' }}>Price</span>
        <span>Ask</span>
        <span style={{ textAlign: 'right' }}>Mine</span>
      </div>
      <div className="ladder-body mono">
        {askRows.map((l, i) => (
          <div className={`lvl${l && onPick ? ' pick' : ''}`} key={`a${i}`} onClick={() => l && onPick?.('S', l[0])}>
            <span />
            <span />
            <span className="lvl-px ask">{l && px2(l[0])}</span>
            <span className="bar-cell">
              {l && <span className="bar ask" style={{ width: `${(l[1] / max) * 100}%` }} />}
              <span className="bar-text">{l && fmtN(l[1])}</span>
            </span>
            <span className="mine">{l && mineText(mineAsks, l[0])}</span>
          </div>
        ))}
        <div className="spread">
          <span className="muted small">spread {both ? px2(bestAsk - bestBid) : '–'}</span>
          <span className="last">{px2(last)}</span>
          <span className="muted small">mid {both ? ((bestBid + bestAsk) / 200).toFixed(3) : '–'}</span>
        </div>
        {bidRows.map((l, i) => (
          <div className={`lvl${l && onPick ? ' pick' : ''}`} key={`b${i}`} onClick={() => l && onPick?.('B', l[0])}>
            <span className="mine">{l && mineText(mineBids, l[0])}</span>
            <span className="bar-cell right">
              {l && <span className="bar bid" style={{ width: `${(l[1] / max) * 100}%` }} />}
              <span className="bar-text">{l && fmtN(l[1])}</span>
            </span>
            <span className="lvl-px bid">{l && px2(l[0])}</span>
            <span />
            <span />
          </div>
        ))}
      </div>
    </Panel>
  )
}
