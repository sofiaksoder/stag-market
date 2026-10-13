import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig({
  // The demo is served from /stag-trader/ on GitHub Pages.
  base: process.env.VITE_BASE ?? '/',
  plugins: [react()],
})
