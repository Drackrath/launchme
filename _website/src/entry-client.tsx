import { StrictMode } from "react";
import { createRoot, hydrateRoot } from "react-dom/client";
import { App } from "./App";
import type { PageInfo } from "./lib/page";
import { FALLBACK_RELEASE, type Release } from "./lib/release";
import "./styles.css";

declare global {
  interface Window {
    __LAUNCHME__?: { page: PageInfo; release: Release };
  }
}

const root = document.getElementById("root")!;
const baked = window.__LAUNCHME__;

if (baked && root.hasChildNodes()) {
  hydrateRoot(
    root,
    <StrictMode>
      <App page={baked.page} release={baked.release} />
    </StrictMode>,
  );
} else {
  // Dev server: the route comes from the address bar (/, /pl/, /v2/, /pl/v2/).
  const path = location.pathname;
  const lang = /(^|\/)pl(\/|$)/.test(path) ? "pl" : "en";
  const layout = /(^|\/)v2(\/|$)/.test(path) ? "v2" : "v1";
  createRoot(root).render(
    <StrictMode>
      <App page={{ lang, layout, depth: 0 }} release={FALLBACK_RELEASE} dev />
    </StrictMode>,
  );
}
