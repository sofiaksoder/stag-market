import { describe, expect, it } from 'vitest'
import { checkTicket, notional, stepPx } from './ticket'

const limit = { side: 'B', type: 'limit', px: '142.48', qty: '300' } as const

describe('checkTicket', () => {
  it('builds a limit order in ticks', () => {
    expect(checkTicket(limit).order).toEqual({ side: 'B', type: 'limit', px: 14248, qty: 300 })
  })

  it('rounds away float noise', () => {
    expect(checkTicket({ ...limit, px: '142.29' }).order?.px).toBe(14229)
  })

  it('rejects bad quantities', () => {
    for (const qty of ['', '0', '-5', '1.5', 'abc', '50001']) expect(checkTicket({ ...limit, qty }).error).toBeTruthy()
    expect(checkTicket({ ...limit, qty: '50000' }).error).toBeUndefined()
  })

  it('rejects bad prices', () => {
    for (const px of ['', '0', '-1', 'x', 'Infinity', '0.001']) expect(checkTicket({ ...limit, px }).error).toBeTruthy()
  })

  it('needs liquidity for a market order', () => {
    const mkt = { ...limit, type: 'market' } as const
    expect(checkTicket(mkt, 14240, undefined).error).toMatch(/liquidity/)
    expect(checkTicket(mkt, 14240, 14250).order).toEqual({ side: 'B', type: 'market', px: 0, qty: 300 })
    expect(checkTicket({ ...mkt, side: 'S' }, undefined, 14250).error).toMatch(/liquidity/)
  })
})

describe('notional', () => {
  it('uses the limit price or the opposite touch', () => {
    expect(notional(limit)).toBeCloseTo(300 * 142.48)
    expect(notional({ ...limit, type: 'market' }, 14240, 14250)).toBeCloseTo(300 * 142.5)
    expect(notional({ ...limit, type: 'market', side: 'S' }, 14240, 14250)).toBeCloseTo(300 * 142.4)
    expect(notional({ ...limit, qty: '' })).toBeNull()
  })
})

describe('stepPx', () => {
  it('moves one tick and starts from the mid when empty', () => {
    expect(stepPx('142.48', 1, 14250)).toBe('142.49')
    expect(stepPx('142.48', -1, 14250)).toBe('142.47')
    expect(stepPx('', 1, 14250)).toBe('142.51')
    expect(stepPx('0.01', -1, 14250)).toBe('0.01')
  })
})
