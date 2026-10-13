import type { ReactNode } from 'react'

interface Props {
  title: string
  right?: ReactNode
  className?: string
  header?: ReactNode // replaces the title bar
  children?: ReactNode
}

export function Panel({ title, right, className = '', header, children }: Props) {
  return (
    <section className={`panel ${className}`}>
      {header ?? (
        <div className="panel-head mono">
          <span>{title}</span>
          {right !== undefined && <span>{right}</span>}
        </div>
      )}
      {children}
    </section>
  )
}
