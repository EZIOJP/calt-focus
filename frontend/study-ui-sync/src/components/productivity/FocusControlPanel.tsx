/**
 * Web Focus — control UI. Native calt_enforcer owns kills (zero-Python tracker).
 * SoftLand / Arm writes prefer Focus→enforcer named-pipe gateway; HTTP is fallback.
 */
import { useCallback, useEffect, useState, type ReactNode } from "react";
import { Link } from "react-router";
import {
  fetchFocusDashboard,
  postFocusFreeOverride,
  postFocusSpendEarned,
  putEnforcerPolicy,
  type FocusDashboardSnapshot,
} from "../../api/behaviorClient";
import {
  fetchEnforcerStatusMirror,
  type EnforcerStatusMirror,
} from "../../api/focusMirrors";
import { enforcerNativeCmd, isFocusEnforcerBridgeAvailable } from "../../lib/enforcerNativeCmd";
import { invalidateFocusStatusBus } from "../../lib/focusStatusBus";
import { useFocusStatus } from "../../hooks/useFocusStatus";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";
import { focusDataUrl } from "../../utils/focusDataUrl";

type OfflineSoftLandWhy = {
  why: string;
  untilLabel: string;
  softlandOn: boolean;
  asOf: string;
};

/** Phase 1: read softland_policy.json via Focus shell virtual host (no :8000). */
async function loadOfflineSoftLandWhy(): Promise<OfflineSoftLandWhy | null> {
  if (!isFocusDesktopShell()) return null;
  try {
    const r = await fetch(focusDataUrl("softland_policy.json"), { cache: "no-store" });
    if (!r.ok) return null;
    const j = (await r.json()) as {
      softland_enabled?: boolean;
      updated_at?: string;
      runtime?: {
        free_until?: string | null;
        incubation_until?: string | null;
      };
    };
    const softlandOn = Boolean(j.softland_enabled);
    const incub = j.runtime?.incubation_until || "";
    const free = j.runtime?.free_until || "";
    let why = softlandOn
      ? "SoftLand on (offline snapshot from softland_policy.json)."
      : "SoftLand off (offline snapshot).";
    let untilLabel = "";
    if (incub) {
      why = "Incubation active (offline). Entertainment stays blocked until the clock below.";
      untilLabel = incub;
    } else if (free) {
      why = "Free window on file (offline). Spend earned minutes from Unlock → Now.";
      untilLabel = free;
    }
    return {
      why,
      untilLabel,
      softlandOn,
      asOf: j.updated_at || new Date().toISOString(),
    };
  } catch {
    return null;
  }
}

function SectionBar({ title, children }: { title: string; children: ReactNode }) {
  return (
    <section className="space-y-3">
      <h3 className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground border-b border-border/60 pb-1.5">
        {title}
      </h3>
      {children}
    </section>
  );
}

const STATUS_FRESH_S = 15;

export function FocusControlPanel() {
  const [snap, setSnap] = useState<FocusDashboardSnapshot | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [hint, setHint] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [exes, setExes] = useState<string[]>(["notepad.exe"]);
  const [lockMode, setLockMode] = useState<"none" | "timer" | "password" | "phrase">("none");
  const [timerMinutes, setTimerMinutes] = useState(25);
  const [lockSecret, setLockSecret] = useState("");
  const [antiTamper, setAntiTamper] = useState(true);
  const [protectUninstall, setProtectUninstall] = useState(false);

  const [offlineWhy, setOfflineWhy] = useState<OfflineSoftLandWhy | null>(null);
  const [offlineEnf, setOfflineEnf] = useState<EnforcerStatusMirror | null>(null);
  const status = useFocusStatus();

  // Prefer shared bus for SoftLand/Arm chips — avoids a second pipe poller.
  useEffect(() => {
    if (!isFocusDesktopShell()) return;
    const j = status.softland;
    if (j) {
      const softlandOn = Boolean(j.softland_enabled);
      const incub = j.runtime?.incubation_until || "";
      const free = j.runtime?.free_until || "";
      let why = softlandOn
        ? "SoftLand on (status bus / softland_policy.json)."
        : "SoftLand off (status bus).";
      let untilLabel = "";
      if (incub) {
        why = "Incubation active. Entertainment stays blocked until the clock below.";
        untilLabel = incub;
      } else if (free) {
        why = "Free window on file. Spend earned minutes from Unlock → Now.";
        untilLabel = free;
      }
      setOfflineWhy({
        why,
        untilLabel,
        softlandOn,
        asOf: j.updated_at || new Date().toISOString(),
      });
    }
    if (status.enforcerStatus) setOfflineEnf(status.enforcerStatus);
  }, [status.updatedAt, status.softland, status.enforcerStatus]);

  const refresh = useCallback(async () => {
    if (typeof document !== "undefined" && document.visibilityState === "hidden") return;
    try {
      const s = await fetchFocusDashboard();
      setSnap(s);
      setErr(null);
      // Keep bus-backed offline mirrors; don't clear them on dashboard success.
      const list = s.enforcer_policy?.exes;
      if (Array.isArray(list) && list.length) {
        setExes(list.map(String));
      }
      const lm = String(s.enforcer_policy?.lock_mode || "none");
      if (lm === "timer" || lm === "password" || lm === "phrase" || lm === "none") {
        setLockMode(lm);
      }
      if (typeof s.enforcer_policy?.anti_tamper === "boolean") {
        setAntiTamper(s.enforcer_policy.anti_tamper);
      }
      if (typeof s.enforcer_policy?.protect_uninstall === "boolean") {
        setProtectUninstall(s.enforcer_policy.protect_uninstall);
      }
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
    }
  }, []);

  useEffect(() => {
    let cancelled = false;
    let inFlight = false;
    const tick = async () => {
      if (cancelled || inFlight) return;
      inFlight = true;
      try {
        await refresh();
      } finally {
        inFlight = false;
      }
    };
    void tick();
    // Dashboard is secondary; shared bus covers SoftLand/Arm freshness at 4s.
    const t = window.setInterval(() => void tick(), 30_000);
    const onVis = () => {
      if (document.visibilityState === "visible") void tick();
    };
    document.addEventListener("visibilitychange", onVis);
    return () => {
      cancelled = true;
      window.clearInterval(t);
      document.removeEventListener("visibilitychange", onVis);
    };
  }, [refresh]);

  useEffect(() => {
    if (!hint) return;
    const t = window.setTimeout(() => setHint(null), 5000);
    return () => window.clearTimeout(t);
  }, [hint]);

  const active = snap?.active;
  const earned = snap?.earned;
  const inc = snap?.incubation;
  const actions = snap?.actions;
  const enf = snap?.enforcer;
  const pol = snap?.enforcer_policy;

  const age = typeof enf?.status_age_s === "number" ? enf.status_age_s : null;
  const statusFresh =
    enf?.status_source === "enforcer_status.json" && age != null && age <= STATUS_FRESH_S;
  const enforcerHealthy = Boolean(statusFresh && (enf?.owns || enf?.lock_present));
  /** Offline mirror when dashboard API is down (Phase 5). */
  const armedShown = Boolean(
    active?.hard_block_armed || pol?.hard_block_armed || enf?.armed || offlineEnf?.armed,
  );
  const ownsShown = Boolean(enf?.owns || offlineEnf?.owns);
  const lockPresentShown = Boolean(enf?.lock_present || offlineEnf?.lock_present);

  const lockModeActive = String(pol?.lock_mode || "none").toLowerCase();
  const timerStillActive =
    lockModeActive === "timer" &&
    typeof pol?.lock_until_unix === "number" &&
    pol.lock_until_unix > Math.floor(Date.now() / 1000);
  const strongLockActive =
    Boolean(pol?.hard_block_armed) &&
    lockModeActive !== "none" &&
    (lockModeActive === "password" ||
      lockModeActive === "phrase" ||
      timerStillActive ||
      (lockModeActive === "timer" && !pol?.lock_until_unix));
  const killListLocked = strongLockActive;

  async function onFree() {
    if (!actions?.can_pin_free || actions?.incubation_blocks_pin) return;
    const pin = window.prompt("TRACKER_EXIT_PIN (or leave blank if unset)") ?? "";
    setBusy(true);
    try {
      const res = await postFocusFreeOverride(pin);
      setSnap(res.snapshot);
      setHint("Free-time override applied.");
    } catch (e) {
      window.alert(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function onSpend() {
    if (!actions?.can_spend_earned) return;
    const bal = earned?.balance_minutes ?? 0;
    const raw = window.prompt(`Spend how many earned minutes? (balance ${bal})`, String(Math.min(15, bal)));
    if (raw == null) return;
    const minutes = Math.max(1, Math.min(bal, parseInt(raw, 10) || 15));
    setBusy(true);
    try {
      if (isFocusEnforcerBridgeAvailable()) {
        const native = await enforcerNativeCmd("softland.spend_free", { minutes });
        if (native?.ok) {
          setHint(`Spent ${minutes} earned minute(s) via enforcer gateway.`);
          window.setTimeout(() => {
            invalidateFocusStatusBus({ pipe: true });
            void refresh();
          }, 400);
          return;
        }
        if (native && native.error === "insufficient_ledger") {
          throw new Error("Not enough earned minutes in native ledger.");
        }
        // fall through to HTTP if gateway rejected for other reasons
      }
      const pin = window.prompt("TRACKER_EXIT_PIN (or leave blank if unset)") ?? "";
      const res = await postFocusSpendEarned(pin, minutes);
      setSnap(res.snapshot);
      setHint(`Spent ${minutes} earned minute(s).`);
    } catch (e) {
      window.alert(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function setArmed(armed: boolean) {
    setBusy(true);
    try {
      const killList = exes.map((s) => s.trim()).filter(Boolean);
      if (armed) {
        const body: Parameters<typeof putEnforcerPolicy>[0] = {
          hard_block_armed: true,
          gate_locked: true,
          incubation_active: false,
          exes: killList,
          lock_mode: lockMode,
          anti_tamper: antiTamper,
        };
        if (lockMode === "timer") {
          body.lock_until_unix = Math.floor(Date.now() / 1000) + Math.max(1, timerMinutes) * 60;
        }
        if (lockMode === "password") body.unlock_password = lockSecret;
        if (lockMode === "phrase") body.unlock_phrase = lockSecret;

        if (isFocusEnforcerBridgeAvailable()) {
          const native = await enforcerNativeCmd("arm.set", {
            hard_block_armed: true,
            gate_locked: true,
            incubation_active: false,
            exes: killList,
            lock_mode: lockMode,
            anti_tamper: antiTamper,
            lock_until_unix: body.lock_until_unix,
            unlock_password: body.unlock_password,
            unlock_phrase: body.unlock_phrase,
          });
          if (native?.ok) {
            setHint(
              enforcerHealthy
                ? `Armed via enforcer gateway — kills: ${killList.join(", ") || "(empty list)"}`
                : `Policy written via gateway, but status looks unhealthy — check enforcer.`,
            );
            window.setTimeout(() => {
            invalidateFocusStatusBus({ pipe: true });
            void refresh();
          }, 400);
            return;
          }
        }

        const res = await putEnforcerPolicy(body);
        setSnap(res.snapshot);
        setHint(
          enforcerHealthy
            ? `Armed — native will kill: ${killList.join(", ") || "(empty list)"}`
            : `Policy written, but enforcer is not running — start console/service for kills.`,
        );
      } else {
        let provided = "";
        const mode = String(pol?.lock_mode || lockMode || "none");
        if (mode === "password" || mode === "phrase") {
          provided = window.prompt(mode === "password" ? "Unlock password" : "Unlock phrase") ?? "";
        }

        if (isFocusEnforcerBridgeAvailable()) {
          const native = await enforcerNativeCmd("arm.set", {
            hard_block_armed: false,
            gate_locked: false,
            incubation_active: false,
            exes: killList,
            // Clear strong lock on disarm so ApplyStickyLock cannot re-arm next tick.
            lock_mode: "none",
            provided_unlock: provided,
            anti_tamper: antiTamper,
          });
          if (native?.ok) {
            setHint("Disarmed via enforcer gateway.");
            window.setTimeout(() => {
            invalidateFocusStatusBus({ pipe: true });
            void refresh();
          }, 400);
            return;
          }
          if (native?.error === "unlock_failed") {
            throw new Error("Unlock password/phrase did not match.");
          }
        }

        const res = await putEnforcerPolicy({
          hard_block_armed: false,
          gate_locked: false,
          incubation_active: false,
          exes: killList,
          provided_unlock: provided,
        });
        setSnap(res.snapshot);
        setHint("Disarmed — enforcer_policy.json cleared.");
      }
      window.setTimeout(() => {
        invalidateFocusStatusBus({ pipe: true });
        void refresh();
      }, 400);
    } catch (e) {
      window.alert(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function setProtectUninstallOn(next: boolean) {
    setBusy(true);
    try {
      const killList = exes.map((s) => s.trim()).filter(Boolean);
      const armed = Boolean(pol?.hard_block_armed);
      const locked = Boolean(pol?.gate_locked);
      if (next) {
        let pwd = lockSecret.trim();
        if (!pwd && !(pol?.unlock_password || pol?.unlock_phrase)) {
          pwd =
            window.prompt(
              "Set an unlock password for protect-uninstall (same as Focus lock password)",
            ) ?? "";
        }
        if (!pwd && !(pol?.unlock_password || pol?.unlock_phrase)) {
          window.alert("Need a password or phrase before protecting uninstall.");
          return;
        }
        const body: Parameters<typeof putEnforcerPolicy>[0] = {
          hard_block_armed: armed,
          gate_locked: locked || armed,
          incubation_active: Boolean(pol?.incubation_active),
          exes: killList,
          lock_mode: (pol?.lock_mode as typeof lockMode) || lockMode,
          anti_tamper: antiTamper,
          protect_uninstall: true,
        };
        if (pwd) body.unlock_password = pwd;
        const res = await putEnforcerPolicy(body);
        setSnap(res.snapshot);
        setProtectUninstall(true);
        setHint("Uninstall protected — hidden from Apps & features until you unlock.");
      } else {
        const provided =
          window.prompt("Enter Focus unlock password/phrase to allow uninstall") ?? "";
        const res = await putEnforcerPolicy({
          hard_block_armed: armed,
          gate_locked: locked,
          incubation_active: Boolean(pol?.incubation_active),
          exes: killList,
          anti_tamper: antiTamper,
          protect_uninstall: false,
          provided_unlock: provided,
        });
        setSnap(res.snapshot);
        setProtectUninstall(false);
        setHint("Uninstall protect off — Apps & features entry restored.");
      }
      window.setTimeout(() => {
        invalidateFocusStatusBus({ pipe: true });
        void refresh();
      }, 400);
    } catch (e) {
      window.alert(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  const enfLabel = ownsShown
    ? err && offlineEnf
      ? "owns kills + tracking (offline mirror)"
      : "owns kills + tracking"
    : enf?.service_running === true || offlineEnf?.service_running === true
      ? "service up (lock stale?)"
      : enf?.exe_built
        ? "built — install or run console"
        : err && offlineEnf
          ? "enforcer status from mirror"
          : "not built";

  const ageLabel =
    age == null ? "no status file" : age < 1 ? "just now" : `${Math.round(age)}s ago`;

  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-end justify-between gap-3">
        <div>
          <h2 className="text-xl font-semibold tracking-tight text-foreground">Now</h2>
          <p className="text-sm text-muted-foreground mt-1 max-w-xl">
            SoftLand = websites in the browser. Arm = OS kill list (~1.5s). SoftLand ON ≠ Armed —
            turning SoftLand on does not arm anything.
          </p>
        </div>
        <button
          type="button"
          disabled={busy}
          onClick={() => void refresh()}
          className="rounded-lg border border-border px-3 py-1.5 text-sm hover:bg-muted/40"
        >
          Refresh
        </button>
      </div>

      {!enforcerHealthy && (
        <div className="rounded-xl border border-amber-500/40 bg-amber-500/10 px-3 py-3 text-sm space-y-2">
          <p className="font-medium text-amber-100">Enforcer not healthy</p>
          <p className="text-xs text-muted-foreground leading-relaxed">
            Status: <code className="text-foreground/80">{enf?.status_source || "—"}</code>
            {" · "}
            age {ageLabel}
            {enf?.exe_built === false ? " · native exe not built" : ""}
            {enf?.service_running === true ? " · service listed but status stale" : ""}
          </p>
          <p className="text-[11px] text-muted-foreground font-mono break-all">
            scripts\desktop_tracker\run\run_native_enforcer_console.bat
            <br />
            or Admin: install_native_enforcer.ps1
          </p>
          {isFocusDesktopShell() ? (
            <button
              type="button"
              disabled={busy}
              onClick={() => {
                const w = window as Window & {
                  chrome?: { webview?: { postMessage: (m: unknown) => void } };
                };
                if (w.chrome?.webview?.postMessage) {
                  w.chrome.webview.postMessage({
                    type: "focus_shell",
                    v: 1,
                    action: "restart_enforcer",
                  });
                  setHint("Restarting enforcer… check tray tip in a few seconds.");
                  window.setTimeout(() => {
                    invalidateFocusStatusBus({ pipe: true });
                    void refresh();
                  }, 2500);
                } else {
                  window.alert("Restart only works inside calt_focus.exe WebView.");
                }
              }}
              className="rounded-lg border border-amber-400/50 bg-amber-500/20 px-3 py-1.5 text-xs text-amber-50 hover:bg-amber-500/30"
            >
              Restart enforcer (tracker)
            </button>
          ) : null}
        </div>
      )}

      {enforcerHealthy && (
        <div className="flex flex-wrap items-center gap-3 text-xs">
          <p className="text-emerald-300/90">
            Enforcer LIVE · Tracker LIVE · status {ageLabel}
            {enf?.status_source ? ` · ${enf.status_source}` : ""}
          </p>
          {isFocusDesktopShell() ? (
            <button
              type="button"
              disabled={busy}
              onClick={() => {
                const w = window as Window & {
                  chrome?: { webview?: { postMessage: (m: unknown) => void } };
                };
                if (!w.chrome?.webview?.postMessage) return;
                w.chrome.webview.postMessage({
                  type: "focus_shell",
                  v: 1,
                  action: "restart_enforcer",
                });
                setHint("Restarting enforcer…");
                window.setTimeout(() => {
                  invalidateFocusStatusBus({ pipe: true });
                  void refresh();
                }, 2500);
              }}
              className="rounded-lg border border-border px-2.5 py-1 text-[11px] hover:bg-muted/40"
            >
              Restart enforcer
            </button>
          ) : null}
        </div>
      )}

      <div className="rounded-xl border border-sky-500/25 bg-sky-500/5 px-3 py-2.5 text-xs text-muted-foreground space-y-1">
        <p className="font-medium text-sky-100/95">Edge extensions</p>
        <p>
          After code updates: open{" "}
          <code className="text-foreground/80">edge://extensions</code> → Reload{" "}
          <strong className="text-foreground/85">SelfTracker</strong> +{" "}
          <strong className="text-foreground/85">CALT Gate</strong> (Edge-only SoftLand + tab stats).
        </p>
      </div>

      {err && (
        <p className="rounded-lg border border-destructive/40 bg-destructive/10 px-3 py-2 text-sm text-destructive">
          {err}
          {offlineWhy ? (
            <span className="block mt-1 text-xs text-muted-foreground">
              SoftLand why below is a frozen file snapshot (as of {offlineWhy.asOf}).
              SoftLand/Arm run via Gate + enforcer — no Study API required.{" "}
              {offlineEnf
                ? `Enforcer mirror: ${offlineEnf.armed ? "armed" : "disarmed"}${
                    offlineEnf.owns ? ", owns kills" : ""
                  }.`
                : ""}
            </span>
          ) : null}
        </p>
      )}
      {hint && <p className="text-xs text-emerald-300">{hint}</p>}

      <SectionBar title="Now">
        <div className="grid gap-3 md:grid-cols-2">
          <div className="rounded-xl border border-border bg-card/60 p-4 space-y-2">
            <p className="text-xs text-muted-foreground">Browser SoftLand (Gate)</p>
            <p className="text-2xl font-semibold">
              {active?.browser_mode_label ||
                (offlineWhy ? (offlineWhy.softlandOn ? "SoftLand (offline)" : "SoftLand off") : "—")}
            </p>
            <p className="text-sm">
              <span className="rounded-full border px-2 py-0.5 text-xs">
                {armedShown ? "OS hard block armed" : "OS hard block off"}
              </span>{" "}
              <span className="text-muted-foreground text-xs">Next: {active?.morning_next || "—"}</span>
            </p>
            <p className="text-[15px] leading-relaxed">
              {active?.why || offlineWhy?.why || (err ? "API offline — why unavailable" : "Loading…")}
            </p>
            <p className="text-emerald-400 text-sm">
              {active?.until?.label || offlineWhy?.untilLabel || ""}
            </p>
            <p className="text-[11px] text-muted-foreground pt-1">
              SoftLand = sites in Edge. OS kills = Enforcer kill list (not this SoftLand mode alone).
            </p>
          </div>

          <div className="rounded-xl border border-border bg-card/60 p-4 space-y-2">
            <p className="text-xs text-muted-foreground">Blocked</p>
            <ul className="list-disc pl-5 text-sm text-muted-foreground space-y-1">
              {(active?.blocked_summary || []).map((line) => (
                <li key={line}>{line}</li>
              ))}
              {!(active?.blocked_summary || []).length && <li>Nothing listed</li>}
            </ul>
          </div>

          {inc?.active && (
            <div className="rounded-xl border border-amber-500/30 bg-amber-500/10 p-4 md:col-span-2">
              <p className="text-xs uppercase tracking-wider text-amber-200/80">Incubation</p>
              <p className="text-sm mt-1">
                {inc.remaining_sec ?? 0}s left of {inc.total_sec ?? 0}s — entertainment stays blocked.
              </p>
            </div>
          )}

          <div className="rounded-xl border border-border bg-card/60 p-4 space-y-2 md:col-span-2">
            <p className="text-xs text-muted-foreground">Earned free time</p>
            <p className="text-sm flex justify-between">
              <span>Balance</span>
              <strong>{earned?.balance_minutes ?? 0} min</strong>
            </p>
            <p className="text-sm flex justify-between text-muted-foreground">
              <span>Earned today</span>
              <span>
                {earned?.daily_earned ?? 0} / {earned?.daily_cap ?? 60}
              </span>
            </p>
          </div>
        </div>
      </SectionBar>

      <SectionBar title="Actions">
        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            disabled={busy || !actions?.can_pin_free || !!actions?.incubation_blocks_pin}
            onClick={() => void onFree()}
            className="rounded-lg bg-emerald-600/20 border border-emerald-500/40 px-3 py-2 text-sm text-emerald-100 disabled:opacity-40"
          >
            Free time (PIN)…
          </button>
          <button
            type="button"
            disabled={
              busy || !actions?.can_spend_earned || !(earned?.balance_minutes && earned.balance_minutes > 0)
            }
            onClick={() => void onSpend()}
            className="rounded-lg bg-emerald-600/20 border border-emerald-500/40 px-3 py-2 text-sm text-emerald-100 disabled:opacity-40"
          >
            Spend earned…
          </button>
        </div>
      </SectionBar>

      <SectionBar title="Arm (apps)">
        <p className="text-[11px] text-muted-foreground -mt-1 mb-2 max-w-xl">
          Arm on: kills processes on the kill list about every 1.5s. Arm off: no OS kills. Edit the
          list under Blocks → Apps. SoftLand site lists never belong here.
        </p>
        <div className="rounded-xl border border-rose-400/20 bg-card/60 p-4 space-y-3 max-w-xl">
          <div className="flex flex-wrap items-center gap-2">
            <span
              className={`rounded-full border px-2.5 py-0.5 text-xs font-medium ${
                armedShown
                  ? "border-rose-500/50 bg-rose-500/15 text-rose-100"
                  : "border-border text-muted-foreground"
              }`}
            >
              {armedShown ? "Armed" : "Disarmed"}
            </span>
            <span
              className={`rounded-full border px-2.5 py-0.5 text-xs ${
                ownsShown
                  ? "border-emerald-500/40 bg-emerald-500/10 text-emerald-100"
                  : "border-amber-500/40 text-amber-100/90"
              }`}
            >
              {ownsShown ? "Owns kills" : "Not owning"}
            </span>
            <span className="rounded-full border border-border px-2.5 py-0.5 text-xs text-muted-foreground">
              Lock {lockPresentShown ? "present" : "missing"}
              {err && offlineEnf ? " (offline mirror)" : ""}
            </span>
            <span className="rounded-full border border-border px-2.5 py-0.5 text-xs text-muted-foreground">
              {ageLabel}
            </span>
          </div>
          <p className="text-sm font-medium">{enfLabel}</p>
          <dl className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-xs">
            <dt className="text-muted-foreground">Status file</dt>
            <dd>{enf?.status_source || "—"}</dd>
            <dt className="text-muted-foreground">Policy</dt>
            <dd>{enf?.policy_source || (pol ? "enforcer_policy.json" : "—")}</dd>
            <dt className="text-muted-foreground">Last kill</dt>
            <dd className="break-all font-mono text-[11px] text-foreground/90">
              {enf?.last_kill || "None yet"}
            </dd>
          </dl>
          <div className="space-y-2">
            <div className="flex flex-wrap items-center justify-between gap-2">
              <span className="text-xs text-muted-foreground">Kill list (from policy)</span>
              <a
                className="rounded-lg border border-sky-500/40 bg-sky-500/10 px-2.5 py-1 text-[11px] text-sky-100 hover:bg-sky-500/20"
                href="/productivity?tab=settings&section=apps"
              >
                Edit in Settings → Apps →
              </a>
            </div>
            <div className="flex flex-wrap gap-1.5 min-h-[2rem]">
              {exes.length === 0 ? (
                <span className="text-[11px] text-muted-foreground">
                  Empty — add exes under Settings → Blocks → Apps before Arm.
                </span>
              ) : (
                exes.map((exe) => (
                  <span
                    key={exe}
                    className="rounded-md border border-rose-400/30 bg-rose-500/10 px-2 py-0.5 text-[11px] font-mono text-rose-100"
                  >
                    {exe}
                  </span>
                ))
              )}
            </div>
            <p className="text-[11px] text-muted-foreground">
              CRUD + presets live only at{" "}
              <a
                className="underline text-sky-300/90"
                href="/productivity?tab=settings&section=apps"
              >
                Settings → Blocks → Apps
              </a>
              {" "}— Unlock Arms the saved list (no second editor). SoftLand ON ≠ Armed.
            </p>
            {killListLocked ? (
              <p className="text-[11px] text-amber-200/90">
                Locked while armed — unlock/disarm to edit the list under Settings → Apps.
              </p>
            ) : null}
          </div>
          <label className="block text-xs text-muted-foreground space-y-1">
            <span>Lock mode (native refuses disarm until met)</span>
            <select
              value={lockMode}
              onChange={(e) => setLockMode(e.target.value as typeof lockMode)}
              className="w-full rounded-lg border border-border bg-background px-3 py-2 text-sm text-foreground"
            >
              <option value="none">None</option>
              <option value="timer">Timer</option>
              <option value="password">Password</option>
              <option value="phrase">Phrase</option>
            </select>
          </label>
          {lockMode === "timer" && (
            <label className="block text-xs text-muted-foreground space-y-1">
              <span>Lock minutes</span>
              <input
                type="number"
                min={1}
                max={24 * 60}
                value={timerMinutes}
                onChange={(e) => setTimerMinutes(Math.max(1, parseInt(e.target.value, 10) || 1))}
                className="w-full rounded-lg border border-border bg-background px-3 py-2 text-sm text-foreground"
              />
            </label>
          )}
          {(lockMode === "password" || lockMode === "phrase") && (
            <label className="block text-xs text-muted-foreground space-y-1">
              <span>{lockMode === "password" ? "Unlock password" : "Unlock phrase"}</span>
              <input
                type={lockMode === "password" ? "password" : "text"}
                value={lockSecret}
                onChange={(e) => setLockSecret(e.target.value)}
                className="w-full rounded-lg border border-border bg-background px-3 py-2 text-sm text-foreground"
              />
            </label>
          )}
          <label className="flex items-center gap-2 text-xs text-muted-foreground">
            <input type="checkbox" checked={antiTamper} onChange={(e) => setAntiTamper(e.target.checked)} />
            Anti-tamper (kill Task Manager / process explorers while locked)
          </label>
          <div className="rounded-lg border border-border/80 bg-background/40 p-3 space-y-2">
            <label className="flex items-start gap-2 text-xs text-muted-foreground">
              <input
                type="checkbox"
                className="mt-0.5"
                checked={protectUninstall || Boolean(pol?.protect_uninstall)}
                disabled={busy}
                onChange={(e) => void setProtectUninstallOn(e.target.checked)}
              />
              <span>
                Protect uninstall (option B) — hide from Apps &amp; features; require Focus unlock
                password/phrase to show again. Admin can still bypass.
              </span>
            </label>
            {(protectUninstall || pol?.protect_uninstall) && (
              <p className="text-[11px] text-amber-200/90 pl-6">
                Protected — use Start Menu &quot;Uninstall CALT Productivity&quot; with password, or
                uncheck here.
              </p>
            )}
          </div>
          {pol?.lock_mode && pol.lock_mode !== "none" && (
            <p className="text-xs text-amber-200/90">
              Active lock: {pol.lock_mode}
              {pol.lock_mode === "timer" && pol.lock_until_unix
                ? ` until ${new Date(pol.lock_until_unix * 1000).toLocaleTimeString()}`
                : ""}
            </p>
          )}
          <div className="flex flex-wrap gap-2">
            <button
              type="button"
              disabled={busy}
              onClick={() => void setArmed(true)}
              className="rounded-lg bg-rose-600/20 border border-rose-500/40 px-3 py-2 text-sm text-rose-100 disabled:opacity-40"
            >
              Arm hard block
            </button>
            <button
              type="button"
              disabled={busy}
              onClick={() => void setArmed(false)}
              className="rounded-lg border border-border px-3 py-2 text-sm hover:bg-muted/40 disabled:opacity-40"
            >
              Disarm
            </button>
          </div>
          <p className="text-[11px] text-muted-foreground pt-1">
            Writes <code className="text-foreground/80">data/productivity/behavior/enforcer_policy.json</code> —
            <code className="text-foreground/80">calt_enforcer</code> reads it each tick. Configuration only here —
            do not Arm unless you mean to start OS kills. SoftLand ON ≠ Armed.
          </p>
        </div>
      </SectionBar>

      <SectionBar title="Related">
        <div className="flex flex-wrap gap-2 text-sm">
          <Link
            to="/productivity"
            className="rounded-lg border border-border px-3 py-1.5 text-muted-foreground hover:text-foreground"
          >
            Calendar
          </Link>
          <Link
            to="/bible"
            className="rounded-lg border border-border px-3 py-1.5 text-muted-foreground hover:text-foreground"
          >
            Bible
          </Link>
          <Link
            to="/journal"
            className="rounded-lg border border-border px-3 py-1.5 text-muted-foreground hover:text-foreground"
          >
            Journal
          </Link>
        </div>
      </SectionBar>
    </div>
  );
}

export default FocusControlPanel;
