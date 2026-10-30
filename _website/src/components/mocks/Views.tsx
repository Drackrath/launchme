import { AnimatePresence, motion } from "motion/react";
import { ArrowUpRight, ChevronDown, Filter, LayoutGrid, MoreHorizontal, Search } from "lucide-react";
import { useState, type CSSProperties, type ReactNode } from "react";
import { art, gameById, ids, rating, type Game } from "../../lib/assets";
import { cn } from "../../lib/cn";
import { usePage, type Lang } from "../../lib/page";

// The launcher's ten library views, drawn from its renderers' geometry at 1920×1080 and scaled with
// container units. Covers are the SteamGridDB art the launcher cached. Click a game to move the focus.
const px = (n: number) => `${(n / 1920) * 100}cqw`;

export type ViewKey = "grid" | "list" | "listplus" | "cinematic" | "overview" | "detailed" | "coverflow" | "shelf" | "floppy" | "cartridge";

const L = {
  en: {
    tabs: ["All games", "Recently played", "Recently updated"],
    sort: "Recently played",
    play: "Play",
    search: "Search",
    categories: "Categories",
    back: "Back",
    year: "Year",
    playtime: "Playtime",
    rating: "Rating",
    achievements: "Achievements",
    description: "Description",
    information: "Information",
    installed: "Installed",
    details: "Details",
    developer: "Developer",
    release: "Release",
    genre: "Genre",
  },
  pl: {
    tabs: ["Wszystkie gry", "Ostatnio grane", "Ostatnio zaktualizowane"],
    sort: "Ostatnio grane",
    play: "Graj",
    search: "Szukaj",
    categories: "Kategorie",
    back: "Wstecz",
    year: "Rok",
    playtime: "Czas gry",
    rating: "Ocena",
    achievements: "Osiągnięcia",
    description: "Opis",
    information: "Informacje",
    installed: "Zainstalowane",
    details: "Szczegóły",
    developer: "Developer",
    release: "Premiera",
    genre: "Gatunek",
  },
} satisfies Record<Lang, Record<string, string | string[]>>;

const pick = (from: number, count: number) => Array.from({ length: count }, (_, i) => ids[(from + i) % ids.length]);
const g = (id: string): Game => gameById[id];
const outline = (color: string) => `0 0 0 ${px(3.2)} ${color}, 0 0 ${px(14)} ${px(2)} ${color}66, 0 ${px(18)} ${px(40)} -${px(10)} rgb(0 0 0 / 0.8)`;
const glass = "bg-white/[0.07] backdrop-blur-[0.4cqw] border border-white/[0.08]";

/* ----------------------------------------------------------------- chrome */

function Hero({ id, opacity, kind = "h" }: { id: string; opacity: number; kind?: "h" | "l" }) {
  const { asset } = usePage();
  return (
    <AnimatePresence>
      <motion.img
        key={id}
        src={asset(art(kind, id))}
        alt=""
        className="absolute inset-0 h-full w-full object-cover"
        initial={{ opacity: 0 }}
        animate={{ opacity }}
        exit={{ opacity: 0 }}
        transition={{ duration: 0.45 }}
      />
    </AnimatePresence>
  );
}

function TopBar({ tabs, title }: { tabs?: boolean; title?: string }) {
  const { lang } = usePage();
  const t = L[lang];
  const counts = [24, 8, 8];
  return (
    <>
      <span className={cn("absolute flex items-center justify-center rounded-full", glass)} style={{ left: px(17), top: px(41), width: px(48), height: px(48) }}>
        <span className="block rounded-full bg-white" style={{ width: px(6), height: px(6) }} />
      </span>
      {title && (
        <span className="absolute font-medium text-white/80" style={{ left: px(82), top: px(47), fontSize: px(26) }}>
          {title}
        </span>
      )}
      {tabs &&
        t.tabs.map((label, i) => (
          <span
            key={label}
            className={cn("absolute flex items-center justify-center whitespace-nowrap rounded-full font-semibold uppercase", i === 0 ? cn(glass, "text-white") : "text-white/50")}
            style={{ left: px(121 + i * 380), top: px(43), width: px(300), height: px(46), gap: px(10), fontSize: px(16), letterSpacing: "0.02em" }}
          >
            {label}
            <span className="rounded-full bg-white/12 font-medium text-white/70" style={{ fontSize: px(13), padding: `${px(1)} ${px(8)}` }}>
              {counts[i]}
            </span>
          </span>
        ))}
      <span className={cn("absolute flex items-center rounded-full text-white", glass)} style={{ left: px(1344), top: px(41), width: px(356), height: px(50), padding: `0 ${px(22)}`, gap: px(14), fontSize: px(22) }}>
        <Filter style={{ width: px(22), height: px(22) }} />
        <span className="flex-1 text-center font-medium">{t.sort}</span>
        <ChevronDown style={{ width: px(20), height: px(20) }} />
      </span>
      {[Search, LayoutGrid].map((Icon, i) => (
        <span key={i} className="absolute flex items-center justify-center text-white" style={{ left: px(1721 + i * 70), top: px(41), width: px(50), height: px(50) }}>
          <Icon style={{ width: px(30), height: px(30) }} />
        </span>
      ))}
    </>
  );
}

function Hints({ categories = true }: { categories?: boolean }) {
  const { lang } = usePage();
  const t = L[lang];
  const items: [string, string][] = [["Enter", t.play], ["A–Z", t.search], ...(categories ? ([["PgUp PgDn", t.categories]] as [string, string][]) : []), ["Esc", t.back]];
  return (
    <div className={cn("absolute flex items-center rounded-full", glass)} style={{ right: px(44), top: px(1030), height: px(40), gap: px(22), padding: `0 ${px(16)}`, fontSize: px(19) }}>
      {items.map(([key, label]) => (
        <span key={key} className="flex items-center whitespace-nowrap text-white/85" style={{ gap: px(9) }}>
          <span className="rounded-md bg-white/85 font-semibold text-black" style={{ padding: `${px(1)} ${px(9)}`, fontSize: px(15) }}>
            {key}
          </span>
          {label}
        </span>
      ))}
    </div>
  );
}

function Tile({ id, kind, focused, onClick, radius, className, children, style }: { id: string; kind: "c" | "p" | "l"; focused: boolean; onClick: () => void; radius: number; className?: string; children?: ReactNode; style?: CSSProperties }) {
  const { asset } = usePage();
  return (
    <motion.button
      type="button"
      onClick={onClick}
      className={cn("absolute block overflow-hidden", className)}
      style={{ borderRadius: px(radius), ...style }}
      animate={{ boxShadow: focused ? outline(g(id).accent) : `0 0 0 ${px(1)} rgb(255 255 255 / 0.16)`, opacity: focused ? 1 : 0.86 }}
      transition={{ duration: 0.25 }}
      aria-label={g(id).name}
    >
      <img src={asset(art(kind, id))} alt="" loading="lazy" className="h-full w-full object-cover" />
      {children}
    </motion.button>
  );
}

const Base = ({ children }: { children: ReactNode }) => <div className="absolute inset-0 bg-[#040506]">{children}</div>;

/* ------------------------------------------------------------------- grid */

function GridView() {
  const list = pick(0, 21);
  const [focus, setFocus] = useState(2);
  return (
    <Base>
      <TopBar tabs />
      {list.map((id, i) => {
        const x = 89 + (i % 7) * 253;
        const y = 228 + Math.floor(i / 7) * 300;
        return (
          <div key={id}>
            <Tile id={id} kind="c" focused={i === focus} onClick={() => setFocus(i)} radius={52} style={{ left: px(x), top: px(y), width: px(223), height: px(223) }} />
            <span className={cn("absolute truncate text-center", i === focus ? "text-white" : "text-white/65")} style={{ left: px(x), top: px(y + 236), width: px(223), fontSize: px(18) }}>
              {g(id).name}
            </span>
          </div>
        );
      })}
      <Hints />
    </Base>
  );
}

/* ------------------------------------------------------------------- list */

function Rows({ group, list, focus, setFocus, x, width, top, thumbs, pill, columns }: { group: string; list: string[]; focus: number; setFocus: (i: number) => void; x: number; width: number; top: number; thumbs?: boolean; pill?: boolean; columns?: boolean }) {
  const { asset } = usePage();
  return (
    <>
      {list.map((id, i) => {
        const focused = i === focus;
        const game = g(id);
        const y = top + i * 94;
        return (
          <button key={id} type="button" onClick={() => setFocus(i)} className="absolute flex items-center text-left" style={{ left: px(x), top: px(y), width: px(width), height: px(84) }}>
            {focused && (
              <motion.span
                layoutId={`row-${group}`}
                className={cn("absolute inset-0", pill ? "bg-white/92" : "bg-white/13")}
                style={{ borderRadius: px(pill ? 18 : 16) }}
                transition={{ type: "spring", stiffness: 420, damping: 36 }}
              />
            )}
            {focused && !pill && <span className="absolute" style={{ left: px(-10), top: px(22), width: px(4), height: px(40), background: game.accent, borderRadius: px(2) }} />}
            {thumbs && <img src={asset(art("c", id))} alt="" loading="lazy" className="relative shrink-0" style={{ marginLeft: px(7), width: px(62), height: px(62), borderRadius: px(12) }} />}
            <span className={cn("relative min-w-0 flex-1 truncate font-medium", pill && focused ? "text-[#111]" : "text-white")} style={{ marginLeft: px(thumbs ? 26 : 22), fontSize: px(22) }}>
              {game.name}
            </span>
            {columns && (
              <>
                <span className="relative text-right tabular-nums text-white/60" style={{ width: px(70), fontSize: px(19) }}>
                  {game.year}
                </span>
                <span className="relative text-right tabular-nums text-white/60" style={{ width: px(115), fontSize: px(19) }}>
                  {game.time}
                </span>
                <span className="relative text-right tabular-nums" style={{ width: px(100), marginRight: px(22), fontSize: px(19), color: game.accent }}>
                  {rating(id)}%
                </span>
              </>
            )}
            {!columns && (
              <span className={cn("relative text-right tabular-nums", pill && focused ? "text-[#444]" : "text-white/55")} style={{ width: px(110), marginRight: px(22), fontSize: px(19) }}>
                {game.time}
              </span>
            )}
          </button>
        );
      })}
    </>
  );
}

function ListView() {
  const { asset, lang } = usePage();
  const t = L[lang];
  const list = pick(3, 8);
  const [focus, setFocus] = useState(1);
  const game = g(list[focus]);
  return (
    <Base>
      <Hero id={game.id} opacity={0.18} />
      <TopBar tabs />
      <div className="absolute bg-[#060606]/48" style={{ left: px(67), top: px(208), width: px(982), height: px(820), borderRadius: px(22) }} />
      {[t.year, t.playtime, t.rating].map((label, i) => (
        <span key={label} className="absolute text-right font-semibold uppercase text-white/45" style={{ left: px([727, 797, 915][i]), top: px(222), width: px([70, 115, 100][i]), fontSize: px(13) }}>
          {label}
        </span>
      ))}
      <Rows group="list" list={list} focus={focus} setFocus={setFocus} x={82} width={955} top={256} thumbs columns />
      <AnimatePresence mode="wait">
        <motion.div key={game.id} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.2 }}>
          <img src={asset(art("c", game.id))} alt="" className="absolute" style={{ left: px(1104), top: px(228), width: px(245), height: px(245), borderRadius: px(48) }} />
          <div className="absolute" style={{ left: px(1373), top: px(232), width: px(460) }}>
            <p className="font-semibold leading-tight text-white" style={{ fontSize: px(42) }}>
              {game.name}
            </p>
            <p className="text-white/55" style={{ fontSize: px(17), marginTop: px(14) }}>
              {t.installed}
            </p>
            <p className="text-white/55" style={{ fontSize: px(17), marginTop: px(16), lineHeight: 1.6 }}>
              {game.genres[lang].join(", ")}
              <br />
              {game.developer}
              <br />
              {game.year}
            </p>
            <p className="flex items-center text-white/80" style={{ fontSize: px(17), marginTop: px(10), gap: px(6) }}>
              Steam {rating(game.id)}% (100) <ArrowUpRight style={{ width: px(16), height: px(16) }} />
            </p>
          </div>
          <div className={cn("absolute grid grid-cols-2", glass)} style={{ left: px(1104), top: px(523), width: px(730), height: px(106), borderRadius: px(22), padding: `${px(18)} ${px(28)}` }}>
            <div>
              <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(13) }}>
                {t.playtime}
              </p>
              <p className="font-semibold text-white" style={{ fontSize: px(30), marginTop: px(4) }}>
                {game.time}
              </p>
            </div>
            <div>
              <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(13) }}>
                {t.achievements}
              </p>
              <p className="font-semibold text-white" style={{ fontSize: px(30), marginTop: px(4) }}>
                {game.ach[0]} / {game.ach[1]}
              </p>
              <div className="rounded-full bg-white/15" style={{ height: px(4), marginTop: px(6) }}>
                <div className="h-full rounded-full bg-white" style={{ width: `${(game.ach[0] / game.ach[1]) * 100}%` }} />
              </div>
            </div>
          </div>
          <div className={cn("absolute", glass)} style={{ left: px(1104), top: px(653), width: px(730), height: px(355), borderRadius: px(22), padding: px(28) }}>
            <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(13) }}>
              {t.description}
            </p>
            <p className="text-white/80" style={{ fontSize: px(19), marginTop: px(12), lineHeight: 1.5 }}>
              {game.desc[lang]}
            </p>
          </div>
        </motion.div>
      </AnimatePresence>
      <Hints />
    </Base>
  );
}

/* ------------------------------------------------------------------ list+ */

function ListPlusView() {
  const { asset, lang } = usePage();
  const t = L[lang];
  const list = pick(0, 8);
  const [focus, setFocus] = useState(2);
  const game = g(list[focus]);
  return (
    <Base>
      <Hero id={game.id} opacity={0.08} />
      <TopBar tabs />
      <div className="absolute overflow-hidden" style={{ left: px(82), top: px(230), width: px(490), height: px(732), borderRadius: px(20) }}>
        <AnimatePresence initial={false}>
          <motion.img key={game.id} src={asset(art("p", game.id))} alt="" className="absolute inset-0 h-full w-full object-cover" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.3 }} />
        </AnimatePresence>
      </div>
      <Rows group="listplus" list={list} focus={focus} setFocus={setFocus} x={612} width={570} top={228} pill />
      <AnimatePresence mode="wait">
        <motion.div key={game.id} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.2 }} className="absolute" style={{ left: px(1222), top: px(228), width: px(616) }}>
          <img src={asset(art("l", game.id))} alt="" className="w-full object-cover" style={{ height: px(312), borderRadius: px(16) }} />
          <p className="font-semibold text-white" style={{ fontSize: px(32), marginTop: px(30) }}>
            {game.name}
          </p>
          <p className="text-white/65" style={{ fontSize: px(18), marginTop: px(8) }}>
            {game.year} · {t.playtime}: {game.time}
          </p>
          <p className="text-white/80" style={{ fontSize: px(18), marginTop: px(10) }}>
            Steam {rating(game.id)}% (100) ↗
          </p>
          <p className="text-white/65" style={{ fontSize: px(18), marginTop: px(8) }}>
            {t.achievements}: {game.ach[0]} / {game.ach[1]}
          </p>
          <div className="rounded-full bg-white/15" style={{ height: px(4), marginTop: px(10) }}>
            <div className="h-full rounded-full bg-white" style={{ width: `${(game.ach[0] / game.ach[1]) * 100}%` }} />
          </div>
          <p className="text-white/55" style={{ fontSize: px(17), marginTop: px(18) }}>
            {game.genres[lang].join(", ")}
          </p>
          <p className="text-white/75" style={{ fontSize: px(19), marginTop: px(12), lineHeight: 1.45 }}>
            {game.desc[lang]}
          </p>
        </motion.div>
      </AnimatePresence>
      <Hints />
    </Base>
  );
}

/* -------------------------------------------------------------- cinematic */

function Logo({ id, box }: { id: string; box: [number, number, number, number] }) {
  const { asset } = usePage();
  const [x, y, w, h] = box;
  return (
    <AnimatePresence mode="wait">
      <motion.div key={id} className="absolute flex items-end" style={{ left: px(x), top: px(y), width: px(w), height: px(h) }} initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }} transition={{ duration: 0.3 }}>
        <img src={asset(art("g", id))} alt={g(id).name} className="max-h-full max-w-full object-contain object-left-bottom drop-shadow-[0_0.3cqw_0.8cqw_rgb(0_0_0/0.6)]" />
      </motion.div>
    </AnimatePresence>
  );
}

function CinematicView() {
  const list = pick(4, 12);
  const [focus, setFocus] = useState(0);
  const game = g(list[focus]);
  return (
    <Base>
      <Hero id={game.id} opacity={0.85} />
      <div className="absolute inset-0" style={{ background: `linear-gradient(180deg, transparent ${px(120)}, rgb(0 0 0 / 0.96) ${px(610)})` }} />
      <TopBar tabs />
      <Logo id={game.id} box={[72, 160, 760, 160]} />
      {list.map((id, i) => {
        const x = 72 + (i % 4) * 451;
        const y = 463 + Math.floor(i / 4) * 280;
        const focused = i === focus;
        return (
          <div key={id}>
            <Tile id={id} kind="l" focused={focused} onClick={() => setFocus(i)} radius={14} style={{ left: px(x), top: px(y), width: px(430), height: px(202), scale: focused ? 1.045 : 1 }} />
            <span className={cn("absolute truncate font-medium", focused ? "text-white" : "text-white/70")} style={{ left: px(x), top: px(y + 216), width: px(430), fontSize: px(20) }}>
              {g(id).name}
            </span>
          </div>
        );
      })}
      <Hints />
    </Base>
  );
}

/* --------------------------------------------------------------- overview */

function OverviewView() {
  const { asset, lang } = usePage();
  const t = L[lang];
  const list = pick(0, 24);
  const [focus, setFocus] = useState(4);
  const game = g(list[focus]);
  return (
    <Base>
      <Hero id={game.id} opacity={0.32} />
      <div className="absolute inset-0 bg-[linear-gradient(90deg,rgb(4_5_6/0.75),rgb(4_5_6/0.35))]" />
      <TopBar tabs />
      <AnimatePresence mode="wait">
        <motion.div key={game.id} className="absolute" style={{ left: px(82), top: px(228), width: px(345) }} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.2 }}>
          <img src={asset(art("c", game.id))} alt="" className="w-full" style={{ height: px(345), borderRadius: px(56) }} />
          <p className="font-semibold leading-tight text-white" style={{ fontSize: px(30), marginTop: px(26) }}>
            {game.name}
          </p>
          <p className="text-white/55" style={{ fontSize: px(16), marginTop: px(8) }}>
            Steam
          </p>
          <div className="grid grid-cols-2" style={{ gap: px(9), marginTop: px(18) }}>
            {[
              [t.playtime, game.time],
              [t.achievements, `${game.ach[0]} / ${game.ach[1]}`],
            ].map(([label, value]) => (
              <div key={label} className="bg-white/5" style={{ borderRadius: px(14), padding: `${px(8)} ${px(12)}` }}>
                <p className="truncate font-semibold uppercase text-white/45" style={{ fontSize: px(12) }}>
                  {label}
                </p>
                <p className="font-semibold text-white" style={{ fontSize: px(17) }}>
                  {value}
                </p>
              </div>
            ))}
          </div>
          <p className="text-white/70" style={{ fontSize: px(17), marginTop: px(18), lineHeight: 1.45 }}>
            {game.desc[lang]}
          </p>
        </motion.div>
      </AnimatePresence>
      <div className="absolute flex" style={{ left: px(82), top: px(984), gap: px(9) }}>
        {[t.details, t.play].map((label) => (
          <span key={label} className={cn("flex items-center justify-center rounded-full font-semibold text-white", glass)} style={{ width: px(168), height: px(46), fontSize: px(18) }}>
            {label}
          </span>
        ))}
      </div>
      <div className="absolute overflow-hidden" style={{ left: px(455), top: px(213), width: px(1440), height: px(830) }}>
        {list.map((id, i) => {
          const focused = i === focus;
          return (
            <Tile
              key={id}
              id={id}
              kind="l"
              focused={focused}
              onClick={() => setFocus(i)}
              radius={10}
              style={{ left: px(15 + (i % 5) * 277), top: px(15 + Math.floor(i / 5) * 138), width: px(262), height: px(122), scale: focused ? 1.045 : 1 }}
            />
          );
        })}
      </div>
      <Hints />
    </Base>
  );
}

/* --------------------------------------------------------------- detailed */

function DetailedView() {
  const { asset, lang } = usePage();
  const t = L[lang];
  const list = pick(8, 8);
  const [focus, setFocus] = useState(0);
  const game = g(list[focus]);
  const info: [string, string][] = [
    [t.developer, game.developer],
    [t.release, String(game.year)],
    [t.genre, game.genres[lang].join(", ")],
  ];
  return (
    <Base>
      <Hero id={game.id} opacity={0.12} />
      <TopBar title="Steam" />
      <Rows group="detailed" list={list} focus={focus} setFocus={setFocus} x={82} width={494} top={228} thumbs />
      <div className="absolute overflow-hidden" style={{ left: px(629), top: px(228), width: px(1209), height: px(312), borderRadius: px(20) }}>
        <AnimatePresence initial={false}>
          <motion.img key={game.id} src={asset(art("h", game.id))} alt="" className="absolute inset-0 h-full w-full object-cover" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.3 }} />
        </AnimatePresence>
        <div className="absolute inset-0 bg-[linear-gradient(90deg,rgb(0_0_0/0.8),transparent_70%),linear-gradient(0deg,rgb(0_0_0/0.6),transparent_60%)]" />
      </div>
      <Logo id={game.id} box={[665, 255, 500, 170]} />
      <p className="absolute text-white/75" style={{ left: px(665), top: px(452), fontSize: px(17) }}>
        {t.installed} · {game.year} · {game.genres[lang].join(", ")}
      </p>
      <div className="absolute flex items-center" style={{ left: px(665), top: px(490), gap: px(14) }}>
        <span className="flex items-center justify-center rounded-full bg-white/18 font-semibold text-white" style={{ width: px(240), height: px(48), fontSize: px(20) }}>
          {t.play}
        </span>
        <span className="flex items-center justify-center rounded-full bg-white/90 text-black" style={{ width: px(48), height: px(48) }}>
          <MoreHorizontal style={{ width: px(24), height: px(24) }} />
        </span>
      </div>
      <div className={cn("absolute", glass)} style={{ left: px(629), top: px(612), width: px(643), height: px(415), borderRadius: px(20), padding: px(26) }}>
        <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(13) }}>
          {t.description}
        </p>
        <p className="text-white/80" style={{ fontSize: px(19), marginTop: px(12), lineHeight: 1.5 }}>
          {game.desc[lang]}
        </p>
      </div>
      <div className={cn("absolute", glass)} style={{ left: px(1301), top: px(612), width: px(537), height: px(415), borderRadius: px(20), padding: px(26) }}>
        <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(13) }}>
          {t.information}
        </p>
        <div className="grid grid-cols-2" style={{ marginTop: px(16) }}>
          <div>
            <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(12) }}>
              {t.playtime}
            </p>
            <p className="font-semibold text-white" style={{ fontSize: px(28) }}>
              {game.time}
            </p>
          </div>
          <div>
            <p className="font-semibold uppercase text-white/45" style={{ fontSize: px(12) }}>
              {t.achievements}
            </p>
            <p className="font-semibold text-white" style={{ fontSize: px(28) }}>
              {game.ach[0]} / {game.ach[1]}
            </p>
            <div className="rounded-full bg-white/15" style={{ height: px(4), marginTop: px(4), width: "80%" }}>
              <div className="h-full rounded-full bg-white" style={{ width: `${(game.ach[0] / game.ach[1]) * 100}%` }} />
            </div>
          </div>
        </div>
        <div style={{ marginTop: px(30) }}>
          {info.map(([label, value]) => (
            <p key={label} className="flex" style={{ fontSize: px(17), marginTop: px(12) }}>
              <span className="shrink-0 text-white/50" style={{ width: px(130) }}>
                {label}
              </span>
              <span className="truncate text-white/85">{value}</span>
            </p>
          ))}
        </div>
      </div>
      <Hints categories={false} />
    </Base>
  );
}

/* ---------------------------------------- media rows: coverflow, floppy, cart */

type Media = { big: [number, number]; small: [number, number]; top: number; smallTop: number; firstSmall: number; step: number };

function MediaRow({ list, focus, setFocus, media, render }: { list: string[]; focus: number; setFocus: (i: number) => void; media: Media; render: (id: string, focused: boolean) => ReactNode }) {
  return (
    <>
      {list.map((id, i) => {
        const d = i - focus;
        const focused = d === 0;
        const x = d < 0 ? -media.small[0] - 120 : focused ? 125 : media.firstSmall + (d - 1) * media.step;
        const [w, h] = focused ? media.big : media.small;
        return (
          <motion.button
            key={id}
            type="button"
            onClick={() => setFocus(i)}
            className="absolute block text-left"
            initial={false}
            animate={{ left: px(x), top: px(focused ? media.top : media.smallTop), width: px(w), height: px(h), opacity: d < 0 ? 0 : 1 }}
            transition={{ type: "spring", stiffness: 190, damping: 28 }}
            style={{ zIndex: focused ? 2 : 1 }}
            aria-label={g(id).name}
          >
            {render(id, focused)}
          </motion.button>
        );
      })}
    </>
  );
}

function MediaTitle({ id }: { id: string }) {
  return (
    <AnimatePresence mode="wait">
      <motion.div key={id} className="absolute" style={{ left: px(125), top: px(870) }} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.2 }}>
        <p className="font-semibold text-white" style={{ fontSize: px(48) }}>
          {g(id).name}
        </p>
        <p className="text-white/55" style={{ fontSize: px(18), marginTop: px(10) }}>
          Steam
        </p>
      </motion.div>
    </AnimatePresence>
  );
}

function CoverflowView() {
  const { asset } = usePage();
  const list = pick(10, 9);
  const [focus, setFocus] = useState(0);
  const game = g(list[focus]);
  return (
    <Base>
      <Hero id={game.id} opacity={0.32} />
      <TopBar title="Steam" />
      <MediaRow
        list={list}
        focus={focus}
        setFocus={setFocus}
        media={{ big: [372, 554], small: [274, 408], top: 286, smallTop: 360, firstSmall: 622, step: 296 }}
        render={(id, focused) => (
          <div className="relative h-full w-full">
            {/* The disc slides out of the focused case and spins. */}
            <motion.div
              className="absolute overflow-hidden rounded-full"
              style={{ top: "18%", height: "70%", aspectRatio: "1", boxShadow: `0 0 0 ${px(5)} ${g(id).accent}aa` }}
              initial={false}
              animate={{ left: focused ? "48%" : "10%", rotate: focused ? 360 : 0, opacity: focused ? 1 : 0 }}
              transition={{ left: { type: "spring", stiffness: 70, damping: 16, delay: focused ? 0.85 : 0 }, rotate: { duration: 8.6, ease: "linear", repeat: focused ? Infinity : 0 }, opacity: { duration: 0.2, delay: focused ? 0.85 : 0 } }}
            >
              <img src={asset(art("p", id))} alt="" className="h-full w-full object-cover" />
              <span className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 rounded-full bg-[#ddd]" style={{ width: "28%", height: "28%", boxShadow: "inset 0 0 0 0.25cqw #bbb" }} />
              <span className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 rounded-full bg-[#040506]" style={{ width: "9%", height: "9%" }} />
            </motion.div>
            <div className="absolute inset-0 overflow-hidden" style={{ borderRadius: px(10), boxShadow: `0 ${px(30)} ${px(60)} -${px(20)} rgb(0 0 0 / 0.9)` }}>
              <img src={asset(art("p", id))} alt="" className="h-full w-full object-cover" />
              {/* Steelbook: brushed metal light over the print. */}
              <div className="absolute inset-0 bg-[linear-gradient(115deg,rgb(255_255_255/0.22),transparent_28%,transparent_62%,rgb(255_255_255/0.12)_80%,transparent)] mix-blend-screen" />
              <div className="absolute inset-0" style={{ background: focused ? "transparent" : "rgb(0 0 0 / 0.16)" }} />
            </div>
          </div>
        )}
      />
      <MediaTitle id={game.id} />
      <Hints categories={false} />
    </Base>
  );
}

function FloppyView() {
  const { asset } = usePage();
  const list = pick(12, 7);
  const [focus, setFocus] = useState(0);
  return (
    <Base>
      <Hero id={list[focus]} opacity={0.32} />
      <TopBar title="Steam" />
      <MediaRow
        list={list}
        focus={focus}
        setFocus={setFocus}
        media={{ big: [557, 557], small: [415, 415], top: 286, smallTop: 355, firstSmall: 701, step: 437 }}
        render={(id) => (
          <div
            className="relative h-full w-full bg-[linear-gradient(160deg,#2a2b2e,#141416)]"
            style={{ clipPath: "polygon(0 0, 94% 0, 100% 6%, 100% 100%, 0 100%)", borderRadius: px(6), boxShadow: "inset 0 0 0 0.12cqw rgb(255 255 255 / 0.06)" }}
          >
            <div className="absolute bg-[repeating-linear-gradient(90deg,#d9dbdf_0_2px,#b9bcc2_2px_4px)]" style={{ left: "23.5%", right: "22.5%", top: 0, height: "32%", boxShadow: "inset 0 -0.2cqw 0.4cqw rgb(0 0 0 / 0.25)" }}>
              <div className="absolute bg-[#111214]" style={{ left: "62%", width: "21%", top: "14%", height: "72%", borderRadius: "2px" }} />
            </div>
            <svg className="absolute" viewBox="0 0 10 10" style={{ left: "6%", top: "4%", width: "4.5%" }}>
              <path d="M5 1 L9 9 L1 9 Z" fill="none" stroke="rgb(255 255 255 / 0.3)" strokeWidth="1" />
            </svg>
            <div className="absolute bg-[#f1efe9]" style={{ left: "19%", right: "19%", top: "36%", bottom: "2.5%", padding: "2.4%" }}>
              <img src={asset(art("c", id))} alt="" className="h-full w-full object-cover" />
            </div>
            <span className="absolute bg-black" style={{ left: "5%", bottom: "6%", width: "3.2%", height: "5%" }} />
            <span className="absolute bg-black" style={{ right: "5%", bottom: "6%", width: "3.2%", height: "5%" }} />
          </div>
        )}
      />
      <MediaTitle id={list[focus]} />
      <Hints categories={false} />
    </Base>
  );
}

function CartridgeView() {
  const { asset } = usePage();
  const list = pick(16, 8);
  const [focus, setFocus] = useState(0);
  return (
    <Base>
      <Hero id={list[focus]} opacity={0.32} />
      <TopBar title="Steam" />
      <MediaRow
        list={list}
        focus={focus}
        setFocus={setFocus}
        media={{ big: [391, 559], small: [290, 408], top: 283, smallTop: 360, firstSmall: 550, step: 319 }}
        render={(id) => (
          <div
            className="relative h-full w-full bg-[linear-gradient(180deg,#232427,#121315)]"
            style={{
              clipPath: "polygon(4% 0, 72% 0, 74% 2.5%, 96% 2.5%, 100% 5.5%, 100% 96%, 96% 100%, 4% 100%, 0 96%, 0 4%)",
              boxShadow: "inset 0 0 0 0.12cqw rgb(255 255 255 / 0.06)",
            }}
          >
            <span className="absolute bg-white/10" style={{ left: "28%", width: "32%", top: "1.2%", height: "0.8%", borderRadius: 4 }} />
            <div className="absolute overflow-hidden bg-black" style={{ left: "8.5%", right: "8.5%", top: "6.5%", bottom: "5.5%", borderRadius: px(6), boxShadow: "inset 0 0 0 0.2cqw rgb(0 0 0 / 0.6)" }}>
              <img src={asset(art("p", id))} alt="" className="h-full w-full object-cover" />
            </div>
          </div>
        )}
      />
      <MediaTitle id={list[focus]} />
      <Hints categories={false} />
    </Base>
  );
}

/* ------------------------------------------------------------------ shelf */

function ShelfView() {
  const { asset } = usePage();
  const list = pick(5, 13);
  const [focus, setFocus] = useState(6);
  const game = g(list[focus]);
  const W = 326, H = 499;
  return (
    <div className="absolute inset-0 bg-[#020203]">
      <Hero id={game.id} opacity={0.14} />
      <div className="absolute inset-0" style={{ background: `radial-gradient(${px(660)} ${px(420)} at 50% ${px(515)}, rgb(255 255 255 / 0.07), transparent 70%), linear-gradient(180deg, transparent ${px(744)}, rgb(0 0 0 / 0.97) ${px(1000)})` }} />
      <TopBar title="Steam" />
      <div className="absolute inset-0" style={{ perspective: px(1500), perspectiveOrigin: `50% ${px(500)}` }}>
        {list.map((id, i) => {
          const d = i - focus;
          const side = Math.sign(d);
          const offset = d === 0 ? 0 : side * (345 + (Math.abs(d) - 1) * 149);
          return (
            <motion.button
              key={id}
              type="button"
              onClick={() => setFocus(i)}
              className="absolute"
              style={{ top: px(245), width: px(W), height: px(H), left: px(960 - W / 2), transformStyle: "preserve-3d", zIndex: 30 - Math.abs(d) }}
              initial={false}
              animate={{
                x: px(offset),
                z: d === 0 ? px(0) : px(-271),
                rotateY: d === 0 ? 0 : side * -69,
                opacity: Math.abs(d) > 6 ? 0 : 1,
              }}
              transition={{ type: "spring", stiffness: 140, damping: 24 }}
              aria-label={g(id).name}
            >
              <div className="relative h-full w-full overflow-hidden" style={{ borderRadius: px(6), boxShadow: "0 1cqw 3cqw rgb(0 0 0 / 0.7)" }}>
                <img src={asset(art("p", id))} alt="" loading="lazy" className={cn("h-full w-full object-cover transition-[filter] duration-500", d !== 0 && "brightness-[0.75] grayscale")} />
                {d < 0 && (
                  <span className="absolute inset-y-0 left-0 flex items-center justify-center bg-[#0b0b0c]" style={{ width: "13%" }}>
                    <span className="absolute top-0 w-full bg-[#1b2838] text-center text-white/80" style={{ height: "7%", fontSize: px(13), lineHeight: px(30) }}>
                      ◆
                    </span>
                    <span className="whitespace-nowrap text-white/80" style={{ writingMode: "vertical-rl", transform: "rotate(180deg)", fontSize: px(17) }}>
                      {g(id).name}
                    </span>
                  </span>
                )}
                {d > 0 && <span className="absolute inset-y-0 right-0 bg-[linear-gradient(90deg,#3a3a3c,#8d8d90)]" style={{ width: "5%" }} />}
              </div>
              {/* Reflection on the floor. */}
              <div className="absolute left-0 w-full overflow-hidden" style={{ top: "100.6%", height: "32%", opacity: 0.34, maskImage: "linear-gradient(180deg,#000,transparent)", WebkitMaskImage: "linear-gradient(180deg,#000,transparent)" }}>
                <img src={asset(art("p", id))} alt="" loading="lazy" className={cn("w-full -scale-y-100 object-cover", d !== 0 && "grayscale")} style={{ height: px(H) }} />
              </div>
            </motion.button>
          );
        })}
      </div>
      <AnimatePresence mode="wait">
        <motion.div key={game.id} className="absolute inset-x-0 flex flex-col items-center" style={{ top: px(868) }} initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.2 }}>
          <p className="font-semibold text-white" style={{ fontSize: px(46) }}>
            {game.name}
          </p>
          <p className="flex items-center text-white/55" style={{ fontSize: px(18), marginTop: px(10), gap: px(10) }}>
            <span className="rounded-md bg-white font-semibold text-black" style={{ padding: `${px(1)} ${px(10)}` }}>
              Steam
            </span>
            {game.year} · {game.developer} · {game.time}
          </p>
        </motion.div>
      </AnimatePresence>
      <Hints categories={false} />
    </div>
  );
}

/* ----------------------------------------------------------------- export */

const VIEWS: Record<ViewKey, () => ReactNode> = {
  grid: GridView,
  list: ListView,
  listplus: ListPlusView,
  cinematic: CinematicView,
  overview: OverviewView,
  detailed: DetailedView,
  coverflow: CoverflowView,
  shelf: ShelfView,
  floppy: FloppyView,
  cartridge: CartridgeView,
};

export function ViewMock({ id }: { id: ViewKey; running?: boolean }) {
  const View = VIEWS[id];
  return (
    <div className="absolute inset-0 overflow-hidden bg-[#040506] [container-type:inline-size]">
      <View />
    </div>
  );
}
