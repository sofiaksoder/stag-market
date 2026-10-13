// The replay file written by stag_replay: a snapshot every 500 ms of exchange time.
import type { Trade } from './feed'

export interface Snapshot {
  seq: number
  t: number // ms since epoch
  last: number
  bids: [number, number][]
  asks: [number, number][]
  tape: Trade[] // newest first
  ev: string[] // what happened since the previous snapshot
}

export interface ReplayData {
  session: string // date
  seed: number // 0 for a live capture
  stepMs: number
  snapshots: Snapshot[]
}

interface Raw extends Omit<ReplayData, 'snapshots'> {
  snapshots: (Omit<Snapshot, 'tape'> & { tape: [number, number, number, number, 'B' | 'S'][] })[]
}

export async function loadReplay(url: string): Promise<ReplayData> {
  const res = await fetch(url)
  if (!res.ok) throw new Error(`${res.status}`)
  const raw = (await res.json()) as Raw
  return {
    ...raw,
    snapshots: raw.snapshots.map((s) => ({
      ...s,
      tape: s.tape.map(([seq, ts, px, qty, side]) => ({ seq, ts, px, qty, side })),
    })),
  }
}

// Every event in order, with the snapshot it belongs to.
export function flattenEvents(snaps: Snapshot[]) {
  const out: { text: string; snap: number }[] = []
  snaps.forEach((s, i) => s.ev.forEach((text) => out.push({ text, snap: i })))
  return out
}

// Rows for the event log: some before the cursor, the cursor, a few after.
// The cursor is the last event up to snapshot i.
export function eventWindow(events: { text: string; snap: number }[], i: number, before = 11, after = 3) {
  let cursor = -1
  for (let k = 0; k < events.length && events[k].snap <= i; k++) cursor = k
  const from = Math.max(0, cursor - before)
  const rows = events.slice(from, cursor + after + 1).map((e, k) => ({ n: from + k + 1, text: e.text, state: from + k < cursor ? 'past' : from + k === cursor ? 'now' : 'future' }) as const)
  return rows
}

// Bar heights (0 to 1) for the price sparkline, sampled down to `bars`.
export function sparkline(snaps: Snapshot[], bars: number) {
  if (snaps.length === 0) return []
  const picks = Array.from({ length: bars }, (_, j) => Math.round((j * (snaps.length - 1)) / Math.max(1, bars - 1)))
  const px = picks.map((k) => snaps[k].last)
  const lo = Math.min(...px)
  const hi = Math.max(...px)
  return picks.map((k, j) => ({ snap: k, h: hi === lo ? 0.5 : 0.1 + (0.9 * (px[j] - lo)) / (hi - lo) }))
}
