import { useEffect } from "react";
import {
  HashRouter,
  BrowserRouter,
  Routes,
  Route,
  Navigate,
  Outlet,
  useLocation,
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
import { MathRoadmapPage } from "@/pages/MathRoadmapPage";
import { LifeTrackerPage } from "@/pages/LifeTrackerPage";
import { NutritionPage } from "@/plugins/nutrinode/NutritionPage";
import { NutritionProvider } from "@/plugins/nutrinode/NutritionContext";
import { CurvedScrollArea } from "@/components/ui/CurvedScrollArea";

/** Lean chrome: no Study presence / data-pipeline / Face overlay graph. */
function FocusShell() {
  const { pathname } = useLocation();
  const onMath = pathname === "/math" || pathname.endsWith("/math");

  return (
    <DashboardChromeProvider>
      <div className="relative flex h-screen w-screen overflow-hidden bg-background">
        <MorningGateRedirect />
        <MorningBibleOverlay />
        <MorningPlanOverlay />
        <AppSidebar />
        <div className="flex min-w-0 flex-1 flex-col min-h-0">
          <main className="flex min-h-0 flex-1 flex-col">
            <CurvedScrollArea
              className="min-h-0 flex-1"
              tone="accent"
              radius={24}
              contentClassName="curved-scrollbar__content--focus-shell"
            >
              {/* Keep Math iframe mounted so leaving /math does not reload the companion. */}
              <div className={onMath ? "hidden" : undefined}>
                <Outlet />
              </div>
              <div
                className={onMath ? "min-h-[26rem]" : "hidden"}
                aria-hidden={!onMath}
              >
                <MathRoadmapPage />
              </div>
            </CurvedScrollArea>
          </main>
        </div>
      </div>
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
        {/* Math is kept alive in FocusShell — empty outlet stub */}
        <Route path="math" element={<span className="sr-only">Math → ML</span>} />
        <Route path="life" element={<LifeTrackerPage />} />
        <Route
          path="nutrition"
          element={
            <NutritionProvider>
              <NutritionPage />
            </NutritionProvider>
          }
        />
        <Route path="*" element={<Navigate to="/productivity?tab=home" replace />} />
      </Route>
    </Routes>
  );
}

/**
 * Focus-only SPA entry — no plugin registry / GRE / math / mermaid.
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
