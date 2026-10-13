import { describe, expect, it } from 'vitest'
import { binEdge, binTone, engineShare, fmtNs, wireSegments } from './perf'

const hist = { lo: 100, hi: 1600, counts: new Array(4).fill(0) } // edges at 200, 400, 800, 1600

describe('histogram bins', () => {
  it('spaces edges evenly on a log scale', () => {
    expect([0, 1, 2, 3].map((b) => Math.round(binEdge(hist, b)))).toEqual([200, 400, 800, 1600])
  })

  it('colours bins by where they sit against p50 and p99', () => {
    const tones = [0, 1, 2, 3].map((b) => binTone(hist, b, 250, 700))
    expect(tones).toEqual(['p50', 'p99', 'p99', 'tail'])
  })
})

describe('wire breakdown', () => {
  const w = { haveRtt: true, decode: 0, book: 170, publish: 230, socket: 34_600, rttP50: 35_000, rttP99: 60_000 }

  it('lists the four parts in order', () => {
    expect(wireSegments(w).map((s) => s.ns)).toEqual([0, 170, 230, 34_600])
  })

  it('works out the engine share of the round trip', () => {
    expect(engineShare(w)).toBeCloseTo(400 / 35_000)
    expect(engineShare({ ...w, rttP50: 0 })).toBe(0)
  })
})

describe('fmtNs', () => {
  it('switches to microseconds', () => {
    expect(fmtNs(168)).toBe('168 ns')
    expect(fmtNs(2450)).toBe('2.45 µs')
    expect(fmtNs(34_560)).toBe('34.6 µs')
  })
})
