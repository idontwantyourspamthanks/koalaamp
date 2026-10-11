// SPDX-License-Identifier: MIT
// capture.mjs — render Webamp's main window headlessly (Chrome via
// puppeteer-core) at 1x and save reference PNGs for the FE-3 pixel diff.
//
// Webamp (MIT, https://github.com/captbaritone/webamp) draws the classic main
// window from the same skinSprite coordinate table our C++ port uses; running
// the real thing in a browser is the independent reference for the diff.
//
// Usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js]
//                          [--states a,b,c] [--media stereo.mp3] [--media-mono mono.mp3]
//   CHROME_PATH   Chrome/Chromium binary (default /usr/bin/google-chrome)
//   FFMPEG_PATH   ffmpeg binary (default /usr/bin/ffmpeg); used to synthesise
//                 the fixture media when --media/--media-mono are not given.
//
// Outputs: webamp_main_<state>.png for every captured state plus
//          reference-state.json mapping each state name to the DOM-derived
//          state object (see readState() below for every derivation). Each
//          state object carries the required className/shade/status/volume/
//          balance/positionPercent/shuffle/repeat/channels/timeSeconds/kbps/
//          khz/title/marqueeOffsetPx/working keys plus `vis`
//          ("spectrum" | "oscilloscope" | "off") for the visualiser mode.
//
// States (canonical order, `--states` defaults to all of them):
//   default, toggled, shade, shade_play, playing, paused, vol0, vol100,
//   bal_left, bal_right, pos_half, pos_max, playing_mono, vis_off, scroll
// Every state is applied from a normalised baseline (no track, stopped,
// volume 78, balance 0, shuffle/repeat off, unshaded) so states are
// independent and any subset can be captured.
import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import puppeteer from "puppeteer-core";

const WEBAMP_VERSION = "2.3.1";
const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Fixed settle time (ms) waited after a state is applied and before the
// screenshot: long enough for React re-renders, media seeks and the marquee
// step to land, short enough that a playing frame does not drift far.
const SETTLE_MS = 700;
const settle = () => new Promise((r) => setTimeout(r, SETTLE_MS));

const USAGE =
  "usage: node capture.mjs <skin.wsz> <out-dir> [webamp-bundle.js] " +
  "[--states a,b,c] [--media stereo.mp3] [--media-mono mono.mp3]";

// Canonical capture order; `default`/`toggled` keep their historical frames.
const ALL_STATES = [
  "default", "toggled", "shade", "shade_play", "playing", "paused",
  "vol0", "vol100", "bal_left", "bal_right", "pos_half", "pos_max",
  "playing_mono", "vis_off", "scroll",
];
// States whose recipe needs a decodable mp3 with real bitrate/sampleRate/channels.
const NEEDS_MEDIA = new Set(["playing", "paused", "pos_half", "pos_max", "playing_mono", "vis_off", "scroll"]);

// ---------------------------------------------------------------------------
// CLI: the first three positional arguments keep their historical meaning.
// ---------------------------------------------------------------------------
const positional = [];
const opts = { states: null, media: null, mediaMono: null };
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  const take = (prefix) => (a === prefix ? argv[++i] : a.startsWith(prefix + "=") ? a.slice(prefix.length + 1) : null);
  let v;
  if ((v = take("--states")) !== null) opts.states = v;
  else if ((v = take("--media-mono")) !== null) opts.mediaMono = v;
  else if ((v = take("--media")) !== null) opts.media = v;
  else if (a === "--help" || a === "-h") { console.log(USAGE); process.exit(0); }
  else positional.push(a);
}

const skinPath = positional[0];
const outDir = positional[1] || ".";
const bundlePath = positional[2] || path.resolve(__dirname, "../../../third_party/webamp/webamp.bundle.min.js");

if (!skinPath) {
  console.error(USAGE);
  process.exit(2);
}

let stateNames = opts.states ? opts.states.split(",").map((s) => s.trim()).filter(Boolean) : ALL_STATES;
const unknown = stateNames.filter((s) => !ALL_STATES.includes(s));
if (unknown.length > 0 || stateNames.length === 0) {
  console.error(`unknown state(s): ${unknown.join(", ") || "(none given)"}\nknown: ${ALL_STATES.join(", ")}`);
  process.exit(2);
}
// De-duplicate while preserving the requested order.
stateNames = [...new Set(stateNames)];

const chromePath = process.env.CHROME_PATH || "/usr/bin/google-chrome";
const ffmpegPath = process.env.FFMPEG_PATH || "/usr/bin/ffmpeg";

// Ensure the pinned Webamp bundle is available (cached outside the repo).
if (!fs.existsSync(bundlePath)) {
  fs.mkdirSync(path.dirname(bundlePath), { recursive: true });
  const url = `https://unpkg.com/webamp@${WEBAMP_VERSION}/built/webamp.bundle.min.js`;
  console.error(`fetching ${url}`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`failed to fetch webamp bundle: ${res.status}`);
  fs.writeFileSync(bundlePath, Buffer.from(await res.arrayBuffer()));
}

// ---------------------------------------------------------------------------
// Media fixtures. Silence is fine for the pixel diff, but the MPEG headers must
// be real so music-metadata can report bitrate/sample rate/channels (Webamp
// skips metadata parsing entirely when a track is given `metaData`, and would
// then fall back to its fake 192kbps/44kHz/2ch defaults). The ID3 title/artist
// are kept short so the composed marquee stays under 31 characters and never
// scrolls (offset stays 0 -> deterministic frame).
// ---------------------------------------------------------------------------
const wantsMedia = stateNames.some((s) => NEEDS_MEDIA.has(s));
let mediaDir = null;
const mediaFiles = { stereo: opts.media, mono: opts.mediaMono };
async function mediaFixtures() {
  if (!wantsMedia) return;
  if (mediaFiles.stereo && mediaFiles.mono) return;
  mediaDir = fs.mkdtempSync(path.join(os.tmpdir(), "se2-media-"));
  const gen = (out, rate, channels, kbps) => {
    if (mediaFiles[out]) return;
    const file = path.join(mediaDir, `${out}.mp3`);
    execFileSync(ffmpegPath, [
      "-hide_banner", "-loglevel", "error", "-y",
      "-f", "lavfi", "-i", `anullsrc=r=${rate}:cl=${channels}`,
      "-t", "60", "-c:a", "libmp3lame", "-b:a", kbps,
      "-metadata", "title=Hi", "-metadata", "artist=X",
      "-id3v2_version", "3", file,
    ]);
    mediaFiles[out] = file;
  };
  // stereo: 128kbps / 44100Hz / 2ch / 60s   mono: 64kbps / 22050Hz / 1ch / 60s
  gen("stereo", 44100, "stereo", "128k");
  gen("mono", 22050, "mono", "64k");
  console.log(`generated fixture media in ${mediaDir}`);
}
await mediaFixtures();
// Fixed URLs keep the page-side code independent of the caller's file names.
const mediaRoutes = new Map();
if (wantsMedia) {
  mediaRoutes.set("/media/stereo.mp3", fs.readFileSync(mediaFiles.stereo));
  mediaRoutes.set("/media/mono.mp3", fs.readFileSync(mediaFiles.mono));
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

// Tiny static server for the harness page, the bundle, the skin and the media.
// Media is served with Range support so the audio element can seek (needed by
// pos_half/pos_max); the other routes are byte-for-byte as before.
const skinBytes = fs.readFileSync(skinPath);
const bundleBytes = fs.readFileSync(bundlePath);
function serveMedia(req, res, buf) {
  const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || "");
  if (range) {
    const start = range[1] ? Number(range[1]) : 0;
    const end = range[2] ? Number(range[2]) : buf.length - 1;
    res.writeHead(206, {
      "content-type": "audio/mpeg", "accept-ranges": "bytes",
      "content-range": `bytes ${start}-${end}/${buf.length}`,
      "content-length": end - start + 1,
    });
    res.end(buf.subarray(start, end + 1));
  } else {
    res.writeHead(200, { "content-type": "audio/mpeg", "accept-ranges": "bytes", "content-length": buf.length });
    res.end(buf);
  }
}
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
  } else if (mediaRoutes.has(req.url)) {
    serveMedia(req, res, mediaRoutes.get(req.url));
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
  args: ["--no-sandbox", "--disable-gpu", "--force-device-scale-factor=1", "--hide-scrollbars",
         "--autoplay-policy=no-user-gesture-required"],
});

const states = {};
const saved = [];
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1200, height: 800, deviceScaleFactor: 1 });
  await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: "networkidle0", timeout: 30000 });
  await page.waitForFunction("window.__ready === true || window.__err !== null", { timeout: 30000 });
  const err = await page.evaluate(() => window.__err);
  if (err) throw new Error(`Webamp failed to render: ${err}`);

  // -------------------------------------------------------------------------
  // State derivation — every field is read from the live DOM.
  // -------------------------------------------------------------------------
  function readState() {
    const mw = document.getElementById("main-window");
    const q = (sel) => document.querySelector(sel);
    // className verbatim so callers can re-check any of the flags themselves.
    const className = mw.className;
    const windowClasses = className.split(/\s+/);
    // `#main-window` carries `play`/`pause`/`stop` (Webamp's media status) and
    // `shade` (windowshade mode); both come straight off the class list.
    const status = windowClasses.includes("play") ? "playing"
      : windowClasses.includes("pause") ? "paused"
      : "stopped";
    const shade = windowClasses.includes("shade");
    // Sliders are range inputs driven by React state; their `value` is the
    // authoritative rendered position.
    const volume = Number(q("#volume input").value);
    const balance = Number(q("#balance").value);
    const positionPercent = Number(q("#position").value);
    // Shuffle/repeat buttons get Webamp's `selected` class when engaged.
    const shuffle = q("#shuffle").classList.contains("selected");
    const repeat = q("#repeat").classList.contains("selected");
    // Channel indicator: `#mono`/`#stereo` are the two sprite halves; the one
    // Webamp marks `selected` is the parsed channel count (0 = none parsed).
    const channels = q("#mono").classList.contains("selected") ? 1
      : q("#stereo").classList.contains("selected") ? 2 : 0;
    // The time display is four `.digit digit-N` divs, in MM:SS order
    // (minute-first, minute-second, second-first, second-second). It has no
    // layout box (display:none / inside a hidden ancestor, e.g. windowshade)
    // whenever Webamp hides it -> -1.
    let timeSeconds = -1;
    const digitEls = [...document.querySelectorAll("#time .digit")];
    const timeBox = q("#time") ? q("#time").getBoundingClientRect() : null;
    if (digitEls.length === 4 && timeBox && timeBox.width > 0) {
      const digits = digitEls.map((el) => {
        const m = /digit-(\d)/.exec(el.className);
        return m ? Number(m[1]) : null;
      });
      if (digits.every((d) => d !== null)) {
        timeSeconds = (digits[0] * 10 + digits[1]) * 60 + digits[2] * 10 + digits[3];
      }
    }
    // Bitrate/sample-rate readouts: the drawn character text of `#kbps`/`#khz`
    // ("" while no track is loaded, e.g. in the default frame).
    const kbps = q("#kbps") ? q("#kbps").textContent : "";
    const khz = q("#khz") ? q("#khz").textContent : "";
    // Marquee title: concatenation of the marquee's `.character` text nodes.
    const title = [...document.querySelectorAll("#marquee .character")].map((c) => c.textContent).join("");
    // Marquee offset: the inline `transform: translateX(-Npx)` of the inner
    // scroller div, reported as the non-negative scroll distance in px.
    const inner = q("#marquee") ? q("#marquee").querySelector("div") : null;
    const m = inner ? /translateX\((-?[\d.]+)px\)/.exec(inner.getAttribute("style") || "") : null;
    const marqueeOffsetPx = m ? Math.max(0, Math.round(Math.abs(Number(m[1])))) : 0;
    // `working` is the disk/work LED: lit (`selected`) while Webamp is buffering.
    const working = q("#work-indicator") ? q("#work-indicator").classList.contains("selected") : false;
    // Visualiser mode. Webamp only renders the `#visualizer` canvas while the
    // status is not STOPPED, so with no track there is no canvas at all and
    // nothing is drawn in the vis area ("off"). When the canvas exists, what it
    // paints is decided by Webamp's own style index (display.visualizerStyle:
    // 0 = BAR spectrum, 1 = OSCILLOSCOPE, 2 = NONE/off); the canvas element
    // carries no class or attribute for it and BAR vs OSCILLOSCOPE differ only
    // in pixels, so the index is read from the live state the canvas renderer
    // itself branches on. NONE really is blank: the canvas repaint clears it to
    // fully transparent (all 76x16 alpha 0) and the MAIN.BMP art behind shows.
    const visCanvas = q("#visualizer");
    const visStyle = visCanvas
      ? window.__webamp.store.getState().display.visualizerStyle
      : null;
    const vis = visStyle === null || visStyle === 2 ? "off"
      : visStyle === 1 ? "oscilloscope" : "spectrum";
    return { className, shade, status, volume, balance, positionPercent, shuffle, repeat, channels,
      timeSeconds, kbps, khz, title, marqueeOffsetPx, working, vis };
  }

  async function capture(name) {
    const main = await page.$("#main-window");
    const file = `webamp_main_${name}.png`;
    await main.screenshot({ path: path.join(outDir, file) });
    states[name] = await page.evaluate(readState);
    saved.push(file);
    console.log(`captured ${file}`);
  }

  // Push a range input through React's value tracker: assigning `.value`
  // directly leaves the tracker's snapshot untouched, so React drops the
  // synthetic change. Using the prototype setter + native input/change events
  // is the "set .value then dispatch input/change" idiom in its reliable form.
  function setRangeValue(sel, value, extraEvents) {
    return page.evaluate((s, v, extras) => {
      const el = document.querySelector(s);
      const setter = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(el), "value").set;
      setter.call(el, String(v));
      el.dispatchEvent(new Event("input", { bubbles: true }));
      el.dispatchEvent(new Event("change", { bubbles: true }));
      for (const type of extras) el.dispatchEvent(new globalThis[type[0]](type[1], { bubbles: true }));
    }, sel, value, extraEvents || []);
  }

  // Webamp leaves transient, purely cosmetic classes behind that no state wants
  // to keep: `clicked` on the title-bar buttons (it is React state set on
  // pointerdown for the button's normal sprite, and clicking `#shade` leaves it
  // set, so every later frame draws the shade button on top of the title bar -
  // 60px at 254,3 that a fresh mount does not have) and `winamp-active` on
  // transport buttons (stuck if the pointerup that clears it was missed).
  // Stripping them restores the byte-identical fresh-mount title bar.
  async function clearTransientClasses() {
    await page.evaluate(() => {
      document.querySelectorAll("#title-bar #option, #title-bar #minimize, #title-bar #shade, #title-bar #close")
        .forEach((el) => el.classList.remove("clicked", "selected"));
      document.querySelectorAll(".actions div, #eject").forEach((el) => el.classList.remove("winamp-active"));
      document.querySelectorAll("#shuffle, #repeat").forEach((el) => el.classList.remove("selected"));
    });
  }

  // Baseline shared by every recipe: return to the pristine startup frame.
  async function normalizeStopped() {
    const shaded = await page.evaluate(() => document.getElementById("main-window").classList.contains("shade"));
    if (shaded) await page.click("#shade");
    await page.evaluate(() => {
      const w = window.__webamp;
      w.setTracksToPlay([]);           // STOP + remove every track
      w.setVolume(78);                 // startup defaults (see `default` frame)
      w.store.dispatch({ type: "SET_BALANCE", balance: 0 }); // no setBalance() on the facade
      if (w.isShuffleEnabled()) w.toggleShuffle();
      if (w.isRepeatEnabled()) w.toggleRepeat();
      // Webamp's visualiser style is sticky across tracks, so without this a
      // state could inherit `vis_off`'s NONE when captured together with it but
      // BAR when captured alone. Walk the real cycle action back to BAR.
      while (w.store.getState().display.visualizerStyle !== 0) {
        w.store.dispatch({ type: "TOGGLE_VISUALIZER_STYLE" });
      }
    });
    // Clear stale cosmetic classes after the store reset, so React has already
    // re-rendered from the (now off) shuffle/repeat state and cannot re-add them.
    await clearTransientClasses();
    await settle();
  }

  async function setVolume(v) {
    await setRangeValue("#volume input", v);
    await settle();
    if (await page.$eval("#volume input", (el, want) => Number(el.value) === want, v)) return;
    // Fallback: Webamp's volume slider is onChange-bound and React's value
    // tracker can swallow a synthetic change; the facade has setVolume().
    console.log(`  note: #volume did not follow the synthetic event, using setVolume(${v})`);
    await page.evaluate((val) => window.__webamp.setVolume(val), v);
    await settle();
  }

  async function setBalance(v) {
    await setRangeValue("#balance", v);
    await settle();
    if (await page.$eval("#balance", (el, want) => Number(el.value) === want, v)) return;
    // Fallback: Webamp 2.3.1 exposes no setBalance(); dispatch the same
    // SET_BALANCE action its balance slider emits (clamped to [-100,100], with
    // |balance| < 25 snapped to 0 by the action creator).
    console.log(`  note: #balance did not follow the synthetic event, dispatching SET_BALANCE ${v}`);
    await page.evaluate((val) => window.__webamp.store.dispatch({ type: "SET_BALANCE", balance: val }), v);
    await settle();
  }

  // Seek by dragging `#position` to `percent`. `input` only moves the scrub
  // handle (SET_FOCUS + SET_SCRUB_POSITION); the seek itself happens on
  // `pointerup`, which also clears the focus so the marquee keeps showing the
  // track title instead of the transient "Seek to: ..." message.
  async function seekToPercent(percent) {
    await setRangeValue("#position", percent, [["PointerEvent", "pointerup"]]);
    await page.waitForFunction(
      (want) => Number(document.querySelector("#position").value) >= want - 3,
      { timeout: 5000 }, percent).catch(() => {});
  }

  // Load a track and wait for playback plus parsed media tags, so bitrate,
  // sample rate, channels and the marquee duration are all present.
  async function playTrack(url, metaData) {
    await page.evaluate(async (u, md) => {
      await window.__webamp.stop();
      window.__webamp.setTracksToPlay([md ? { url: u, metaData: md } : { url: u }]);
      await window.__webamp.play();
    }, url, metaData || null);
    await page.waitForFunction(() => {
      const st = window.__webamp.store.getState();
      return st.media.status === "PLAYING"
        && document.querySelector("#position").value !== "0"
        && (document.querySelector("#kbps").textContent || "").trim() !== "";
    }, { timeout: 15000 }).catch(() => {});
  }

  // Press and hold a transport button so the screenshot catches its held frame.
  async function captureHeld(sel, name) {
    const box = await (await page.$(sel)).boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await settle();
    await capture(name);
    await page.mouse.up();
  }

  const stereoUrl = "/media/stereo.mp3";
  const monoUrl = "/media/mono.mp3";
  // >= 31 chars -> Webamp composes "<title>  ***  <title>" and animates the
  // marquee, so this one state is intentionally not reproducible.
  const SCROLL_META = { title: "A Very Long Track Title Indeed", artist: "Some Artist" };

  const recipes = {
    // No-track startup state: stopped, volume 78, balance 0.
    async default() {
      await settle();
      await capture("default");
    },
    // shuffle + repeat selected, play button held down (historical frame).
    async toggled() {
      await normalizeStopped();
      await page.click("#shuffle");
      await page.click("#repeat");
      await captureHeld(".actions #play", "toggled");
    },
    // Windowshade mode, stopped.
    async shade() {
      await normalizeStopped();
      await page.click("#shade");
      await settle();
      await capture("shade");
    },
    // Windowshade mode with the (mini) play button held down. Observation: the
    // windowshade title bar lays the transport row out over the bar
    // (`#play` sits at 176,2, 10x10), but Webamp's shade CSS paints nothing for
    // it (`.shade .actions #play, .shade .actions #play.winamp-active
    // { background: none }`), so the held frame is byte-identical to `shade`.
    async shade_play() {
      await normalizeStopped();
      await page.click("#shade");
      await settle();
      await captureHeld("#play", "shade_play");
    },
    async playing() {
      await normalizeStopped();
      await playTrack(stereoUrl);
      await settle();
      await capture("playing");
    },
    async paused() {
      await normalizeStopped();
      await playTrack(stereoUrl);
      await page.evaluate(() => window.__webamp.pause());
      await settle();
      await capture("paused");
    },
    async vol0() {
      await normalizeStopped();
      await setVolume(0);
      await settle();
      await capture("vol0");
    },
    async vol100() {
      await normalizeStopped();
      await setVolume(100);
      await settle();
      await capture("vol100");
    },
    async bal_left() {
      await normalizeStopped();
      await setBalance(-100);
      await settle();
      await capture("bal_left");
    },
    async bal_right() {
      await normalizeStopped();
      await setBalance(100);
      await settle();
      await capture("bal_right");
    },
    async pos_half() {
      await normalizeStopped();
      await playTrack(stereoUrl);
      await seekToPercent(50);
      await settle();
      await capture("pos_half");
    },
    // Playing with the position slider dragged to 100%. Observation: a seek to
    // the very end makes Webamp's `ended` handler stop the transport (the store
    // goes ENDED and the window renders the *stopped* frame: no time, no
    // kbps/khz, no position thumb - the position slider only paints its thumb in
    // play/pause mode), i.e. the requested "playing at 100%" frame is never on
    // screen. To capture it we park the transport at the end of the track: pause
    // the <audio> element Webamp decodes into (a bare pause - Webamp only mirrors
    // the play/pause/stop it issues itself, so the window keeps its `play` class
    // and the still-playing UI state) and then run the real seek through the
    // slider, which leaves the slider at 100 and the time display at 1:00.
    async pos_max() {
      await normalizeStopped();
      await playTrack(stereoUrl);
      await page.evaluate(() => {
        const audio = window.__webamp.media._source._audio; // not attached to the document
        if (audio) audio.pause();
      });
      await seekToPercent(100);
      await settle();
      await capture("pos_max");
    },
    async playing_mono() {
      await normalizeStopped();
      await playTrack(monoUrl);
      await settle();
      await capture("playing_mono");
    },
    // Cycle the main-window visualizer style so the spectrum is off.
    // Observed: Webamp cycles BAR -> OSCILLOSCOPE -> NONE, so two clicks on the
    // `#visualizer` canvas reach NONE (store display.visualizerStyle === 2).
    // In NONE the canvas repaints fully transparent (confirmed below from its
    // pixels: 0 non-transparent of 76x16), so the vis area shows the MAIN.BMP
    // art behind it - Webamp blanks it on its own, nothing extra to do. The
    // canvas only exists while the status is not STOPPED, i.e. this state has
    // to be captured while a track is playing.
    async vis_off() {
      await normalizeStopped();
      await playTrack(stereoUrl);
      await page.click("#visualizer");
      await settle();
      await page.click("#visualizer");
      const visState = await page.evaluate(() => {
        const cv = document.querySelector("#visualizer");
        const style = window.__webamp.store.getState().display.visualizerStyle;
        if (!cv) return { style, size: null, opaque: null };
        const d = cv.getContext("2d").getImageData(0, 0, cv.width, cv.height).data;
        let opaque = 0;
        for (let i = 3; i < d.length; i += 4) if (d[i]) opaque++;
        return { style, size: `${cv.width}x${cv.height}`, opaque };
      });
      console.log(`vis_off: visualizerStyle=${visState.style} (0=BAR, 1=OSCILLOSCOPE, 2=NONE); ` +
        `canvas ${visState.size} non-transparent px=${visState.opaque}`);
      await settle();
      await capture("vis_off");
    },
    // Long title: the marquee scrolls, so both the frame and marqueeOffsetPx
    // are expected to vary between runs. The long title is injected through
    // `metaData`, which makes Webamp skip tag parsing, so the kbps/khz/channels
    // readouts in this state's json come from its fallback defaults
    // (192kbps/44kHz/2ch) - only the marquee is interesting here.
    async scroll() {
      await normalizeStopped();
      await playTrack(stereoUrl, SCROLL_META);
      await settle();
      await capture("scroll");
    },
  };

  for (const name of stateNames) {
    console.log(`--- state: ${name}`);
    await recipes[name]();
  }

  fs.writeFileSync(path.join(outDir, "reference-state.json"), JSON.stringify(states, null, 2));
  console.log(JSON.stringify(states, null, 2));
  console.log(`saved ${saved.length} PNG(s) + reference-state.json in ${outDir}: ${saved.join(", ")}`);
} finally {
  await browser.close();
  server.close();
  if (mediaDir) fs.rmSync(mediaDir, { recursive: true, force: true });
}
