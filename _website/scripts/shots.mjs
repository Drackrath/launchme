// Screenshots of the built site through a local Edge/Chrome (playwright-core, no download).
// Usage: node scripts/shots.mjs [outDir] [route...]   (needs `npm run preview` running on 4173)
import { mkdir } from "node:fs/promises";
import path from "node:path";
import { chromium } from "playwright-core";

const out = process.argv[2] ?? path.join(process.env.TEMP ?? ".", "launchme-shots");
const routes = process.argv.slice(3).length ? process.argv.slice(3) : ["/", "/v2/"];
const base = process.env.BASE ?? "http://localhost:4173";
await mkdir(out, { recursive: true });

const browser = await chromium.launch({ channel: process.env.CHANNEL ?? "msedge", headless: true });
for (const [name, viewport] of [
  ["desktop", { width: 1440, height: 900 }],
  ["mobile", { width: 390, height: 844 }],
]) {
  if (process.env.ONLY && process.env.ONLY !== name) continue;
  const context = await browser.newContext({ viewport, deviceScaleFactor: 1 });
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => message.type() === "error" && errors.push(message.text()));
  for (const route of routes) {
    await page.goto(base + route, { waitUntil: "networkidle" });
    await page.waitForTimeout(1800);
    const slug = route.replace(/\//g, "_") || "_";
    await page.screenshot({ path: path.join(out, `${name}${slug}top.png`) });
    // Walk down the page so every whileInView animation runs, taking a viewport shot per step.
    const height = await page.evaluate(() => document.documentElement.scrollHeight);
    let step = 0;
    for (let y = viewport.height; y < height; y += viewport.height) {
      await page.evaluate((top) => window.scrollTo(0, top), y);
      await page.waitForTimeout(1100);
      if (!process.env.NOSTEPS) await page.screenshot({ path: path.join(out, `${name}${slug}${String(++step).padStart(2, "0")}.png`) });
    }
    console.log(route, name, "height", height, "steps", step, errors.length ? "errors: " + errors.join(" | ") : "");
    errors.length = 0;
  }
  await context.close();
}
await browser.close();
