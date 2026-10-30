// Renders the social preview (public/og.jpg) and the PNG icons from favicon.svg with a headless browser.
// Run after changing the logo or the hero screenshot: node scripts/icons.mjs  (CHANNEL=chrome to use Chrome)
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const pub = (file) => path.join(root, "public", file);
// Inline data URLs: a page set from a string may not load file:// resources.
const data = async (file, type) => `data:${type};base64,${(await readFile(file)).toString("base64")}`;
const url = (file) => data(pub(file), "image/webp");
const font = (pkg, file) => data(path.join(root, "node_modules", "@fontsource-variable", pkg, "files", file), "font/woff2");

const browser = await chromium.launch({ channel: process.env.CHANNEL ?? "msedge", headless: true });
const page = await browser.newPage({ deviceScaleFactor: 1 });

const logo = (await readFile(pub("favicon.svg"), "utf8")).replace("<svg ", '<svg class="mark" ');

// Social preview, 1200x630.
await page.setViewportSize({ width: 1200, height: 630 });
await page.setContent(`<!doctype html><html><head><style>
  @font-face { font-family: Display; src: url(${await font("inter-tight", "inter-tight-latin-wght-normal.woff2")}) format("woff2"); font-weight: 100 900; }
  @font-face { font-family: Sans; src: url(${await font("inter", "inter-latin-wght-normal.woff2")}) format("woff2"); font-weight: 100 900; }
  * { margin: 0; box-sizing: border-box; }
  body { width: 1200px; height: 630px; overflow: hidden; background: #08090b; color: #fff; font-family: Sans; position: relative; }
  .glow { position: absolute; inset: 0; background:
    radial-gradient(600px 420px at 78% 60%, rgb(232 194 106 / 0.28), transparent 70%),
    radial-gradient(520px 380px at 8% 0%, rgb(90 110 255 / 0.22), transparent 70%); }
  .shot { position: absolute; left: 560px; top: 118px; width: 820px; border-radius: 22px; padding: 8px; background: #000;
    border: 1px solid rgb(255 255 255 / 0.14); box-shadow: 0 40px 120px -20px rgb(0 0 0 / 0.9);
    transform: perspective(1400px) rotateY(-14deg) rotateX(4deg); transform-origin: left center; }
  .shot img { display: block; width: 100%; border-radius: 15px; }
  .copy { position: absolute; left: 72px; top: 76px; width: 560px; }
  .brand { display: flex; align-items: center; gap: 14px; font-family: Display; font-weight: 650; font-size: 30px; letter-spacing: -0.02em; }
  .mark { width: 46px; height: 46px; }
  h1 { margin-top: 92px; font-family: Display; font-weight: 650; font-size: 66px; line-height: 1.02; letter-spacing: -0.04em; }
  h1 span { display: block; color: rgb(255 255 255 / 0.5); }
  p { margin-top: 30px; font-size: 22px; line-height: 1.45; color: rgb(255 255 255 / 0.66); max-width: 470px; }
  .chips { position: absolute; left: 72px; bottom: 60px; display: flex; gap: 10px; }
  .chips b { font-weight: 600; font-size: 16px; padding: 8px 14px; border-radius: 999px; border: 1px solid rgb(255 255 255 / 0.16); background: rgb(255 255 255 / 0.05); color: rgb(255 255 255 / 0.85); }
</style></head><body>
  <div class="glow"></div>
  <div class="shot"><img src="${await url("img/shots/shelves.webp")}" /></div>
  <div class="copy">
    <div class="brand">${logo}launchme</div>
    <h1>All your PC games.<span>Made for a couch.</span></h1>
    <p>Native C++, no Electron. Steam, GOG, Epic, Xbox and more in one library.</p>
  </div>
  <div class="chips"><b>Free</b><b>Open source</b><b>Windows · Linux</b></div>
</body></html>`);
await page.waitForLoadState("networkidle");
await page.evaluate(() => document.fonts.ready);
await page.screenshot({ path: pub("og.jpg"), type: "jpeg", quality: 86 });

// Icons from the SVG mark.
for (const [file, size] of [
  ["favicon-32.png", 32],
  ["apple-touch-icon.png", 180],
  ["icon-192.png", 192],
  ["icon-512.png", 512],
]) {
  await page.setViewportSize({ width: size, height: size });
  await page.setContent(`<!doctype html><html><body style="margin:0;background:transparent">${logo.replace('class="mark"', `width="${size}" height="${size}" style="display:block"`)}</body></html>`);
  await page.screenshot({ path: pub(file), omitBackground: file !== "apple-touch-icon.png" });
}
await browser.close();

await writeFile(
  pub("manifest.webmanifest"),
  JSON.stringify(
    {
      name: "launchme",
      short_name: "launchme",
      description: "A native game launcher for all your PC games.",
      start_url: "./",
      display: "standalone",
      background_color: "#08090b",
      theme_color: "#08090b",
      icons: [
        { src: "icon-192.png", sizes: "192x192", type: "image/png" },
        { src: "icon-512.png", sizes: "512x512", type: "image/png" },
      ],
    },
    null,
    2,
  ) + "\n",
);
console.log("wrote og.jpg, favicon-32.png, apple-touch-icon.png, icon-192.png, icon-512.png, manifest.webmanifest");
