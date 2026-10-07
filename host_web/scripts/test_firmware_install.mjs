// Contract checks for the simplified full-install and advanced App-update UI.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { firmwareLocales, pickFirmwareLocale } from '../i18n.js';

const html = readFileSync(new URL('../index.html', import.meta.url), 'utf8');
const app = readFileSync(new URL('../app.js', import.meta.url), 'utf8');
const tabOrder = [...html.matchAll(/data-tab="([^"]+)"/g)].map(match => match[1]);
assert.deepEqual(tabOrder, ['firmware', 'assets', 'writer', 'serial', 'screens', 'settings']);
for (const id of ['firmwareInstallConnectBtn', 'firmwareInstallBtn', 'firmwareInstallConfirm', 'firmwareInstallConfirmBtn', 'firmwareInstallCancelBtn', 'firmwareInstallProgress', 'firmwareInstallNotesLink']) {
  assert.match(html, new RegExp(`id="${id}"`), id);
}
assert.match(html, /id="firmwareTarget"[\s\S]*?value="merged"/);
assert.match(html, /在线完整安装/);
assert.doesNotMatch(html, /<summary>高级固件烧录/);
assert.match(html, /firmwareInstallNotes/);
assert.match(html, /id="hostVersion">v1\.0\.13/);
assert.match(app, /const HOST_WEB_VERSION = "v1.0.13"/);
assert.match(html, /firmware-install-layout/);
assert.match(html, /firmware-notes-panel/);
assert.match(app, /firmwareInstallBusy/);
assert.match(app, /firmwareChipVerified/);
assert.match(app, /firmwareFlashSizeBytes/);
assert.match(app, /firmwareInstallSnapshot/);
assert.match(app, /firmwareInstallConfirmBtn/);
assert.match(app, /getFlashSize/);
assert.match(app, /!firmwareChipVerified \|\| !firmwareFlashSizeBytes/);
assert.match(app, /eraseAll: false/);
assert.doesNotMatch(app, /eraseAll:\s*true/);
assert.match(app, /return true;/);
assert.match(app, /return false;/);
assert.match(app, /summarizeFirmwareNotes/);
assert.match(app, /formatFirmwareNotes/);
assert.match(app, /releaseUrl/);
// Firmware locale selection: zh-TW is the base image; chosen locale wins, then the UI language, then zh-TW.
assert.deepEqual(firmwareLocales, ['zh-TW', 'zh-CN', 'en', 'ja']);
const all = ['zh-TW', 'zh-CN', 'en', 'ja'];
assert.equal(pickFirmwareLocale(all, undefined, 'ja'), 'ja');
assert.equal(pickFirmwareLocale(all, 'en', 'ja'), 'en');
assert.equal(pickFirmwareLocale(['zh-TW', 'en'], 'ja', 'en'), 'en');
assert.equal(pickFirmwareLocale(['zh-TW', 'en'], 'ja', 'zh-CN'), 'zh-TW');
assert.equal(pickFirmwareLocale(['zh-TW'], 'en', 'en'), 'zh-TW');
assert.equal(pickFirmwareLocale([], undefined, 'en'), 'zh-TW');
assert.match(html, /id="firmwareLocaleSelect"/);
assert.match(html, /id="firmwareInstallConfirmLocale"/);
// Old manifests without "locales" still yield a zh-TW-only pair built from top-level app/merged.
assert.match(app, /const locales = \{ "zh-TW": \{ app, merged \} \};/);
assert.match(app, /item\.locales\?\.\[locale\]/);
assert.match(app, /remoteFirmwareManifest\.locales\[currentFirmwareLocale\(\)\]\?\.\[kind\]/);
assert.match(app, /pickFirmwareLocale\(Object\.keys\(manifest\?\.locales \|\| \{\}\), firmwareLocaleChoice, getLanguage\(\)\)/);
assert.match(app, /locale: currentFirmwareLocale\(\),/);
assert.match(app, /selectedRemoteFirmwareImage\("merged"\)\?\.sha256 !== snapshot\.image\.sha256/);
console.log('Firmware install UI order, confirmation, chip/flash guards, busy state and no-erase contract passed.');
