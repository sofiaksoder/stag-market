import logo from '../assets/stag-logo.svg'

export function Logo({ size }: { size: number }) {
  return <img src={logo} width={size} height={size} alt="" />
}
