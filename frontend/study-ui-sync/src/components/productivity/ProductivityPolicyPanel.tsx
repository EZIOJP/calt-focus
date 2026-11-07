import { useCallback, useEffect, useState } from "react";
import { Loader2, Shield, Save } from "lucide-react";
import {
  fetchCategoryScores,
  fetchDistractionGate,
  fetchProductivityPolicy,
  saveCategoryScores,
  saveProductivityPolicy,
  saveStudyLoopGate,
  type DistractionGate,
  type ProductivityPolicy,
} from "../../api/behaviorClient";
import { requestRewardDay, requestBibleDayPass } from "../../api/bibleClient";
import {
  GOALS_UPDATED_EVENT,
  goalMinutesToFocusHours,
  loadProductivityGoals,
  persistProductivityGoals,
} from "./ProductivityGoalsPanel";
import { formatHoursMins, formatHoursMinsPair } from "../../utils/formatDuration";
import { DeviceBlockPanel } from "./DeviceBlockPanel";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";

const COMMON_CATEGORIES = [
  "IDE / Code Editor",
  "Terminal",
  "Dev Tools",
  "Coding Practice",
  "Study / Reading",
  "Study (Browser)",
  "Coursework (Browser)",
  "AI Tools",
  "AI / ML",
  "Research",
  "Documentation",
  "Knowledge Work",
  "Office / Docs",
  "Gaming",
  "Video Streaming",
  "Music / Media",
  "Social Media",
  "Entertainment",
  "Shopping",
  "Browser",
  "Communication",
  "Other",
];

function fmtRemainMinutes(m: number): string {
  return formatHoursMins(m);
}

export type PolicyPanelVariant = "all" | "softland" | "unlock" | "scoring";

type Props = {
  onSaved?: () => void;
  /** Disassembled Settings groups — default keeps legacy full panel. */
  variant?: PolicyPanelVariant;
};

export function ProductivityPolicyPanel({ onSaved, variant = "all" }: Props) {
  const showSoftlandToggle = variant === "all" || variant === "softland";
  const showUnlockExtras = variant === "all" || variant === "unlock";
  const showCommitmentBox = showSoftlandToggle || showUnlockExtras;
  const showScoring = variant === "all" || variant === "scoring";
  const showDevice = variant === "all";
  const [policy, setPolicy] = useState<ProductivityPolicy | null>(null);
  const [scores, setScores] = useState<Record<string, number>>({});
  const [gate, setGate] = useState<DistractionGate | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [hint, setHint] = useState<string | null>(null);
  const [overrideKey, setOverrideKey] = useState("");
  const [overrideCat, setOverrideCat] = useState("Study / Reading");
  const [newBlockExe, setNewBlockExe] = useState("");

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [p, s, g] = await Promise.all([
        fetchProductivityPolicy(),
        fetchCategoryScores(),
        fetchDistractionGate().catch(() => null),
      ]);
      setPolicy({
        ...p,
        softland_enabled: p.softland_enabled ?? p.hard_block_enabled ?? false,
        hard_block_enabled: p.softland_enabled ?? p.hard_block_enabled ?? false,
        daily_goal_minutes: p.daily_goal_minutes ?? 240,
        hard_block_gaming: p.hard_block_gaming ?? true,
        hard_block_exes: p.hard_block_exes ?? [],
      });
      setScores(s.scores || {});
      setGate(g);
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : "Failed to load policy");
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  useEffect(() => {
    const onGoals = () => void load();
    window.addEventListener(GOALS_UPDATED_EVENT, onGoals);
    return () => window.removeEventListener(GOALS_UPDATED_EVENT, onGoals);
  }, [load]);

  const toggleList = (list: "productive_categories" | "blocked_categories", cat: string) => {
    if (!policy) return;
    const cur = new Set(policy[list]);
    if (cur.has(cat)) cur.delete(cat);
    else cur.add(cat);
    const next = { ...policy, [list]: [...cur].sort() };
    // Mutual exclusivity
    if (list === "productive_categories" && cur.has(cat)) {
      next.blocked_categories = next.blocked_categories.filter((c) => c !== cat);
    }
    if (list === "blocked_categories" && cur.has(cat)) {
      next.productive_categories = next.productive_categories.filter((c) => c !== cat);
    }
    setPolicy(next);
  };

  const save = async () => {
    if (!policy) return;
    setSaving(true);
    setError(null);
    setHint(null);
    try {
      const saved = await saveProductivityPolicy({
        ...policy,
        softland_enabled: policy.softland_enabled ?? policy.hard_block_enabled ?? false,
      });
      // Prefer native SoftLand SoT when Focus bridge is available (Phase 2 gateway).
      try {
        const { enforcerNativeCmd, isFocusEnforcerBridgeAvailable } = await import(
          "../../lib/enforcerNativeCmd"
        );
        if (isFocusEnforcerBridgeAvailable()) {
          const enabled = Boolean(saved.softland_enabled ?? saved.hard_block_enabled);
          const payload: { enabled: boolean; confirm?: string } = { enabled };
          // Server requires confirm=UNLOCK when turning SoftLand off (commitment device).
          if (!enabled) payload.confirm = "UNLOCK";
          const native = await enforcerNativeCmd("softland.set_enabled", payload);
          if (native && native.ok === false && native.error === "confirm_required") {
            throw new Error('Type UNLOCK to turn SoftLand off (gateway confirm required).');
          }
        }
      } catch (e) {
        if (e instanceof Error && e.message.includes("UNLOCK")) throw e;
        /* HTTP save already applied; gateway optional when bridge down */
      }
      setPolicy({
        ...saved,
        softland_enabled: saved.softland_enabled ?? saved.hard_block_enabled ?? false,
        hard_block_enabled: saved.softland_enabled ?? saved.hard_block_enabled ?? false,
      });
      await saveCategoryScores(scores).catch(() => {
        /* Focus shell: category scores may be Study-only until native classify write */
      });
      const hours = goalMinutesToFocusHours(saved.daily_goal_minutes ?? 240);
      const local = loadProductivityGoals();
      if (local.focusHoursPerDay !== hours) {
        persistProductivityGoals({ ...local, focusHoursPerDay: hours });
      }
      const g = await fetchDistractionGate().catch(() => null);
      setGate(g);
      setHint(
        `Policy saved — daily goal is now ${saved.daily_goal_minutes} min (${hours}h). Tracker picks up within ~30s.`,
      );
      onSaved?.();
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : "Save failed");
    } finally {
      setSaving(false);
    }
  };

  const addOverride = () => {
    if (!policy || !overrideKey.trim()) return;
    setPolicy({
      ...policy,
      app_overrides: {
        ...policy.app_overrides,
        [overrideKey.trim()]: overrideCat,
      },
    });
    setOverrideKey("");
  };

  if (loading) {
    return (
      <div className="gloss-panel rounded-xl p-4 flex items-center gap-2 text-sm text-muted-foreground">
        <Loader2 size={14} className="animate-spin" /> Loading productivity policy…
      </div>
    );
  }

  if (!policy) {
    return (
      <div className="gloss-panel rounded-xl p-4 text-sm text-rose-300">
        {error || "No policy"}
      </div>
    );
  }

  const allCats = Array.from(
    new Set([...COMMON_CATEGORIES, ...Object.keys(scores), ...policy.productive_categories, ...policy.blocked_categories]),
  ).sort();

  return (
    <div className="gloss-panel rounded-xl p-4 space-y-4">
      <div className="flex items-start justify-between gap-3">
        <div>
          <h3 className="text-sm font-semibold flex items-center gap-2">
            <Shield size={14} className="text-violet-300" />
            Productivity policy
          </h3>
          <p className="text-xs text-muted-foreground mt-1 max-w-xl">
            Tuned for AI/ML (Scaler) deep work. Blocked categories never count as productive — even during a
            planned block. Override apps or single sessions for edge cases.
          </p>
        </div>
        <button
          type="button"
          disabled={saving}
          onClick={() => void save()}
          className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-violet-600/80 hover:bg-violet-600 text-xs disabled:opacity-50"
        >
          {saving ? <Loader2 size={12} className="animate-spin" /> : <Save size={12} />}
          Save
        </button>
      </div>

      {error && <p className="text-xs text-rose-300">{error}</p>}
      {hint && <p className="text-xs text-emerald-300">{hint}</p>}

      {showCommitmentBox && (
      <div className="rounded-lg border border-amber-500/30 bg-amber-500/5 p-3 space-y-3">
        {showSoftlandToggle && (
        <div className="flex items-start justify-between gap-2">
          <div>
            <p className="text-xs font-semibold text-sky-100">SoftLand (sites)</p>
            <p className="text-[11px] text-muted-foreground mt-0.5">
              On: blocks listed / watch / built-in sites in Edge when mode says so. Off: SoftLand
              allows all sites (hosts-file device lock and Arm stay independent). SoftLand ON never
              means Armed.
            </p>
          </div>
          <label className="flex items-center gap-2 text-xs shrink-0">
            <input
              type="checkbox"
              checked={Boolean(policy.softland_enabled ?? policy.hard_block_enabled)}
              onChange={(e) => {
                const on = e.target.checked;
                if (!on && (policy.softland_enabled ?? policy.hard_block_enabled)) {
                  const ok = window.prompt(
                    "Type UNLOCK to turn SoftLand (site block) off:",
                  );
                  if (ok !== "UNLOCK") return;
                }
                setPolicy({ ...policy, softland_enabled: on, hard_block_enabled: on });
              }}
            />
            SoftLand on
          </label>
        </div>
        )}
        {showUnlockExtras && (
        <>
        <p className="text-[11px] text-muted-foreground">
          Day unlocks when you hit your <strong className="text-foreground/80">study goal</strong>{" "}
          and tick <strong className="text-foreground/80">1 Bible chapter</strong> (until midnight).{" "}
          <a className="underline text-amber-200/90" href="/bible">
            Open Bible reader
          </a>
        </p>
        {!isFocusDesktopShell() ? (
        <div className="rounded-md border border-white/10 bg-black/20 p-2.5 space-y-2">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <div>
              <p className="text-xs font-medium text-foreground/90">Require daily Study Loop</p>
              <p className="text-[11px] text-muted-foreground mt-0.5">
                When on, SoftLand stays on study until today’s path is done. Off = optional (Review
                Hub still works). Currently paused by default.
              </p>
            </div>
            <label className="text-xs flex items-center gap-2 shrink-0">
              <input
                type="checkbox"
                checked={Boolean(gate?.morning?.study_loop_gate?.enabled ?? gate?.morning?.config?.study_loop_required)}
                onChange={(e) => {
                  const on = e.target.checked;
                  void (async () => {
                    try {
                      setHint(null);
                      const saved = await saveStudyLoopGate(on);
                      setGate((g) =>
                        g
                          ? {
                              ...g,
                              morning: g.morning
                                ? {
                                    ...g.morning,
                                    study_loop_gate: saved,
                                    config: {
                                      ...(g.morning.config || {}),
                                      study_loop_required: saved.enabled,
                                    },
                                  }
                                : g.morning,
                            }
                          : g,
                      );
                      setHint(
                        saved.enabled
                          ? "Study Loop gate ON — SoftLand will require today’s path after plan."
                          : "Study Loop gate OFF — SoftLand will not force study.",
                      );
                      const refreshed = await fetchDistractionGate().catch(() => null);
                      if (refreshed) setGate(refreshed);
                    } catch (err: unknown) {
                      setError(err instanceof Error ? err.message : "Could not save Study Loop gate");
                    }
                  })();
                }}
              />
              {gate?.morning?.study_loop_gate?.enabled || gate?.morning?.config?.study_loop_required
                ? "Required"
                : "Off"}
            </label>
          </div>
        </div>
        ) : null}
        <div className="rounded-md border border-teal-400/25 bg-teal-500/5 p-2.5 space-y-2">
          <p className="text-[11px] text-muted-foreground">
            Earned Free Day: complete your study goal and one Bible chapter on{" "}
            <strong className="text-foreground/80">4 days</strong> to bank one reward day. Reward days stack
            and unlock games plus normal browsing until midnight; tracking stays on and the adult filter remains on.
            {gate?.reward_day_status
              ? ` · ${gate.reward_day_status.available} banked · ${gate.reward_day_status.days_to_next_reward} day(s) to next`
              : ""}
          </p>
          <button
            type="button"
            className="text-xs px-3 py-1.5 rounded-md bg-teal-500/15 text-teal-100 border border-teal-400/30 hover:bg-teal-500/25 disabled:opacity-40"
            disabled={Boolean(
              gate?.reward_day ||
                gate?.day_unlimited ||
                !gate?.reward_day_status ||
                gate.reward_day_status.available <= 0,
            )}
            onClick={() => {
              const ok = window.prompt(
                "Use one earned reward day? It unlocks games and normal browsing until midnight. Type REWARD:",
              );
              if (ok !== "REWARD") return;
              void (async () => {
                try {
                  const r = await requestRewardDay("REWARD");
                  const g = await fetchDistractionGate().catch(() => null);
                  setGate(g);
                  setHint(r.message || "Reward day active — free mode until midnight.");
                } catch (e: unknown) {
                  setError(e instanceof Error ? e.message : "Reward day failed");
                }
              })();
            }}
          >
            {gate?.reward_day || gate?.day_unlimited
              ? "Free day active today"
              : (gate?.reward_day_status?.available ?? 0) > 0
                ? `Use reward day (${gate?.reward_day_status?.available} banked)`
                : `Earn reward day (${gate?.reward_day_status?.days_to_next_reward ?? 4} days left)`}
          </button>
          <div className="rounded-md border border-amber-400/20 bg-amber-500/5 p-2.5 space-y-2">
            <p className="text-[11px] text-muted-foreground">
              Day pass: 2 per week. Buys SoftLand free until midnight (does not Arm).
              {gate?.day_pass_status
                ? ` · ${gate.day_pass_status.remaining ?? 0} left this week`
                : ""}
            </p>
            <button
              type="button"
              className="text-xs px-3 py-1.5 rounded-md bg-amber-500/15 text-amber-100 border border-amber-400/30 hover:bg-amber-500/25 disabled:opacity-40"
              disabled={Boolean(
                gate?.day_pass ||
                  gate?.day_unlimited ||
                  (gate?.day_pass_status?.remaining ?? 0) <= 0,
              )}
              onClick={() => {
                const ok = window.prompt('Use one day pass? Type PASS:');
                if (ok !== "PASS") return;
                void (async () => {
                  try {
                    const r = await requestBibleDayPass("PASS");
                    const g = await fetchDistractionGate().catch(() => null);
                    setGate(g);
                    setHint(r.message || "Day pass active until midnight.");
                  } catch (e: unknown) {
                    setError(e instanceof Error ? e.message : "Day pass failed");
                  }
                })();
              }}
            >
              {gate?.day_pass || gate?.day_unlimited
                ? "Pass already active today"
                : (gate?.day_pass_status?.remaining ?? 0) > 0
                  ? `Use day pass (${gate?.day_pass_status?.remaining} left)`
                  : "No day passes left this week"}
            </button>
          </div>
        </div>
        {gate && (
          <div className="rounded-md border border-white/10 bg-black/30 p-2.5 flex gap-3 items-center">
            <div className="relative w-14 h-14 shrink-0">
              <svg className="w-full h-full -rotate-90" viewBox="0 0 56 56" aria-hidden>
                <circle
                  cx="28"
                  cy="28"
                  r="22"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="5"
                  className="text-muted/30"
                />
                <circle
                  cx="28"
                  cy="28"
                  r="22"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="5"
                  strokeLinecap="round"
                  className={
                    gate.locked ? "text-amber-400" : gate.enabled ? "text-teal-400" : "text-muted-foreground"
                  }
                  strokeDasharray={`${2 * Math.PI * 22}`}
                  strokeDashoffset={`${
                    2 *
                    Math.PI *
                    22 *
                    (1 -
                      Math.min(
                        1,
                        gate.daily_goal_minutes > 0
                          ? gate.productive_minutes / gate.daily_goal_minutes
                          : 0,
                      ))
                  }`}
                />
              </svg>
              <span className="absolute inset-0 flex items-center justify-center text-[10px] font-bold tabular-nums">
                {gate.daily_goal_minutes > 0
                  ? Math.min(
                      100,
                      Math.round((gate.productive_minutes / gate.daily_goal_minutes) * 100),
                    )
                  : 0}
                %
              </span>
            </div>
            <div className="min-w-0 flex-1 space-y-0.5">
              <p className="text-xs font-medium text-foreground/95">
                {!gate.enabled
                  ? "Hard-block is off"
                  : gate.day_unlimited
                    ? "Unlimited games today (study + Bible done)"
                    : gate.locked
                      ? "Games locked — Bible bank or finish study+Bible"
                      : `Game bank open · ${formatHoursMins(gate.game_bank_remaining_minutes ?? 0)} left`}
              </p>
              <p className="text-[11px] text-muted-foreground">
                Study {formatHoursMinsPair(gate.productive_minutes, gate.daily_goal_minutes)} · Bible{" "}
                {formatHoursMinsPair(gate.bible_minutes ?? 0, 30)}
                {gate.locked && gate.remaining_minutes
                  ? ` · ${fmtRemainMinutes(gate.remaining_minutes)} study left`
                  : ""}
              </p>
              {(gate.browser?.mode || gate.browser_mode) && (
                <p className="text-[11px] mt-1">
                  <span
                    className={`inline-flex items-center gap-1 rounded px-1.5 py-0.5 font-semibold tracking-wide ${
                      ["bible", "planning", "study"].includes(
                        String(gate.browser?.mode || gate.browser_mode || "").toLowerCase(),
                      )
                        ? "bg-amber-500/20 text-amber-100 border border-amber-400/30"
                        : "bg-teal-500/15 text-teal-100 border border-teal-400/25"
                    }`}
                  >
                    Mode:{" "}
                    {gate.browser?.mode_label ||
                      String(gate.browser?.mode || gate.browser_mode || "").toUpperCase()}
                  </span>
                  <span className="text-muted-foreground ml-2">
                    {["bible", "planning", "study"].includes(
                      String(gate.browser?.mode || gate.browser_mode || "").toLowerCase(),
                    )
                      ? "YouTube blocked until daily focus goal · then FREE (distraction filter stays on)"
                      : "FREE — YouTube OK · distraction filter still on"}
                  </span>
                </p>
              )}
              {gate.browser?.note && (
                <p className="text-[10px] text-muted-foreground/90 mt-1 leading-snug">{gate.browser.note}</p>
              )}
              {gate.browser?.allowed_browsers && gate.browser.allowed_browsers.length > 0 && (
                <p className="text-[10px] text-muted-foreground mt-0.5">
                  Allowed browser:{" "}
                  <code className="font-mono text-foreground/80">
                    {gate.browser.allowed_browsers.join(", ")}
                  </code>
                  {" · "}others / installers soft-lock while enforcing
                </p>
              )}
              <div className="h-1.5 rounded-full bg-muted/30 overflow-hidden mt-1">
                <div
                  className={`h-full rounded-full ${gate.locked ? "bg-amber-400" : "bg-teal-400"}`}
                  style={{
                    width: `${Math.min(
                      100,
                      gate.daily_goal_minutes > 0
                        ? (gate.productive_minutes / gate.daily_goal_minutes) * 100
                        : 0,
                    )}%`,
                  }}
                />
              </div>
            </div>
          </div>
        )}
        <div className="flex flex-wrap items-center gap-3 text-xs">
          <label className="flex items-center gap-2">
            Daily goal
            <input
              type="number"
              min={15}
              max={960}
              value={policy.daily_goal_minutes ?? 240}
              onChange={(e) =>
                setPolicy({ ...policy, daily_goal_minutes: Number(e.target.value) || 240 })
              }
              className="w-20 rounded border border-white/10 bg-black/30 px-2 py-1"
            />
            <span className="text-muted-foreground">
              min · {formatHoursMins(policy.daily_goal_minutes ?? 240)}
            </span>
          </label>
        </div>
        <div className="rounded-md border border-border/40 bg-background/30 p-2.5 space-y-1.5">
          <p className="text-[11px] text-muted-foreground leading-relaxed">
            OS kill list and SoftLand site lists live under{" "}
            <strong className="text-foreground/80">Settings → Blocks</strong> (Sites / Apps). Unlock
            Arms the saved app list — SoftLand ON ≠ Armed.
          </p>
          <div className="flex flex-wrap gap-2">
            <a
              className="rounded-lg border border-sky-500/35 bg-sky-500/10 px-2.5 py-1 text-[11px] text-sky-100 hover:bg-sky-500/20"
              href="/productivity?tab=settings&section=sites"
            >
              Sites →
            </a>
            <a
              className="rounded-lg border border-rose-500/35 bg-rose-500/10 px-2.5 py-1 text-[11px] text-rose-100 hover:bg-rose-500/20"
              href="/productivity?tab=settings&section=apps"
            >
              Apps →
            </a>
          </div>
        </div>
        {!isFocusDesktopShell() ? (
          <>
            <label className="flex items-center gap-2 text-xs">
              <input
                type="checkbox"
                checked={policy.hard_block_gaming !== false}
                onChange={(e) => setPolicy({ ...policy, hard_block_gaming: e.target.checked })}
              />
              Auto-block Gaming category (Study scoring)
            </label>
            <div>
              <p className="text-[10px] uppercase tracking-wider text-muted-foreground mb-1">
                SoftLand game-bank exes (not OS kill list)
              </p>
              <p className="text-[11px] text-muted-foreground mb-2">
                Study-only scoring helpers. OS kill list is Settings → Blocks → Apps.
              </p>
              <div className="flex flex-wrap gap-1.5 mb-2">
                {(policy.hard_block_exes || []).map((exe) => (
                  <button
                    key={exe}
                    type="button"
                    className="text-[10px] px-2 py-0.5 rounded border border-white/10 hover:border-rose-400/50"
                    onClick={() =>
                      setPolicy({
                        ...policy,
                        hard_block_exes: (policy.hard_block_exes || []).filter((x) => x !== exe),
                      })
                    }
                    title="Remove"
                  >
                    {exe} ×
                  </button>
                ))}
              </div>
              <div className="flex gap-2">
                <input
                  value={newBlockExe}
                  onChange={(e) => setNewBlockExe(e.target.value)}
                  placeholder="e.g. discord.exe"
                  className="flex-1 rounded border border-white/10 bg-black/30 px-2 py-1 text-xs"
                />
                <button
                  type="button"
                  className="px-2 py-1 rounded border border-white/15 text-xs"
                  onClick={() => {
                    const name = newBlockExe.trim();
                    if (!name || !policy) return;
                    const cur = policy.hard_block_exes || [];
                    if (cur.some((x) => x.toLowerCase() === name.toLowerCase())) {
                      setNewBlockExe("");
                      return;
                    }
                    setPolicy({ ...policy, hard_block_exes: [...cur, name] });
                    setNewBlockExe("");
                  }}
                >
                  Add
                </button>
              </div>
            </div>
          </>
        ) : null}
        </>
        )}
      </div>
      )}

      {showScoring && (
      <>
      <label className="flex items-center gap-2 text-xs">
        Productive threshold
        <input
          type="number"
          min={1}
          max={100}
          value={policy.threshold}
          onChange={(e) => setPolicy({ ...policy, threshold: Number(e.target.value) || 60 })}
          className="w-16 rounded border border-white/10 bg-black/30 px-2 py-1"
        />
      </label>

      <div className="grid md:grid-cols-2 gap-4">
        <div>
          <p className="text-[10px] uppercase tracking-wider text-emerald-300/80 mb-2">Counts as productive</p>
          <div className="max-h-48 overflow-y-auto space-y-1 pr-1">
            {allCats.map((cat) => {
              const on = policy.productive_categories.includes(cat);
              return (
                <label key={`p-${cat}`} className="flex items-center gap-2 text-xs cursor-pointer">
                  <input
                    type="checkbox"
                    checked={on}
                    onChange={() => toggleList("productive_categories", cat)}
                  />
                  <span className={on ? "text-emerald-100" : "text-muted-foreground"}>{cat}</span>
                </label>
              );
            })}
          </div>
        </div>
        <div>
          <p className="text-[10px] uppercase tracking-wider text-rose-300/80 mb-2">Blocked (never productive)</p>
          <div className="max-h-48 overflow-y-auto space-y-1 pr-1">
            {allCats.map((cat) => {
              const on = policy.blocked_categories.includes(cat);
              return (
                <label key={`b-${cat}`} className="flex items-center gap-2 text-xs cursor-pointer">
                  <input
                    type="checkbox"
                    checked={on}
                    onChange={() => toggleList("blocked_categories", cat)}
                  />
                  <span className={on ? "text-rose-100" : "text-muted-foreground"}>{cat}</span>
                </label>
              );
            })}
          </div>
        </div>
      </div>

      <div>
        <p className="text-[10px] uppercase tracking-wider text-sky-300/80 mb-2">Category scores (0–100)</p>
        <div className="grid sm:grid-cols-2 gap-2 max-h-40 overflow-y-auto">
          {allCats.slice(0, 24).map((cat) => (
            <label key={`s-${cat}`} className="flex items-center justify-between gap-2 text-[11px]">
              <span className="truncate text-muted-foreground">{cat}</span>
              <input
                type="number"
                min={0}
                max={100}
                value={scores[cat] ?? 35}
                onChange={(e) =>
                  setScores({ ...scores, [cat]: Math.max(0, Math.min(100, Number(e.target.value) || 0)) })
                }
                className="w-14 rounded border border-white/10 bg-black/30 px-1.5 py-0.5"
              />
            </label>
          ))}
        </div>
      </div>

      <div>
        <p className="text-[10px] uppercase tracking-wider text-violet-300/80 mb-2">App overrides (exe / domain → category)</p>
        <div className="flex flex-wrap gap-2 mb-2">
          <input
            value={overrideKey}
            onChange={(e) => setOverrideKey(e.target.value)}
            placeholder="steam.exe or scaler.com"
            className="flex-1 min-w-[140px] rounded border border-white/10 bg-black/30 px-2 py-1 text-xs"
          />
          <select
            value={overrideCat}
            onChange={(e) => setOverrideCat(e.target.value)}
            className="rounded border border-white/10 bg-black/30 px-2 py-1 text-xs"
          >
            {allCats.map((c) => (
              <option key={c} value={c}>
                {c}
              </option>
            ))}
          </select>
          <button
            type="button"
            onClick={addOverride}
            className="px-2 py-1 rounded border border-white/15 text-xs hover:bg-white/5"
          >
            Add
          </button>
        </div>
        <ul className="space-y-1 text-xs">
          {Object.entries(policy.app_overrides).map(([key, cat]) => (
            <li key={key} className="flex items-center justify-between gap-2 rounded border border-white/5 px-2 py-1">
              <span>
                <span className="text-sky-200">{key}</span>
                <span className="text-muted-foreground"> → {cat}</span>
              </span>
              <button
                type="button"
                className="text-rose-300/80 hover:text-rose-200"
                onClick={() => {
                  const next = { ...policy.app_overrides };
                  delete next[key];
                  setPolicy({ ...policy, app_overrides: next });
                }}
              >
                Remove
              </button>
            </li>
          ))}
          {Object.keys(policy.app_overrides).length === 0 && (
            <li className="text-muted-foreground text-[11px]">No app overrides yet.</li>
          )}
        </ul>
      </div>
      </>
      )}

      {showDevice && <DeviceBlockPanel />}
    </div>
  );
}

export default ProductivityPolicyPanel;
