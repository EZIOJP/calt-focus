import { defineConfig } from "vite";
import path from "path";
import fs from "fs";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "..");
const shellRoot = path.join(__dirname, "shell");

/** Standalone Focus — HTML shell only (no Study React src/). */
function caltDataMiddleware() {
  return {
    name: "calt-data-middleware",
    configureServer(server) {
      const serveRoot = (urlPrefix, absDir) => {
        server.middlewares.use((req, res, next) => {
          const url = (req.url || "").split("?")[0];
          if (!url.startsWith(urlPrefix)) return next();
          const rel = decodeURIComponent(url.slice(urlPrefix.length) || "");
          if (!rel || rel.includes("..")) {
            res.statusCode = 400;
            res.end("bad path");
            return;
          }
          const file = path.resolve(absDir, rel);
          if (!file.startsWith(absDir + path.sep) && file !== absDir) {
            res.statusCode = 400;
            res.end("bad path");
            return;
          }
          fs.readFile(file, (err, buf) => {
            if (err) {
              res.statusCode = 404;
              res.end("missing");
              return;
            }
            res.setHeader(
              "Content-Type",
              file.endsWith(".json") ? "application/json; charset=utf-8" : "application/octet-stream",
            );
            res.setHeader("Cache-Control", "no-store");
            res.end(buf);
          });
        });
      };
      serveRoot("/calt-data/", path.join(repoRoot, "data", "productivity", "behavior"));
      serveRoot("/calt-bible/", path.join(repoRoot, "data", "productivity", "bible"));
    },
  };
}

export default defineConfig({
  root: shellRoot,
  server: {
    host: "127.0.0.1",
    port: 5180,
    strictPort: true,
    fs: { allow: [repoRoot] },
  },
  build: {
    outDir: path.join(repoRoot, "dist-focus"),
    emptyOutDir: true,
  },
  plugins: [caltDataMiddleware()],
});
