import { useEffect } from "react";
import {
  HashRouter,
  BrowserRouter,
  Routes,
  Route,
  Navigate,
  Outlet,
} from "react-router";
import { ThemeProvider } from "@/context/ThemeContext";
import { AuthProvider } from "@/context/AuthContext";
import { PomodoroProvider } from "@/context/PomodoroContext";
import { AppErrorBoundary } from "@/components/layout/AppErrorBoundary";
import { DashboardChromeProvider } from "@/context/DashboardChromeContext";
import { AppSidebar } from "@/layout/AppSidebar";
import { MorningGateRedirect } from "@/components/MorningGateRedirect";
import MorningBibleOverlay from "@/components/productivity/MorningBibleOverlay";
import MorningPlanOverlay from "@/components/productivity/MorningPlanOverlay";
import { installFocusStatusPushListener } from "@/lib/focusStatusBus";
import { useEmailDailyReport } from "@/hooks/useEmailDailyReport";
import { isFocusDesktopShell } from "@/utils/focusDesktopShell";
import { ProductivityPage } from "@/pages/ProductivityPage";
import FocusPage from "@/pages/FocusPage";
import { JournalPage } from "@/pages/JournalPage";
import { BibleReaderPage } from "@/pages/bible/BibleReaderPage";
import { LifeTrackerPage } from "@/pages/LifeTrackerPage";
import { CurvedScrollArea } from "@/components/ui/CurvedScrollArea";
import { BodyCareRuntime } from "@/components/productivity/BodyCareRuntime";
import { BodyCareVisionHost } from "@/components/productivity/BodyCareVisionHost";
import { PlanningPrefsSheet } from "@/components/productivity/PlanningPrefsSheet";
import { PlannerRemindersRuntime } from "@/components/productivity/PlannerRemindersRuntime";

/** Lean chrome: SoftLand≠Arm shell — no Math / Nutri / Study plugins. */
function FocusShell() {
  return (
    <DashboardChromeProvider>
      <BodyCareVisionHost>
        <div className="focus-shell-root relative flex h-screen w-screen overflow-hidden bg-background">
          <BodyCareRuntime />
          <PlannerRemindersRuntime />
          <MorningGateRedirect />
          <PlanningPrefsSheet />
          <AppSidebar />
          <div className="flex min-w-0 flex-1 flex-col min-h-0">
            <MorningBibleOverlay />
            <MorningPlanOverlay />
            <main className="focus-shell-main flex min-h-0 flex-1 flex-col">
              <CurvedScrollArea
                className="min-h-0 flex-1"
                tone="accent"
                radius={24}
                contentClassName="curved-scrollbar__content--focus-shell"
              >
                <Outlet />
              </CurvedScrollArea>
            </main>
          </div>
        </div>
      </BodyCareVisionHost>
    </DashboardChromeProvider>
  );
}

function FocusRoutes() {
  return (
    <Routes>
      <Route element={<FocusShell />}>
        <Route index element={<Navigate to="/productivity?tab=home" replace />} />
        <Route path="productivity" element={<ProductivityPage />} />
        <Route path="productivity/focus" element={<FocusPage />} />
        <Route path="journal" element={<JournalPage />} />
        <Route path="bible" element={<BibleReaderPage />} />
        <Route path="life" element={<LifeTrackerPage />} />
        <Route path="*" element={<Navigate to="/productivity?tab=home" replace />} />
      </Route>
    </Routes>
  );
}

/**
 * Focus-only SPA entry — no plugin registry / GRE / math / mermaid / Nutri.
 * Tray + calt.app use HashRouter; design host :5180 uses BrowserRouter.
 */
export function FocusApp() {
  const useHash =
    typeof window !== "undefined" &&
    window.location.port !== "5180" &&
    isFocusDesktopShell();
  const Router = useHash ? HashRouter : BrowserRouter;

  useEffect(() => installFocusStatusPushListener(), []);
  useEmailDailyReport(true);

  useEffect(() => {
    document.documentElement.setAttribute("data-focus-shell", "1");
    if (!document.documentElement.getAttribute("data-typography-pack")) {
      document.documentElement.setAttribute("data-typography-pack", "hero");
    }
    return () => {
      document.documentElement.removeAttribute("data-focus-shell");
    };
  }, []);

  return (
    <ThemeProvider>
      <AuthProvider>
        <PomodoroProvider>
          <Router>
            <AppErrorBoundary>
              <FocusRoutes />
            </AppErrorBoundary>
          </Router>
        </PomodoroProvider>
      </AuthProvider>
    </ThemeProvider>
  );
}
