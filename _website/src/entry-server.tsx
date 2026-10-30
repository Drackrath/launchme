import { StrictMode } from "react";
import { renderToString } from "react-dom/server";
import { App } from "./App";
import { dictionaries, routeDepth, type Lang, type LayoutId } from "./lib/page";
import type { Release } from "./lib/release";

export const routes: { lang: Lang; layout: LayoutId }[] = [
  { lang: "en", layout: "v1" },
  { lang: "pl", layout: "v1" },
  { lang: "en", layout: "v2" },
  { lang: "pl", layout: "v2" },
];

export function render(lang: Lang, layout: LayoutId, release: Release) {
  const page = { lang, layout, depth: routeDepth(lang, layout) };
  const html = renderToString(
    <StrictMode>
      <App page={page} release={release} />
    </StrictMode>,
  );
  const t = dictionaries[lang];
  return { html, page, title: t.meta.title, description: t.meta.description };
}

export { fetchRelease, FALLBACK_RELEASE } from "./lib/release";
