// Renders every route of the site into static HTML (dist/, dist/pl/, dist/v2/, dist/pl/v2/).
// The newest GitHub release is fetched now and baked in; the browser refreshes it later.
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dist = path.join(root, "dist");
const server = await import(pathToFileURL(path.join(root, ".ssr", "entry-server.js")).href);
const template = await readFile(path.join(dist, "index.html"), "utf8");
// Absolute address of the published site: social previews and canonical links need full URLs.
const site = (process.env.SITE_URL ?? "https://uxandai.github.io/launchme/").replace(/\/?$/, "/");
// In CI the workflow token lifts the GitHub API rate limit.
const auth = process.env.GITHUB_TOKEN ? { Authorization: `Bearer ${process.env.GITHUB_TOKEN}` } : {};

let release = server.FALLBACK_RELEASE;
try {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  release = await server.fetchRelease((url, init) => fetch(url, { ...init, signal: controller.signal, headers: { ...(init?.headers ?? {}), ...auth, "User-Agent": "launchme-www" } }));
  clearTimeout(timer);
  if (release.stars == null) release.stars = server.FALLBACK_RELEASE.stars;
  console.log(`release: ${release.tag} (${release.windows?.name ?? "no windows asset"}, ${release.linux?.name ?? "no linux asset"}), stars ${release.stars}`);
} catch (error) {
  console.warn("release: GitHub API unavailable, using the snapshot:", error.message);
}

const escape = (value) => value.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
const routePath = (lang, layout) => `${lang === "pl" ? "pl/" : ""}${layout === "v2" ? "v2/" : ""}`;

for (const { lang, layout } of server.routes) {
  const { html, page, title, description } = server.render(lang, layout, release);
  const prefix = page.depth === 0 ? "./" : "../".repeat(page.depth);
  const alternates = ["en", "pl"]
    .map((other) => `<link rel="alternate" hreflang="${other}" href="${site}${routePath(other, layout)}" />`)
    .join("\n    ");
  const head = [
    `<meta name="description" content="${escape(description)}" />`,
    `<meta property="og:type" content="website" />`,
    `<meta property="og:title" content="${escape(title)}" />`,
    `<meta property="og:description" content="${escape(description)}" />`,
    `<meta property="og:url" content="${site}${routePath(lang, layout)}" />`,
    `<meta property="og:site_name" content="launchme" />`,
    `<meta property="og:locale" content="${lang === "pl" ? "pl_PL" : "en_US"}" />`,
    `<meta property="og:image" content="${site}og.jpg" />`,
    `<meta property="og:image:width" content="1200" />`,
    `<meta property="og:image:height" content="630" />`,
    `<meta property="og:image:alt" content="launchme — all your PC games, made for a couch" />`,
    `<meta name="twitter:card" content="summary_large_image" />`,
    `<meta name="twitter:image" content="${site}og.jpg" />`,
    `<meta name="theme-color" content="#08090b" />`,
    `<link rel="canonical" href="${site}${routePath(lang, layout)}" />`,
    `<link rel="icon" type="image/png" sizes="32x32" href="${prefix}favicon-32.png" />`,
    `<link rel="apple-touch-icon" href="${prefix}apple-touch-icon.png" />`,
    `<link rel="manifest" href="${prefix}manifest.webmanifest" />`,
    `<link rel="preload" as="image" href="${prefix}img/shots/shelves.webp" />`,
    alternates,
  ].join("\n    ");
  const data = JSON.stringify({ page, release }).replace(/</g, "\\u003c");
  let out = template
    .replace('<html lang="en"', `<html lang="${lang}"`)
    .replace("<title>launchme</title>", `<title>${escape(title)}</title>`)
    .replace("<!--head-->", head)
    .replace("<!--app-->", html)
    .replace("<!--page-data-->", `<script>window.__LAUNCHME__=${data}</script>`);
  if (page.depth > 0) out = out.replaceAll('"./assets/', `"${prefix}assets/`).replaceAll('"./favicon.svg"', `"${prefix}favicon.svg"`);
  const target = path.join(dist, routePath(lang, layout), "index.html");
  await mkdir(path.dirname(target), { recursive: true });
  await writeFile(target, out, "utf8");
  console.log("wrote", path.relative(root, target), `${Math.round(out.length / 1024)} KB`);
}

await rm(path.join(root, ".ssr"), { recursive: true, force: true });
