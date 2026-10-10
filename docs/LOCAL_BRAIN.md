# Local brain — Qwen2.5-1.5B via llama.cpp

CALT Focus can load a small GGUF on this PC. The default is **Qwen2.5-1.5B-Instruct Q4_K_M** (~1.12 GB). llama.cpp serves it on `127.0.0.1` only. The phone still talks to Focus on `:8765`; Focus asks the local server when it needs text.

This model reads text. It does not see plate photos. Photo recognize stays on Gemini.

## Load it

1. `scripts\run\fetch_local_brain.bat`
2. Open **CALT Focus**.

That script saves:

| File | Where |
|------|--------|
| `qwen2.5-1.5b-instruct-q4_k_m.gguf` | `data/models/` |
| `llama-server.exe` (llama.cpp **b10976**, Windows CPU) | `tools/llama/` |

`GET http://127.0.0.1:8765/api/brain/status` stays `ready: false` with detail `loading` until the weights are in memory, then `ready: true`. The NutriNode page adds **Qwen** next to the hub line when that happens.

## What it answers

- Foods that are not in the built-in table. `POST /api/nutrition/meals` and `POST /api/nutrition/foods/estimate` ask Qwen for per-gram macros, then keep the answer only when the numbers are in a sane range. Otherwise the meal uses the generic fallback.
- `POST /api/brain/chat` with `{"prompt":"..."}` for a short reply. Add `"json": true` when the caller wants a JSON object. Optional `system` and `max_tokens` (16–768).

## Memory

The model file supports a 32,768-token window. Focus starts it at **4096** tokens and CPU layers (`-ngl 0`) so weights plus cache stay around 2 GB. Override before launch:

| Env | Default | Meaning |
|-----|---------|---------|
| `CALT_LLM_CTX` | `4096` | Context. `32768` is the model maximum and uses more RAM. |
| `CALT_LLM_THREADS` | `4` | CPU threads |
| `CALT_LLM_NGL` | `0` | GPU layers. Raise it only with a CUDA/Vulkan `llama-server`. |
| `CALT_LLAMA_PORT` | `8099` | Loopback port |
| `CALT_LLM_GGUF` | the Qwen file above | Another GGUF, such as SmolLM3-3B |
| `CALT_LLAMA_SERVER` | `tools\llama\llama-server.exe` | Server binary |
| `CALT_LLM_DISABLE` | unset | `1` skips launch |

If `data/models` has no Qwen file, the first `*.gguf` in that folder is used.

Log: `data/models/llama-server.log`. Focus stops the server on Quit.
