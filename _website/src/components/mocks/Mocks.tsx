import { Gamepad2, Image as ImageIcon, LayoutGrid, Newspaper, Play, Power, Search, Settings, ShoppingBag } from "lucide-react";
import { art, gameById, ids } from "../../lib/assets";
import { usePage } from "../../lib/page";

// Still mocks of the two home layouts there is no screenshot of, drawn with the cached SteamGridDB art
// at 1920×1080 and scaled with container units like a screenshot.
const px = (n: number) => `${(n / 1920) * 100}cqw`;

const FLIX_FOCUS = "liesofp";
const FLIX_ROW = ["clair", "silksong", "liesofp", "cyberpunk", "bg3", "wukong", "ds2", "silenthillf", "onimusha"];
const FLIX_ROW_2 = ["castlevania", "ebb", "kena", "seaofstars", "cuphead", "p3r", "witcher3", "ff7rebirth", "kcd2"];

// Flix: the focused game's hero fills the screen, rows of posters below; the poster you rest on
// unfolds into a wide card with its art, logo and Play.
export function FlixMock() {
  const { asset, lang } = usePage();
  const game = gameById[FLIX_FOCUS];
  const poster = { w: 204, h: 307, gap: 26 };
  let x = 72 - 2 * (poster.w + poster.gap);
  return (
    <div className="absolute inset-0 overflow-hidden bg-[#060708] [container-type:inline-size]">
      <img src={asset(art("h", game.id))} alt="" className="absolute inset-0 h-full w-full object-cover" />
      <div className="absolute inset-0 bg-[linear-gradient(180deg,rgb(0_0_0/0.05)_30%,rgb(0_0_0/0.85)_75%),linear-gradient(90deg,rgb(0_0_0/0.45),transparent_55%)]" />

      <div className="absolute flex items-center text-white" style={{ right: px(48), top: px(46), gap: px(20), fontSize: px(20) }}>
        <span className="flex items-center rounded-full bg-black/35" style={{ gap: px(10), padding: `${px(5)} ${px(8)} ${px(5)} ${px(14)}` }}>
          Akane
          <span className="flex items-center justify-center rounded-full bg-white font-semibold text-black" style={{ width: px(28), height: px(28), fontSize: px(14) }}>
            C
          </span>
        </span>
        <span className="flex items-center rounded-full bg-black/35" style={{ gap: px(10), padding: `${px(5)} ${px(8)} ${px(5)} ${px(14)}` }}>
          ✦ Glance
          <span className="flex items-center justify-center rounded-full bg-white font-semibold text-black" style={{ width: px(28), height: px(28), fontSize: px(14) }}>
            G
          </span>
        </span>
        <span className="font-semibold tracking-wide">STEAM</span>
      </div>

      <p className="absolute font-semibold text-white" style={{ left: px(72), top: px(560), fontSize: px(26) }}>
        {lang === "pl" ? "Ostatnio grane" : "Recently played"} <span className="text-white/60">•</span> {game.name}
      </p>
      <div className="absolute" style={{ top: px(608), left: 0, right: 0, height: px(poster.h) }}>
        {FLIX_ROW.map((id) => {
          const focused = id === FLIX_FOCUS;
          const width = focused ? 548 : poster.w;
          const left = x;
          x += width + poster.gap;
          return (
            <div
              key={id}
              className="absolute overflow-hidden"
              style={{
                left: px(left),
                width: px(width),
                height: px(poster.h),
                borderRadius: px(18),
                boxShadow: focused ? `0 0 0 ${px(3.5)} #fff, 0 ${px(20)} ${px(50)} -${px(14)} rgb(0 0 0 / 0.8)` : undefined,
              }}
            >
              <img src={asset(art(focused ? "l" : "p", id))} alt="" loading="lazy" className="h-full w-full object-cover" />
              {focused && (
                <div className="absolute inset-0 flex flex-col justify-end bg-[linear-gradient(0deg,rgb(10_10_12/0.95)_32%,transparent_75%)]" style={{ padding: `${px(18)} ${px(22)}` }}>
                  <img src={asset(art("g", id))} alt={gameById[id].name} className="object-contain object-left" style={{ height: px(70), maxWidth: px(320) }} />
                  <div className="flex items-center justify-between" style={{ marginTop: px(16) }}>
                    <span className="flex items-center font-semibold text-white" style={{ gap: px(10), fontSize: px(20) }}>
                      <span className="flex items-center justify-center rounded-full bg-white text-black" style={{ width: px(30), height: px(30) }}>
                        <Play className="fill-current" style={{ width: px(14), height: px(14) }} />
                      </span>
                      {lang === "pl" ? "Graj" : "Play"}
                    </span>
                    <span className="text-white/60" style={{ fontSize: px(17) }}>
                      Steam · {gameById[id].time}
                    </span>
                  </div>
                </div>
              )}
            </div>
          );
        })}
      </div>
      <p className="absolute font-semibold text-white/85" style={{ left: px(72), top: px(958), fontSize: px(26) }}>
        Souls-like
      </p>
      <div className="absolute flex" style={{ left: px(72), top: px(1004), gap: px(poster.gap) }}>
        {FLIX_ROW_2.map((id) => (
          <img key={id} src={asset(art("p", id))} alt="" loading="lazy" className="shrink-0 object-cover opacity-80" style={{ width: px(poster.w), height: px(poster.h), borderRadius: px(18) }} />
        ))}
      </div>
    </div>
  );
}

const SNITCH_ROW = ids.slice(6, 18);
const SNITCH_DOCK = [Search, LayoutGrid, Newspaper, Gamepad2, ShoppingBag, ImageIcon, Settings, Power];

// Matches the launcher's snitch renderer: grey 0.175 background, profile top left, battery and clock
// top right, the focused game's name in its cover's colour after a cartridge mark, one row of
// equal squares with the focused one at the left, and the dock near the bottom.
export function SnitchMock() {
  const { asset } = usePage();
  const size = 360, gap = 20;
  const game = gameById[SNITCH_ROW[0]];
  return (
    <div className="absolute inset-0 overflow-hidden bg-[#2d2d2d] [container-type:inline-size]">
      <span className="absolute flex items-center justify-center rounded-full bg-[#4d4d4d]" style={{ left: px(72), top: px(48), width: px(64), height: px(64) }}>
        <svg viewBox="0 0 64 64" className="h-full w-full" fill="none" stroke="#f5f5f5" strokeWidth="2">
          <circle cx="32" cy="25" r="8" />
          <rect x="17" y="38" width="30" height="13" rx="6" />
        </svg>
      </span>
      <div className="absolute flex items-center text-white" style={{ right: px(80), top: px(62), gap: px(18), fontSize: px(30) }}>
        <span className="tabular-nums">7:52</span>
        <span className="text-white/90" style={{ fontSize: px(26) }}>
          85%
        </span>
        <span className="relative block border-2 border-white" style={{ width: px(38), height: px(20) }}>
          <span className="absolute bg-white" style={{ left: px(2), top: px(2), bottom: px(2), width: px(26) }} />
        </span>
      </div>

      <div className="absolute flex items-center" style={{ left: px(128), top: px(262), gap: px(20), color: game.accent }}>
        <span className="block" style={{ width: px(26), height: px(32), background: game.accent, borderRadius: px(2) }} />
        <span className="font-medium" style={{ fontSize: px(36) }}>
          {game.name}
        </span>
      </div>

      <div className="absolute flex" style={{ left: px(128), top: px(324), gap: px(gap) }}>
        {SNITCH_ROW.map((id, i) => (
          <div
            key={id}
            className="relative shrink-0 overflow-hidden"
            style={{ width: px(size), height: px(size), borderRadius: px(14), boxShadow: i === 0 ? `0 0 0 ${px(6)} #ffffff, 0 ${px(28)} ${px(56)} -${px(20)} rgb(0 0 0 / 0.7)` : undefined }}
          >
            <img src={asset(art("c", id))} alt="" loading="lazy" className="h-full w-full object-cover" />
          </div>
        ))}
      </div>

      <div className="absolute flex justify-center" style={{ left: 0, right: 0, top: px(874), gap: px(36) }}>
        {SNITCH_DOCK.map((Icon, i) => (
          <span key={i} className="flex items-center justify-center rounded-full bg-[#404040] text-white" style={{ width: px(88), height: px(88) }}>
            <Icon style={{ width: px(36), height: px(36) }} strokeWidth={1.8} />
          </span>
        ))}
      </div>
    </div>
  );
}
