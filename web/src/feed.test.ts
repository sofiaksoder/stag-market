import { describe, expect, it } from 'vitest'
import { MarketState, chipState, decode } from './feed'

// Builders that match the gateway's wire format.
function delta(seq: number, side: 0 | 1, px: number, size: number) {
  const v = new DataView(new ArrayBuffer(22))
  v.setUint8(0, 20)
  v.setBigUint64(1, BigInt(seq), true)
  v.setUint8(9, side)
  v.setBigInt64(10, BigInt(px), true)
  v.setUint32(18, size, true)
  return v.buffer
}

function trade(seq: number, px: number, qty: number, aggressor: 0 | 1, tsMs = 1_700_000_000_000) {
  const v = new DataView(new ArrayBuffer(30))
  v.setUint8(0, 21)
  v.setBigUint64(1, BigInt(seq), true)
  v.setBigUint64(9, BigInt(tsMs) * 1_000_000n, true)
  v.setBigInt64(17, BigInt(px), true)
  v.setUint32(25, qty, true)
  v.setUint8(29, aggressor)
  return v.buffer
}

function snapshot(seq: number, bids: [number, number][], asks: [number, number][]) {
  const v = new DataView(new ArrayBuffer(13 + 12 * (bids.length + asks.length)))
  v.setUint8(0, 22)
  v.setBigUint64(1, BigInt(seq), true)
  v.setUint16(9, bids.length, true)
  v.setUint16(11, asks.length, true)
  let at = 13
  for (const [px, size] of [...bids, ...asks]) {
    v.setBigInt64(at, BigInt(px), true)
    v.setUint32(at + 8, size, true)
    at += 12
  }
  return v.buffer
}

function synced() {
  let requests = 0
  const s = new MarketState(() => requests++)
  s.onOpen()
  s.onMessage(snapshot(10, [[14249, 300]], [[14251, 200]]))
  return { s, requests: () => requests }
}

describe('decode', () => {
  it('reads each message type', () => {
    expect(decode(delta(5, 1, 14252, 40))).toEqual({ kind: 'delta', seq: 5, side: 'S', px: 14252, size: 40 })
    const t = decode(trade(6, 14250, 100, 0))
    expect(t).toMatchObject({ kind: 'trade', seq: 6, trade: { px: 14250, qty: 100, side: 'B', ts: 1_700_000_000_000 } })
    expect(decode(snapshot(7, [[100, 1]], [[101, 2], [102, 3]]))).toEqual({
      kind: 'snapshot', seq: 7, bids: [[100, 1]], asks: [[101, 2], [102, 3]],
    })
  })
})

describe('MarketState', () => {
  it('starts from the snapshot and applies deltas', () => {
    const { s } = synced()
    expect(s.status).toBe('sync')
    s.onMessage(delta(11, 0, 14250, 500))
    s.onMessage(delta(12, 1, 14251, 0)) // level gone
    expect(s.levels('B', 5)).toEqual([[14250, 500], [14249, 300]])
    expect(s.levels('S', 5)).toEqual([])
  })

  it('records trades and the last price', () => {
    const { s } = synced()
    s.onMessage(trade(11, 14251, 100, 0))
    s.onMessage(trade(12, 14250, 50, 1))
    expect(s.last).toBe(14250)
    expect(s.trades.map((t) => t.seq)).toEqual([12, 11])
  })

  it('freezes on a gap, then recovers from a snapshot plus buffered deltas', () => {
    const { s, requests } = synced()
    s.onMessage(delta(11, 0, 14250, 500))
    s.onMessage(delta(14, 0, 14248, 70)) // 12 and 13 missing
    expect(s.status).toBe('recover')
    expect(s.gap).toMatchObject({ from: 12, to: 13 })
    expect(requests()).toBe(1)

    s.onMessage(delta(15, 0, 14247, 10)) // buffered, book still frozen
    expect(s.levels('B', 5).map((l) => l[0])).toEqual([14250, 14249])

    s.onMessage(snapshot(13, [[14250, 500], [14249, 300]], [[14251, 200]]))
    expect(s.status).toBe('sync')
    expect(s.seq).toBe(15)
    expect(s.levels('B', 5).map((l) => l[0])).toEqual([14250, 14249, 14248, 14247])
  })

  it('asks again if the snapshot still leaves a hole', () => {
    const { s, requests } = synced()
    s.onMessage(delta(14, 0, 14248, 70))
    s.onMessage(delta(20, 0, 14247, 10))
    s.onMessage(snapshot(13, [[14249, 300]], [[14251, 200]]))
    expect(s.seq).toBe(14)
    expect(s.status).toBe('recover')
    expect(requests()).toBe(2)
  })

  it('shows a gap briefly before RECOVERING', () => {
    let t = 1000
    const s = new MarketState(() => {}, () => t)
    s.onOpen()
    s.onMessage(snapshot(10, [], []))
    s.onMessage(delta(13, 0, 14249, 1))
    expect(chipState(s, t)).toMatchObject({ label: 'SEQ GAP', detail: 'missing 11–12' })
    t += 1000
    expect(chipState(s, t).label).toBe('RECOVERING')
  })
})

describe('candles', () => {
  const at = (ms: number) => ms
  it('groups trades into 3 second buckets', () => {
    const { s } = synced()
    s.onMessage(trade(11, 14250, 100, 0, at(3000)))
    s.onMessage(trade(12, 14260, 50, 0, at(4500)))
    s.onMessage(trade(13, 14240, 25, 1, at(5999)))
    s.onMessage(trade(14, 14245, 10, 1, at(6000)))
    expect(s.candles).toEqual([
      { k: 1, o: 14250, h: 14260, l: 14240, c: 14240, v: 175 },
      { k: 2, o: 14245, h: 14245, l: 14245, c: 14245, v: 10 },
    ])
  })

  it('keeps only the most recent 120', () => {
    const { s } = synced()
    for (let i = 0; i < 130; i++) s.onMessage(trade(11 + i, 14250, 1, 0, i * 3000))
    expect(s.candles.length).toBe(120)
    expect(s.candles[0].k).toBe(10)
  })
})
