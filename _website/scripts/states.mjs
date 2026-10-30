// Captures every layout in the picker and every library view (dev check).
import { mkdir } from "node:fs/promises";
import path from "node:path";
import { chromium } from "playwright-core";

const out = process.argv[2] ?? path.join(process.env.TEMP ?? ".", "launchme-states");
await mkdir(out, { recursive: true });
const browser = await chromium.launch({ channel: "msedge", headless: true });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto((process.env.BASE ?? "http://localhost:4173") + (process.env.ROUTE ?? "/"), { waitUntil: "networkidle" });

const layouts = page.locator("#layouts [role=tab]");
await page.locator("#layouts").scrollIntoViewIfNeeded();
for (let i = 0; i < (await layouts.count()); i++) {
  await layouts.nth(i).click();
  await page.waitForTimeout(i === 2 ? 2600 : 1400);
  await page.locator("#layouts .aspect-video").first().screenshot({ path: path.join(out, `layout-${i}.png`) });
}
const views = page.locator("#views button");
await page.locator("#views").scrollIntoViewIfNeeded();
for (let i = 0; i < 10; i++) {
  await views.nth(i).click();
  await page.waitForTimeout(1500);
  await page.locator("#views .aspect-video").first().screenshot({ path: path.join(out, `view-${i}.png`) });
}
await browser.close();
console.log("done", out);
