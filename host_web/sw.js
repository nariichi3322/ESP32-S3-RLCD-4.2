const CACHE_PREFIX = `weather-clock-unified:${self.location.pathname}:`;
const CACHE_NAME = `${CACHE_PREFIX}v44`;
const ASSETS = [
  "./",
  "./index.html",
  "./styles.css",
  "./app.js",
  "./assets/weather_clock_main.png",
  "./assets/weather_clock_preview_sheet_1.png",
  "./firmware/manifest.example.json",
  "./vendor/esptool-js/0.5.6/bundle.js",
  "./vendor/esptool-js/0.5.6/LICENSE",
  "./vendor/esp-web-tools/10.0.1/install-button.js",
  "./vendor/esp-web-tools/10.0.1/install-dialog-BWZCBYvU.js",
  "./vendor/esp-web-tools/10.0.1/index-BbuTar3J.js",
  "./vendor/esp-web-tools/10.0.1/styles-ChWDJ3ue.js",
  "./vendor/esp-web-tools/10.0.1/rom-B2LvkjpK.js",
  "./vendor/esp-web-tools/10.0.1/esp32-D9Bry5AK.js",
  "./vendor/esp-web-tools/10.0.1/esp32c2-C0aHw_np.js",
  "./vendor/esp-web-tools/10.0.1/esp32c3-1QKN64_Z.js",
  "./vendor/esp-web-tools/10.0.1/esp32c6-CgjBrh_Q.js",
  "./vendor/esp-web-tools/10.0.1/esp32h2-Bm3EZXXU.js",
  "./vendor/esp-web-tools/10.0.1/esp32s2-DxMNCsFV.js",
  "./vendor/esp-web-tools/10.0.1/esp32s3-DkYcGTTD.js",
  "./vendor/esp-web-tools/10.0.1/esp8266-DEFNY3lv.js",
  "./vendor/esp-web-tools/10.0.1/LICENSE"
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => cache.addAll(ASSETS))
      .catch(() => undefined)
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) => Promise.all(
      keys.filter((key) => key.startsWith(CACHE_PREFIX) && key !== CACHE_NAME).map((key) => caches.delete(key))
    ))
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) return;
  if (/\/firmware\/releases(?:\/|\.json$)/.test(url.pathname)) {
    event.respondWith(fetch(event.request));
    return;
  }
  event.respondWith(
    caches.match(event.request).then((cached) => (
      cached || fetch(event.request).then((response) => {
        const copy = response.clone();
        caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
        return response;
      })
    ))
  );
});
