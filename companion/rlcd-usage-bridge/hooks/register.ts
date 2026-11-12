import type { EngineInterface, Register, SessionRateLimit } from 'claude-code'
import { nextRetryUntil, shouldFetch, type FetchResult } from './fetch_policy.ts'

// Same file and shape the companion's statusLine helper writes; the companion
// reads it unchanged. The VS Code extension never runs statusLine, so this mod
// is the source there.
const FILE = 'CodexUsageDisplay\\claude-rate-limits.json'
const DETAIL_FILE = 'CodexUsageDisplay\\claude-usage-detail.json'
const USAGE_URL = 'https://api.anthropic.com/api/oauth/usage?cedar_ember=1'

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

const object = (value: unknown): Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown> : {}
const timestamp = (value: unknown): number =>
  typeof value === 'number' && Number.isSafeInteger(value) && value > 0 ? value : 0

let checking = false
const checkDetail = async ($: EngineInterface) => {
  if (checking) return
  checking = true
  try {
    const root = await $.env.get('LOCALAPPDATA')
    if (!root) return
    const path = `${root}\\${DETAIL_FILE}`
    let state: Record<string, unknown> = {}
    try { state = object(JSON.parse(await $.fs.read(path))) } catch { /* no usable state */ }
    const now = Math.floor((await $.clock.now()) / 1000)
    if (!shouldFetch({ lastOkAt: timestamp(object(state.last_ok).captured_at),
      fetchingAt: timestamp(state.fetching_at), retryUntil: timestamp(state.retry_until), now })) return
    state.fetching_at = now
    await $.fs.write(path, JSON.stringify(state))

    let result: FetchResult = 'no_credential'
    let retryAfter: string | undefined
    let body: Record<string, unknown> | undefined
    try {
      const credential = await $.session.authorize()
      if (credential && credential.kind === 'bearer') {
        const response = await $.http.fetch(USAGE_URL, {
          method: 'GET', auth: credential.handle,
          headers: { 'anthropic-beta': 'oauth-2025-04-20',
            'user-agent': 'claude-cli/2.1.321 (external, cli)' },
        })
        if (response.status === 401 || response.status === 403) result = 'auth_rejected'
        else if (response.status === 429) {
          result = 'rate_limited'
          retryAfter = response.headers['retry-after']
        } else if (response.status === 200) {
          try {
            const parsed: unknown = JSON.parse(response.text)
            if (parsed !== null && typeof parsed === 'object' && !Array.isArray(parsed)) {
              body = parsed as Record<string, unknown>
              result = 'ok'
            } else result = 'error'
          } catch { result = 'error' }
        } else result = 'error'
      }
    } catch { result = 'no_credential' }
    const at = Math.floor((await $.clock.now()) / 1000)
    if (result === 'ok') state.last_ok = { captured_at: at, body }
    state.last_attempt = { at, result }
    state.fetching_at = 0
    state.retry_until = nextRetryUntil(result, at, retryAfter)
    await $.fs.write(path, JSON.stringify(state))
  } catch { /* Never interrupt a Claude session for detail collection. */ }
  finally { checking = false }
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const result = await next(e)
    await save($, (await $.session.usage()).rateLimits).catch(() => undefined)
    void checkDetail($) // never hold up session start on a network request
    try { $.clock.every(5 * 60 * 1000, () => { void checkDetail($) }) } catch { /* optional */ }
    return result
  })

  on('session.measure', async ($, e, next) => {
    if (e.changed.includes('rateLimits')) {
      await save($, e.rateLimits).catch(() => undefined)
    }
    return next(e)
  })
}
