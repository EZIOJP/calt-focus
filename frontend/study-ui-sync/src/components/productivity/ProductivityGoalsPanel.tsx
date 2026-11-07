import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { Check, ChevronDown, ChevronUp, Plus, Shield, Target, Trash2 } from "lucide-react";
import type { AdherenceSummary } from "../../api/plannerClient";
import type { StudyTask } from "../../api/plannerClient";
import {
  fetchProductivityPolicy,
  saveProductivityPolicy,
} from "../../api/behaviorClient";
import { cn } from "../../app/components/ui/utils";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";
import { focusDataUrl } from "../../utils/focusDataUrl";
import { enforcerNativeCmd, isFocusEnforcerBridgeAvailable } from "../../lib/enforcerNativeCmd";
import { GoalsTodayPlanBlocks } from "./GoalsTodayPlanBlocks";

/** Fixed system reward — not user-edited copy */
export const IMPLIED_FOCUS_REWARD =
  "Hit today's on-plan focus target — SoftLand watch sites open in free/reward. SoftLand ON ≠ Armed.";

async function loadDailyFocusHours(): Promise<number | null> {
  if (isFocusDesktopShell()) {
    try {
      const r = await fetch(focusDataUrl("softland_policy.json"), { cache: "no-store" });
      if (r.ok) {
        const j = (await r.json()) as { goals?: { daily_focus_minutes?: number } };
        const mins = Number(j.goals?.daily_focus_minutes);
        if (Number.isFinite(mins) && mins > 0) return goalMinutesToFocusHours(mins);
      }
    } catch {
      /* fall through */
    }
    return null;
  }
  try {
    const p = await fetchProductivityPolicy();
    return goalMinutesToFocusHours(p.daily_goal_minutes ?? 240);
  } catch {
    return null;
  }
}

async function saveDailyFocusHours(hours: number): Promise<void> {
  const minutes = focusHoursToGoalMinutes(hours);
  if (isFocusEnforcerBridgeAvailable()) {
    let goalsObj: Record<string, unknown> = {
      daily_focus_minutes: minutes,
      goal_met: false,
      bible_done: false,
    };
    try {
      const r = await fetch(focusDataUrl("softland_policy.json"), { cache: "no-store" });
      if (r.ok) {
        const j = (await r.json()) as { goals?: Record<string, unknown> };
        if (j.goals && typeof j.goals === "object") {
          goalsObj = { ...j.goals, daily_focus_minutes: minutes };
        }
      }
    } catch {
      /* use defaults */
    }
    const res = await enforcerNativeCmd("softland.patch_goals", { goals: goalsObj });
    if (!res?.ok) {
      throw new Error(typeof res?.error === "string" ? res.error : "Enforcer rejected daily focus");
    }
    return;
  }
  if (isFocusDesktopShell()) {
    // Design host / no pipe — localStorage only (already persisted by the panel).
    return;
  }
  await saveProductivityPolicy({ daily_goal_minutes: minutes });
}

const LS_KEY = "productivity:goals:v1";
export const GOALS_UPDATED_EVENT = "productivity:goals-updated";

export type ExtraGoal = {
  id: string;
  title: string;
  done: boolean;
};

export type ProductivityGoals = {
  focusHoursPerDay: number;
  weeklyFocusHours: number;
  mainGoal: string;
  reward: string;
  extraGoals: ExtraGoal[];
  studyTasks: StudyTask[];
  weekKey?: string;
};

const DEFAULT_GOALS: ProductivityGoals = {
  focusHoursPerDay: 4,
  weeklyFocusHours: 24,
  mainGoal: "Complete the Scaler AI/ML course — daily lessons + practice before entertainment.",
  reward: IMPLIED_FOCUS_REWARD,
  extraGoals: [],
  studyTasks: [],
};

function newExtraId(): string {
  return `g-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
}

function newStudyTaskId(): string {
  return `task-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
}

function currentWeekKey(): string {
  const d = new Date();
  const target = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  const dayNum = target.getUTCDay() || 7;
  target.setUTCDate(target.getUTCDate() + 4 - dayNum);
  const yearStart = new Date(Date.UTC(target.getUTCFullYear(), 0, 1));
  const weekNo = Math.ceil(((target.getTime() - yearStart.getTime()) / 86400000 + 1) / 7);
  return `${target.getUTCFullYear()}-W${String(weekNo).padStart(2, "0")}`;
}

function normalizeStudyTasks(raw: unknown): StudyTask[] {
  if (!Array.isArray(raw)) return [];
  return raw
    .filter((t): t is StudyTask => Boolean(t && typeof t === "object"))
    .map((t) => ({
      id: String(t.id || newStudyTaskId()),
      title: String(t.title || "Study").trim(),
      minutes: Math.max(25, Number(t.minutes) || 60),
      allowHosts: Array.isArray(t.allowHosts) ? t.allowHosts.map(String) : [],
      blockCategories: Array.isArray(t.blockCategories) ? t.blockCategories.map(String) : [],
    }))
    .filter((t) => t.title);
}

function FocusHoursHero({
  value,
  min,
  max,
  step,
  effectiveHours,
  plannedHours,
  onChange,
}: {
  value: number;
  min: number;
  max: number;
  step: number;
  effectiveHours: number;
  /** Hours already on today's calendar (all blocks). */
  plannedHours?: number;
  onChange: (n: number) => void;
}) {
  const bump = (dir: 1 | -1) => {
    const next = Math.round((value + dir * step) * 10) / 10;
    onChange(Math.min(max, Math.max(min, next)));
  };
  const pct = value > 0 ? Math.min(100, Math.round((effectiveHours / value) * 100)) : 0;
  const remaining = Math.max(0, value - effectiveHours);
  const met = remaining <= 0 && value > 0;
  const planned = typeof plannedHours === "number" && plannedHours > 0 ? plannedHours : null;

  return (
    <section className="goals-focus-hero">
      <div className="goals-focus-hero__row">
        <div className="min-w-0">
          <p className="goals-focus-hero__label">Daily focus</p>
          <div className="goals-focus-hero__value-row">
            <span className="goals-focus-hero__value tabular-nums">{value}</span>
            <span className="goals-focus-hero__unit">hours</span>
          </div>
        </div>
        <div className="goals-focus-hero__stepper" aria-label="Adjust daily focus hours">
          <button type="button" aria-label="Increase daily focus" onClick={() => bump(1)}>
            <ChevronUp size={14} />
          </button>
          <button type="button" aria-label="Decrease daily focus" onClick={() => bump(-1)}>
            <ChevronDown size={14} />
          </button>
        </div>
      </div>

      <div className="goals-focus-hero__meter" aria-hidden={!value}>
        <div className="goals-focus-hero__bar">
          <div
            className={cn("goals-focus-hero__fill", met && "is-met")}
            style={{ width: `${pct}%` }}
          />
        </div>
        <p className={cn("goals-focus-hero__status", met && "is-met")}>
          {met
            ? "Focus met — Blocker lifts"
            : `${effectiveHours.toFixed(1)} / ${value}h · ${remaining.toFixed(1)}h left`}
          {planned != null ? (
            <span className="text-sky-300/80"> · {planned.toFixed(1)}h study planned</span>
          ) : null}
        </p>
      </div>
    </section>
  );
}

type Props = {
  adherence: AdherenceSummary | null;
  /** Hours already locked by routines / blocks on the selected day */
  lockedHours?: number;
  /** Assumed waking window for capacity hint (default 16h) */
  wakingHours?: number;
  /** Watch sleep score 0–100 for recovery-based capacity hint */
  sleepScore?: number | null;
  onGoalsTextChange?: (text: string) => void;
  /** Fired when the user explicitly Saves — marks the Plan stepper Goals step complete */
  onConfirmed?: () => void;
  /** After apply / plan block changes — refresh calendar mirrors */
  onPlanBlocksChange?: () => void;
  /** Bump when parent reloads planner */
  planRefreshKey?: number;
  /** Switch Productivity tab to Calendar (avoids Router Link crash in Focus). */
  onOpenCalendar?: () => void;
  /** Plan step Confirm / Back / Next — folded into this card */
  footer?: ReactNode;
};

export function focusHoursToGoalMinutes(hours: number): number {
  return Math.min(960, Math.max(15, Math.round(Number(hours) * 60)));
}

export function goalMinutesToFocusHours(mins: number): number {
  return Math.round((Math.max(15, Number(mins) || 240) / 60) * 10) / 10;
}

export function persistProductivityGoals(goals: ProductivityGoals): void {
  localStorage.setItem(LS_KEY, JSON.stringify(goals));
  window.dispatchEvent(new CustomEvent(GOALS_UPDATED_EVENT, { detail: goals }));
}

export function loadProductivityGoals(): ProductivityGoals {
  try {
    const raw = localStorage.getItem(LS_KEY);
    if (!raw) return { ...DEFAULT_GOALS };
    const parsed = { ...DEFAULT_GOALS, ...JSON.parse(raw) } as ProductivityGoals;
    if (!Array.isArray(parsed.extraGoals)) parsed.extraGoals = [];
    parsed.extraGoals = parsed.extraGoals
      .filter((g) => g && typeof g.title === "string")
      .map((g) => ({
        id: g.id || newExtraId(),
        title: g.title,
        done: Boolean(g.done),
      }));
    parsed.studyTasks = normalizeStudyTasks(parsed.studyTasks);
    const wk = currentWeekKey();
    if (parsed.weekKey && parsed.weekKey !== wk) {
      parsed.weekKey = wk;
    } else if (!parsed.weekKey) {
      parsed.weekKey = wk;
    }
    if (
      parsed.mainGoal === "Finish AI/ML and Scaler work before entertainment." ||
      !parsed.mainGoal?.trim()
    ) {
      parsed.mainGoal = DEFAULT_GOALS.mainGoal;
    }
    if (
      !parsed.reward?.trim() ||
      parsed.reward === "Guilt-free break after hitting the focus target." ||
      parsed.reward.startsWith("Unlock games / free time")
    ) {
      parsed.reward = IMPLIED_FOCUS_REWARD;
    }
    return parsed;
  } catch {
    return { ...DEFAULT_GOALS };
  }
}

export function formatGoalsForPrompt(goals: ProductivityGoals): string {
  const extras = goals.extraGoals.filter((g) => g.title.trim());
  const extraBlock =
    extras.length === 0
      ? ""
      : ` Extra goals/todos: ${extras
          .map((g) => `${g.done ? "[done] " : ""}${g.title.trim()}`)
          .join("; ")}.`;
  const tasks = goals.studyTasks || [];
  const taskBlock =
    tasks.length === 0
      ? ""
      : ` Study tasks: ${tasks
          .map((t) => {
            const allow = (t.allowHosts || []).join(", ").slice(0, 120);
            const block = (t.blockCategories || []).join(", ").slice(0, 80);
            return `${t.title} (${t.minutes}m; allow ${allow || "study sites"}; block ${block || "distractions"})`;
          })
          .join(" | ")}.`;
  return `${goals.mainGoal} Daily effective-focus target: ${goals.focusHoursPerDay}h. Weekly target: ${goals.weeklyFocusHours}h. Reward: ${IMPLIED_FOCUS_REWARD}.${extraBlock}${taskBlock}`;
}

export function ProductivityGoalsPanel({
  adherence,
  lockedHours,
  onGoalsTextChange,
  onPlanBlocksChange,
  planRefreshKey = 0,
  onOpenCalendar,
  footer,
}: Props) {
  const [goals, setGoals] = useState<ProductivityGoals>(() => loadProductivityGoals());
  const [saveError, setSaveError] = useState<string | null>(null);
  const [draftExtra, setDraftExtra] = useState("");
  const [focusHydrated, setFocusHydrated] = useState(false);
  const lastSavedFocusHours = useRef<number | null>(null);

  const effectiveHours = (adherence?.effective_focus_minutes ?? 0) / 60;

  const goalsText = useMemo(() => formatGoalsForPrompt(goals), [goals]);

  useEffect(() => {
    onGoalsTextChange?.(goalsText);
  }, [goalsText, onGoalsTextChange]);

  useEffect(() => {
    const onUpdated = () => setGoals(loadProductivityGoals());
    window.addEventListener(GOALS_UPDATED_EVENT, onUpdated);
    return () => window.removeEventListener(GOALS_UPDATED_EVENT, onUpdated);
  }, []);

  useEffect(() => {
    let cancelled = false;
    void loadDailyFocusHours().then((hours) => {
      if (cancelled) return;
      if (hours != null) {
        lastSavedFocusHours.current = hours;
        setGoals((prev) =>
          prev.focusHoursPerDay === hours ? prev : { ...prev, focusHoursPerDay: hours },
        );
      } else {
        lastSavedFocusHours.current = loadProductivityGoals().focusHoursPerDay;
      }
      setFocusHydrated(true);
    });
    return () => {
      cancelled = true;
    };
  }, []);

  // Local persist on any edit (silent — avoid GOALS_UPDATED echo loop).
  useEffect(() => {
    localStorage.setItem(
      LS_KEY,
      JSON.stringify({ ...goals, reward: IMPLIED_FOCUS_REWARD }),
    );
  }, [goals]);

  useEffect(() => {
    if (!focusHydrated) return;
    if (lastSavedFocusHours.current === goals.focusHoursPerDay) return;
    const t = window.setTimeout(() => {
      void (async () => {
        try {
          await saveDailyFocusHours(goals.focusHoursPerDay);
          lastSavedFocusHours.current = goals.focusHoursPerDay;
          setSaveError(null);
        } catch (e: unknown) {
          setSaveError(e instanceof Error ? e.message : "Could not update daily focus");
        }
      })();
    }, 450);
    return () => window.clearTimeout(t);
  }, [goals.focusHoursPerDay, focusHydrated]);

  const addExtra = () => {
    const title = draftExtra.trim();
    if (!title) return;
    setGoals({
      ...goals,
      extraGoals: [...goals.extraGoals, { id: newExtraId(), title, done: false }],
    });
    setDraftExtra("");
  };

  return (
    <div className="goals-panel space-y-5">
      <header className="flex items-center justify-between gap-3">
        <h3 className="font-semibold text-sm flex items-center gap-2">
          <Target size={15} className="text-emerald-300" aria-hidden />
          Goals
        </h3>
        {saveError ? <p className="text-[11px] text-rose-300">{saveError}</p> : null}
      </header>

      <FocusHoursHero
        value={goals.focusHoursPerDay}
        min={0.5}
        max={16}
        step={0.5}
        effectiveHours={effectiveHours}
        plannedHours={lockedHours}
        onChange={(n) => setGoals({ ...goals, focusHoursPerDay: n })}
      />

      <GoalsTodayPlanBlocks
        focusHours={goals.focusHoursPerDay}
        studyTasks={goals.studyTasks}
        refreshKey={planRefreshKey}
        onChanged={onPlanBlocksChange}
        onOpenCalendar={onOpenCalendar}
      />

      {isFocusDesktopShell() ? (
        <section className="rounded-xl border border-border/50 bg-card/20 p-3 space-y-1.5">
          <div className="flex items-center gap-2">
            <Shield size={13} className="text-sky-300 shrink-0" aria-hidden />
            <h4 className="text-[11px] font-semibold text-foreground">Blocker lists</h4>
          </div>
          <p className="text-[11px] text-muted-foreground leading-relaxed">
            Site allow/watch/block and Arm kill lists live under Settings → Blocks. SoftLand ON ≠
            Armed.
          </p>
          <div className="flex flex-wrap gap-2 pt-0.5">
            <a
              href="/productivity?tab=settings&section=sites"
              className="rounded-lg border border-sky-500/35 bg-sky-500/10 px-2.5 py-1 text-[11px] text-sky-100 hover:bg-sky-500/20"
            >
              Sites →
            </a>
            <a
              href="/productivity?tab=settings&section=apps"
              className="rounded-lg border border-rose-500/35 bg-rose-500/10 px-2.5 py-1 text-[11px] text-rose-100 hover:bg-rose-500/20"
            >
              Apps →
            </a>
          </div>
        </section>
      ) : null}

      <section className="goals-todos space-y-2">
        <div className="flex items-center justify-between gap-2">
          <h4 className="text-xs font-semibold text-foreground">Todos</h4>
          {goals.extraGoals.filter((g) => !g.done && g.title.trim()).length > 0 ? (
            <span className="text-[10px] text-muted-foreground tabular-nums">
              {goals.extraGoals.filter((g) => !g.done && g.title.trim()).length} open
            </span>
          ) : null}
        </div>
        <ul className="space-y-1">
          {goals.extraGoals.length === 0 ? (
            <li className="text-[11px] text-muted-foreground py-0.5">Optional side goals</li>
          ) : (
            goals.extraGoals.map((g) => (
              <li key={g.id} className="flex items-center gap-2 group">
                <button
                  type="button"
                  onClick={() =>
                    setGoals({
                      ...goals,
                      extraGoals: goals.extraGoals.map((x) =>
                        x.id === g.id ? { ...x, done: !x.done } : x,
                      ),
                    })
                  }
                  className={cn(
                    "h-5 w-5 rounded border flex items-center justify-center shrink-0",
                    g.done
                      ? "border-emerald-500/60 bg-emerald-500/30 text-emerald-200"
                      : "border-white/20 hover:border-white/40",
                  )}
                  aria-label={g.done ? "Mark open" : "Mark done"}
                >
                  {g.done ? <Check size={12} /> : null}
                </button>
                <input
                  value={g.title}
                  onChange={(e) =>
                    setGoals({
                      ...goals,
                      extraGoals: goals.extraGoals.map((x) =>
                        x.id === g.id ? { ...x, title: e.target.value } : x,
                      ),
                    })
                  }
                  className={cn(
                    "flex-1 min-w-0 rounded border border-transparent bg-transparent px-1.5 py-1 text-xs text-foreground focus:border-white/15 focus:bg-black/30",
                    g.done && "line-through text-muted-foreground",
                  )}
                />
                <button
                  type="button"
                  onClick={() =>
                    setGoals({
                      ...goals,
                      extraGoals: goals.extraGoals.filter((x) => x.id !== g.id),
                    })
                  }
                  className="opacity-0 group-hover:opacity-100 p-1 text-muted-foreground hover:text-rose-300"
                  aria-label="Remove goal"
                >
                  <Trash2 size={12} />
                </button>
              </li>
            ))
          )}
        </ul>
        <div className="flex gap-2">
          <input
            value={draftExtra}
            onChange={(e) => setDraftExtra(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter") {
                e.preventDefault();
                addExtra();
              }
            }}
            placeholder="Add a todo…"
            className="flex-1 rounded-lg border border-white/10 bg-black/25 px-2.5 py-1.5 text-xs text-foreground"
          />
          <button
            type="button"
            onClick={addExtra}
            disabled={!draftExtra.trim()}
            className="inline-flex items-center gap-1 rounded-lg border border-white/12 px-2.5 py-1.5 text-xs text-muted-foreground hover:text-foreground hover:bg-white/5 disabled:opacity-40"
          >
            <Plus size={12} /> Add
          </button>
        </div>
      </section>

      {footer}
    </div>
  );
}

export default ProductivityGoalsPanel;
