/** WebView2 → calt_focus → enforcer named pipe (serialized). */

function hasBridge() {
  return Boolean(window.chrome?.webview?.postMessage);
}

const queue = [];
let draining = false;

async function runOne(item) {
  if (!hasBridge()) {
    item.resolve(null);
    return;
  }
  const id =
    crypto.randomUUID?.() || `cmd-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const result = await new Promise((resolve) => {
    const timer = setTimeout(() => {
      cleanup();
      resolve({ ok: false, id, error: "timeout" });
    }, item.timeoutMs);
    const onMsg = (e) => {
      let data = null;
      try {
        data = typeof e.data === "string" ? JSON.parse(e.data) : e.data;
      } catch {
        return;
      }
      if (!data || data.type !== "enforcer_cmd_result") return;
      if (data.id && data.id !== id) return;
      cleanup();
      resolve(data);
    };
    const cleanup = () => {
      clearTimeout(timer);
      window.chrome.webview.removeEventListener("message", onMsg);
    };
    window.chrome.webview.addEventListener("message", onMsg);
    window.chrome.webview.postMessage({
      type: "enforcer_cmd",
      v: 1,
      id,
      op: item.op,
      payload: item.payload,
    });
  });
  item.resolve(result);
}

async function drain() {
  if (draining) return;
  draining = true;
  try {
    while (queue.length) {
      const next = queue.shift();
      await runOne(next);
    }
  } finally {
    draining = false;
    if (queue.length) void drain();
  }
}

export function enforcerCmd(op, payload = {}, timeoutMs = 2500) {
  return new Promise((resolve) => {
    queue.push({ op, payload, timeoutMs, resolve });
    void drain();
  });
}

export function bridgeAvailable() {
  return hasBridge();
}
