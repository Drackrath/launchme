import { AnimatePresence, motion, useInView } from "motion/react";
import { Brain, Flame, Lock, MessageCircle, Moon, Sparkles } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import { Reveal, SectionHeader } from "../components/ui";
import { cn } from "../lib/cn";
import { useLowMem } from "../lib/lowmem";
import { usePage } from "../lib/page";

const CAST = ["yuki", "saki", "akane", "kiko", "mika"] as const;
const GLOW: Record<(typeof CAST)[number], string> = { yuki: "#b8c4e6", saki: "#ffc27a", akane: "#ff5a6a", kiko: "#7f9cff", mika: "#ff8cc6" };

/* -------------------------------------------------------- memory sphere */

// A small take on the launcher's "Mind and memory" sphere: points on a sphere, a few named memories
// linked to their nearest neighbours, turning slowly while it's on screen.
function MemorySphere({ labels, active, color }: { labels: string[]; active: boolean; color: string }) {
  const points = useMemo(() => {
    const n = 90;
    return Array.from({ length: n }, (_, i) => {
      const y = 1 - (i / (n - 1)) * 2;
      const r = Math.sqrt(1 - y * y);
      const theta = Math.PI * (3 - Math.sqrt(5)) * i;
      return [Math.cos(theta) * r, y, Math.sin(theta) * r] as const;
    });
  }, []);
  const named = useMemo(() => labels.map((_, i) => Math.floor(((i + 0.5) / labels.length) * points.length * 0.86 + 6)), [labels, points.length]);
  const links = useMemo(
    () =>
      named.flatMap((a) => {
        const [ax, ay, az] = points[a];
        return points
          .map((p, b) => [b, (p[0] - ax) ** 2 + (p[1] - ay) ** 2 + (p[2] - az) ** 2] as const)
          .filter(([b]) => b !== a)
          .sort((x, y) => x[1] - y[1])
          .slice(0, 4)
          .map(([b]) => [a, b] as const);
      }),
    [named, points],
  );
  const [angle, setAngle] = useState(0.6);
  useEffect(() => {
    if (!active) return;
    let frame = 0;
    let last = performance.now();
    const tick = (now: number) => {
      setAngle((a) => a + ((now - last) / 1000) * 0.16);
      last = now;
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [active]);

  const tilt = 0.38;
  const project = ([x, y, z]: readonly [number, number, number]) => {
    const x1 = x * Math.cos(angle) + z * Math.sin(angle);
    const z1 = -x * Math.sin(angle) + z * Math.cos(angle);
    const y2 = y * Math.cos(tilt) - z1 * Math.sin(tilt);
    const z2 = y * Math.sin(tilt) + z1 * Math.cos(tilt);
    return { x: 200 + x1 * 140, y: 200 + y2 * 140, z: z2 };
  };
  const projected = points.map(project);

  return (
    <svg viewBox="-120 0 640 400" className="h-full w-full overflow-visible" aria-hidden>
      <defs>
        <radialGradient id="sphere-glow">
          <stop offset="0" stopColor={color} stopOpacity="0.28" />
          <stop offset="1" stopColor={color} stopOpacity="0" />
        </radialGradient>
      </defs>
      <circle cx="200" cy="200" r="190" fill="url(#sphere-glow)" />
      {links.map(([a, b], i) => {
        const p = projected[a];
        const q = projected[b];
        const depth = (p.z + q.z) / 2;
        return <line key={i} x1={p.x} y1={p.y} x2={q.x} y2={q.y} stroke={color} strokeOpacity={0.12 + Math.max(0, depth) * 0.35} strokeWidth={0.8} />;
      })}
      {projected.map((p, i) => {
        const isNamed = named.includes(i);
        return (
          <circle
            key={i}
            cx={p.x}
            cy={p.y}
            r={(isNamed ? 4.2 : 1.9) * (0.75 + (p.z + 1) * 0.25)}
            fill={isNamed ? color : "#ffffff"}
            opacity={0.18 + ((p.z + 1) / 2) * (isNamed ? 0.82 : 0.55)}
          />
        );
      })}
      {named.map((index, i) => {
        const p = projected[index];
        if (p.z < 0.05) return null;
        const right = p.x >= 200;
        return (
          <text
            key={i}
            x={p.x + (right ? 10 : -10)}
            y={p.y + 4}
            textAnchor={right ? "start" : "end"}
            fontSize="11.5"
            fill="#fff"
            opacity={Math.min(1, p.z * 1.6)}
            style={{ fontFamily: "var(--font-sans)" }}
          >
            {labels[i]}
          </text>
        );
      })}
    </svg>
  );
}

/* ---------------------------------------------------------------- section */

export function Companion() {
  const { t, asset } = usePage();
  const c = t.companion;
  const { lowmem } = useLowMem();
  const stage = useRef<HTMLDivElement>(null);
  const inView = useInView(stage, { margin: "-15% 0px" });
  const [who, setWho] = useState<(typeof CAST)[number]>("yuki");
  const person = c.people[who];
  const [bubble, setBubble] = useState(0);
  useEffect(() => setBubble(0), [who]);
  useEffect(() => {
    if (!inView) return;
    const id = setInterval(() => setBubble((b) => (b + 1) % person.bubbles.length), 3400);
    return () => clearInterval(id);
  }, [inView, person.bubbles.length, who]);
  const icons = [Brain, Moon, Sparkles, MessageCircle];

  return (
    <section id="companion" className="relative mx-auto mt-40 max-w-6xl scroll-mt-28 px-5">
      <SectionHeader kicker={c.kicker} title={c.title} lead={c.lead} />

      <Reveal className="mt-12 grid gap-4 lg:grid-cols-[1fr_1.1fr]" delay={0.1}>
        {/* The character, with something to say about your evening. */}
        <div ref={stage} className="relative flex min-h-[460px] flex-col overflow-hidden rounded-[26px] border border-line bg-panel/80">
          <div aria-hidden className="absolute inset-0 transition-[background] duration-700" style={{ background: `radial-gradient(70% 60% at 50% 75%, ${GLOW[who]}40, transparent 70%)` }} />
          <div className="relative flex items-center justify-between p-5">
            <span className="flex items-center gap-2 rounded-full border border-line bg-black/40 px-3 py-1.5 text-[13px] text-white">
              <span className="font-semibold capitalize">{who}</span>
              <span className="rounded-full bg-white/12 px-1.5 text-[11px] font-semibold">
                {c.level} 12
              </span>
            </span>
            <span className="flex items-center gap-1.5 rounded-full border border-line bg-black/40 px-3 py-1.5 text-[13px] text-amber-300">
              <Flame className="size-4 fill-amber-400/40" /> 7 <span className="text-white/50">{c.streak}</span>
            </span>
          </div>
          <div className="relative flex flex-1 items-end justify-center">
            <AnimatePresence mode="wait">
              <motion.img
                key={who}
                src={asset(`img/companion/${who}.webp`)}
                alt={who}
                loading="lazy"
                className="relative z-0 max-h-[380px] w-auto object-contain"
                initial={{ opacity: 0, y: 30, filter: "blur(8px)" }}
                animate={{ opacity: 1, y: 0, filter: "blur(0px)" }}
                exit={{ opacity: 0, y: 10, filter: "blur(6px)" }}
                transition={{ duration: 0.45, ease: [0.22, 1, 0.36, 1] }}
              />
            </AnimatePresence>
            <div className="absolute left-5 top-6 z-10 max-w-[56%]">
              <AnimatePresence mode="wait">
                <motion.p
                  key={`${who}-${bubble}`}
                  initial={{ opacity: 0, y: 8, scale: 0.96 }}
                  animate={{ opacity: 1, y: 0, scale: 1 }}
                  exit={{ opacity: 0, y: -6 }}
                  transition={{ duration: 0.3 }}
                  className="rounded-2xl rounded-bl-md bg-white px-4 py-2.5 text-[14px] font-medium leading-snug text-ink shadow-[0_14px_40px_-12px_rgb(0_0_0/0.8)]"
                >
                  {person.bubbles[bubble]}
                </motion.p>
              </AnimatePresence>
            </div>
          </div>
          <div className="relative flex flex-wrap gap-1.5 border-t border-line bg-black/40 p-3">
            {CAST.map((name, i) => (
              <button
                key={name}
                onClick={() => setWho(name)}
                className={cn("rounded-full px-3 py-1.5 text-[12.5px] transition-colors", who === name ? "bg-white text-ink" : "text-white/65 hover:bg-white/8 hover:text-white")}
              >
                {c.characters[i]}
              </button>
            ))}
          </div>
        </div>

        {/* Her memory, slightly over-engineered. */}
        <div className="relative flex flex-col overflow-hidden rounded-[26px] border border-line bg-[#0b0a0d] p-5">
          <div className="flex items-center justify-between">
            <p className="text-[12.5px] font-semibold uppercase tracking-[0.14em] text-white/55">
              {c.memory} · <span className="capitalize" style={{ color: GLOW[who] }}>{who}</span>
            </p>
            <span className="text-[12px] text-white/35">{c.memoryHint}</span>
          </div>
          <div className="relative mx-auto mt-2 aspect-[16/10] w-full">
            <AnimatePresence mode="wait">
              <motion.div key={who} className="absolute inset-0" initial={{ opacity: 0, scale: 0.94 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: 1.04 }} transition={{ duration: 0.35 }}>
                <MemorySphere labels={person.memories} active={inView && !lowmem} color={GLOW[who]} />
              </motion.div>
            </AnimatePresence>
          </div>
        </div>
      </Reveal>

      <div className="mt-4 grid gap-4 md:grid-cols-2 lg:grid-cols-4">
        {c.cards.map((card, i) => {
          const Icon = icons[i];
          return (
            <Reveal key={card.title} delay={0.06 * i} className="rounded-[22px] border border-line bg-panel/70 p-5">
              <span className="flex size-9 items-center justify-center rounded-xl border border-line bg-white/5">
                <Icon className="size-4 text-white/85" />
              </span>
              <p className="mt-4 font-display text-[17px] font-semibold text-white">{card.title}</p>
              <p className="mt-2 text-[14px] leading-relaxed text-muted">{card.text}</p>
            </Reveal>
          );
        })}
      </div>

      <Reveal className="mt-4 grid gap-4 md:grid-cols-2">
        <p className="flex items-start gap-3 rounded-[22px] border border-line bg-panel/50 p-5 text-[14px] leading-relaxed text-white/70">
          <Flame className="mt-0.5 size-4 shrink-0 text-amber-300" />
          {c.levels}
        </p>
        <p className="flex items-start gap-3 rounded-[22px] border border-line bg-panel/50 p-5 text-[14px] leading-relaxed text-white/70">
          <Lock className="mt-0.5 size-4 shrink-0 text-white/60" />
          {c.privacy}
        </p>
      </Reveal>
    </section>
  );
}
