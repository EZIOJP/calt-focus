import { useEffect } from "react";
import { HashRouter, BrowserRouter, Routes, Route, Navigate, Outlet } from "react-router";
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
import { isFocusDesktopShell } from "@/utils/focusDesktopShell";
import { ProductivityPage } from "@/pages/ProductivityPage";
import FocusPage from "@/pages/FocusPage";
import { JournalPage } from "@/pages/JournalPage";
import { BibleReaderPage } from "@/pages/bible/BibleReaderPage";
import { CurvedScrollArea } from "@/components/ui/CurvedScrollArea";

/** Lean chrome: no Study presence / data-pipeline / Face overlay graph. */
function FocusShell() {
  return (
    <DashboardChromeProvider>
      <div className="relative flex h-screen w-screen overflow-hidden bg-background">
        <MorningGateRedirect />
        <MorningBibleOverlay />
        <MorningPlanOverlay />
        {/* Sidebar overlays; pages use full width (no reserved rail inset). */}
        <AppSidebar />
        <div className="flex min-h-0 min-w-0 flex-1 flex-col">
          <main className="flex min-h-0 flex-1 flex-col">
            <CurvedScrollArea
              className="min-h-0 flex-1"
              tone="accent"
              radius={24}
              contentClassName="curved-scrollbar__content--flush px-3 py-4 sm:px-4 sm:py-5"
            >
              <Outlet />
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
