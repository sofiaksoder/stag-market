// Numbers written by stag_bench.

export interface Hist {
  lo: number // ns
  hi: number
  counts: number[]
}

export interface OpStats {
  p50: number // ns
  p99: number
  hist: Hist
}

export interface Variant {
  name: string
  p50: number
  p99: number
  opsPerSec: number
}

export interface PerfData {
  date: string
  cpu: string
  compiler: string
  orders: number
  runs: number
  timerNs: number
  add: OpStats
  match: OpStats
  cancel: OpStats
  opsPerSec: number
  variants: Variant[]
  wire: { haveRtt: boolean; decode: number; book: number; publish: number; socket: number; rttP50: number; rttP99: number }
}

export async function loadPerf(url: string): Promise<PerfData> {
  const res = await fetch(url)
  if (!res.ok) throw new Error(`${res.status}`)
  return (await res.json()) as PerfData
}

// Where bin b ends, since the bins are evenly spaced on a log scale.
export const binEdge = (h: Hist, b: number) => h.lo * Math.pow(h.hi / h.lo, (b + 1) / h.counts.length)

// Ink up to the median, grey up to p99, red past it.
export function binTone(h: Hist, b: number, p50: number, p99: number): 'p50' | 'p99' | 'tail' {
  const lower = b === 0 ? 0 : binEdge(h, b - 1)
  const mid = Math.sqrt(Math.max(lower, h.lo) * binEdge(h, b))
  return mid <= p50 ? 'p50' : mid <= p99 ? 'p99' : 'tail'
}

export interface Segment {
  label: string
  ns: number
}

// Pieces of one order's round trip. The socket part is whatever is left after the engine's share.
export function wireSegments(w: PerfData['wire']): Segment[] {
  return [
    { label: 'Decode + validate', ns: w.decode },
    { label: 'Book', ns: w.book },
    { label: 'Encode + publish', ns: w.publish },
    { label: 'Kernel / socket / client', ns: w.socket },
  ]
}

// How much of the round trip is the engine itself.
export function engineShare(w: PerfData['wire']) {
  const engine = w.decode + w.book + w.publish
  return w.rttP50 > 0 ? engine / w.rttP50 : 0
}

export function fmtNs(ns: number) {
  return ns >= 1000 ? `${(ns / 1000).toFixed(ns >= 10_000 ? 1 : 2)} µs` : `${Math.round(ns)} ns`
}

export const fmtOps = (n: number) => `${(n / 1e6).toFixed(1)}M`
