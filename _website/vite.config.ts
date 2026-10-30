import { existsSync } from "node:fs";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

// Videos are optional: drop them into public/media/ and the page uses them on the next build.
const media = (name: string) => existsSync(new URL(`./public/media/${name}`, import.meta.url));

// Relative base: the prerender script rewrites asset paths per page depth, so the
// built site works from any folder (a subpath on a host, or a local static server).
export default defineConfig({
  base: "./",
  plugins: [react(), tailwindcss()],
  define: {
    __MEDIA__: JSON.stringify({
      heroMp4: media("hero.mp4"),
      heroWebm: media("hero.webm"),
      interfaceMp4: media("interface.mp4"),
      interfaceWebm: media("interface.webm"),
    }),
  },
  build: {
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    reportCompressedSize: false,
    chunkSizeWarningLimit: 900,
  },
  ssr: {
    noExternal: ["motion", "lucide-react"],
  },
});
