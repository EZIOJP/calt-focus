# Focus AI brain (LLM host)

CALT Focus does **not** put SoftLand / Arm / Gate behind an LLM. Session **Suggest** and **Jarvis** free chat call any OpenAI-compatible `/v1/chat/completions` endpoint.

**Default brain: OpenJarvis** (`jarvis serve` on port 8000).

## Recommended: OpenJarvis

```bat
REM From your OpenJarvis install (venv activated):
jarvis serve --port 8000 --model qwen3.5:4b --agent simple
```

Verified working: `/health`, `/v1/models`, `/v1/chat/completions` with local Ollama models (`qwen3.5:4b`, `qwen3.5:2b`, `qwen2.5:3b`). Cloud engines configured inside OpenJarvis (NIM / providers) appear on the same `/v1` surface.

Focus WebView is `https://calt.app`. If fetch fails to `:8000`, allow local origins (OpenJarvis/Ollama CORS) or call from the desktop shell.

In Focus → Settings → More → **Focus AI brain** / **OpenJarvis**:

| Field | Value |
|-------|--------|
| Preset | OpenJarvis |
| Base URL | `http://127.0.0.1:8000/v1` |
| Model | `qwen3.5:4b` (or pick from model chips) |
| Bearer | leave empty for local |

**Use as Focus AI brain** on the OpenJarvis card sets this automatically when the API is up.

## Direct Ollama (bypass OpenJarvis)

```bat
winget install Ollama.Ollama
ollama pull qwen2.5:3b
setx OLLAMA_ORIGINS "https://calt.app,http://localhost,http://127.0.0.1,*"
```

Preset **Ollama**, base `http://127.0.0.1:11434/v1`.

## OpenRouter (cloud)

Preset **OpenRouter**, base `https://openrouter.ai/api/v1`, Bearer `sk-or-…`, model e.g. `qwen/qwen-2.5-72b-instruct`.

## Custom

Any OpenAI-compatible gateway on `/v1`. Prefs live in `localStorage` (`calt:focus-llm-host:v1`) — never in the enforcer.
