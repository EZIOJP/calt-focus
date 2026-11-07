import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { endOfDay, startOfDay } from "date-fns";
import {
  Check,
  Loader2,
  Shield,
  Target,
  Trash2,
} from "lucide-react";
import {
  deleteDayTask,
  evaluateDayClose,
  setDayTaskDone,
  type DayLoopSnapshot,
  type DayLoopTask,
} from "../../api/dayLoopClient";
import {
  fetchPlannerBlocks,
  type PlannerBlock,
} from "../../api/plannerClient";
import { isFocusEnforcerBridgeAvailable } from "../../lib/enforcerNativeCmd";
import { invalidateFocusStatusBus } from "../../lib/focusStatusBus";
import { useFocusStatus } from "../../hooks/useFocusStatus";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";
import { cn } from "../../app/components/ui/utils";
import { MORNING_UPDATED_EVENT } from "./ConfirmPlanButton";
import { GlanceBar } from "./GlanceBar";
import type { DesktopStats, GoalsStatusResponse } from "../../api/behaviorClient";
import { formatHoursMins } from "../../utils/formatDuration";

type SiteRules = {
  allow_extra: string[];
  watch_extra: string[];
  block_extra: string[];
};

function asList(v: unknown): string[] {
  if (!Array.isArray(v)) return [];
  return v.map((x) => String(x || "").trim().toLowerCase()).filter(Boolean);
}

function pickActiveBlock(blocks: PlannerBlock[], now: Date): PlannerBlock | null {
  const open = blocks.filter(
    (b) => b.status !== "done" && b.status !== "cancelled" && b.status !== "rolled",
  );
  for (const b of open) {
    const start = new Date(b.start_at).getTime();
    const end = new Date(b.end_at).getTime();
    const t = now.getTime();
    if (start <= t && t < end) return b;
  }
  return null;
}

function formatRange(startIso: string, endIso: string): string {
  try {
    const a = new Date(startIso);
    const b = new Date(endIso);
    const opts: Intl.DateTimeFormatOptions = { hour: "numeric", minute: "2-digit" };
    return `${a.toLocaleTimeString([], opts)} – ${b.toLocaleTimeString([], opts)}`;
  } catch {
    return "—";
  }
}

function formatTime(iso: string): string {
  try {
    return new Date(iso).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
  } catch {
    return "—";
  }
}

function dayBounds(day: Date): { start: number; end: number } {
  const start = new Date(day);
  start.setHours(0, 0, 0, 0);
  const end = new Date(day);
  end.setHours(24, 0, 0, 0);
  return { start: start.getTime(), end: end.getTime() };
}

function clamp01(n: number): number {
  return Math.max(0, Math.min(1, n));
}

function WaterFill({
  pct,
  ok,
  variant,
}: {
  pct: number;
  ok: boolean;
  variant: "time" | "tasks";
}) {
  const width = Math.max(0, Math.min(100, pct));
  if (width <= 0) return null;

  const bubbles =
    variant === "time"
      ? [
          { left: "8%", size: 6, dur: "2.8s", delay: "0s" },
          { left: "18%", size: 4, dur: "3.2s", delay: "0.35s" },
          { left: "29%", size: 5.5, dur: "2.5s", delay: "0.8s" },
          { left: "41%", size: 3.5, dur: "3.6s", delay: "0.15s" },
          { left: "52%", size: 7, dur: "2.9s", delay: "1.05s" },
          { left: "63%", size: 4, dur: "3.1s", delay: "0.55s" },
          { left: "74%", size: 5, dur: "2.4s", delay: "1.3s" },
          { left: "84%", size: 3.5, dur: "3.4s", delay: "0.7s" },
          { left: "91%", size: 6, dur: "2.7s", delay: "1.15s" },
        ]
      : [
          { left: "15%", size: 4, dur: "2.6s", delay: "0.2s" },
          { left: "38%", size: 5, dur: "3.1s", delay: "0.65s" },
          { left: "58%", size: 3.5, dur: "2.8s", delay: "1.1s" },
          { left: "78%", size: 4.5, dur: "3.3s", delay: "0.4s" },
        ];

  return (
    <div
      className={cn(
        "day-water",
        variant === "time" ? "day-water--time" : "day-water--tasks",
        ok && "day-water--ok",
      )}
      style={{ width: `${width}%` }}
      aria-hidden
    >
      <div className="day-water__body" />
      <div className="day-water__surface" />
      <div className="day-water__bubbles">
        {bubbles.map((b, i) => (
          <span
            key={i}
            className="day-water__bubble"
            style={
              {
                left: b.left,
                width: b.size,
                height: b.size,
                ["--dur" as string]: b.dur,
                ["--delay" as string]: b.delay,
              } as CSSProperties
            }
          />
        ))}
      </div>
      <div className="day-water__crest" />
    </div>
  );
}

function modeTone(mode: string): { glow: string } {
  const m = mode.toUpperCase();
  if (m === "STUDY") return { glow: "text-sky-100" };
  if (m === "FREE") return { glow: "text-emerald-100" };
  if (m === "BEDTIME") return { glow: "text-violet-100" };
  return { glow: "text-foreground" };
}

function DayBlocksTimeline({
  day,
  blocks,
  activeId,
}: {
  day: Date;
  blocks: PlannerBlock[];
  activeId?: string | number | null;
}) {
  const { start, end } = dayBounds(day);
  const span = Math.max(1, end - start);
  const now = Date.now();
  const nowPct = clamp01((now - start) / span) * 100;
  const visible = blocks
    .filter((b) => b.status !== "cancelled" && b.status !== "rolled")
    .slice()
    .sort((a, b) => new Date(a.start_at).getTime() - new Date(b.start_at).getTime());

  if (!visible.length) {
    return (
      <p className="mt-2.5 text-[11px] text-muted-foreground">
        No planned blocks today — add some in Planner.
      </p>
    );
  }

  return (
    <div className="mt-3 space-y-2">
      <div className="relative h-2.5 rounded-full bg-white/8 overflow-hidden border border-white/10">
        {visible.map((b) => {
          const a = new Date(b.start_at).getTime();
          const z = new Date(b.end_at).getTime();
          const left = clamp01((a - start) / span) * 100;
          const width = Math.max(1.5, clamp01((z - a) / span) * 100);
          const isActive = activeId != null && String(b.id) === String(activeId);
          return (
            <div
              key={b.id}
              title={`${b.title} · ${formatRange(b.start_at, b.end_at)}`}
              className={cn(
                "absolute top-0 bottom-0 rounded-full",
                isActive ? "bg-sky-400/80" : "bg-white/25",
              )}
              style={{ left: `${left}%`, width: `${width}%` }}
            />
          );
        })}
        {now >= start && now <= end ? (
          <div
            className="absolute top-0 bottom-0 w-0.5 bg-amber-300/90"
            style={{ left: `${nowPct}%` }}
            aria-hidden
          />
        ) : null}
      </div>
      <div className="flex gap-1.5 overflow-x-auto pb-0.5">
        {visible.map((b) => {
          const isActive = activeId != null && String(b.id) === String(activeId);
          return (
            <span
              key={b.id}
              className={cn(
                "shrink-0 rounded-md border px-2 py-1 text-[10px]",
                isActive
                  ? "border-sky-400/40 bg-sky-500/15 text-sky-100"
                  : "border-white/10 bg-black/25 text-muted-foreground",
              )}
              title={formatRange(b.start_at, b.end_at)}
            >
              <span className="tabular-nums text-foreground/80">{formatTime(b.start_at)}</span>
              <span className="mx-1 opacity-40">·</span>
              <span className="max-w-[7rem] truncate inline-block align-bottom">{b.title}</span>
            </span>
          );
        })}
      </div>
    </div>
  );
}

type Props = {
  day: Date;
  refreshKey?: number;
  desktop: DesktopStats | null;
  dueReviews: number;
  rangeLabel?: string;
  sleepHours?: number | null;
  sleepScore?: number | null;
  goalsStatus?: GoalsStatusResponse | null;
  onScheduleReview?: () => void;
};

/**
 * Home: current SoftLand mode + today’s stats + goal/distraction cards + tasks.
 */
export default function HomeModeBoard({
  day,
  refreshKey = 0,
  desktop,
  dueReviews,
  rangeLabel = "today",
  sleepHours = null,
  sleepScore = null,
  goalsStatus = null,
  onScheduleReview,
}: Props) {
  const focus = isFocusDesktopShell();
  const status = useFocusStatus();
  const [loop, setLoop] = useState<DayLoopSnapshot | null>(null);
  const [rules, setRules] = useState<SiteRules>({
    allow_extra: [],
    watch_extra: [],
    block_extra: [],
  });
  const [modeLabel, setModeLabel] = useState("—");
  const [active, setActive] = useState<PlannerBlock | null>(null);
  const [dayBlocks, setDayBlocks] = useState<PlannerBlock[]>([]);
  const [loading, setLoading] = useState(false);
  const [hint, setHint] = useState<string | null>(null);
  const [previewTasks, setPreviewTasks] = useState<
    { id: number; title: string; done: boolean }[]
  >(() => {
    try {
      const raw = localStorage.getItem("calt-home-preview-tasks");
      if (raw) {
        const parsed = JSON.parse(raw) as { id: number; title: string; done: boolean }[];
        if (Array.isArray(parsed) && parsed.length) return parsed;
      }
    } catch {
      /* ignore */
    }
    return [
      { id: 1, title: "Deep work block", done: false },
      { id: 2, title: "SRS review", done: false },
      { id: 3, title: "Evening wrap-up", done: false },
    ];
  });
  const previewSeeded = useRef(false);

  // SoftLand + day-loop from shared bus (no per-panel pipe poll).
  useEffect(() => {
    if (!focus) return;
    if (status.dayLoop) setLoop(status.dayLoop);
    const j = status.softland;
    if (!j) return;
    if (j.runtime?.bedtime_active) setModeLabel("BEDTIME");
    else if (j.runtime?.free_until) setModeLabel("FREE");
    else if (j.softland_enabled) setModeLabel("STUDY");
    else setModeLabel("OFF");
    const sr = j.site_rules || {};
    setRules({
      allow_extra: asList(sr.allow_extra),
      watch_extra: asList(sr.watch_extra),
      block_extra: asList(sr.block_extra),
    });
  }, [focus, status.updatedAt, status.dayLoop, status.softland]);

  const refreshPlan = useCallback(async () => {
    if (!focus) return;
    setLoading(true);
    try {
      const blocks = await fetchPlannerBlocks(startOfDay(day), endOfDay(day)).catch(
        () => [] as PlannerBlock[],
      );
      const list = Array.isArray(blocks) ? blocks : [];
      setDayBlocks(list);
      setActive(pickActiveBlock(list, new Date()));
    } finally {
      setLoading(false);
    }
  }, [day, focus]);

  useEffect(() => {
    void refreshPlan();
    // Plan list is heavier — much slower than status bus tick.
    const t = window.setInterval(() => void refreshPlan(), 45_000);
    const onMorning = () => {
      invalidateFocusStatusBus({ pipe: true });
      void refreshPlan();
    };
    window.addEventListener(MORNING_UPDATED_EVENT, onMorning);
    return () => {
      window.clearInterval(t);
      window.removeEventListener(MORNING_UPDATED_EVENT, onMorning);
    };
  }, [refreshPlan, refreshKey]);

  // Design host: prefer today’s planned blocks as the checklist once they load.
  useEffect(() => {
    if (isFocusEnforcerBridgeAvailable()) return;
    if (previewSeeded.current) return;
    const open = dayBlocks.filter(
      (b) => b.status !== "cancelled" && b.status !== "rolled",
    );
    if (!open.length) return;
    previewSeeded.current = true;
    const next = open.map((b) => ({
      id: b.id,
      title: b.title || "Untitled block",
      done: b.status === "done",
    }));
    setPreviewTasks(next);
    try {
      localStorage.setItem("calt-home-preview-tasks", JSON.stringify(next));
    } catch {
      /* ignore */
    }
  }, [dayBlocks]);

  const persistPreviewTasks = (next: { id: number; title: string; done: boolean }[]) => {
    setPreviewTasks(next);
    try {
      localStorage.setItem("calt-home-preview-tasks", JSON.stringify(next));
    } catch {
      /* ignore */
    }
  };

  const goalName = useMemo(() => {
    if (active?.title) return active.title;
    const openTask = (loop?.tasks || []).find((t) => !t.done);
    if (openTask) return openTask.title;
    return "No goal block running";
  }, [active, loop?.tasks]);

  async function toggleTask(t: DayLoopTask) {
    await setDayTaskDone(t.id, !t.done);
    invalidateFocusStatusBus({ pipe: true });
    await evaluateDayClose();
    invalidateFocusStatusBus({ pipe: true });
  }

  async function removeTask(id: number) {
    await deleteDayTask(id);
    invalidateFocusStatusBus({ pipe: true });
  }

  if (!focus) return null;

  const tasks = loop?.tasks || [];
  const tone = modeTone(modeLabel);
  // Prefer the best available source. `??` alone is wrong: a stale loop with
  // tracked_productive_minutes: 0 would hide a live day_rollup of 15+.
  const trackedCandidates: number[] = [];
  if (typeof loop?.tracked_productive_minutes === "number" && Number.isFinite(loop.tracked_productive_minutes)) {
    trackedCandidates.push(Math.max(0, Math.floor(loop.tracked_productive_minutes)));
  }
  if (typeof status.dayRollup?.productive_minutes === "number" && Number.isFinite(status.dayRollup.productive_minutes)) {
    trackedCandidates.push(Math.max(0, Math.floor(status.dayRollup.productive_minutes)));
  } else if (
    typeof status.dayRollup?.productive_seconds === "number" &&
    Number.isFinite(status.dayRollup.productive_seconds)
  ) {
    trackedCandidates.push(Math.max(0, Math.floor(status.dayRollup.productive_seconds / 60)));
  }
  if (desktop && Number.isFinite(desktop.total_seconds)) {
    trackedCandidates.push(Math.max(0, Math.round(desktop.total_seconds / 60)));
  }
  if (desktop && Number.isFinite(desktop.productive_seconds)) {
    trackedCandidates.push(Math.max(0, Math.round(desktop.productive_seconds / 60)));
  }
  const trackedMin = trackedCandidates.length ? Math.max(...trackedCandidates) : null;
  const trackedLabel =
    trackedMin != null && Number.isFinite(trackedMin) ? formatHoursMins(trackedMin) : "—";
  const modeDetail = active
    ? `${goalName} · ${formatRange(active.start_at, active.end_at)}${
        active.category ? ` · ${active.category}` : ""
      }`
    : goalName;

  const planned = loop?.planned_minutes ?? 0;
  const trackedForGate = trackedMin ?? 0;
  const threshold = loop?.threshold_minutes ?? Math.round(planned * 0.5);
  const timeDenom = threshold > 0 ? threshold : planned > 0 ? planned : 0;
  const timeOk = Boolean(loop?.time_ok);
  const checksOk = Boolean(loop?.checkboxes_ok);

  const previewing = !loop && !isFocusEnforcerBridgeAvailable();
  const checklist = previewing ? previewTasks : tasks;
  const taskTotal = previewing
    ? checklist.length
    : (loop?.tasks_total ?? tasks.length);
  const taskDone = previewing
    ? checklist.filter((t) => t.done).length
    : (loop?.tasks_done ?? tasks.filter((t) => t.done).length);
  const taskPct =
    taskTotal > 0 ? Math.min(100, Math.round((taskDone / taskTotal) * 100)) : 0;

  const dailyGoal = goalsStatus?.goals?.[0];
  const goalCurrentMin = previewing
    ? Math.round((dailyGoal?.current_seconds ?? goalsStatus?.productive_seconds ?? 0) / 60)
    : trackedForGate;
  const goalTargetMin = previewing
    ? Math.max(
        1,
        Math.round((dailyGoal?.target_seconds ?? 0) / 60) || timeDenom || 120,
      )
    : timeDenom || planned || 0;
  const timePct = previewing
    ? Math.min(100, Math.round(dailyGoal?.pct ?? (goalCurrentMin / goalTargetMin) * 100))
    : timeDenom > 0
      ? Math.min(100, Math.round((trackedForGate / timeDenom) * 100))
      : 0;
  const gatePct = taskTotal > 0 ? Math.min(timePct, taskPct) : timePct;

  const fillTimePct = timePct;
  const fillTaskPct = taskPct;
  const fillTimeOk = previewing ? fillTimePct >= 100 : timeOk;
  const fillChecksOk = previewing ? taskTotal > 0 && taskDone === taskTotal : checksOk;
  const fillGatePct = previewing ? fillTimePct : gatePct;

  async function onChecklistToggle(id: number, done: boolean) {
    if (previewing) {
      persistPreviewTasks(
        previewTasks.map((t) => (t.id === id ? { ...t, done: !done } : t)),
      );
      return;
    }
    const t = tasks.find((x) => x.id === id);
    if (t) await toggleTask(t);
  }

  return (
    <div className="space-y-4">
      {/* One card: mode + timeline + glance + day progress */}
      <section className="gloss-panel rounded-2xl border border-border/40 overflow-hidden">
        <div className="flex flex-col sm:flex-row sm:items-stretch">
          <div className="min-w-0 flex-1 px-4 pt-3.5 pb-3.5">
            <div className="flex flex-wrap items-baseline justify-between gap-x-3 gap-y-1">
              <div className="flex flex-wrap items-baseline gap-x-3 gap-y-1 min-w-0">
                <h2 className={cn("text-3xl font-semibold tracking-tight leading-none", tone.glow)}>
                  {modeLabel}
                </h2>
                {loop?.free_until ? (
                  <span className="rounded-md border border-emerald-500/25 bg-emerald-500/10 px-2 py-0.5 text-[11px] text-emerald-100">
                    Free until {loop.free_until}
                  </span>
                ) : null}
              </div>
              <div className="flex items-center gap-2 shrink-0">
                {loading ? <Loader2 className="size-3.5 animate-spin text-muted-foreground" /> : null}
                {loop?.plan_confirmed ? (
                  <span className="rounded-full border border-emerald-500/30 bg-emerald-500/10 px-2 py-0.5 text-[10px] text-emerald-100">
                    Plan locked
                  </span>
                ) : null}
              </div>
            </div>

            <p className="mt-2 text-sm text-muted-foreground truncate" title={modeDetail}>
              {modeDetail}
            </p>

            <DayBlocksTimeline day={day} blocks={dayBlocks} activeId={active?.id} />

            {hint ? (
              <p className="mt-1.5 text-[11px] text-amber-100/90">{hint}</p>
            ) : null}
          </div>

          <div className="sm:w-[11.5rem] sm:border-l border-t sm:border-t-0 border-border/40 px-4 py-3.5 flex flex-col justify-center bg-muted/20">
            <p className="text-[10px] uppercase tracking-[0.14em] text-muted-foreground">
              Tracked · {rangeLabel}
            </p>
            <p className="mt-1 text-2xl font-semibold tabular-nums tracking-tight text-sky-100 leading-none">
              {trackedLabel}
            </p>
          </div>
        </div>

        <GlanceBar
          variant="rail"
          hideTracked
          desktop={desktop}
          dueReviews={dueReviews}
          rangeLabel={rangeLabel}
          sleepHours={sleepHours}
          sleepScore={sleepScore}
          goalsStatus={goalsStatus}
          onScheduleReview={onScheduleReview}
        />

        {/* Day progress fill — water L→R with bubbles + crest */}
        <div
          className="relative h-12 border-t border-border/40 bg-black/45 overflow-hidden"
          role="img"
          aria-label={`Day progress ${fillGatePct} percent`}
        >
          <div className="pointer-events-none absolute inset-0 z-0" aria-hidden>
            {[25, 50, 75].map((mark) => (
              <div
                key={mark}
                className="absolute top-0 bottom-0 w-px bg-white/10"
                style={{ left: `${mark}%` }}
              />
            ))}
          </div>
          <WaterFill pct={fillTimePct} ok={fillTimeOk} variant="time" />
          <WaterFill pct={fillTaskPct} ok={fillChecksOk} variant="tasks" />
          <div className="relative z-[1] flex h-full items-center justify-between gap-3 px-3.5">
            <div className="flex items-baseline gap-2 min-w-0">
              <span className="text-[10px] uppercase tracking-[0.16em] text-white/70 drop-shadow">
                Time
              </span>
              <span className="text-sm font-semibold tabular-nums text-white drop-shadow">
                {fillTimePct}%
              </span>
            </div>
            <div className="flex items-baseline gap-2 shrink-0">
              <span className="text-[10px] uppercase tracking-[0.16em] text-white/70 drop-shadow">
                Tasks
              </span>
              <span className="text-sm font-semibold tabular-nums text-white drop-shadow">
                {taskTotal > 0 ? `${fillTaskPct}%` : "—"}
              </span>
            </div>
          </div>
        </div>

        <div className="px-3 py-2.5 border-t border-border/30">
          {isFocusEnforcerBridgeAvailable() ? (
            <div className="flex justify-end mb-1.5">
              <button
                type="button"
                className="rounded-md border border-amber-400/30 bg-amber-500/15 px-2 py-0.5 text-[10px] text-amber-50 hover:bg-amber-500/25"
                onClick={() =>
                    void evaluateDayClose().then((r) => {
                    setHint(
                      r.granted
                        ? `1h free until ${r.free_until || "soon"}`
                        : "Dual gate not met yet (checkboxes + ≥50% planned time).",
                    );
                    invalidateFocusStatusBus({ pipe: true });
                  })
                }
              >
                Check unlock
              </button>
            </div>
          ) : null}

          <ul className="divide-y divide-border/30 rounded-lg border border-border/40 overflow-hidden bg-black/20">
            {checklist.map((t) => (
              <li key={t.id} className="flex items-center gap-2 px-2.5 py-1.5">
                <button
                  type="button"
                  className={cn(
                    "size-4 rounded-[3px] border flex items-center justify-center shrink-0 transition-colors",
                    t.done
                      ? "border-emerald-400/40 bg-emerald-500/15"
                      : "border-border/60 hover:border-foreground/40",
                  )}
                  onClick={() => void onChecklistToggle(t.id, t.done)}
                  aria-label={t.done ? "Mark incomplete" : "Mark done"}
                >
                  {t.done ? <Check className="size-2.5 text-emerald-300" /> : null}
                </button>
                <span
                  className={`flex-1 text-[13px] leading-snug ${t.done ? "line-through text-muted-foreground" : "text-foreground/90"}`}
                >
                  {t.title}
                </span>
                {!previewing &&
                !loop?.plan_confirmed &&
                isFocusEnforcerBridgeAvailable() ? (
                  <button
                    type="button"
                    className="text-muted-foreground hover:text-rose-300"
                    onClick={() => void removeTask(t.id)}
                    aria-label="Delete task"
                  >
                    <Trash2 className="size-3" />
                  </button>
                ) : null}
              </li>
            ))}
            {!checklist.length ? (
              <li className="px-2.5 py-2 text-[11px] text-muted-foreground">
                No tasks yet — planned blocks or day-loop tasks will show here.
              </li>
            ) : null}
          </ul>
        </div>
      </section>

      {/* Blocks — status only; editors live in Settings → Blocks */}
      <section className="gloss-panel rounded-2xl border border-border/40 p-4 space-y-3">
        <div className="flex flex-wrap items-start justify-between gap-2 px-0.5">
          <div>
            <p className="text-[10px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
              Blocks
            </p>
            <p className="mt-0.5 text-[11px] text-muted-foreground">
              SoftLand site lists — edit under Settings → Blocks → Sites
            </p>
          </div>
          <a
            href="/productivity?tab=settings&section=sites"
            className="rounded-lg border border-sky-500/35 bg-sky-500/10 px-2.5 py-1 text-[11px] text-sky-100 hover:bg-sky-500/20 shrink-0"
          >
            Edit lists →
          </a>
        </div>
        <div className="grid gap-2 sm:grid-cols-2">
          <div className="rounded-xl border border-sky-400/25 border-l-[3px] border-l-sky-400/70 bg-muted/15 px-3.5 py-3">
            <div className="flex items-center gap-2">
              <span className="flex size-7 items-center justify-center rounded-lg bg-sky-500/15 text-sky-100">
                <Target className="size-3.5" />
              </span>
              <div className="min-w-0">
                <p className="text-xs font-semibold text-foreground">Watch</p>
                <p className="text-[10px] text-muted-foreground">
                  {rules.watch_extra.length} host{rules.watch_extra.length === 1 ? "" : "s"} · study
                  blocked / free opens
                </p>
              </div>
            </div>
            {rules.watch_extra.length ? (
              <p className="mt-2 truncate font-mono text-[10px] text-muted-foreground">
                {rules.watch_extra.slice(0, 3).join(" · ")}
                {rules.watch_extra.length > 3 ? "…" : ""}
              </p>
            ) : null}
          </div>
          <div className="rounded-xl border border-rose-400/25 border-l-[3px] border-l-rose-400/70 bg-muted/15 px-3.5 py-3">
            <div className="flex items-center gap-2">
              <span className="flex size-7 items-center justify-center rounded-lg bg-rose-500/15 text-rose-100">
                <Shield className="size-3.5" />
              </span>
              <div className="min-w-0">
                <p className="text-xs font-semibold text-foreground">Block</p>
                <p className="text-[10px] text-muted-foreground">
                  {rules.block_extra.length} host{rules.block_extra.length === 1 ? "" : "s"} · survives
                  free
                </p>
              </div>
            </div>
            {rules.block_extra.length ? (
              <p className="mt-2 truncate font-mono text-[10px] text-muted-foreground">
                {rules.block_extra.slice(0, 3).join(" · ")}
                {rules.block_extra.length > 3 ? "…" : ""}
              </p>
            ) : null}
          </div>
        </div>
      </section>
    </div>
  );
}
