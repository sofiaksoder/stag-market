import { useEffect, useState } from 'react'
import { binTone, engineShare, fmtNs, fmtOps, loadPerf, wireSegments, type PerfData } from '../perf'
import { Panel } from './Panel'

const PERF_URL = import.meta.env.VITE_PERF_URL ?? `${import.meta.env.BASE_URL}perf.json`
const SEGMENT_COLORS = ['#55554e', '#1c1d1a', '#8d897e', '#dcd8ce']

export function Performance() {
  const [data, setData] = useState<PerfData | null>(null)
  const [error, setError] = useState(false)

  useEffect(() => {
    loadPerf(PERF_URL).then(setData, () => setError(true))
  }, [])

  if (error) {
    return (
      <div className="page">
        <h1 className="page-h1">Performance</h1>
        <div className="empty-box">
          <p>No benchmark results found. Run the benchmark on a Release build:</p>
          <pre className="mono">{`cmake -S . -B build-rel && cmake --build build-rel
./build-rel/stag_bench web/public/perf.json`}</pre>
        </div>
      </div>
    )
  }
  if (!data) return <div className="page muted">Loading results…</div>
  return <Results d={data} />
}

function Results({ d }: { d: PerfData }) {
  const [op, setOp] = useState<'add' | 'match' | 'cancel'>('add')
  const cards = [
    { label: 'Add order (resting)', s: d.add },
    { label: 'Match + fill', s: d.match },
    { label: 'Cancel', s: d.cancel },
  ]
  const h = d[op].hist
  const maxCount = Math.max(1, ...h.counts)
  const wire = d.wire
  const segs = wireSegments(wire)
  const total = segs.reduce((a, s) => a + s.ns, 0)
  const base = d.variants[0]

  return (
    <div className="page">
      <div className="page-title">
        <h1>Performance</h1>
        <span className="muted mono small">
          {d.cpu} · single thread · {d.orders.toLocaleString('en-US')} orders, best of {d.runs} runs · {d.date}
        </span>
      </div>

      <div className="stats four">
        {cards.map((c) => (
          <div className="stat" key={c.label}>
            <div className="label mono">{c.label}</div>
            <div className="perf-value mono">
              {fmtNs(c.s.p50)} <span className="muted small">p50</span>
            </div>
            <div className="mono small muted">{fmtNs(c.s.p99)} p99</div>
          </div>
        ))}
        <div className="stat">
          <div className="label mono">Throughput</div>
          <div className="perf-value mono">
            {fmtOps(d.opsPerSec)} <span className="muted small">msgs/s</span>
          </div>
          <div className="mono small muted">add, match and cancel mixed</div>
        </div>
      </div>

      <Panel
        title="Latency histogram"
        right={
          <span className="seg inline-seg">
            {(['add', 'match', 'cancel'] as const).map((k) => (
              <button key={k} className={op === k ? 'on' : ''} onClick={() => setOp(k)}>
                {k === 'add' ? 'Add' : k === 'match' ? 'Match' : 'Cancel'}
              </button>
            ))}
          </span>
        }
      >
        <div className="hist">
          {h.counts.map((c, b) => (
            <div className="hist-slot" key={b} title={`${c.toLocaleString('en-US')} orders`}>
              <div
                className={`hist-bar ${binTone(h, b, d[op].p50, d[op].p99)}`}
                style={{ height: `${(c / maxCount) * 100}%` }}
              />
            </div>
          ))}
        </div>
        <div className="hist-axis mono small muted">
          <span>{fmtNs(h.lo)}</span>
          <span>log scale · {d.orders.toLocaleString('en-US')} samples · clock read ≈ {d.timerNs} ns is included</span>
          <span>{fmtNs(h.hi)}+</span>
        </div>
        <div className="legend mono small">
          <span><i className="sw p50" /> up to p50 ({fmtNs(d[op].p50)})</span>
          <span><i className="sw p99" /> up to p99 ({fmtNs(d[op].p99)})</span>
          <span><i className="sw tail" /> beyond p99</span>
        </div>
      </Panel>

      <Panel title="Wire to wire" right={<span className="nocase">{wire.haveRtt ? `p50 ${fmtNs(wire.rttP50)} · p99 ${fmtNs(wire.rttP99)}` : 'no gateway run'}</span>}>
        {wire.haveRtt ? (
          <div className="wire">
            <div className="stack">
              {segs.map((s, i) => (
                <div key={s.label} style={{ flex: Math.max(s.ns, 0.001) / total, background: SEGMENT_COLORS[i], minWidth: s.ns > 0 ? 2 : 0 }} title={`${s.label}: ${fmtNs(s.ns)}`} />
              ))}
            </div>
            <div className="legend mono small">
              {segs.map((s, i) => (
                <span key={s.label}>
                  <i className="sw" style={{ background: SEGMENT_COLORS[i] }} /> {s.label} {s.ns < 1 ? '<1 ns' : fmtNs(s.ns)}
                </span>
              ))}
            </div>
            <p className="caption">
              One resting order over loopback TCP, from send to ack. The engine (decode, book, encode and publish) is about{' '}
              {(engineShare(wire) * 100).toFixed(1)}% of it. The rest is the kernel, the socket and the client, so cutting
              the socket cost is the next big lever. There are no thread hops in these numbers yet, since the gateway and
              engine still share one thread.
            </p>
          </div>
        ) : (
          <p className="caption">The benchmark couldn't start the gateway, so there's no round trip to show.</p>
        )}
      </Panel>

      <Panel title="Optimization log" right="add / match / cancel mixed">
        <div className="opt-row cols mono">
          <span>#</span><span>Change</span><span style={{ textAlign: 'right' }}>Add p50</span><span style={{ textAlign: 'right' }}>Add p99</span><span style={{ textAlign: 'right' }}>Throughput</span><span style={{ textAlign: 'right' }}>vs. baseline</span>
        </div>
        <div className="mono">
          {d.variants.map((v, i) => {
            const last = i === d.variants.length - 1
            return (
              <div className="opt-row" key={v.name} style={{ fontWeight: last ? 600 : 400 }}>
                <span>{i + 1}</span>
                <span>{v.name}</span>
                <span style={{ textAlign: 'right' }}>{fmtNs(v.p50)}</span>
                <span style={{ textAlign: 'right' }}>{fmtNs(v.p99)}</span>
                <span style={{ textAlign: 'right' }}>{fmtOps(v.opsPerSec)}/s</span>
                <span style={{ textAlign: 'right', color: last ? 'var(--bid-text)' : undefined }}>
                  {i === 0 ? '1.0×' : `${(v.opsPerSec / base.opsPerSec).toFixed(1)}×`}
                </span>
              </div>
            )
          })}
        </div>
      </Panel>
    </div>
  )
}
