import data from "../data/assets.json";
import gameData from "../data/games.json";

export type ScreenId = keyof typeof data.screens;
export const screens = data.screens as Record<string, { w: number; h: number; color: string }>;

// Hand-tuned glows: the extracted colours are close, these read better behind the page.
export const ambient: Record<string, string> = {
  shelves: "#8a6a2a",
  psfive: "#a3392f",
  xmb: "#5a6e3a",
  metro: "#2b62c4",
  flix: "#7a3fb0",
  snitch: "#d43b3b",
  desktop: "#a64130",
  "desktop-settings": "#304ca6",
  layouts: "#6b5a3a",
  "list-plus": "#1f9aa0",
  shelf: "#3d5f8f",
  details: "#a3283a",
  search: "#2f5f8a",
  widgets: "#8f3a2e",
  glance: "#b85a3c",
  companion: "#a04a6a",
};

// Games drawn in the mockups, with the SteamGridDB art the launcher cached (scripts/import_art.py).
export type Game = {
  id: string;
  app: string;
  name: string;
  year: number | null;
  genres: { en: string[]; pl: string[] };
  developer: string;
  desc: { en: string; pl: string };
  time: string;
  ach: [number, number];
  accent: string;
};
export const games = gameData as Game[];
export const gameById = Object.fromEntries(games.map((game) => [game.id, game])) as Record<string, Game>;
export const ids = games.map((game) => game.id);

export type ArtKind = "c" | "p" | "l" | "h" | "g";
/** Path under public/ of one game's art: c cover, p poster, l landscape, h hero, g logo. */
export const art = (kind: ArtKind, id: string) => `img/art/${kind}/${id}.webp`;

/** A stable fake Steam score for the mockups. */
export function rating(id: string) {
  let hash = 0;
  for (const char of id) hash = (hash * 31 + char.charCodeAt(0)) >>> 0;
  return 78 + (hash % 20);
}
