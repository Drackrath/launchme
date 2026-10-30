import { AnimatePresence, motion, useInView } from "motion/react";
import {
  ArchiveRestore,
  ArrowUpRight,
  BarChart3,
  Check,
  Cpu,
  Gamepad2,
  ImageIcon,
  Layers,
  Monitor,
  Palette,
  Pause,
  Play,
  Puzzle,
  RotateCcw,
  Snowflake,
  Terminal,
  Trophy,
  Undo2,
  Zap,
} from "lucide-react";
import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { LogoSymbol } from "../components/Logo";
import { PadToast, type ToastDevice } from "../components/PadToast";
import { Marquee, Reveal, SectionHeader, SpotlightCard } from "../components/ui";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";
import { art, gameById } from "../lib/assets";
import { LINUX_DOC_URL, SDK_URL } from "../lib/release";

/* ------------------------------------------------------------ card shell */

function Card({ className, tag, icon, title, text, children, id }: { className?: string; tag: string; icon: ReactNode; title: string; text?: string; children?: ReactNode; id?: string }) {
  return (
    <SpotlightCard
      id={id}
      data-reveal
      className={cn("flex scroll-mt-28 flex-col p-6 sm:p-7", className)}
      initial={{ opacity: 0, y: 28 }}
      whileInView={{ opacity: 1, y: 0 }}
      viewport={{ once: true, margin: "0px 0px -6% 0px" }}
      transition={{ duration: 0.7, ease: [0.22, 1, 0.36, 1] }}
    >
      <div className="relative flex items-center gap-2 text-[12.5px] font-semibold uppercase tracking-[0.14em] text-muted">
        <span className="flex size-7 items-center justify-center rounded-lg border border-line bg-white/5 text-white/85">{icon}</span>
        {tag}
      </div>
      <h3 className="relative mt-4 font-display text-[24px] font-semibold leading-tight tracking-tight text-white sm:text-[27px] text-balance">{title}</h3>
      {text && <p className="relative mt-2.5 max-w-prose text-[15px] leading-relaxed text-muted text-pretty">{text}</p>}
      {children}
    </SpotlightCard>
  );
}

/* ------------------------------------------------------------- VRAM card */

const SAMPLES = 72;

function MemoryGraph({ playing, active }: { playing: boolean; active: boolean }) {
  const [points, setPoints] = useState(() => ({ vram: Array(SAMPLES).fill(143) as number[], ram: Array(SAMPLES).fill(150) as number[] }));
  const since = useRef(0);
  useEffect(() => {
    since.current = 0;
  }, [playing]);
  useEffect(() => {
    if (!active) return;
    const id = setInterval(() => {
      since.current += 1;
      setPoints((prev) => {
        const last = prev.vram[prev.vram.length - 1];
        const lastRam = prev.ram[prev.ram.length - 1];
        // The launcher waits a few seconds, then gives everything back at once.
        const vramTarget = playing ? (since.current > 14 ? 0 : 143) : 143;
        const ramTarget = playing ? (since.current > 14 ? 100 : 150) : 150;
        const noise = () => (Math.random() - 0.5) * 3;
        const vram = Math.max(0, last + (vramTarget - last) * 0.35 + (vramTarget ? noise() : 0));
        const ram = lastRam + (ramTarget - lastRam) * 0.25 + noise();
        return { vram: [...prev.vram.slice(1), vram], ram: [...prev.ram.slice(1), ram] };
      });
    }, 110);
    return () => clearInterval(id);
  }, [active, playing]);

  const toPath = (values: number[], max: number, close: boolean) => {
    const d = values.map((v, i) => `${i === 0 ? "M" : "L"}${(i / (SAMPLES - 1)) * 300},${100 - (v / max) * 88}`).join(" ");
    return close ? `${d} L300,100 L0,100 Z` : d;
  };
  const vramNow = Math.round(points.vram[SAMPLES - 1]);
  const ramNow = Math.round(points.ram[SAMPLES - 1]);
  return (
    <div className="relative">
      <div className="mb-3 flex items-end gap-6">
        <div>
          <p className="text-[12px] font-semibold uppercase tracking-wider text-[#7fb2ff]">VRAM</p>
          <p className="font-display text-4xl font-semibold tabular-nums text-white">
            {vramNow}
            <span className="ml-1 text-lg text-white/50">MB</span>
          </p>
        </div>
        <div>
          <p className="text-[12px] font-semibold uppercase tracking-wider text-white/45">RAM</p>
          <p className="font-display text-2xl font-semibold tabular-nums text-white/70">
            ~{ramNow}
            <span className="ml-1 text-base text-white/40">MB</span>
          </p>
        </div>
      </div>
      <svg viewBox="0 0 300 100" preserveAspectRatio="none" className="h-36 w-full overflow-visible">
        <defs>
          <linearGradient id="vram-fill" x1="0" x2="0" y1="0" y2="1">
            <stop offset="0" stopColor="#7fb2ff" stopOpacity="0.45" />
            <stop offset="1" stopColor="#7fb2ff" stopOpacity="0" />
          </linearGradient>
        </defs>
        {[25, 50, 75].map((y) => (
          <line key={y} x1="0" x2="300" y1={y} y2={y} stroke="rgb(255 255 255 / 0.06)" vectorEffect="non-scaling-stroke" />
        ))}
        <path d={toPath(points.ram, 220, false)} fill="none" stroke="rgb(255 255 255 / 0.35)" strokeWidth="1.5" strokeDasharray="4 4" vectorEffect="non-scaling-stroke" />
        <path d={toPath(points.vram, 220, true)} fill="url(#vram-fill)" />
        <path d={toPath(points.vram, 220, false)} fill="none" stroke="#7fb2ff" strokeWidth="2" vectorEffect="non-scaling-stroke" />
      </svg>
    </div>
  );
}

function VramCard() {
  const { t } = usePage();
  const v = t.bento.vram;
  const ref = useRef<HTMLDivElement>(null);
  const inView = useInView(ref, { margin: "-10% 0px" });
  const [playing, setPlaying] = useState(false);
  return (
    <Card className="lg:col-span-4 lg:row-span-2" tag={v.tag} icon={<Zap className="size-4" />} title={v.title} text={v.text}>
      <div ref={ref} className="relative mt-6 grid items-center gap-6 md:grid-cols-[1.25fr_1fr]">
        <div className="rounded-2xl border border-line bg-black/30 p-4">
          <div className="mb-3 flex items-center justify-between gap-3">
            <span className="flex items-center gap-2 text-[13px] text-white/60">
              <span className={cn("size-2 rounded-full", playing ? "bg-ok shadow-[0_0_10px_#3ddc84]" : "bg-white/40")} />
              {playing ? v.running : v.idle}
            </span>
            <button
              onClick={() => setPlaying((p) => !p)}
              className={cn(
                "inline-flex items-center gap-2 rounded-full px-4 py-2 text-[13px] font-semibold transition-colors",
                playing ? "bg-white/10 text-white hover:bg-white/15" : "bg-white text-ink hover:bg-white/90",
              )}
            >
              {playing ? <Undo2 className="size-4" /> : <Play className="size-4 fill-current" />}
              {playing ? v.back : v.play}
            </button>
          </div>
          <MemoryGraph playing={playing} active={inView} />
        </div>
        <div className="flex flex-col justify-center gap-5">
          {v.rows.map((row, i) => (
            <div key={row.label}>
              <div className="mb-2 flex items-baseline justify-between text-[13.5px]">
                <span className="text-white/75">{row.label}</span>
                <span className="tabular-nums text-white/50">
                  <span className="line-through decoration-white/30">{row.from}</span> → <span className="font-semibold text-white">{row.to} MB</span>
                </span>
              </div>
              <div className="relative h-2.5 overflow-hidden rounded-full bg-white/8">
                <div className="absolute inset-y-0 left-0 rounded-full bg-white/15" style={{ width: `${(row.from / 650) * 100}%` }} />
                <motion.div
                  className="absolute inset-y-0 left-0 rounded-full bg-[linear-gradient(90deg,#7fb2ff,#b9d4ff)]"
                  initial={{ width: `${(row.from / 650) * 100}%` }}
                  whileInView={{ width: `${(row.to / 650) * 100}%` }}
                  viewport={{ once: true }}
                  transition={{ duration: 1.6, delay: 0.4 + i * 0.2, ease: [0.22, 1, 0.36, 1] }}
                />
              </div>
            </div>
          ))}
          <p className="flex items-start gap-2 rounded-2xl border border-line bg-white/[0.03] p-3.5 text-[13.5px] leading-snug text-white/70">
            <Layers className="mt-0.5 size-4 shrink-0 text-[#7fb2ff]" />
            {v.bc1}
          </p>
        </div>
      </div>
    </Card>
  );
}

/* -------------------------------------------------------------- Pads card */

const PADS = ["dualsense", "xbox", "ultimate2", "dualshock4", "switch-pro", "steam-controller2", "dualsense-edge", "xbox-elite", "steam-deck", "joycon-pair", "8bitdo-sn", "zero2"];
const PAD_NAMES: Record<string, string> = {
  dualsense: "DualSense",
  xbox: "Xbox Series X|S",
  ultimate2: "8BitDo Ultimate 2",
  dualshock4: "DualShock 4",
  "switch-pro": "Switch Pro Controller",
  "steam-controller2": "Steam Controller",
  "dualsense-edge": "DualSense Edge",
  "xbox-elite": "Xbox Elite",
  "steam-deck": "Steam Deck",
  "joycon-pair": "Joy-Con (L/R)",
  "8bitdo-sn": "8BitDo SN30 Pro",
  zero2: "8BitDo Zero 2",
};

function PadsCard() {
  const { t, asset } = usePage();
  const p = t.bento.pads;
  const ref = useRef<HTMLDivElement>(null);
  const inView = useInView(ref);
  const [index, setIndex] = useState(0);
  useEffect(() => {
    if (!inView) return;
    const id = setInterval(() => setIndex((i) => (i + 1) % (PADS.length + 1)), 2400);
    return () => clearInterval(id);
  }, [inView]);
  const headphones = index === PADS.length;
  const device: ToastDevice = headphones
    ? { kind: "headphones", name: "ATH-M50xBT2", battery: 65 }
    : { kind: "pad", pad: PADS[index], name: PAD_NAMES[PADS[index]], battery: [85, null, 40, 15, 100, 72][index % 6] };
  return (
    <Card className="lg:col-span-2 lg:row-span-2" tag={p.tag} icon={<Gamepad2 className="size-4" />} title={p.title} text={p.text}>
      <div ref={ref} className="relative mt-6 flex flex-1 flex-col gap-4">
        <PadToast device={device} title={headphones ? p.headphones : p.connected} className="w-full" />
        <div className="grid grid-cols-4 gap-2">
          {PADS.map((pad, i) => (
            <button
              key={pad}
              onClick={() => setIndex(i)}
              title={PAD_NAMES[pad]}
              className={cn(
                "flex aspect-[4/3] items-center justify-center rounded-xl border p-1.5 transition-all duration-300",
                i === index ? "border-white/60 bg-white/10" : "border-line bg-white/[0.02] opacity-60 hover:opacity-100",
              )}
            >
              <img src={asset(`img/pads/${pad}.webp`)} alt={PAD_NAMES[pad]} className="max-h-full max-w-full object-contain" loading="lazy" />
            </button>
          ))}
        </div>
      </div>
    </Card>
  );
}

/* --------------------------------------------------- Customization card */

const ACCENTS = ["#ffffff", "#7fb2ff", "#4f7bff", "#a78bfa", "#e879f9", "#ff5d8f", "#ff7a6b", "#ff9f43", "#e8c26a", "#c6f432", "#3ddc84", "#2dd4bf"];
const GLYPHS = {
  xbox: { label: "Xbox", a: "A", b: "B", colors: ["#3ddc84", "#ff5d5d"] },
  ps: { label: "PlayStation", a: "✕", b: "○", colors: ["#7fb2ff", "#ff7a8a"] },
  nintendo: { label: "Nintendo", a: "B", b: "A", colors: ["#f3f4f6", "#f3f4f6"] },
} as const;
type GlyphKey = keyof typeof GLYPHS;

function Segmented<T extends string>({ value, options, onChange, labels }: { value: T; options: readonly T[]; onChange: (v: T) => void; labels?: readonly string[] }) {
  return (
    <div className="inline-flex rounded-full border border-line bg-black/30 p-0.5">
      {options.map((option, i) => (
        <button
          key={option}
          onClick={() => onChange(option)}
          className={cn("relative rounded-full px-3 py-1.5 text-[12.5px] transition-colors", value === option ? "text-ink" : "text-white/65 hover:text-white")}
        >
          {value === option && <motion.span layoutId={`seg-${options.join("")}`} className="absolute inset-0 rounded-full bg-white" transition={{ type: "spring", stiffness: 500, damping: 36 }} />}
          <span className="relative">{labels?.[i] ?? option}</span>
        </button>
      ))}
    </div>
  );
}

function CustomCard() {
  const { t, asset } = usePage();
  const c = t.bento.custom;
  const [shape, setShape] = useState<"squircle" | "rounded" | "square">("squircle");
  const [glyph, setGlyph] = useState<GlyphKey>("xbox");
  const [accent, setAccent] = useState(ACCENTS[0]);
  const [entrance, setEntrance] = useState<"grow" | "rise" | "none">("grow");
  const [replay, setReplay] = useState(0);
  const radius = shape === "squircle" ? "28%" : shape === "rounded" ? "12%" : "3%";
  const tiles = ["silksong", "clair", "cuphead", "seaofstars"];
  const g = GLYPHS[glyph];
  const initial = entrance === "grow" ? { scale: 0.6, opacity: 0 } : entrance === "rise" ? { y: 30, opacity: 0 } : { opacity: 1 };

  return (
    <Card className="lg:col-span-3 lg:row-span-2" tag={c.tag} icon={<Palette className="size-4" />} title={c.title} text={c.text}>
      <div className="relative mt-5 overflow-hidden rounded-2xl border border-line">
        <div className="relative aspect-[16/8] w-full bg-[#0b0c0f] transition-[background] duration-500" style={{ background: `radial-gradient(120% 90% at 20% 0%, ${accent}22, transparent 60%), #0b0c0f` }}>
          <div className="absolute inset-0 flex items-center gap-3 px-5" key={`${replay}-${entrance}`}>
            {tiles.map((tile, i) => (
              <motion.div
                key={tile}
                className="relative aspect-square flex-1 overflow-hidden"
                initial={initial}
                animate={{ scale: i === 0 ? 1.08 : 1, opacity: 1, y: 0 }}
                transition={{ type: "spring", stiffness: 260, damping: 22, delay: entrance === "none" ? 0 : 0.08 * i }}
                style={{ borderRadius: radius, boxShadow: i === 0 ? `0 0 0 3px ${accent}, 0 10px 30px -10px ${accent}` : undefined, zIndex: i === 0 ? 2 : 1 }}
              >
                <img src={asset(art("c", tile))} alt="" className="h-full w-full object-cover" />
              </motion.div>
            ))}
          </div>
          <div className="absolute bottom-3 right-3 flex items-center gap-3 rounded-full bg-black/55 px-3 py-1.5 text-[12px] text-white/85 backdrop-blur">
            <span className="flex items-center gap-1.5">
              <span className="flex size-5 items-center justify-center rounded-full text-[11px] font-bold text-ink" style={{ background: g.colors[0] }}>
                {g.a}
              </span>
              Play
            </span>
            <span className="flex items-center gap-1.5">
              <span className="flex size-5 items-center justify-center rounded-full text-[11px] font-bold text-ink" style={{ background: g.colors[1] }}>
                {g.b}
              </span>
              Back
            </span>
          </div>
        </div>
      </div>
      <div className="relative mt-4 grid gap-2.5 text-[12.5px] text-white/55 sm:grid-cols-2">
        <label className="flex flex-col gap-1.5">
          {c.shape}
          <Segmented value={shape} options={["squircle", "rounded", "square"] as const} labels={c.shapes} onChange={setShape} />
        </label>
        <label className="flex flex-col gap-1.5">
          {c.glyphs}
          <Segmented value={glyph} options={["xbox", "ps", "nintendo"] as const} labels={["Xbox", "PS", "Nintendo"]} onChange={setGlyph} />
        </label>
        <label className="flex flex-col gap-1.5">
          {c.entrance}
          <div className="flex items-center gap-2">
            <Segmented
              value={entrance}
              options={["grow", "rise", "none"] as const}
              labels={c.entrances}
              onChange={(v) => {
                setEntrance(v);
                setReplay((r) => r + 1);
              }}
            />
            <button onClick={() => setReplay((r) => r + 1)} className="flex size-8 items-center justify-center rounded-full border border-line text-white/70 hover:text-white" aria-label={c.replay}>
              <RotateCcw className="size-3.5" />
            </button>
          </div>
        </label>
        <div className="flex flex-col gap-1.5 sm:col-span-2">
          {c.accent}
          <div className="flex flex-wrap items-center gap-2">
            {ACCENTS.map((color) => (
              <button
                key={color}
                onClick={() => setAccent(color)}
                className={cn("size-7 rounded-full border-2 transition-transform hover:scale-110", accent === color ? "scale-110 border-white" : "border-transparent")}
                style={{ background: color }}
                aria-label={color}
              />
            ))}
          </div>
        </div>
      </div>
      <ul className="relative mt-5 grid gap-1.5 text-[13.5px] text-white/70 sm:grid-cols-2">
        {c.list.map((item) => (
          <li key={item} className="flex items-start gap-2">
            <Check className="mt-0.5 size-3.5 shrink-0 text-white/40" />
            {item}
          </li>
        ))}
      </ul>
    </Card>
  );
}

/* ------------------------------------------------------------ Stores card */

const LEFT_STORES = [
  { name: "Steam", color: "#66c0f4" },
  { name: "GOG", color: "#b46bff" },
  { name: "Epic Games", color: "#e6e6e6" },
  { name: "Battle.net", color: "#00aeff" },
  { name: "EA app", color: "#ff4747" },
  { name: "Ubisoft Connect", color: "#3a8dff" },
];
const RIGHT_STORES = [
  { name: "Amazon Games", color: "#ff9900" },
  { name: "Xbox (PC)", color: "#3ddc84" },
  { name: "DRM-free", color: "#d0d0d0" },
  { name: "Prism Launcher", color: "#7ed957" },
  { name: "Emulators", color: "#ff6bd6" },
  { name: "Your .exe", color: "#e8c26a" },
];

function StoresCard() {
  const { t } = usePage();
  const s = t.bento.stores;
  const rows = LEFT_STORES.length;
  const y = (i: number) => 8 + (i * 84) / (rows - 1);
  const beam = (side: "l" | "r", i: number) => {
    const x0 = side === "l" ? 27 : 73;
    const y0 = y(i);
    return `M${x0},${y0} C${side === "l" ? 40 : 60},${y0} ${side === "l" ? 40 : 60},50 50,50`;
  };
  return (
    <Card className="lg:col-span-3 lg:row-span-2" tag={s.tag} icon={<Layers className="size-4" />} title={s.title} text={s.text}>
      <div className="relative mt-6 h-[300px] flex-1">
        <svg viewBox="0 0 100 100" preserveAspectRatio="none" className="absolute inset-0 h-full w-full">
          <defs>
            <linearGradient id="beam" x1="0" x2="1">
              <stop offset="0" stopColor="#ffffff" stopOpacity="0" />
              <stop offset="0.5" stopColor="#ffffff" stopOpacity="0.95" />
              <stop offset="1" stopColor="#ffffff" stopOpacity="0" />
            </linearGradient>
          </defs>
          {(["l", "r"] as const).flatMap((side) =>
            Array.from({ length: rows }, (_, i) => (
              <g key={side + i}>
                <path d={beam(side, i)} fill="none" stroke="rgb(255 255 255 / 0.09)" strokeWidth="1.2" vectorEffect="non-scaling-stroke" />
                <motion.path
                  d={beam(side, i)}
                  fill="none"
                  stroke={(side === "l" ? LEFT_STORES : RIGHT_STORES)[i].color}
                  strokeWidth="1.6"
                  strokeLinecap="round"
                  vectorEffect="non-scaling-stroke"
                  initial={{ pathLength: 0.18, pathOffset: 0, opacity: 0 }}
                  animate={{ pathOffset: [0, 0.82], opacity: [0, 1, 1, 0] }}
                  transition={{ duration: 2.4, repeat: Infinity, delay: (i * 0.37 + (side === "r" ? 0.6 : 0)) % 2.4, ease: "easeInOut" }}
                />
              </g>
            )),
          )}
        </svg>
        {(["l", "r"] as const).map((side) =>
          (side === "l" ? LEFT_STORES : RIGHT_STORES).map((store, i) => (
            <span
              key={store.name}
              className={cn(
                "absolute flex -translate-y-1/2 items-center gap-2 whitespace-nowrap rounded-full border border-line bg-panel-3/90 px-3 py-1.5 text-[12.5px] text-white/85",
                side === "l" ? "left-0" : "right-0",
              )}
              style={{ top: `${y(i)}%` }}
            >
              <span className="size-2 rounded-full" style={{ background: store.color, boxShadow: `0 0 8px ${store.color}` }} />
              {store.name}
            </span>
          )),
        )}
        <div className="absolute left-1/2 top-1/2 flex size-20 -translate-x-1/2 -translate-y-1/2 items-center justify-center rounded-[26px] border border-white/20 bg-[#101114] shadow-[0_0_60px_-10px_rgb(255_255_255/0.35)]">
          <motion.span
            className="lowmem-hide absolute inset-0 rounded-[26px] border border-white/30"
            animate={{ scale: [1, 1.25], opacity: [0.6, 0] }}
            transition={{ duration: 2.2, repeat: Infinity, ease: "easeOut" }}
          />
          <LogoSymbol className="size-10 text-white" />
        </div>
      </div>
    </Card>
  );
}

/* ------------------------------------------------------------ Linux card */

function Typewriter({ text, active }: { text: string; active: boolean }) {
  const [count, setCount] = useState(0);
  useEffect(() => {
    if (!active) return;
    setCount(0);
    let i = 0;
    const id = setInterval(() => {
      i += 1;
      setCount(i);
      if (i >= text.length) clearInterval(id);
    }, 38);
    return () => clearInterval(id);
  }, [text, active]);
  return (
    <span>
      {text.slice(0, count)}
      <span className="ml-px inline-block h-[1.05em] w-[0.5em] translate-y-[0.15em] animate-blink bg-white/80" />
    </span>
  );
}

function LinuxCard() {
  const { t } = usePage();
  const l = t.bento.linux;
  const ref = useRef<HTMLDivElement>(null);
  const inView = useInView(ref, { once: true, margin: "-15% 0px" });
  const rows: [string, string, boolean][] = [
    ["Proton / Wine", "GE-Proton10-25", true],
    ["Prefix", "elden-ring · 2.3 GB", false],
    ["Synchronisation", "ntsync", true],
    ["gamescope", "2560×1440 · FSR", true],
    ["MangoHud", "on", false],
    ["lsfg-vk", "×2 frame generation", true],
  ];
  return (
    <Card id="linux" className="lg:col-span-6 lg:row-span-2" tag={l.tag} icon={<Terminal className="size-4" />} title={l.title} text={l.text}>
      <div className="relative mt-6 grid flex-1 gap-6 lg:grid-cols-[1fr_1.15fr]">
        <div className="flex flex-col gap-6">
          <div className="flex flex-wrap gap-2">
            {l.chips.map((chip, i) => (
              <motion.span
                key={chip}
                initial={{ opacity: 0, y: 8 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ delay: 0.04 * i }}
                className="rounded-full border border-line bg-white/[0.04] px-3 py-1.5 font-mono text-[12.5px] text-white/80"
              >
                {chip}
              </motion.span>
            ))}
          </div>

          <div className="mt-auto flex flex-wrap items-center gap-3">
            <p className="rounded-xl border border-amber-400/25 bg-amber-400/10 px-3.5 py-2 text-[13px] text-amber-200/90">{l.warn}</p>
            <a href={LINUX_DOC_URL} target="_blank" rel="noreferrer" className="inline-flex items-center gap-1.5 text-[14px] font-medium text-white hover:underline">
              {l.doc} <ArrowUpRight className="size-4" />
            </a>
          </div>
        </div>

        {/* A game's Proton / Wine page, condensed. */}
        <div ref={ref} className="overflow-hidden rounded-2xl border border-line bg-[#0b0c0f]">
          <div className="flex items-center justify-between border-b border-line px-4 py-3">
            <span className="text-[14px] font-semibold text-white">Elden Ring · Proton / Wine</span>
            <span className="flex gap-1.5">
              <span className="rounded-full bg-[#b4c7dc]/15 px-2 py-0.5 text-[11px] font-semibold text-[#c9d8ea]">ProtonDB Platinum</span>
              <span className="rounded-full bg-ok/15 px-2 py-0.5 text-[11px] font-semibold text-ok">Deck Verified</span>
            </span>
          </div>
          <div className="divide-y divide-line">
            {rows.map(([name, value, own], i) => (
              <motion.div
                key={name}
                initial={{ opacity: 0, x: 12 }}
                animate={inView ? { opacity: 1, x: 0 } : {}}
                transition={{ delay: 0.15 + i * 0.08 }}
                className="flex items-center justify-between gap-3 px-4 py-2.5 text-[13.5px]"
              >
                <span className="text-white/60">{name}</span>
                <span className="flex items-center gap-2">
                  {own && <span className="rounded-full border border-white/25 px-2 py-0.5 text-[10.5px] font-semibold uppercase tracking-wide text-white/75">This game</span>}
                  <span className="font-mono text-[12.5px] text-white">{value}</span>
                </span>
              </motion.div>
            ))}
          </div>
          <div className="border-t border-line bg-black/40 px-4 py-3.5">
            <p className="mb-1.5 text-[11.5px] font-semibold uppercase tracking-wider text-white/40">Launch options</p>
            <p className="break-all font-mono text-[13px] leading-relaxed text-[#a8d1ff]">
              <Typewriter text="PROTON_ENABLE_WAYLAND=1 WINEDLLOVERRIDES=dxgi=n,b gamemoderun %command% -dx12" active={inView} />
            </p>
          </div>
        </div>
      </div>
    </Card>
  );
}

/* ----------------------------------------------------------- small cards */

function PluginsCard() {
  const { t } = usePage();
  const p = t.bento.plugins;
  const json: [string, string][] = [
    ["apiVersion", "1"],
    ["id", '"launchme.howlongtobeat"'],
    ["entry", '"howlongtobeat.dll"'],
    ["tags", '["details", "widget"]'],
  ];
  return (
    <Card className="lg:col-span-2 lg:row-span-2" tag={p.tag} icon={<Puzzle className="size-4" />} title={p.title} text={p.text}>
      <div className="relative mt-5 flex flex-1 flex-col overflow-hidden rounded-2xl border border-line bg-[#0b0c0f] p-4 font-mono text-[12.5px] leading-6">
        <p className="mb-2 text-[11px] uppercase tracking-wider text-white/35">plugin.json</p>
        <p className="text-white/50">{"{"}</p>
        {json.map(([key, value], i) => (
          <motion.p key={key} className="pl-4" initial={{ opacity: 0, x: -6 }} whileInView={{ opacity: 1, x: 0 }} viewport={{ once: true }} transition={{ delay: 0.1 * i }}>
            <span className="text-[#9cdcfe]">"{key}"</span>
            <span className="text-white/50">: </span>
            <span className={value.startsWith('"') || value.startsWith("[") ? "text-[#ce9178]" : "text-[#b5cea8]"}>{value}</span>
            {i < json.length - 1 && <span className="text-white/50">,</span>}
          </motion.p>
        ))}
        <p className="text-white/50">{"}"}</p>
        <div className="mt-auto flex flex-wrap gap-1.5 pt-3">
          {["HowLongToBeat", "Achievements"].map((name) => (
            <span key={name} className="rounded-full bg-white/8 px-2.5 py-1 font-sans text-[11.5px] text-white/75">
              {name}
            </span>
          ))}
        </div>
      </div>
      <a href={SDK_URL} target="_blank" rel="noreferrer" className="relative mt-4 inline-flex items-center gap-1.5 text-[14px] font-medium text-white hover:underline">
        {p.sdk} <ArrowUpRight className="size-4" />
      </a>
    </Card>
  );
}

function SavesCard() {
  const { t, asset } = usePage();
  const s = t.bento.saves;
  const [restored, setRestored] = useState(false);
  const saves = useMemo(
    () => [
      { when: "Today, 21:42", size: "18.4 MB" },
      { when: "Yesterday, 23:10", size: "18.1 MB" },
      { when: "27 Oct, 20:05", size: "17.6 MB" },
    ],
    [],
  );
  return (
    <Card className="lg:col-span-2 lg:row-span-2" tag={s.tag} icon={<ArchiveRestore className="size-4" />} title={s.title} text={s.text}>
      <div className="relative mt-5 flex flex-1 flex-col rounded-2xl border border-line bg-[#0b0c0f] p-3">
        <div className="flex items-center gap-3 border-b border-line px-1 pb-3">
          <img src={asset(art("c", "silksong"))} alt="" className="size-10 rounded-xl" loading="lazy" />
          <div>
            <p className="text-[14px] font-semibold text-white">Hollow Knight: Silksong</p>
            <p className="text-[12px] text-white/50">Ludusavi · 3 backups</p>
          </div>
        </div>
        {saves.map((save, i) => (
          <div key={save.when} className="flex items-center justify-between px-1 py-2.5 text-[13px]">
            <span className="flex items-center gap-2 text-white/75">
              <Check className={cn("size-3.5", i === 0 ? "text-ok" : "text-white/30")} />
              {save.when}
            </span>
            <span className="tabular-nums text-white/40">{save.size}</span>
          </div>
        ))}
        <button
          onClick={() => {
            setRestored(true);
            setTimeout(() => setRestored(false), 2200);
          }}
          className="mt-auto flex w-full items-center justify-center gap-2 rounded-xl bg-white/8 py-2.5 text-[13px] font-semibold text-white transition-colors hover:bg-white/12"
        >
          <AnimatePresence mode="wait">
            <motion.span key={String(restored)} initial={{ opacity: 0, y: 6 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -6 }} className="flex items-center gap-2">
              {restored ? <Check className="size-4 text-ok" /> : <RotateCcw className="size-4" />}
              {restored ? s.backup : s.restore}
            </motion.span>
          </AnimatePresence>
        </button>
      </div>
    </Card>
  );
}

function AchievementsCard() {
  const { t } = usePage();
  const a = t.bento.achievements;
  const sources = ["Steam", "GOG", "Epic", "RetroAchievements", "RPCS3", "shadPS4"];
  return (
    <Card className="lg:col-span-2" tag={a.tag} icon={<Trophy className="size-4" />} title={a.title} text={a.text}>
      <div className="relative mt-4 flex flex-wrap gap-1.5">
        {sources.map((source) => (
          <span key={source} className="rounded-full border border-gold/25 bg-gold/10 px-2.5 py-1 text-[12px] text-gold">
            {source}
          </span>
        ))}
      </div>
    </Card>
  );
}

function NativeCard() {
  const { t } = usePage();
  const n = t.bento.native;
  return (
    <Card className="lg:col-span-2" tag={n.tag} icon={<Cpu className="size-4" />} title={n.title} text={n.text}>
      <div className="relative mt-4 flex flex-wrap gap-1.5 font-mono text-[12px]">
        {["C++20", "Direct2D", "DirectWrite", "XAudio2", "SDL3", "Skia", "Vulkan"].map((tech) => (
          <span key={tech} className="rounded-md border border-line bg-white/[0.04] px-2 py-1 text-white/70">
            {tech}
          </span>
        ))}
      </div>
    </Card>
  );
}

function ArtworkCard() {
  const { t, asset } = usePage();
  const a = t.bento.artwork;
  const covers = ["castlevania", "silenthillf", "clair", "wukong", "ds2", "p3r"];
  return (
    <Card className="lg:col-span-3 lg:row-span-2" tag={a.tag} icon={<ImageIcon className="size-4" />} title={a.title} text={a.text}>
      <div className="relative mt-6 flex flex-1 items-center justify-center pb-4 [perspective:900px]">
        {covers.map((id, i) => {
          const mid = (covers.length - 1) / 2;
          const d = i - mid;
          return (
            <motion.img
              key={id}
              src={asset(art("p", id))}
              alt=""
              loading="lazy"
              className="-mx-5 w-[22%] rounded-xl border border-white/10 shadow-[0_20px_40px_-15px_rgb(0_0_0/0.9)]"
              initial={{ rotateY: 0, y: 40, opacity: 0 }}
              whileInView={{ rotateY: d * -9, y: Math.abs(d) * 10, opacity: 1, rotateZ: d * 2.5 }}
              whileHover={{ y: -14, scale: 1.06, zIndex: 10 }}
              viewport={{ once: true }}
              transition={{ type: "spring", stiffness: 160, damping: 18, delay: 0.06 * i }}
              style={{ zIndex: 10 - Math.round(Math.abs(d)) }}
            />
          );
        })}
      </div>
    </Card>
  );
}

function DesktopCard() {
  const { t, asset } = usePage();
  const d = t.bento.desktop;
  return (
    <Card className="overflow-hidden lg:col-span-3 lg:row-span-2" tag={d.tag} icon={<Monitor className="size-4" />} title={d.title} text={d.text}>
      <motion.div
        className="relative -mb-7 -mr-7 mt-6 flex-1 overflow-hidden rounded-tl-2xl border-l border-t border-white/12 sm:-mb-7"
        initial={{ x: 40, y: 30, opacity: 0 }}
        whileInView={{ x: 0, y: 0, opacity: 1 }}
        viewport={{ once: true }}
        transition={{ duration: 0.9, ease: [0.22, 1, 0.36, 1] }}
      >
        <img src={asset("img/shots/desktop-sm.webp")} alt="Desktop window" loading="lazy" className="w-[140%] max-w-none object-cover object-left-top" />
      </motion.div>
    </Card>
  );
}

/* ------------------------------------------------------- Quick Resume card */

function QuickResumeCard() {
  const { t, asset } = usePage();
  const q = t.bento.quick;
  const games = ["clair", "silksong", "cyberpunk"];
  const [running, setRunning] = useState(1);
  const [suspended, setSuspended] = useState<number[]>([0]);
  // Starting or resuming one game suspends the others, like the launcher does.
  const resume = (i: number) => {
    setSuspended((list) => [...list.filter((n) => n !== i), ...(running >= 0 && running !== i ? [running] : [])]);
    setRunning(i);
  };
  const suspend = (i: number) => {
    setSuspended((list) => [...list, i]);
    setRunning(-1);
  };
  return (
    <Card className="lg:col-span-3 lg:row-span-2" tag={q.tag} icon={<Snowflake className="size-4" />} title={q.title} text={q.text}>
      <div className="relative mt-6 flex flex-1 flex-col gap-2.5">
        {games.map((id, i) => {
          const isRunning = running === i;
          const isSuspended = suspended.includes(i);
          return (
            <motion.div layout key={id} className="flex items-center gap-3 rounded-2xl border border-line bg-[#0b0c0f] p-2.5 pr-3 sm:gap-4">
              <div className="relative h-12 w-20 shrink-0 overflow-hidden rounded-xl sm:h-14 sm:w-[120px]">
                <img src={asset(art("l", id))} alt="" loading="lazy" className={cn("h-full w-full object-cover transition-[filter] duration-700", isSuspended && "brightness-75 grayscale")} />
                <AnimatePresence>
                  {isSuspended && (
                    <motion.span initial={{ opacity: 0, scale: 0.6 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0 }} className="absolute inset-0 flex items-center justify-center bg-[#7fb2ff]/15">
                      <Snowflake className="size-6 text-[#cfe2ff]" />
                    </motion.span>
                  )}
                </AnimatePresence>
              </div>
              <div className="min-w-0 flex-1">
                <p className="truncate text-[14.5px] font-semibold text-white">{gameById[id].name}</p>
                <p className="mt-0.5 flex items-center gap-1.5 text-[12.5px]">
                  {isRunning ? (
                    <>
                      <span className="size-1.5 rounded-full bg-ok shadow-[0_0_8px_#3ddc84]" />
                      <span className="text-ok">{q.running}</span>
                    </>
                  ) : isSuspended ? (
                    <span className="text-[#a9c9ff]">{q.suspended}</span>
                  ) : (
                    <span className="text-white/40">—</span>
                  )}
                </p>
              </div>
              <button
                type="button"
                onClick={() => (isRunning ? suspend(i) : resume(i))}
                className={cn(
                  "inline-flex shrink-0 items-center gap-1.5 rounded-full px-3.5 py-1.5 text-[12.5px] font-semibold transition-colors",
                  isRunning ? "bg-white/10 text-white hover:bg-white/15" : "bg-white text-ink hover:bg-white/90",
                )}
              >
                {isRunning ? <Pause className="size-3.5" /> : <Play className="size-3.5 fill-current" />}
                {isRunning ? q.suspend : q.resume}
              </button>
            </motion.div>
          );
        })}
      </div>
    </Card>
  );
}

/* ------------------------------------------------------------ Profile card */

const WEEK = [0, 95, 170, 0, 140, 260, 85];

function ProfileCard() {
  const { t, asset, lang } = usePage();
  const p = t.bento.profile;
  const hours = (minutes: number) => (lang === "pl" ? `${Math.floor(minutes / 60)} godz. ${minutes % 60} min` : `${Math.floor(minutes / 60)} h ${minutes % 60} min`);
  const top: [string, number][] = [
    ["clair", 475],
    ["silksong", 155],
    ["liesofp", 120],
  ];
  return (
    <Card className="lg:col-span-3 lg:row-span-2" tag={p.tag} icon={<BarChart3 className="size-4" />} title={p.title} text={p.text}>
      <div className="relative mt-6 grid flex-1 gap-3 rounded-2xl border border-line bg-[#0b0c0f] p-4">
        <p className="text-[11.5px] font-semibold uppercase tracking-wider text-white/40">{p.week}</p>
        <div className="grid grid-cols-2 gap-2">
          {[
            [p.playtime, hours(750), p.more],
            [p.games, "3", ""],
            [p.launches, "11", ""],
            [p.achievements, "4", p.streak],
          ].map(([label, value, note]) => (
            <div key={label} className="rounded-xl bg-white/[0.04] px-3 py-2.5">
              <p className="text-[10.5px] font-semibold uppercase tracking-wider text-white/40">{label}</p>
              <p className="mt-0.5 font-display text-[18px] font-semibold text-white">{value}</p>
              {note && <p className="mt-0.5 text-[10.5px] leading-tight text-ok">{note}</p>}
            </div>
          ))}
        </div>
        <div className="grid gap-3 sm:grid-cols-[1.1fr_1fr]">
          <div className="flex h-[118px] items-end justify-between gap-2 rounded-xl bg-white/[0.03] px-3 pb-2 pt-4">
            {WEEK.map((minutes, i) => (
              <div key={i} className="flex flex-1 flex-col items-center gap-1.5">
                <motion.div
                  className={cn("w-full max-w-[22px] origin-bottom rounded-md", i === WEEK.length - 1 ? "bg-white" : "bg-white/35")}
                  style={{ height: Math.max(3, (minutes / 260) * 72) }}
                  initial={{ scaleY: 0 }}
                  whileInView={{ scaleY: 1 }}
                  viewport={{ once: true }}
                  transition={{ duration: 0.7, delay: 0.08 * i, ease: [0.22, 1, 0.36, 1] }}
                />
                <span className="text-[10px] text-white/45">{p.days[i]}</span>
              </div>
            ))}
          </div>
          <div className="flex flex-col justify-center gap-2.5 rounded-xl bg-white/[0.03] px-3 py-2.5">
            <p className="text-[10.5px] font-semibold uppercase tracking-wider text-white/40">{p.top}</p>
            {top.map(([id, minutes]) => (
              <div key={id} className="flex items-center gap-2.5">
                <img src={asset(art("c", id))} alt="" loading="lazy" className="size-7 rounded-md" />
                <div className="min-w-0 flex-1">
                  <p className="truncate text-[12px] font-medium text-white">{gameById[id].name}</p>
                  <div className="mt-1 h-1 rounded-full bg-white/10">
                    <motion.div className="h-full rounded-full bg-white/80" initial={{ width: 0 }} whileInView={{ width: `${(minutes / 475) * 100}%` }} viewport={{ once: true }} transition={{ duration: 0.9 }} />
                  </div>
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </Card>
  );
}

/* ------------------------------------------------------------------ Bento */

export function Bento() {
  const { t } = usePage();
  return (
    <section id="features" className="relative mx-auto mt-40 max-w-6xl scroll-mt-28 px-5">
      <SectionHeader kicker={t.bento.kicker} title={t.bento.title} lead={t.bento.lead} />
      <div className="mt-12 grid auto-rows-[minmax(190px,auto)] gap-4 lg:grid-cols-6">
          <VramCard />
          <PadsCard />
          <CustomCard />
          <StoresCard />
          <QuickResumeCard />
          <ProfileCard />
          <LinuxCard />
          <PluginsCard />
          <SavesCard />
          <AchievementsCard />
          <NativeCard />
          <ArtworkCard />
          <DesktopCard />
      </div>
      <Reveal className="mt-10">
        <p className="mb-4 text-center text-[13px] font-semibold uppercase tracking-[0.16em] text-dim">{t.bento.more.title}</p>
        <Marquee duration={55} gap={12}>
          {t.bento.more.items.map((item) => (
            <span key={item} className="flex shrink-0 items-center gap-2 rounded-full border border-line bg-panel/80 px-4 py-2 text-[14px] text-white/75">
              <span className="size-1.5 rounded-full bg-white/40" />
              {item}
            </span>
          ))}
        </Marquee>
      </Reveal>
    </section>
  );
}
