// SPDX-License-Identifier: MIT
//
// SE-5 reference: render Webamp's equalizer window headlessly (Chrome via
// puppeteer-core) so `render-and-diff.sh` can pixel-diff it against
// `koalaamp::ui::renderEqWindow`.
//
// Webamp (MIT, https://github.com/captbaritone/webamp) draws the classic EQ
// window from the same `skinSprites` coordinate table our C++ port uses, so
// running the real thing in a browser is the independent reference. The public
// Webamp facade has no EQ API, so the sliders are driven through the store
// (`SET_BAND_VALUE`) and the window through `SET_FOCUSED_WINDOW`.
//
// Slider values are the Webamp 0-100 field; the harness sets them from AU-1's
// inverted 0-63 field with the same mapping the renderer uses
// (`round((63 - au) * 100 / 63)`), so the diff compares like with like:
// rendering-from-a-position must be byte-identical.
//
// Usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js]
//   CHROME_PATH  Chrome/Chromium binary (default /usr/bin/google-chrome)
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer from "puppeteer-core";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const skinPath = process.argv[2];
const outDir = process.argv[3] || ".";
const bundlePath =
  process.argv[4] || path.resolve(__dirname, "../../../third_party/webamp/webamp.bundle.min.js");
if (!skinPath) {
  console.error("usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js]");
  process.exit(2);
}

const BANDS = ["60", "170", "310", "600", "1000", "3000", "6000", "12000", "14000", "16000"];
// AU-1 0..63 -> Webamp 0..100 (100 = +12 dB at the top).
const v = (au) => Math.round(((63 - au) * 100) / 63);

const skinBytes = fs.readFileSync(skinPath);
const bundleBytes = fs.readFileSync(bundlePath);

const indexHtml = `<!doctype html><html><head><meta charset="utf-8">
<style>body{margin:0;background:#000}</style></head><body><div id="app"></div>
<script src="/webamp.bundle.min.js"></script>
<script>
window.__ready=false; window.__err=null;
try {
  window.__webamp = new Webamp({ initialSkin: { url: '/skin.wsz' }, availableSkins: [], zIndex: 0 });
  window.__webamp.renderWhenReady(document.getElementById('app'))
    .then(() => { window.__ready = true; })
    .catch(e => { window.__err = String(e); });
} catch (e) { window.__err = String(e); }
</script></body></html>`;

const server = http.createServer((req, res) => {
  if (req.url === "/" || req.url === "/index.html") {
    res.writeHead(200, { "content-type": "text/html" });
    res.end(indexHtml);
  } else if (req.url === "/webamp.bundle.min.js") {
    res.writeHead(200, { "content-type": "application/javascript" });
    res.end(bundleBytes);
  } else if (req.url === "/skin.wsz") {
    res.writeHead(200, { "content-type": "application/octet-stream" });
    res.end(skinBytes);
  } else {
    res.writeHead(404);
    res.end();
  }
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;
fs.mkdirSync(outDir, { recursive: true });

const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || "/usr/bin/google-chrome",
  headless: "new",
  args: ["--no-sandbox", "--disable-gpu", "--force-device-scale-factor=1", "--hide-scrollbars"],
});
const page = await browser.newPage();
await page.setViewport({ width: 1200, height: 800, deviceScaleFactor: 1 });
await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle0", timeout: 30000 });
await page.waitForFunction("window.__ready === true || window.__err !== null", { timeout: 30000 });
const bootErr = await page.evaluate(() => window.__err);
if (bootErr) throw new Error(bootErr);
const settle = (ms = 250) => new Promise((r) => setTimeout(r, ms));

async function reset() {
  await page.evaluate((bands) => {
    const w = window.__webamp;
    for (const b of bands) w.store.dispatch({ type: "SET_BAND_VALUE", band: b, value: 50 });
    w.store.dispatch({ type: "SET_BAND_VALUE", band: "preamp", value: 50 });
    w.store.dispatch({ type: "SET_EQ_OFF" });
    w.store.dispatch({ type: "SET_EQ_AUTO", value: false });
    w.store.dispatch({ type: "SET_FOCUSED_WINDOW", window: "equalizer" });
    const g = w.store.getState().windows.genWindows;
    if (!g.equalizer.open) w.store.dispatch({ type: "TOGGLE_WINDOW", windowId: "equalizer" });
    if (g.equalizer.shade) w.store.dispatch({ type: "TOGGLE_WINDOW_SHADE_MODE", windowId: "equalizer" });
    if (g.equalizer.shade) w.store.dispatch({ type: "TOGGLE_WINDOW_SHADE_MODE", windowId: "equalizer" });
  }, BANDS);
  await settle();
}

async function setSlider(band, au) {
  await page.evaluate(
    (b, val) => window.__webamp.store.dispatch({ type: "SET_BAND_VALUE", band: b, value: val }),
    band,
    v(au)
  );
}

async function capture(name) {
  await settle();
  const eq = await page.$("#equalizer-window");
  await eq.screenshot({ path: path.join(outDir, `webamp_${name}.png`) });
  console.log(`captured ${name}`);
}

async function shade(on) {
  await page.evaluate((want) => {
    const w = window.__webamp;
    const cur = w.store.getState().windows.genWindows.equalizer.shade;
    if (cur !== want) w.store.dispatch({ type: "TOGGLE_WINDOW_SHADE_MODE", windowId: "equalizer" });
  }, on);
  await settle();
}

const recipes = {
  // All sliders at AU 31 (0 dB), EQ off; EQ window focused (selected title art).
  async eq_default() {
    await reset();
    await page.evaluate(() => window.__webamp.store.dispatch({ type: "SET_EQ_OFF" }));
    for (const b of BANDS) await setSlider(b, 31);
    await setSlider("preamp", 31);
    await capture("eq_default");
  },
  async eq_on() {
    await reset();
    await page.evaluate(() => window.__webamp.store.dispatch({ type: "SET_EQ_ON" }));
    for (const b of BANDS) await setSlider(b, 31);
    await setSlider("preamp", 31);
    await capture("eq_on");
  },
  async eq_bands_maxboost() {
    await reset();
    for (const b of BANDS) await setSlider(b, 0);
    await setSlider("preamp", 31);
    await capture("eq_bands_maxboost");
  },
  async eq_bands_maxcut() {
    await reset();
    for (const b of BANDS) await setSlider(b, 63);
    await setSlider("preamp", 31);
    await capture("eq_bands_maxcut");
  },
  async eq_preamp_maxboost() {
    await reset();
    for (const b of BANDS) await setSlider(b, 31);
    await setSlider("preamp", 0);
    await capture("eq_preamp_maxboost");
  },
  // Windowshade, unselected (the EQ window loses focus to main).
  async eq_shade() {
    await reset();
    await page.evaluate(() =>
      window.__webamp.store.dispatch({ type: "SET_FOCUSED_WINDOW", window: "equalizer" })
    );
    await shade(true);
    await capture("eq_shade");
  },
  async eq_shade_extremes() {
    await reset();
    await page.evaluate(() => {
      const w = window.__webamp;
      w.setVolume(100);
      w.store.dispatch({ type: "SET_BALANCE", balance: -100 });
      w.store.dispatch({ type: "SET_FOCUSED_WINDOW", window: "equalizer" });
    });
    await shade(true);
    await capture("eq_shade_extremes");
  },
};

for (const name of Object.keys(recipes)) {
  console.log(`--- ${name}`);
  await recipes[name]();
}

await browser.close();
server.close();
console.log(`saved EQ reference PNGs in ${outDir}`);
