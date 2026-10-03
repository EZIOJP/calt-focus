import { createRoot } from "react-dom/client";
import { FocusApp } from "./FocusApp";
import { installClientErrorLogger } from "@/api/systemClient";
import "@/styles/index.css";

installClientErrorLogger();

createRoot(document.getElementById("root")!).render(<FocusApp />);
