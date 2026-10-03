# Focus frontend

**Primary UI:** [`shell/`](shell/) — plain HTML/CSS/JS (hybrid design). No Study `src/`, no React required to run Home/morning.

Serve / ship `shell/` as `dist-focus` for WebView2 (`calt.app`).

The older Vite + React entry under this folder expected shared Study `src/` via `@` — that path is **removed** in the standalone repo. Do not reintroduce Study monorepo aliases.
