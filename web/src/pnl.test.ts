import { describe, expect, it } from 'vitest'
import { FLAT, applyFill, fmtMoney, unrealized } from './pnl'

describe('applyFill', () => {
  it('opens a position', () => {
    expect(applyFill(FLAT, 'B', 14250, 100)).toEqual({ qty: 100, avg: 14250, realized: 0 })
    expect(applyFill(FLAT, 'S', 14250, 100)).toEqual({ qty: -100, avg: 14250, realized: 0 })
  })

  it('averages in when adding', () => {
    let p = applyFill(FLAT, 'B', 14200, 100)
    p = applyFill(p, 'B', 14300, 300)
    expect(p.qty).toBe(400)
    expect(p.avg).toBe(14275)
  })

  it('realizes profit on a partial close, long and short', () => {
    let p = applyFill(FLAT, 'B', 14200, 200)
    p = applyFill(p, 'S', 14250, 50)
    expect(p).toEqual({ qty: 150, avg: 14200, realized: 50 * 50 })

    let s = applyFill(FLAT, 'S', 14200, 200)
    s = applyFill(s, 'B', 14250, 50) // bought back higher: a loss
    expect(s).toEqual({ qty: -150, avg: 14200, realized: -50 * 50 })
  })

  it('goes flat and resets the average', () => {
    let p = applyFill(FLAT, 'B', 14200, 100)
    p = applyFill(p, 'S', 14300, 100)
    expect(p).toEqual({ qty: 0, avg: 0, realized: 10000 })
  })

  it('flips through zero and restarts at the fill price', () => {
    let p = applyFill(FLAT, 'B', 14200, 100)
    p = applyFill(p, 'S', 14300, 250) // closes 100, opens 150 short
    expect(p).toEqual({ qty: -150, avg: 14300, realized: 10000 })
  })
})

describe('unrealized and formatting', () => {
  it('marks to the last price', () => {
    expect(unrealized({ qty: 100, avg: 14200, realized: 0 }, 14250)).toBe(5000)
    expect(unrealized({ qty: -100, avg: 14200, realized: 0 }, 14250)).toBe(-5000)
    expect(unrealized(FLAT, 14250)).toBe(0)
  })

  it('formats money', () => {
    expect(fmtMoney(1234.56)).toBe('+$1,234.56')
    expect(fmtMoney(-12)).toBe('−$12.00')
    expect(fmtMoney(0)).toBe('$0.00')
    expect(fmtMoney(1234.5, false)).toBe('$1,234.50')
  })
})
