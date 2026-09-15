// 核对现有固件配网字段、URL编码与限制；仅使用虚构测试凭据。
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeQuickConfigLink } from '../quick-config.js';

const base = { ssid: 'Demo &+中文', pass: 'Test+#?&=123', weather_city: '杭州' };
const result = makeQuickConfigLink(base);
const url = new URL(result.url);
assert.equal(url.origin, 'http://192.168.4.1');
assert.equal(url.pathname, '/save');
assert.equal(url.searchParams.get('ssid'), base.ssid);
assert.equal(url.searchParams.get('pass'), base.pass);
assert.equal(url.searchParams.get('weather_city'), '杭州');
assert.equal(url.searchParams.has('api_host'), false);
assert.equal(url.searchParams.has('api_key'), false);
assert.equal(url.hash, '');
assert.ok(result.uriBytes <= 512);
assert.throws(() => makeQuickConfigLink({}), /离线/);
assert.throws(() => makeQuickConfigLink({...base, backup_ssid: base.ssid}), /不能相同/);
assert.throws(() => makeQuickConfigLink({...base, backup_pass: 'DEMO_PASS'}), /同时填写/);
assert.throws(() => makeQuickConfigLink({...base, weather_city: '杭州&x=1'}), /特殊字符/);
assert.throws(() => makeQuickConfigLink({...base, ssid: '城'.repeat(11)}), /32 字节/);
assert.throws(() => makeQuickConfigLink({...base, pass: 'a'.repeat(65)}), /64 字节/);
assert.throws(() => makeQuickConfigLink({...base, pass: '&'.repeat(54)}), /编码后过长/);
const retained = makeQuickConfigLink({ssid: 'Demo'});
assert.ok(retained.warnings.some(text => text.includes('自动定位')));
const offline = makeQuickConfigLink({ssid: '', manual_time: '2028-02-29T09:30', api_key: 'IGNORED'});
assert.equal(offline.offline, true);
assert.deepEqual([...new URL(offline.url).searchParams.keys()], ['manual_time']);
for (const date of ['2025-02-29T09:00', '2023-12-31T00:00', '2036-01-01T00:00', '2028-01-01T25:00']) {
  assert.throws(() => makeQuickConfigLink({manual_time: date}), /有效日期/);
}
assert.equal(new URL(makeQuickConfigLink({...base, manual_time: 'invalid'}).url).searchParams.has('manual_time'), false);
const source = readFileSync(new URL('../quick-config.js', import.meta.url), 'utf8');
assert.doesNotMatch(source, /\b(fetch|XMLHttpRequest|localStorage|sessionStorage|sendBeacon)\b/);
const html = readFileSync(new URL('../index.html', import.meta.url), 'utf8');
assert.match(html, /data-tab="settings"[^>]*>快捷配置/);
assert.match(html, /id="quickConfigForm" method="dialog"/);
assert.match(html, /id="quickGenerate"[^>]*disabled/);
assert.match(html, /id="customWeatherCity"/);
assert.match(html, /id="customOtaServer"/);
assert.doesNotMatch(html, /QWeather|qweather|API Key|API Host/);
assert.doesNotMatch(source, /QWeather|qweather|api_key|api_host/);
console.log('Quick configuration encoding, validation, privacy and compatibility tests passed.');
