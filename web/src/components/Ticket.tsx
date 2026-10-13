import type { Side } from '../feed'
import { fmtN } from '../format'
import { notional, stepPx, type Ticket as TicketState } from '../ticket'
import { Panel } from './Panel'

interface Props {
  ticket: TicketState
  onChange: (t: TicketState) => void
  onSubmit: () => void
  error: string | null
  bestBid?: number
  bestAsk?: number
  mid: number // ticks
}

const QTY_CHIPS = [100, 500, 1000]

export function Ticket({ ticket, onChange, onSubmit, error, bestBid, bestAsk, mid }: Props) {
  const set = (patch: Partial<TicketState>) => onChange({ ...ticket, ...patch })
  const color = (s: Side) => (s === 'B' ? 'var(--bid)' : 'var(--ask)')
  const est = notional(ticket, bestBid, bestAsk)
  const qtyLabel = Number(ticket.qty) > 0 ? Number(ticket.qty).toLocaleString('en-US') : ticket.qty
  const label =
    ticket.type === 'limit'
      ? `${ticket.side === 'B' ? 'Buy' : 'Sell'} ${qtyLabel} @ ${ticket.px || (mid / 100).toFixed(2)}`
      : `${ticket.side === 'B' ? 'Buy' : 'Sell'} ${qtyLabel} at market`

  const sideBtn = (s: Side, name: string) => {
    const active = ticket.side === s
    return (
      <button
        className="side-btn"
        onClick={() => set({ side: s })}
        style={{
          borderColor: active ? color(s) : 'var(--hairline)',
          background: active ? color(s) : 'transparent',
          color: active ? 'var(--panel)' : 'var(--muted)',
        }}
      >
        {name}
      </button>
    )
  }

  return (
    <Panel title="Order ticket" right="acct SIM-01" className="ticket">
      <form
        className="ticket-body"
        onSubmit={(e) => {
          e.preventDefault()
          onSubmit()
        }}
      >
        <div className="two-col">
          {sideBtn('B', 'Buy')}
          {sideBtn('S', 'Sell')}
        </div>

        <div className="seg">
          {(['limit', 'market'] as const).map((t) => (
            <button type="button" key={t} className={ticket.type === t ? 'on' : ''} onClick={() => set({ type: t })}>
              {t === 'limit' ? 'Limit' : 'Market'}
            </button>
          ))}
        </div>

        {ticket.type === 'limit' && (
          <label className="field">
            <span className="label mono">Limit price</span>
            <div className="stepper">
              <button type="button" onClick={() => set({ px: stepPx(ticket.px, -1, mid) })}>−</button>
              <input
                className="mono"
                value={ticket.px}
                placeholder={(mid / 100).toFixed(2)}
                onChange={(e) => set({ px: e.target.value })}
                inputMode="decimal"
              />
              <button type="button" onClick={() => set({ px: stepPx(ticket.px, 1, mid) })}>+</button>
            </div>
          </label>
        )}

        <label className="field">
          <span className="label mono">Quantity</span>
          <input
            className="qty mono"
            value={ticket.qty}
            onChange={(e) => set({ qty: e.target.value.replace(/\D/g, '') })}
            inputMode="numeric"
          />
          <div className="chips">
            {QTY_CHIPS.map((q) => (
              <button type="button" key={q} onClick={() => set({ qty: String(q) })}>
                {fmtN(q)}
              </button>
            ))}
          </div>
        </label>

        <div className="est">
          <span className="label mono">Est. notional</span>
          <span className="mono">
            {est === null ? '–' : `$${est.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`}
          </span>
        </div>

        {error && <div className="err">{error}</div>}

        <button type="submit" className="submit" style={{ background: color(ticket.side) }}>
          {label}
        </button>
      </form>
    </Panel>
  )
}
