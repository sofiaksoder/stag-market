// Order messages going to the gateway. Same layout as the TCP protocol
// without the length prefix, since WebSocket keeps message boundaries.

export const NEW_ORDER = 1
export const CANCEL = 2

export interface OrderRequest {
  clOrdId: number
  side: 'B' | 'S'
  type: 'limit' | 'market'
  px: number // ticks, ignored for market orders
  qty: number
}

export function encodeNewOrder(o: OrderRequest): Uint8Array<ArrayBuffer> {
  const buf = new ArrayBuffer(23)
  const v = new DataView(buf)
  v.setUint8(0, NEW_ORDER)
  v.setBigUint64(1, BigInt(o.clOrdId), true)
  v.setUint8(9, o.side === 'B' ? 0 : 1)
  v.setUint8(10, o.type === 'limit' ? 0 : 1)
  v.setBigInt64(11, BigInt(o.type === 'limit' ? o.px : 0), true)
  v.setUint32(19, o.qty, true)
  return new Uint8Array(buf)
}

export function encodeCancel(id: number): Uint8Array<ArrayBuffer> {
  const buf = new ArrayBuffer(9)
  const v = new DataView(buf)
  v.setUint8(0, CANCEL)
  v.setBigUint64(1, BigInt(id), true)
  return new Uint8Array(buf)
}

// Names for the engine's reject codes.
export const REJECT_TEXT: Record<number, string> = {
  1: 'duplicate order id',
  2: 'unknown order',
  3: 'price out of range',
  4: 'bad quantity',
  5: 'exchange is full',
  6: 'no liquidity to trade against',
  7: 'over the 50,000 share limit',
}
