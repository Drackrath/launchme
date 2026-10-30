# launchme website

The landing page at https://uxandai.github.io/launchme/: React + Vite, prerendered to static HTML for `/`, `/pl/`, `/v2/` and `/pl/v2/`. No JavaScript runs on the server.

`.github/workflows/pages.yml` builds and publishes it on every change in `_website/` and on every published release. The build reads the newest release from the GitHub API, so the download buttons, version and date update by themselves.

## Work on it

```
npm install
npm run dev        # http://localhost:5173
npm run build      # dist/, the same as the workflow
npm run preview    # serves dist/ on http://localhost:4173
```

Texts live in `src/content/en.ts` and `src/content/pl.ts`.

## Assets (committed, the workflow doesn't regenerate them)

- `npm run art`: game covers, posters, landscapes, heroes and logos from the launcher's own artwork cache (`%LOCALAPPDATA%\Launchme`, from SteamGridDB) into `public/img/art/`, with `src/data/games.json`; plus one sprite per companion from `../res/companion/`.
- `npm run assets`: screenshots from `../docs/screenshots/` and pad pictures into `public/img/`.
- `npm run icons`: `og.jpg`, PNG icons and the web manifest, rendered with Edge (`CHANNEL=chrome` for Chrome).

## Videos

Drop files into `public/media/` and rebuild; the build checks which exist.

- `hero.mp4` (and optionally `hero.webm`): a Play button over the main page's screenshot.
- `interface.mp4` (and optionally `interface.webm`): the big screen of layout v2; until then it shows a slideshow of screenshots.
