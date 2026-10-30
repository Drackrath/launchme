import { createContext, useContext, type ReactNode } from "react";
import { en, type Dict } from "../content/en";
import { pl } from "../content/pl";
import type { Release } from "./release";

export type Lang = "en" | "pl";
export type LayoutId = "v1" | "v2";

export type PageInfo = {
  lang: Lang;
  layout: LayoutId;
  /** Folder depth of the page below the site root (0 = /index.html, 2 = /pl/v2/index.html). */
  depth: number;
};

export type PageValue = PageInfo & {
  t: Dict;
  release: Release;
  /** Relative prefix to the site root ("./", "../", "../../"), or "/" in the dev server. */
  root: string;
  asset: (path: string) => string;
  href: (lang: Lang, layout: LayoutId) => string;
};

export const dictionaries: Record<Lang, Dict> = { en, pl };

export function routePath(lang: Lang, layout: LayoutId): string {
  return `${lang === "pl" ? "pl/" : ""}${layout === "v2" ? "v2/" : ""}`;
}

export function routeDepth(lang: Lang, layout: LayoutId): number {
  return (lang === "pl" ? 1 : 0) + (layout === "v2" ? 1 : 0);
}

export function makePage(info: PageInfo, release: Release, dev = false): PageValue {
  const root = dev ? "/" : info.depth === 0 ? "./" : "../".repeat(info.depth);
  return {
    ...info,
    t: dictionaries[info.lang],
    release,
    root,
    asset: (path) => root + path.replace(/^\//, ""),
    href: (lang, layout) => {
      const target = routePath(lang, layout);
      return dev ? "/" + target : (root === "./" ? "./" : root) + target;
    },
  };
}

const PageContext = createContext<PageValue | null>(null);

export function PageProvider({ value, children }: { value: PageValue; children: ReactNode }) {
  return <PageContext.Provider value={value}>{children}</PageContext.Provider>;
}

export function usePage(): PageValue {
  const value = useContext(PageContext);
  if (!value) throw new Error("PageProvider missing");
  return value;
}
