export type FetchResult = 'ok' | 'no_credential' | 'auth_rejected' | 'rate_limited' | 'error'

export function shouldFetch({ lastOkAt, fetchingAt, retryUntil, now }: {
  lastOkAt: number, fetchingAt: number, retryUntil: number, now: number
}): boolean {
  return !(lastOkAt > 0 && now - lastOkAt < 900) &&
    !(fetchingAt > 0 && now - fetchingAt < 60) && now >= retryUntil
}

export function nextRetryUntil(result: FetchResult, now: number, retryAfter?: string): number {
  if (result === 'ok') return 0
  if (result !== 'rate_limited') return now + 900
  const header = retryAfter?.trim()
  const seconds = header && /^[0-9]+$/.test(header) ? Number(header) : 0
  return now + (Number.isSafeInteger(seconds) && seconds > 0 && Number.isSafeInteger(now + seconds)
    ? seconds : 600)
}
