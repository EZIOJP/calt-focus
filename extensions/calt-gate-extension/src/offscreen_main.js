/**
 * Offscreen: NSFWJS on TF.js WASM only (no WebGL).
 */
import * as tf from "@tensorflow/tfjs";
import "@tensorflow/tfjs-backend-wasm";
import { setWasmPaths, setThreadsCount } from "@tensorflow/tfjs-backend-wasm";
import * as nsfwjs from "nsfwjs";

let model = null;
let ready = false;
let lastError = "";

async function init() {
  try {
    setWasmPaths(chrome.runtime.getURL("wasm/"));
    // MV3 CSP: script-src 'self' — threaded-simd WASM creates blob: Workers and
    // importScripts(blob) → NetworkError (kills offscreen, Gate lock page flickers).
    // setThreadsCount(1) is NOT enough: TF still picks threaded-simd when SAB exists.
    // Force the non-threaded (SIMD) binary path before setBackend.
    try {
      tf.env().set("WASM_HAS_MULTITHREAD_SUPPORT", false);
    } catch (_) {
      /* older tfjs */
    }
    setThreadsCount(1);
    await tf.setBackend("wasm");
    await tf.ready();
    if (tf.getBackend() !== "wasm") {
      throw new Error("backend_not_wasm:" + tf.getBackend());
    }
    const modelUrl = chrome.runtime.getURL("nsfwjs/mobilenet_v2_mid/");
    model = await nsfwjs.load(modelUrl, { type: "graph", size: 224 });
    ready = true;
    lastError = "";
  } catch (e) {
    ready = false;
    lastError = e && e.message ? e.message : String(e);
    console.warn("CALT Gate NSFWJS init failed", lastError);
  }
}

void init();

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (!msg || msg.type !== "OFFSCREEN_NSFW_CLASSIFY") return false;
  void (async () => {
    if (!ready || !model) {
      sendResponse({ ok: false, scannerOffline: true, error: lastError || "not_ready" });
      return;
    }
    try {
      const dataUrl = msg.dataUrl;
      if (!dataUrl || String(dataUrl).indexOf("data:image") !== 0) {
        sendResponse({ ok: false, error: "no_image" });
        return;
      }
      const img = await createImageBitmap(await (await fetch(dataUrl)).blob());
      const predictions = await model.classify(img, 5);
      img.close();
      const map = {};
      for (let i = 0; i < predictions.length; i++) {
        map[predictions[i].className] = predictions[i].probability;
      }
      const porn = map.Porn || 0;
      const hentai = map.Hentai || 0;
      const sexy = map.Sexy || 0;
      sendResponse({
        ok: true,
        porn,
        hentai,
        sexy,
        pornHentai: porn + hentai,
        classes: map,
        backend: tf.getBackend(),
      });
    } catch (e) {
      sendResponse({ ok: false, scannerOffline: true, error: String(e && e.message) });
    }
  })();
  return true;
});
