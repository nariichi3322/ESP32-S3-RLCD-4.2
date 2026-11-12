import assert from 'node:assert/strict'
import test from 'node:test'
import { nextRetryUntil, shouldFetch } from '../../rlcd-usage-bridge/hooks/fetch_policy.ts'
import { register } from '../../rlcd-usage-bridge/hooks/register.ts'

const base = { lastOkAt: 0, fetchingAt: 0, retryUntil: 0, now: 1000 }

test('successful fetch cooldown is 900 seconds, with zero meaning none', () => {
  assert.equal(shouldFetch({ ...base, lastOkAt: 1000, now: 1899 }), false)
  assert.equal(shouldFetch({ ...base, lastOkAt: 1000, now: 1900 }), true)
  assert.equal(shouldFetch({ ...base, now: 0 }), true)
})

test('in-flight fetch cooldown is 60 seconds', () => {
  assert.equal(shouldFetch({ ...base, fetchingAt: 1000, now: 1059 }), false)
  assert.equal(shouldFetch({ ...base, fetchingAt: 1000, now: 1060 }), true)
})

test('retry deadline includes equality', () => {
  assert.equal(shouldFetch({ ...base, retryUntil: 1001 }), false)
  assert.equal(shouldFetch({ ...base, retryUntil: 1000 }), true)
})

test('retry deadlines follow result and positive integer retry-after', () => {
  assert.equal(nextRetryUntil('ok', 1000), 0)
  for (const result of ['no_credential', 'auth_rejected', 'error'] as const)
    assert.equal(nextRetryUntil(result, 1000), 1900)
  assert.equal(nextRetryUntil('rate_limited', 1000, '120'), 1120)
  assert.equal(nextRetryUntil('rate_limited', 1000, ' 120 '), 1120)
  for (const header of [undefined, '', '0', '-1', '1.2', '1e3', '999999999999999999999'])
    assert.equal(nextRetryUntil('rate_limited', 1000, header), 1600)
})

function harness() {
  let now = 1_794_378_572_000
  const files = new Map<string, string>()
  const writes: { path: string, text: string }[] = []
  const timers: { ms: number, fn: () => void }[] = []
  const calls: { url: string, init: any }[] = []
  let credential: any = { handle: 'opaque-handle', kind: 'bearer' }
  let response: any = { status: 200, headers: {}, text: '{"limits":[]}' }
  const handlers: Record<string, any> = {}
  const $: any = {
    env: { get: async () => 'C:\\Local' },
    clock: { now: async () => now, every: (ms: number, fn: () => void) => {
      timers.push({ ms, fn }); return { cancel() {} }
    } },
    fs: { read: async (path: string) => {
      if (!files.has(path)) throw new Error('missing')
      return files.get(path)
    }, write: async (path: string, text: string) => { writes.push({ path, text }); files.set(path, text) } },
    session: { usage: async () => ({ rateLimits: [] }), authorize: async () => credential },
    http: { fetch: async (url: string, init: any) => {
      calls.push({ url, init })
      if (response instanceof Error) throw response
      return response
    } },
  }
  register(((name: string, fn: any) => { handlers[name] = fn }) as any)
  const path = 'C:\\Local\\CodexUsageDisplay\\claude-usage-detail.json'
  return { files, writes, timers, calls, path, engine: $,
    // The detail check runs in the background; let its (mocked, immediate) awaits drain.
    start: async () => {
      const result = await handlers['session.start']($, {}, async () => 'unchanged')
      await new Promise(resolve => setImmediate(resolve))
      return result
    },
    startOnly: () => handlers['session.start']($, {}, async () => 'unchanged'),
    time: (value: number) => { now = value },
    respond: (value: any) => { response = value },
    authorize: (value: any) => { credential = value },
    state: () => JSON.parse(files.get(path)!),
  }
}

test('session start fetches once, keeps raw body, and rechecks every five minutes', async () => {
  const h = harness()
  assert.equal(await h.start(), 'unchanged')
  assert.equal(h.timers[0].ms, 300_000)
  assert.equal(h.calls.length, 1)
  assert.equal(h.calls[0].url, 'https://api.anthropic.com/api/oauth/usage?cedar_ember=1')
  assert.deepEqual(h.calls[0].init, { method: 'GET', auth: 'opaque-handle', headers: {
    'anthropic-beta': 'oauth-2025-04-20', 'user-agent': 'claude-cli/2.1.321 (external, cli)' } })
  assert.equal(JSON.parse(h.writes[0].text).fetching_at, 1794378572)
  assert.deepEqual(h.state(), { fetching_at: 0,
    last_ok: { captured_at: 1794378572, body: { limits: [] } },
    last_attempt: { at: 1794378572, result: 'ok' }, retry_until: 0 })
  h.time(1_794_378_872_000)
  h.timers[0].fn()
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(h.calls.length, 1)
  h.time(1_794_379_472_000)
  h.timers[0].fn()
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(h.calls.length, 2)
})

test('session start does not wait for the usage request', async () => {
  const h = harness()
  let release!: (value: any) => void
  h.engine.http.fetch = (url: string, init: any) => {
    h.calls.push({ url, init })
    return new Promise(resolve => { release = resolve })
  }
  assert.equal(await h.startOnly(), 'unchanged')
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(h.calls.length, 1)
  assert.equal(h.state().last_attempt, undefined)
  release({ status: 200, headers: {}, text: '{}' })
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(h.state().last_attempt.result, 'ok')
})

test('failures retain last success and set the appropriate retry deadline', async () => {
  const cases = [
    { response: { status: 401, headers: {}, text: '' }, result: 'auth_rejected', retry: 900 },
    { response: { status: 403, headers: {}, text: '' }, result: 'auth_rejected', retry: 900 },
    { response: { status: 429, headers: { 'retry-after': '120' }, text: '' }, result: 'rate_limited', retry: 120 },
    { response: { status: 429, headers: {}, text: '' }, result: 'rate_limited', retry: 600 },
    { response: { status: 500, headers: {}, text: '' }, result: 'error', retry: 900 },
    { response: { status: 200, headers: {}, text: '[' }, result: 'error', retry: 900 },
    { response: { status: 200, headers: {}, text: '[]' }, result: 'error', retry: 900 },
    { response: new Error('offline'), result: 'no_credential', retry: 900 },
  ]
  for (const item of cases) {
    const h = harness()
    h.files.set(h.path, JSON.stringify({ last_ok: { captured_at: 1, body: { saved: true } } }))
    h.respond(item.response)
    await h.start()
    assert.deepEqual(h.state().last_ok, { captured_at: 1, body: { saved: true } })
    assert.deepEqual(h.state().last_attempt, { at: 1794378572, result: item.result })
    assert.equal(h.state().retry_until, 1794378572 + item.retry)
    assert.equal(h.state().fetching_at, 0)
  }
})

test('missing and API key credentials do not call the endpoint', async () => {
  for (const credential of [null, { handle: 'opaque-handle', kind: 'api-key' }]) {
    const h = harness()
    h.authorize(credential)
    await h.start()
    assert.equal(h.calls.length, 0)
    assert.equal(h.state().last_attempt.result, 'no_credential')
  }
})

test('invalid local JSON or timestamps are treated as empty state', async () => {
  for (const previous of ['{', JSON.stringify({ fetching_at: 1e100, retry_until: 1e100,
                                                  last_ok: { captured_at: 1e100 } })]) {
    const h = harness()
    h.files.set(h.path, previous)
    await h.start()
    assert.equal(h.calls.length, 1)
    assert.equal(h.state().last_attempt.result, 'ok')
  }
})

test('filesystem, clock, and authorization failures never escape session start', async () => {
  for (const operation of ['write', 'clock', 'authorize']) {
    const h = harness()
    const fail = async () => { throw new Error('unavailable') }
    if (operation === 'write') h.engine.fs.write = fail
    else if (operation === 'clock') h.engine.clock.now = fail
    else h.engine.session.authorize = fail
    assert.equal(await h.start(), 'unchanged')
    assert.equal(h.timers.length, 1)
    if (operation === 'authorize') assert.equal(h.state().last_attempt.result, 'no_credential')
  }
})
