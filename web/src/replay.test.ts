import { describe, expect, it } from 'vitest'
import { eventWindow, flattenEvents, sparkline, type Snapshot } from './replay'

const snap = (last: number, ev: string[]): Snapshot => ({ seq: 0, t: 0, last, bids: [], asks: [], tape: [], ev })

describe('eventWindow', () => {
  const snaps = [snap(1, ['a', 'b']), snap(2, []), snap(3, ['c']), snap(4, ['d', 'e', 'f'])]
  const events = flattenEvents(snaps)

  it('puts the cursor on the last event up to the snapshot', () => {
    const rows = eventWindow(events, 2)
    expect(rows.map((r) => r.text)).toEqual(['a', 'b', 'c', 'd', 'e', 'f'])
    expect(rows.find((r) => r.state === 'now')?.text).toBe('c')
    expect(rows.filter((r) => r.state === 'future').map((r) => r.text)).toEqual(['d', 'e', 'f'])
  })

  it('works through quiet snapshots and at the ends', () => {
    expect(eventWindow(events, 1).find((r) => r.state === 'now')?.text).toBe('b')
    expect(eventWindow(events, 3).find((r) => r.state === 'now')?.text).toBe('f')
    expect(eventWindow(events, 0, 11, 1).map((r) => r.text)).toEqual(['a', 'b', 'c'])
  })

  it('numbers events from 1', () => {
    expect(eventWindow(events, 3)[0].n).toBe(1)
  })
})

describe('sparkline', () => {
  it('scales prices into bars', () => {
    const s = sparkline([snap(100, []), snap(150, []), snap(200, [])], 3)
    expect(s.map((b) => b.snap)).toEqual([0, 1, 2])
    expect(s[0].h).toBeCloseTo(0.1)
    expect(s[2].h).toBeCloseTo(1)
  })
  it('handles a flat price and an empty replay', () => {
    expect(sparkline([snap(5, []), snap(5, [])], 2).every((b) => b.h === 0.5)).toBe(true)
    expect(sparkline([], 10)).toEqual([])
  })
})
