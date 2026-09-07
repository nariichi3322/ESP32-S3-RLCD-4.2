// 验证 Pages 同源镜像、哈希失败关闭和 Service Worker 缓存隔离。
import assert from 'node:assert/strict';
import { mkdtemp, readFile, readdir, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { createHash } from 'node:crypto';

const directory = await mkdtemp(path.join(os.tmpdir(), 'clock-pages-test-'));
const bytes = new Uint8Array([1, 2, 3, 4]);
const digest = createHash('sha256').update(bytes).digest('hex');
let badHash = false;
globalThis.fetch = async (url) => {
  if (url.includes('api.github.com')) {
    const releases = Array.from({ length: 12 }, (_, i) => ({
      tag_name: `v1.0.${12 - i}`, draft: false, prerelease: i === 0,
      assets: ['', '_merged'].map(suffix => ({
        name: `weather_clock_v1.0.${12 - i}${suffix}.bin`, size: bytes.length,
        digest: `sha256:${badHash ? '0'.repeat(64) : digest}`,
        browser_download_url: `https://github.com/wickenzh/ESP32-S3-RLCD-4.2/releases/download/v1.0.${12 - i}/firmware${suffix}.bin`
      }))
    }));
    return Response.json(releases);
  }
  return new Response(bytes);
};
try {
  process.argv[2] = path.join(directory, 'site');
  await import('./build_pages_site.mjs?valid');
  const manifest = JSON.parse(await readFile(path.join(process.argv[2], 'firmware/releases.json')));
  assert.equal(manifest.items.length, 10);
  assert.equal(manifest.items[0].version, 'v1.0.11');
  assert.equal(manifest.items[0].app.sha256, digest);
  assert(!(await readdir(process.argv[2])).includes('scripts'));
  assert(!(await readdir(process.argv[2])).includes('AI_HOST_WEB_GUIDE.md'));
  badHash = true;
  process.argv[2] = path.join(directory, 'failed-site');
  await assert.rejects(import('./build_pages_site.mjs?bad'), /SHA256 mismatch/);

  const handlers = {};
  const deleted = [];
  const prefix = 'weather-clock-unified:/ESP32-S3-RLCD-4.2/sw.js:';
  const context = { self: { location: { pathname: '/ESP32-S3-RLCD-4.2/sw.js' },
    addEventListener: (name, handler) => { handlers[name] = handler; }, clients: { claim() {} } },
    caches: { keys: async () => ['weather-clock-host-v43', prefix + 'v43', prefix + 'v44'],
      delete: async key => { deleted.push(key); } } };
  vm.runInNewContext(await readFile(new URL('../sw.js', import.meta.url), 'utf8'), context);
  let complete;
  handlers.activate({ waitUntil: promise => { complete = promise; } });
  await complete;
  assert.deepEqual(deleted, [prefix + 'v43']);
  console.log('Pages mirror and cache isolation tests passed');
} finally {
  await rm(directory, { recursive: true, force: true });
}
