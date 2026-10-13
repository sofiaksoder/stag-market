export const px2 = (ticks: number) => (ticks / 100).toFixed(2)
export const fmtN = (n: number) => n.toLocaleString('en-US')

// Local time by default. Replays use UTC so they match the date in the journal.
export function fmtTime(ms: number, utc = false) {
  const d = new Date(ms)
  const p = (n: number, w = 2) => String(n).padStart(w, '0')
  return utc
    ? `${p(d.getUTCHours())}:${p(d.getUTCMinutes())}:${p(d.getUTCSeconds())}.${p(d.getUTCMilliseconds(), 3)}`
    : `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}`
}

// Change against the session reference price, like +0.12 (+0.08%).
export function fmtChange(last: number, ref: number) {
  const diff = last - ref
  const sign = diff >= 0 ? '+' : '−'
  const pct = (Math.abs(diff) / ref) * 100
  return `${sign}${(Math.abs(diff) / 100).toFixed(2)} (${sign}${pct.toFixed(2)}%)`
}
