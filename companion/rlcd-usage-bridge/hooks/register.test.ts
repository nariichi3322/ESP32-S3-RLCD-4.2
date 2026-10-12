import { expect, test } from 'claude-code/testing'

const NOW_MS = 1_800_000_000_000

test('writes the 5h and 7d windows in the companion file format', async ($, on) => {
  const writes: { path: string; text: string }[] = []
  on('env.get', (_$, e) => ({ value: e.name === 'LOCALAPPDATA' ? 'C:\\Local' : undefined }))
  on('clock.now', () => ({ value: NOW_MS }))
  on('fs.write', (_$, e) => { writes.push(e); return { value: undefined } })
  on('session.measure', (_$, e) => ({ changed: e.changed }))

  await $.session.measure({
    context: { window: 200000 },
    rateLimits: [
      { kind: 'five_hour', percentUsed: 8, resetsAt: '2027-01-15T08:00:00Z' },
      { kind: 'seven_day', percentUsed: 1.5, resetsAt: '2027-01-20T00:00:00Z' },
      { kind: 'spend_limit', percentUsed: 50, resetsAt: '2027-02-01T00:00:00Z' },
    ],
    changed: ['rateLimits'],
  })

  expect(writes.length).toBe(1)
  expect(writes[0].path).toBe('C:\\Local\\CodexUsageDisplay\\claude-rate-limits.json')
  expect(JSON.parse(writes[0].text)).toEqual({
    captured_at: NOW_MS / 1000,
    five_hour: { used_percentage: 8, resets_at: Date.parse('2027-01-15T08:00:00Z') / 1000 },
    seven_day: { used_percentage: 1.5, resets_at: Date.parse('2027-01-20T00:00:00Z') / 1000 },
  })
})

test('skips the write when no rate-limit window moved or none is known', async ($, on) => {
  const writes: unknown[] = []
  on('env.get', () => ({ value: 'C:\\Local' }))
  on('clock.now', () => ({ value: NOW_MS }))
  on('fs.write', (_$, e) => { writes.push(e); return { value: undefined } })
  on('session.measure', (_$, e) => ({ changed: e.changed }))

  await $.session.measure({
    context: { window: 200000 },
    rateLimits: [{ kind: 'five_hour', percentUsed: 8, resetsAt: '2027-01-15T08:00:00Z' }],
    changed: ['context'],
  })
  await $.session.measure({ context: { window: 200000 }, rateLimits: [], changed: ['rateLimits'] })

  expect(writes.length).toBe(0)
})
