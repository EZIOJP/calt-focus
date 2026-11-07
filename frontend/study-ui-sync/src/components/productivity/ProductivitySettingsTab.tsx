import type { ReactNode } from "react";
import { Download, Loader2, Terminal } from "lucide-react";
import { DemoModePanel } from "./DemoModePanel";
import { WearablesSyncPanel } from "./WearablesSyncPanel";
import { PlannerRemindersPanel } from "./PlannerRemindersPanel";
import { PlanningSettingsPanel } from "./PlanningSettingsPanel";
import { SessionOverridePanel } from "./SessionOverridePanel";
import { ActivitiesPanel } from "./ActivitiesPanel";
import ClassificationReview from "./ClassificationReview";
import { ExportRangeCalendar } from "./ExportRangeCalendar";
import { DesktopManagedBanner } from "./DesktopManagedBanner";
import MakeSettingsApp, {
  parseSettingsSection,
  type SettingsSectionId,
} from "./settings/make/MakeSettingsApp";
import type { DesktopTimeline } from "../../api/behaviorClient";
import { isFocusDesktopShell } from "../../utils/focusDesktopShell";

export type ProductivitySettingsTabProps = {
  section: SettingsSectionId;
  onSectionChange: (id: SettingsSectionId) => void;
  plannerRefresh: number;
  bumpPlanner: () => void;
  onPolicySaved: () => void;
  timeline: DesktopTimeline | null;
  plannerDayApi: string;
  trackerNoData: boolean;
  onLoad: () => void;
  onDemoChanged: () => void;
  onJumpToDay: (day: Date) => void;
  exportHint: string | null;
  exportDays: number;
  exportStartDay: string;
  exportEndDay: string;
  exporting: boolean;
  applyExportWindow: (start: string, end: string) => void;
  setExportDaysWindow: (days: number, end: string) => void;
  exportWeek: (fmt: "json" | "csv" | "csv_wide") => void;
  toApiDay: (d: Date) => string;
};

export function ProductivitySettingsTab(props: ProductivitySettingsTabProps) {
  const focusShell = isFocusDesktopShell();
  const productiveExtra = (
    <div className="space-y-4">
      {!focusShell ? (
        <>
          <DesktopManagedBanner feature="Session override & classification" />
          <div className="rounded-2xl border border-border/50 bg-card/30 p-4 sm:p-6">
            <SessionOverridePanel
              timeline={props.timeline}
              onSaved={() => {
                props.onLoad();
                props.bumpPlanner();
              }}
            />
          </div>
        </>
      ) : (
        <p className="text-[11px] text-muted-foreground leading-relaxed px-0.5">
          Session override and LLM classification stay in Study. Focus shows today’s activity mirror
          below — SoftLand / Arm lists stay under Blocks.
        </p>
      )}
      <div className="rounded-2xl border border-border/50 bg-card/30 p-4 sm:p-6">
        <ActivitiesPanel
          day={props.plannerDayApi}
          trackerNoData={props.trackerNoData}
          refreshKey={props.plannerRefresh}
        />
      </div>
      {!focusShell ? (
        <div className="rounded-2xl border border-border/50 bg-card/30 p-4 sm:p-6">
          <ClassificationReview trackerNoData={props.trackerNoData} />
        </div>
      ) : null}
    </div>
  );

  const toolsContent: ReactNode = (
    <div className="space-y-6">
      <div className="space-y-2">
        <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">Planning</h3>
        <PlanningSettingsPanel
          refreshKey={props.plannerRefresh}
          onPlannerChange={props.bumpPlanner}
        />
      </div>
      {!focusShell ? (
        <div className="space-y-2">
          <h3 className="text-xs font-semibold uppercase tracking-wider text-amber-200/90">Demo mode</h3>
          <p className="text-[11px] text-muted-foreground">
            Advanced: fake the SoftLand schedule clock for testing. Leave off for real days.
          </p>
          <DemoModePanel onChanged={props.onDemoChanged} onJumpToDay={props.onJumpToDay} />
        </div>
      ) : null}
      <div className="space-y-2">
        <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">Watch ↔ PC</h3>
        <WearablesSyncPanel />
      </div>
      <div className="space-y-2">
        <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">Plan reminders</h3>
        <PlannerRemindersPanel />
      </div>
      <div className="space-y-3">
        <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground flex items-center gap-2">
          <Download size={14} /> Export
        </h3>
        {props.exportHint && (
          <span className="inline-block max-w-full truncate rounded-full border border-sky-500/25 bg-sky-500/10 px-3 py-1 text-xs text-sky-300">
            {props.exportHint}
          </span>
        )}
        <div className="flex flex-wrap gap-1.5">
          {([7, 30, 90, 365] as const).map((days) => (
            <button
              key={days}
              type="button"
              onClick={() => props.setExportDaysWindow(days, props.toApiDay(new Date()))}
              className={`rounded-full border px-3 py-1 text-xs ${
                props.exportDays === days
                  ? "border-primary/30 bg-primary/10 text-primary"
                  : "border-border/50 bg-background/35 text-muted-foreground"
              }`}
            >
              {days === 365 ? "Year" : `${days}d`}
            </button>
          ))}
        </div>
        <div className="grid grid-cols-2 gap-2 max-w-md">
          <label className="block space-y-1 text-xs text-muted-foreground">
            <span>From</span>
            <input
              type="date"
              value={props.exportStartDay}
              onChange={(e) => props.applyExportWindow(e.target.value, props.exportEndDay)}
              className="w-full rounded-lg border border-border/50 bg-background/50 px-2 py-1.5 text-xs"
            />
          </label>
          <label className="block space-y-1 text-xs text-muted-foreground">
            <span>To</span>
            <input
              type="date"
              value={props.exportEndDay}
              onChange={(e) => props.applyExportWindow(props.exportStartDay, e.target.value)}
              className="w-full rounded-lg border border-border/50 bg-background/50 px-2 py-1.5 text-xs"
            />
          </label>
        </div>
        <ExportRangeCalendar
          startIso={props.exportStartDay}
          endIso={props.exportEndDay}
          onRangeChange={(start, end) => props.applyExportWindow(start, end)}
        />
        <p className="text-[11px] text-muted-foreground leading-relaxed max-w-lg">
          JSON includes nested <code className="text-[10px] bg-black/30 px-1 rounded">by_day</code> plus
          tidy <code className="text-[10px] bg-black/30 px-1 rounded">tables</code> (sessions, day×hour,
          categories, blocks). CSV is long-format multi-table for pandas/R/Sheets.
        </p>
        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            disabled={props.exporting}
            onClick={() => void props.exportWeek("json")}
            className="flex items-center gap-1.5 rounded-lg border border-border/50 bg-background/35 px-3 py-2 text-xs hover:bg-accent/50 disabled:opacity-50"
          >
            {props.exporting ? <Loader2 size={13} className="animate-spin" /> : <Download size={13} />}
            JSON
          </button>
          <button
            type="button"
            disabled={props.exporting}
            onClick={() => void props.exportWeek("csv")}
            className="flex items-center gap-1.5 rounded-lg border border-border/50 bg-background/35 px-3 py-2 text-xs hover:bg-accent/50 disabled:opacity-50"
          >
            <Download size={13} /> CSV (long)
          </button>
          <button
            type="button"
            disabled={props.exporting}
            onClick={() => void props.exportWeek("csv_wide")}
            className="flex items-center gap-1.5 rounded-lg border border-border/50 bg-background/35 px-3 py-2 text-xs hover:bg-accent/50 disabled:opacity-50"
          >
            <Download size={13} /> CSV (wide)
          </button>
        </div>
      </div>
      <div className="space-y-2 rounded-xl border border-border/50 bg-background/35 p-4">
        <h3 className="font-semibold flex items-center gap-2 text-sm">
          <Terminal size={15} className="text-sky-400" />
          Setup
        </h3>
        <p className="text-[11px] text-muted-foreground leading-relaxed">
          Edge SelfTracker + CALT Gate for SoftLand.{" "}
          <code className="text-[10px] bg-black/40 px-1 rounded">
            scripts\desktop_tracker\run\run_calt_desktop.bat
          </code>{" "}
          for Focus + enforcer. Arm lives under <strong className="text-foreground/80">Unlock</strong>.
        </p>
      </div>
    </div>
  );

  return (
    <div className="gloss-panel rounded-3xl border border-border/50 overflow-hidden min-h-[70vh]">
      <MakeSettingsApp
        section={props.section}
        onSectionChange={props.onSectionChange}
        onPolicySaved={props.onPolicySaved}
        productiveExtra={productiveExtra}
        toolsContent={toolsContent}
      />
    </div>
  );
}

export { parseSettingsSection };
export type { SettingsSectionId };
