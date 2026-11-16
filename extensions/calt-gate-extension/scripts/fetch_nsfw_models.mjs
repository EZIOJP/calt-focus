/**
 * Download NSFWJS MobileNetV2Mid shards + TF.js WASM binaries into the extension.
 */
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import https from "https";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");

function get(url) {
  return new Promise((resolve, reject) => {
    https
      .get(url, (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          get(res.headers.location).then(resolve, reject);
          return;
        }
        if (res.statusCode !== 200) {
          reject(new Error(url + " -> " + res.statusCode));
          return;
        }
        const chunks = [];
        res.on("data", (c) => chunks.push(c));
        res.on("end", () => resolve(Buffer.concat(chunks)));
      })
      .on("error", reject);
  });
}

async function download(url, dest) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  const buf = await get(url);
  fs.writeFileSync(dest, buf);
  console.log("OK", dest, buf.length);
}

const midBase =
  "https://raw.githubusercontent.com/infinitered/nsfwjs/master/models/mobilenet_v2_mid/";
const midDir = path.join(root, "nsfwjs/mobilenet_v2_mid");
await download(midBase + "model.json", path.join(midDir, "model.json"));
await download(midBase + "group1-shard1of2", path.join(midDir, "group1-shard1of2"));
await download(midBase + "group1-shard2of2", path.join(midDir, "group1-shard2of2"));

// Copy wasm from node_modules after npm install
const wasmSrc = path.join(root, "node_modules/@tensorflow/tfjs-backend-wasm/dist");
const wasmDest = path.join(root, "wasm");
fs.mkdirSync(wasmDest, { recursive: true });
if (fs.existsSync(wasmSrc)) {
  for (const f of fs.readdirSync(wasmSrc)) {
    if (f.endsWith(".wasm") || f.endsWith(".js")) {
      fs.copyFileSync(path.join(wasmSrc, f), path.join(wasmDest, f));
      console.log("copied wasm", f);
    }
  }
} else {
  console.warn("Run npm install before fetch-models to copy wasm binaries");
}
