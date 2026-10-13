import { useEffect, useMemo, useState } from 'react'
import { eventWindow, flattenEvents, loadReplay, sparkline, type ReplayData } from '../replay'
import { fmtN, fmtTime } from '../format'
import { Ladder } from './Ladder'
import { Panel } from './Panel'
import { Tape } from './Tape'

const REPLAY_URL = import.meta.env.VITE_REPLAY_URL ?? `${import.meta.env.BASE_URL}replay.json`
const BOOK_LEVELS = 8
const SPEEDS = [1, 4, 16]
const BARS = 160

export function Replay() {
  const [data, setData] = useState<ReplayData | null>(null)
  const [error, setError] = useState(false)

  useEffect(() => {
    loadReplay(REPLAY_URL).then(setData, () => setError(true))
  }, [])

  if (error) {
    return (
      <div className="page">
        <h1 className="page-h1">Replay</h1>
        <div className="empty-box">
          <p>No replay file found. Make one from a journal:</p>
          <pre className="mono">{`./build/stag_sim 42 2000 session.journal
./build/stag_replay session.journal web/public/replay.json`}</pre>
          <p className="muted">Or run the gateway with a journal file (`stag_gateway 9000 live.journal`) and replay what it recorded.</p>
        </div>
      </div>
    )
  }
  if (!data) return <div className="page muted">Loading replay…</div>
  return <Player data={data} />
}

function Player({ data }: { data: ReplayData }) {
  const snaps = data.snapshots
  const last = snaps.length - 1
  const [i, setI] = useState(0)
  const [playing, setPlaying] = useState(false)
  const [speed, setSpeed] = useState(1)

  const events = useMemo(() => flattenEvents(snaps), [snaps])
  const bars = useMemo(() => sparkline(snaps, BARS), [snaps])

  // One snapshot is 500 ms of exchange time, so 1x plays one every 500 ms.
  useEffect(() => {
    if (!playing) return
    const timer = window.setInterval(() => {
      setI((cur) => {
        if (cur >= last) {
          setPlaying(false)
          return cur
        }
        return cur + 1
      })
    }, data.stepMs / speed)
    return () => window.clearInterval(timer)
  }, [playing, speed, last, data.stepMs])

  const go = (n: number) => setI(Math.min(last, Math.max(0, n)))
  const togglePlay = () => {
    if (!playing && i >= last) setI(0) // play at the end starts over
    setPlaying(!playing)
  }

  const s = snaps[i]
  const rows = eventWindow(events, i)
  const seedText = data.seed ? `deterministic · seed ${data.seed}` : 'live capture'

  return (
    <div className="replay">
      <Ladder bids={s.bids} asks={s.asks} last={s.last} levels={BOOK_LEVELS} title="Book at cursor" right="rebuilt" />

      <Panel title={`Journal replay · session ${data.session}`} right={seedText} className="journal">
        <div className="readouts">
          <Readout label="Exchange time" value={fmtTime(s.t, true)} />
          <Readout label="Sequence" value={fmtN(s.seq)} />
          <Readout label="Snapshot" value={`${fmtN(i + 1)} / ${fmtN(snaps.length)}`} />
        </div>
        <div className="event-log mono">
          {rows.map((r) => (
            <div className={`ev ${r.state}`} key={r.n}>
              <span className="muted">{fmtN(r.n)}</span>
              <span>{r.text}</span>
            </div>
          ))}
        </div>
      </Panel>

      <Tape trades={s.tape} title="Trades at cursor" right="" utc />

      <div className="transport">
        <div className="buttons">
          <button onClick={() => { setPlaying(false); go(0) }} title="Start">|◀</button>
          <button onClick={() => { setPlaying(false); go(i - 1) }}>−1</button>
          <button className="play" onClick={togglePlay}>{playing ? '❚❚ Pause' : '▶ Play'}</button>
          <button onClick={() => { setPlaying(false); go(i + 1) }}>+1</button>
          <button onClick={() => { setPlaying(false); go(last) }} title="End">▶|</button>
        </div>
        <div className="seg speeds">
          {SPEEDS.map((v) => (
            <button key={v} className={speed === v ? 'on' : ''} onClick={() => setSpeed(v)}>{v}×</button>
          ))}
        </div>
        <span className="mono small muted">{fmtTime(snaps[0].t, true)}</span>
        <div className="timeline">
          <div className="spark">
            {bars.map((b, j) => (
              <div key={j} className="spark-bar" style={{ height: `${b.h * 100}%`, background: b.snap <= i ? 'var(--ink)' : '#d6d2c8' }} />
            ))}
          </div>
          <input
            type="range"
            min={0}
            max={last}
            value={i}
            onChange={(e) => { setPlaying(false); go(Number(e.target.value)) }}
            aria-label="Replay position"
          />
        </div>
        <span className="mono small muted">{fmtTime(snaps[last].t, true)}</span>
      </div>
    </div>
  )
}

function Readout({ label, value }: { label: string; value: string }) {
  return (
    <div className="readout">
      <div className="label mono">{label}</div>
      <div className="readout-value mono">{value}</div>
    </div>
  )
}
