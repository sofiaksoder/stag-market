// Draft stag mark (1a). Replace with a refined vector before public use.
export function Logo({ size }: { size: number }) {
  return (
    <svg viewBox="0 0 100 100" width={size} height={size} fill="none" stroke="#1C1D1A" strokeWidth={7.5} strokeLinecap="round" strokeLinejoin="round">
      <path d="M82 32 L68 30 C74 40 72 58 58 60 L30 60 C22 60 18 70 14 88" />
      <path d="M68 30 L62 8" />
      <path d="M65 19 L78 11" />
      <path d="M58 61 L64 88" />
      <path d="M48 62 L48 88" />
      <path d="M37 62 L36 88" />
    </svg>
  )
}
