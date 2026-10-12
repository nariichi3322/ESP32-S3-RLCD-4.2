import type { EngineInterface, Register, SessionRateLimit } from 'claude-code'

// Same file and shape the companion's statusLine helper writes; the companion
// reads it unchanged. The VS Code extension never runs statusLine, so this mod
// is the source there.
const FILE = 'CodexUsageDisplay\\claude-rate-limits.json'

const toWindow = (limit: SessionRateLimit) => {
  const resetsAt = limit.resetsAt ? Date.parse(limit.resetsAt) : NaN
  return Number.isFinite(resetsAt)
    ? { used_percentage: limit.percentUsed, resets_at: Math.floor(resetsAt / 1000) }
    : undefined
}

export const save = async ($: EngineInterface, limits: readonly SessionRateLimit[]) => {
  const out: Record<string, unknown> = {}
  for (const limit of limits) {
    if (limit.kind !== 'five_hour' && limit.kind !== 'seven_day') continue
    const window = toWindow(limit)
    if (window) out[limit.kind] = window
  }
  if (Object.keys(out).length === 0) return
  const root = await $.env.get('LOCALAPPDATA')
  if (!root) return
  const capturedAt = Math.floor((await $.clock.now()) / 1000)
  await $.fs.write(`${root}\\${FILE}`, JSON.stringify({ captured_at: capturedAt, ...out }))
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const result = await next(e)
    await save($, (await $.session.usage()).rateLimits).catch(() => undefined)
    return result
  })

  on('session.measure', async ($, e, next) => {
    if (e.changed.includes('rateLimits')) {
      await save($, e.rateLimits).catch(() => undefined)
    }
    return next(e)
  })
}
