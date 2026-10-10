# Local brain — coach, not nutrition

CALT Focus loads **Qwen2.5-1.5B-Instruct Q4_K_M** (~1.12 GB) through llama.cpp on `127.0.0.1`. It is the coach. It does not log meals or read plate photos.

Open `http://127.0.0.1:8765/assistant` (phone: `http://<PC-LAN-IP>:8765/assistant`).

## What it does

The coach reads the day from the enforcer: site block, whether apps are armed, free window, study pressure, plan, Bible, minutes, and open tasks. It talks in short sentences and pushes the next piece of work.

It can override a **situation**:

| Decision | What changes |
|----------|----------------|
| `work` | Study pressure for 10–45 minutes (incubation). Free mode does not win while that lasts. |
| `ease` | Clears that study pressure and opens a free window for 5–25 minutes. |
| `hold` | Words only. |

Arm, the site-block on/off switch, and device hosts do not move. Four situation changes per hour. A fifth comes back as words with the rules left as they were.

The model answers in a fixed JSON shape. A short check sits in front of that answer: asking for YouTube or to turn the blocks off stays `hold`, and a hospital, injury, collapse, or a long exhausted stretch becomes `ease` even if the small model hesitates. Asking to be pushed stays `work`.

## Load

1. `scripts\run\fetch_local_brain.bat`
2. Open **CALT Focus**.
3. `GET /api/brain/status` until `ready` is true, then open `/assistant`.

| Env | Default | Meaning |
|-----|---------|---------|
| `CALT_LLM_CTX` | `4096` | Context. `32768` is the model maximum and uses more RAM. |
| `CALT_LLM_THREADS` | `4` | CPU threads |
| `CALT_LLM_NGL` | `0` | GPU layers |
| `CALT_LLAMA_PORT` | `8099` | Loopback port |
| `CALT_LLM_GGUF` | Qwen Q4_K_M | Another GGUF, such as SmolLM3-3B |
| `CALT_LLAMA_SERVER` | `tools\llama\llama-server.exe` | Server binary |
| `CALT_LLM_DISABLE` | unset | `1` skips launch |

Log: `data/models/llama-server.log`. Focus stops the server on Quit.
