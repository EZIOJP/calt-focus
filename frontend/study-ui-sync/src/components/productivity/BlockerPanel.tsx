/**
 * @deprecated Focus Plan → Goals no longer mounts this panel.
 * SoftLand site lists + Arm kill list editors live under Settings → Blocks
 * (`SoftLandSiteRulesPanel`, `AppKillRulesPanel`). Kept for reference / Study experiments.
 */
import { useCallback, useEffect, useState } from "react";
import { Plus, Save, Shield } from "lucide-react";
import SoftLandSiteRulesPanel from "./SoftLandSiteRulesPanel";
import { putEnforcerPolicy } from "../../api/behaviorClient";
import { fetchEnforcerStatusMirror } from "../../api/focusMirrors";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";
import { focusDataUrl } from "../../utils/focusDataUrl";
import { cn } from "../../app/components/ui/utils";

function normalizeExe(raw: string): string {
  let s = raw.trim().toLowerCase();
  if (!s) return "";
  if (!s.endsWith(".exe")) s = `${s}.exe`;
  return s;
}

async function loadKillList(): Promise<string[]> {
  if (isFocusDesktopShell()) {
    try {
      const r = await fetch(focusDataUrl("enforcer_policy.json"), { cache: "no-store" });
      if (r.ok) {
        const j = (await r.json()) as { exes?: unknown };
        if (Array.isArray(j.exes)) {
          return j.exes.map((x) => String(x || "").trim().toLowerCase()).filter(Boolean);
        }
      }
    } catch {
      /* fall through */
    }
  }
  try {
    const m = await fetchEnforcerStatusMirror();
    const exes = m?.enforcer_policy?.exes;
    if (Array.isArray(exes)) {
      return exes.map((x) => String(x || "").trim().toLowerCase()).filter(Boolean);
    }
  } catch {
    /* empty */
  }
  return [];
}

function BlockerAppsEditor() {
  const [exes, setExes] = useState<string[]>([]);
  const [draft, setDraft] = useState("");
  const [saving, setSaving] = useState(false);
  const [hint, setHint] = useState<string | null>(null);
  const [armed, setArmed] = useState(false);

  const load = useCallback(async () => {
    setExes(await loadKillList());
    try {
      const m = await fetchEnforcerStatusMirror();
      setArmed(Boolean(m?.armed || m?.enforcer_policy?.hard_block_armed));
    } catch {
      /* ignore */
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const add = () => {
    const exe = normalizeExe(draft);
    if (!exe) return;
    if (exes.includes(exe)) {
      setDraft("");
      return;
    }
    setExes((prev) => [...prev, exe]);
    setDraft("");
    setHint(null);
  };

  const save = async () => {
    setSaving(true);
    setHint(null);
    try {
      await putEnforcerPolicy({
        hard_block_armed: armed,
        gate_locked: false,
        exes,
      });
      setHint("Apps list saved");
    } catch (e: unknown) {
      setHint(e instanceof Error ? e.message : "Could not save apps list");
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap gap-1 min-h-[1.25rem]">
        {exes.length === 0 ? (
          <span className="text-[10px] text-muted-foreground">No blocked apps yet</span>
        ) : (
          exes.map((exe) => (
            <button
              key={exe}
              type="button"
              title="Remove"
              onClick={() => setExes((prev) => prev.filter((x) => x !== exe))}
              className="text-[10px] px-1.5 py-0.5 rounded border border-rose-400/30 bg-rose-500/10 font-mono"
            >
              {exe} ×
            </button>
          ))
        )}
      </div>
      <div className="flex flex-wrap gap-1.5 items-center">
        <input
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") {
              e.preventDefault();
              add();
            }
          }}
          placeholder="discord.exe"
          className="min-w-[8rem] flex-1 rounded-lg border border-white/10 bg-black/30 px-2 py-1 text-[11px]"
        />
        <button
          type="button"
          onClick={add}
          className="inline-flex items-center gap-1 px-2 py-1 rounded-lg border border-white/15 text-[11px]"
        >
          <Plus size={11} /> Add
        </button>
        <button
          type="button"
          disabled={saving}
          onClick={() => void save()}
          className="inline-flex items-center gap-1 px-2 py-1 rounded-lg bg-emerald-600/70 text-[11px] disabled:opacity-50"
        >
          <Save size={11} /> {saving ? "…" : "Save"}
        </button>
      </div>
      <p className="text-[10px] text-muted-foreground">
        OS kills when <span className="text-rose-300/90">Armed</span> — SoftLand ON ≠ Armed.
      </p>
      {hint ? <p className="text-[10px] text-muted-foreground">{hint}</p> : null}
    </div>
  );
}

type BlockerTab = "sites" | "apps";

/** SoftLand sites + OS kill apps under one “Blocker” name. */
export function BlockerPanel() {
  const [tab, setTab] = useState<BlockerTab>("sites");
  const [softlandOn, setSoftlandOn] = useState<boolean | null>(null);
  const [armed, setArmed] = useState<boolean | null>(null);

  useEffect(() => {
    let cancelled = false;
    void (async () => {
      try {
        const m = await fetchEnforcerStatusMirror();
        if (cancelled || !m) return;
        setSoftlandOn(Boolean(m.softland_enabled ?? m.softland?.softland_enabled));
        setArmed(Boolean(m.armed || m.enforcer_policy?.hard_block_armed));
      } catch {
        /* ignore */
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <section className="goals-blocker space-y-2">
      <div className="flex flex-wrap items-center gap-2">
        <Shield size={13} className="text-sky-300 shrink-0" aria-hidden />
        <h4 className="text-[11px] font-semibold text-foreground">Blocker</h4>
        {softlandOn != null ? (
          <span
            className={cn(
              "text-[9px] font-mono px-1.5 py-0.5 rounded border",
              softlandOn
                ? "border-emerald-500/30 bg-emerald-500/10 text-emerald-300"
                : "border-white/10 text-muted-foreground",
            )}
          >
            SoftLand {softlandOn ? "on" : "off"}
          </span>
        ) : null}
        {armed != null ? (
          <span
            className={cn(
              "text-[9px] font-mono px-1.5 py-0.5 rounded border",
              armed
                ? "border-rose-500/30 bg-rose-500/10 text-rose-300"
                : "border-white/10 text-muted-foreground",
            )}
          >
            {armed ? "Armed" : "Disarmed"}
          </span>
        ) : null}
      </div>

      <div className="goals-blocker__tabs" role="tablist" aria-label="Blocker lists">
        <button
          type="button"
          role="tab"
          aria-selected={tab === "sites"}
          className={cn("goals-blocker__tab", tab === "sites" && "is-active")}
          onClick={() => setTab("sites")}
        >
          Sites
        </button>
        <button
          type="button"
          role="tab"
          aria-selected={tab === "apps"}
          className={cn("goals-blocker__tab", tab === "apps" && "is-active")}
          onClick={() => setTab("apps")}
        >
          Apps
        </button>
      </div>

      <div className="goals-blocker__panel" role="tabpanel">
        {tab === "sites" ? <SoftLandSiteRulesPanel mode="blocker" /> : <BlockerAppsEditor />}
      </div>
    </section>
  );
}

export default BlockerPanel;
