import { useEffect, useState } from 'react'
import { Chart } from './components/Chart'
import { Ladder } from './components/Ladder'
import { Portfolio } from './components/Portfolio'
import { Performance } from './components/Performance'
import { Replay } from './components/Replay'
import { Logo } from './components/Logo'
import { OrdersPanel } from './components/OrdersPanel'
import { Tape } from './components/Tape'
import { Ticket } from './components/Ticket'
import { Toast } from './components/Toast'
import { REF_PX, type Side } from './feed'
import { fmtChange, px2 } from './format'
import { FLAT } from './pnl'
import { checkTicket, type Ticket as TicketState } from './ticket'
import { DEMO, useExchange } from './useExchange'

const LADDER_LEVELS = 11
const CANDLE_SLOTS = 60
const NO_ORDERS = { orders: [], fills: [], log: [] }

type Page = 'trade' | 'portfolio' | 'replay' | 'performance'

const PAGES: [Page, string][] = [
  ['trade', 'Trade'],
  ['portfolio', 'Portfolio'],
  ['replay', 'Replay'],
  ['performance', 'Performance'],
]

export default function App() {
  const { view, place, cancel, cancelAll } = useExchange(LADDER_LEVELS)
  const [ticket, setTicket] = useState<TicketState>({ side: 'B', type: 'limit', px: '', qty: '100' })
  const [error, setError] = useState<string | null>(null)
  const [page, setPage] = useState<Page>('trade')

  const last = view?.last ?? REF_PX
  const up = last >= REF_PX
  const chip = view?.chip ?? { label: 'CONNECTING', ok: false, detail: '' }
  const bestBid = view?.bids[0]?.[0]
  const bestAsk = view?.asks[0]?.[0]
  const mid = bestBid !== undefined && bestAsk !== undefined ? Math.round((bestBid + bestAsk) / 2) : last

  // The gateway refused an order: show why under the ticket.
  const rejectAt = view?.reject?.at
  useEffect(() => {
    if (view?.reject) setError(view.reject.text)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [rejectAt])

  const submit = () => {
    // An empty price box means the mid, which is what its placeholder shows.
    const t = ticket.type === 'limit' && !ticket.px.trim() ? { ...ticket, px: px2(mid) } : ticket
    const { order, error } = checkTicket(t, bestBid, bestAsk)
    if (!order) return setError(error ?? 'Invalid order.')
    if (!place(order)) return setError('Not connected to the exchange.')
    setError(null)
  }

  // Clicking a book level stages a limit order there.
  const pick = (side: Side, px: number) => {
    setTicket({ ...ticket, side, type: 'limit', px: px2(px) })
    setError(null)
  }

  return (
    <div className="shell">
      <header className="header">
        <a className="brand" href="/">
          <Logo size={26} />
          <span>Stag Market</span>
        </a>
        <nav className="nav">
          {PAGES.map(([key, label]) => (
            <button key={key} className={page === key ? 'active' : ''} onClick={() => setPage(key)}>
              {label}
            </button>
          ))}
        </nav>
        <div className="grow" />
        {DEMO && <span className="demo-tag mono" title="The C++ engine, compiled to WebAssembly, running in this page. The other traders are simulated.">live demo · engine in your browser</span>}
        <div className="instrument mono">
          <span className="sym">STAG</span>
          <span className="px">{px2(last)}</span>
          <span style={{ color: up ? 'var(--bid)' : 'var(--ask)' }}>{fmtChange(last, REF_PX)}</span>
        </div>
        <div className="chip mono">
          <span className="dot" style={{ background: chip.ok ? 'var(--bid)' : 'var(--ask)' }} />
          <span style={{ color: chip.ok ? 'var(--bid)' : 'var(--ask)' }}>{chip.label}</span>
          <span className="muted">{chip.detail}</span>
          <span className="grow" />
          <span className="muted">{view?.msgPerSec ?? 0} msg/s</span>
        </div>
      </header>

      {page === 'portfolio' && (
        <Portfolio
          position={view?.position ?? FLAT}
          last={last}
          openOrders={view?.orders.length ?? 0}
          fills={view?.fills ?? []}
          equity={view?.equity ?? []}
        />
      )}

      {page === 'replay' && <Replay />}
      {page === 'performance' && <Performance />}

      {page === 'trade' && (
      <main className="grid">
        <Ladder
          bids={view?.bids ?? []}
          asks={view?.asks ?? []}
          last={last}
          levels={LADDER_LEVELS}
          mineBids={view?.mineBids ?? new Map()}
          mineAsks={view?.mineAsks ?? new Map()}
          onPick={pick}
        />
        <Chart candles={view?.candles ?? []} last={last} slots={CANDLE_SLOTS} />
        <Ticket
          ticket={ticket}
          onChange={(t) => {
            setTicket(t)
            setError(null)
          }}
          onSubmit={submit}
          error={error}
          bestBid={bestBid}
          bestAsk={bestAsk}
          mid={mid}
        />
        <OrdersPanel {...(view ?? NO_ORDERS)} onCancel={cancel} onCancelAll={cancelAll} />
        <Tape trades={view?.trades ?? []} mine={view?.myTradeSeqs ?? new Set()} />
      </main>
      )}
      <Toast fill={view?.toast ?? null} />
    </div>
  )
}
