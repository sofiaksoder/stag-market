import { useState } from 'react'
import type { FillRecord, LogEntry, OpenOrder } from '../account'
import { fmtN, fmtTime, px2 } from '../format'
import { FillsTable, sideColor, sideName } from './FillsTable'
import { Panel } from './Panel'

interface Props {
  orders: OpenOrder[]
  fills: FillRecord[]
  log: LogEntry[]
  onCancel: (id: number) => void
  onCancelAll: () => void
}

type Tab = 'orders' | 'fills' | 'log'

const dotColor = { ok: 'var(--bid)', gap: 'var(--ask)', fill: 'var(--ink)', ack: '#a8a296' }

export function OrdersPanel({ orders, fills, log, onCancel, onCancelAll }: Props) {
  const [tab, setTab] = useState<Tab>('orders')
  const tabs: [Tab, string][] = [
    ['orders', `Open orders (${orders.length})`],
    ['fills', `Fills (${fills.length})`],
    ['log', 'Session log'],
  ]

  const head = (
    <div className="tabs mono">
      {tabs.map(([k, label]) => (
        <button key={k} className={tab === k ? 'on' : ''} onClick={() => setTab(k)}>
          {label}
        </button>
      ))}
      <span className="grow" />
      {tab === 'orders' && orders.length > 0 && (
        <button className="cancel-all" onClick={onCancelAll}>
          Cancel all
        </button>
      )}
    </div>
  )

  return (
    <Panel title="" className="orders" header={head}>
      {tab === 'orders' && (
        <>
          <div className="orders-row cols mono">
            <span>Order</span><span>Side</span><span>Price</span><span>Open</span><span>Filled</span><span>Entered</span><span />
          </div>
          <div className="scroll mono">
            {orders.length === 0 && <div className="empty">No resting orders. Use the ticket, or click a price in the book.</div>}
            {orders.map((o) => (
              <div className="orders-row" key={o.id}>
                <span>#{o.id}</span>
                <span style={{ color: sideColor(o.side) }}>{sideName(o.side)}</span>
                <span>{px2(o.px)}</span>
                <span>{fmtN(o.qty - o.filled)}</span>
                <span>{fmtN(o.filled)}</span>
                <span className="muted">{fmtTime(o.ts)}</span>
                <button className="cancel" onClick={() => onCancel(o.id)}>Cancel</button>
              </div>
            ))}
          </div>
        </>
      )}

      {tab === 'fills' && <FillsTable fills={fills} />}

      {tab === 'log' && (
        <div className="scroll mono">
          {log.map((l, i) => (
            <div className="log-row" key={`${l.ts}-${i}`}>
              <span className="dot" style={{ background: dotColor[l.kind] }} />
              <span className="muted">{fmtTime(l.ts)}</span>
              <span>{l.text}</span>
            </div>
          ))}
        </div>
      )}
    </Panel>
  )
}
