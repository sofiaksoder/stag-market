import { describe, expect, it } from 'vitest'
import { splitFrames } from './transport'

// A frame as the engine writes it: u16 length, then type byte and body.
const frame = (type: number, ...body: number[]) => [(body.length + 1) & 255, (body.length + 1) >> 8, type, ...body]

describe('splitFrames', () => {
  it('cuts the outbox into messages without their length', () => {
    const bytes = new Uint8Array([...frame(20, 1, 2, 3), ...frame(21, 9), ...frame(22)])
    const out = splitFrames(bytes).map((b) => [...new Uint8Array(b)])
    expect(out).toEqual([[20, 1, 2, 3], [21, 9], [22]])
  })

  it('handles an empty outbox and a truncated tail', () => {
    expect(splitFrames(new Uint8Array([]))).toEqual([])
    const cut = new Uint8Array([...frame(20, 1, 2, 3), 5, 0, 21, 1])
    expect(splitFrames(cut).length).toBe(1)
  })

  it('reads lengths above 255', () => {
    const body = new Array(300).fill(7)
    const bytes = new Uint8Array(frame(22, ...body))
    expect(new Uint8Array(splitFrames(bytes)[0]).length).toBe(301)
  })
})
