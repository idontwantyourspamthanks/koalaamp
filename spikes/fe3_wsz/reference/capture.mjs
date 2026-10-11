// SPDX-License-Identifier: MIT
// capture.mjs — render Webamp's main window headlessly (Chrome via
// puppeteer-core) at 1x and save reference PNGs for the FE-3 pixel diff.
//
// Webamp (MIT, https://github.com/captbaritone/webamp) draws the classic main
// window from the same skinSprite coordinate table our C++ port uses; running
// the real thing in a browser is the independent reference for the diff.
//
// Usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js]
//   CHROME_PATH   Chrome/Chromium binary (default /usr/bin/google-chrome)
//
// Outputs: webamp_main_default.png (no-track startup state) and
//          webamp_main_toggled.png (shuffle+repeat selected, play held down),
//          plus reference-state.json describing the applied state.
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import puppeteer from "puppeteer-core";

const WEBAMP_VERSION = "2.3.1";
const __dirname = path.dirname(fileURLToPath(import.meta.url));

const skinPath = process.argv[2];
const outDir = process.argv[3] || ".";
const bundlePath = process.argv[4] || path.resolve(__dirname, "../../../third_party/webamp/webamp.bundle.min.js");

if (!skinPath) {
  console.error("usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js]");
  process.exit(2);
}
const chromePath = process.env.CHROME_PATH || "/usr/bin/google-chrome";

// Ensure the pinned Webamp bundle is available (cached outside the repo).
if (!fs.existsSync(bundlePath)) {
  fs.mkdirSync(path.dirname(bundlePath), { recursive: true });
  const url = `https://unpkg.com/webamp@${WEBAMP_VERSION}/built/webamp.bundle.min.js`;
  console.error(`fetching ${url}`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`failed to fetch webamp bundle: ${res.status}`);
  fs.writeFileSync(bundlePath, Buffer.from(await res.arrayBuffer()));
}

const indexHtml = `<!doctype html><html><head><meta charset="utf-8">
<style>body{margin:0;background:#000}</style></head><body><div id="app"></div>
<script src="/webamp.bundle.min.js"></script>
<script>
window.__ready=false; window.__err=null;
try {
  window.__webamp = new Webamp({
    initialSkin: { url: '/skin.wsz' },
    availableSkins: [],
    zIndex: 0,
  });
  window.__webamp.renderWhenReady(document.getElementById('app'))
    .then(() => { window.__ready = true; })
    .catch(e => { window.__err = String(e); });
} catch (e) { window.__err = String(e); }
</script></body></html>`;

// Tiny static server for the harness page, the bundle and the skin.
const skinBytes = fs.readFileSync(skinPath);
const bundleBytes = fs.readFileSync(bundlePath);
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
  executablePath: chromePath,
  headless: "new",
  args: ["--no-sandbox", "--disable-gpu", "--force-device-scale-factor=1", "--hide-scrollbars"],
});
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1200, height: 800, deviceScaleFactor: 1 });
  await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle0", timeout: 30000 });
  await page.waitForFunction("window.__ready === true || window.__err !== null", { timeout: 30000 });
  const err = await page.evaluate(() => window.__err);
  if (err) throw new Error(`Webamp failed to render: ${err}`);

  const state = {};

  // --- default startup state (no tracks): stopped, volume 78, balance 0 ----
  await new Promise((r) => setTimeout(r, 1500));
  const main = await page.$("#main-window");
  await main.screenshot({ path: path.join(outDir, "webamp_main_default.png") });
  state.default = await page.evaluate(() => ({
    class: document.getElementById("main-window").className,
    volume: document.querySelector("#volume input").value,
    balance: document.getElementById("balance").value,
  }));

  // --- toggled state: shuffle + repeat selected, play button held down ------
  await page.click("#shuffle");
  await page.click("#repeat");
  const play = await page.$(".actions #play");
  const box = await play.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await new Promise((r) => setTimeout(r, 300));
  await main.screenshot({ path: path.join(outDir, "webamp_main_toggled.png") });
  state.toggled = await page.evaluate(() => ({
    shuffle: document.getElementById("shuffle").className,
    repeat: document.getElementById("repeat").className,
    play: document.querySelector(".actions #play").className,
  }));
  await page.mouse.up();

  fs.writeFileSync(path.join(outDir, "reference-state.json"), JSON.stringify(state, null, 2));
  console.log(JSON.stringify(state, null, 2));
  console.log(`saved webamp_main_default.png + webamp_main_toggled.png in ${outDir}`);
} finally {
  await browser.close();
  server.close();
}
