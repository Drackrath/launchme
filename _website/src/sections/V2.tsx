import { AnimatePresence, motion, useInView, useMotionValueEvent, useScroll, useSpring, useTransform } from "motion/react";
import { Terminal, Volume2, VolumeX } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { LogoAnimated } from "../components/Logo";
import { ButtonGhost, ButtonPrimary, GitHubIcon, Magnet, WindowsIcon } from "../components/ui";
import { INTERFACE_VIDEO } from "../config";
import { ambient } from "../lib/assets";
import { setAmbient } from "../lib/ambient";
import { usePage } from "../lib/page";
import { formatSize, GITHUB_URL, LATEST_URL } from "../lib/release";
import { AuthorNote } from "./AuthorNote";
import { LAYOUTS, LayoutStage } from "./LayoutPicker";

/* ---------------------------------------------------------------- hero */

export function HeroV2() {
  const { t, release } = usePage();
  return (
    <section className="relative overflow-hidden pt-36 sm:pt-44">
      <div aria-hidden className="pointer-events-none absolute inset-0 -z-10">
        <div className="lines-bg absolute inset-0 [mask-image:radial-gradient(ellipse_60%_50%_at_50%_30%,#000,transparent)]" />
        <div className="lowmem-hide absolute left-1/2 top-0 h-[600px] w-[900px] -translate-x-1/2 rounded-full bg-[radial-gradient(closest-side,var(--ambient),transparent)] opacity-35 blur-3xl transition-[background] duration-1000" />
      </div>
      <div className="mx-auto flex max-w-6xl flex-col items-center px-5 text-center">
        <motion.p initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.1 }} className="mb-10 text-[13px] font-semibold uppercase tracking-[0.22em] text-muted">
          {t.v2.eyebrow}
        </motion.p>
        <LogoAnimated className="w-full max-w-[980px] text-white" />
        <motion.h1 initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 0.9, duration: 0.8 }} className="mt-12 font-display text-3xl font-semibold tracking-tight text-white sm:text-5xl">
          {t.v2.title}
        </motion.h1>
        <motion.p initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 1.05, duration: 0.8 }} className="mt-5 max-w-2xl text-lg leading-relaxed text-muted text-pretty">
          {t.v2.lead}
        </motion.p>
        <motion.div initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 1.2, duration: 0.8 }} className="mt-10 flex flex-wrap items-center justify-center gap-3">
          <Magnet strength={0.18}>
            <ButtonPrimary href={release.windows?.url ?? LATEST_URL}>
              <WindowsIcon className="size-5" />
              {t.hero.downloadWin}
              <span className="text-[13px] font-normal text-ink/55">
                v{release.version}
                {release.windows ? ` · ${formatSize(release.windows.size)}` : ""}
              </span>
            </ButtonPrimary>
          </Magnet>
          <ButtonGhost href={release.linux?.url ?? LATEST_URL}>
            <Terminal className="size-[18px]" />
            {t.hero.downloadLinux}
          </ButtonGhost>
          <ButtonGhost href={GITHUB_URL} target="_blank" rel="noreferrer" className="px-5">
            <GitHubIcon className="size-[18px]" />
            GitHub
          </ButtonGhost>
        </motion.div>
        <motion.div initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 1.4, duration: 0.8 }} className="mt-12 w-full max-w-2xl">
          <AuthorNote compact />
        </motion.div>
      </div>
    </section>
  );
}

/* ------------------------------------------------------- big video stage */

const SLIDES = ["shelves", "psfive", "xmb", "metro", "list-plus", "shelf", "details", "widgets", "desktop"];

function Slideshow({ active }: { active: boolean }) {
  const { asset } = usePage();
  const [index, setIndex] = useState(0);
  useEffect(() => {
    if (!active) return;
    const id = setInterval(() => setIndex((i) => (i + 1) % SLIDES.length), 3800);
    return () => clearInterval(id);
  }, [active]);
  useEffect(() => {
    if (active) setAmbient(ambient[SLIDES[index]] ?? "#8a6a2a");
  }, [index, active]);
  return (
    <AnimatePresence initial={false}>
      <motion.img
        key={SLIDES[index]}
        src={asset(`img/shots/${SLIDES[index]}.webp`)}
        alt=""
        className="absolute inset-0 h-full w-full object-cover"
        initial={{ opacity: 0, scale: 1.06 }}
        animate={{ opacity: 1, scale: 1 }}
        exit={{ opacity: 0 }}
        transition={{ opacity: { duration: 1 }, scale: { duration: 4.8, ease: "linear" } }}
      />
    </AnimatePresence>
  );
}

export function VideoStage() {
  const { t, asset } = usePage();
  const wrap = useRef<HTMLDivElement>(null);
  const video = useRef<HTMLVideoElement>(null);
  const [hasVideo, setHasVideo] = useState(false);
  const [muted, setMuted] = useState(true);
  const inView = useInView(wrap, { margin: "-20% 0px" });
  const { scrollYProgress } = useScroll({ target: wrap, offset: ["start start", "end end"] });
  const progress = useSpring(scrollYProgress, { stiffness: 140, damping: 30, mass: 0.3 });
  const scale = useTransform(progress, [0, 0.45], [0.62, 1]);
  const radius = useTransform(progress, [0, 0.45], [36, 0]);
  const captionOpacity = useTransform(progress, [0, 0.12, 0.3], [1, 1, 0]);
  const outro = useTransform(progress, [0.75, 1], [0, 1]);
  const [phase, setPhase] = useState(0);
  useMotionValueEvent(progress, "change", (v) => setPhase(v));

  useEffect(() => {
    const element = video.current;
    if (!element) return;
    if (inView) element.play().catch(() => {});
    else element.pause();
  }, [inView, hasVideo]);

  return (
    <section ref={wrap} className="relative mt-24 h-[260vh]" aria-label={t.v2.videoLabel}>
      <div className="sticky top-0 flex h-screen items-center justify-center overflow-hidden">
        <motion.p style={{ opacity: captionOpacity }} className="absolute top-[9vh] z-10 text-center text-[13px] font-semibold uppercase tracking-[0.22em] text-muted">
          {t.v2.videoLabel}
        </motion.p>
        <motion.div style={{ scale, borderRadius: radius }} className="relative aspect-video w-[min(100vw,177.78vh)] overflow-hidden bg-black shadow-[0_80px_200px_-60px_var(--ambient)]">
          <Slideshow active={inView && !hasVideo} />
          {INTERFACE_VIDEO && (
            <video
              ref={video}
              className="absolute inset-0 h-full w-full object-cover transition-opacity duration-700"
              style={{ opacity: hasVideo ? 1 : 0 }}
              poster={asset(INTERFACE_VIDEO.poster)}
              muted={muted}
              loop
              playsInline
              preload="metadata"
              onCanPlay={() => setHasVideo(true)}
            >
              {INTERFACE_VIDEO.webm && <source src={asset(INTERFACE_VIDEO.webm)} type="video/webm" />}
              {INTERFACE_VIDEO.mp4 && <source src={asset(INTERFACE_VIDEO.mp4)} type="video/mp4" />}
            </video>
          )}
          <div className="pointer-events-none absolute inset-0 bg-[linear-gradient(180deg,transparent_65%,rgb(0_0_0/0.55))]" />
          <div className="absolute bottom-[3%] left-[3%] right-[3%] flex items-center justify-between gap-3">
            {!hasVideo ? (
              <span className="glass rounded-full border border-line px-3.5 py-1.5 text-[12.5px] text-white/75">{t.v2.videoSoon}</span>
            ) : (
              <span />
            )}
            {hasVideo && (
              <button
                onClick={() => setMuted((m) => !m)}
                className="glass flex size-10 items-center justify-center rounded-full border border-line text-white"
                aria-label={muted ? "Unmute" : "Mute"}
              >
                {muted ? <VolumeX className="size-4" /> : <Volume2 className="size-4" />}
              </button>
            )}
          </div>
        </motion.div>
        <motion.div style={{ opacity: outro }} className="pointer-events-none absolute inset-x-0 bottom-[8vh] text-center">
          <p className="font-display text-2xl font-semibold text-white drop-shadow-[0_4px_20px_rgb(0_0_0/0.8)] sm:text-4xl">{t.hero.titleB}</p>
        </motion.div>
        <div className="absolute bottom-6 left-1/2 h-1 w-40 -translate-x-1/2 overflow-hidden rounded-full bg-white/10">
          <div className="h-full bg-white/70" style={{ width: `${Math.round(phase * 100)}%` }} />
        </div>
      </div>
    </section>
  );
}

/* --------------------------------------------- layouts as scroll chapters */

export function ChapterScroller() {
  const { t } = usePage();
  const wrap = useRef<HTMLDivElement>(null);
  const { scrollYProgress } = useScroll({ target: wrap, offset: ["start start", "end end"] });
  const [current, setCurrent] = useState(0);
  const inView = useInView(wrap, { margin: "-40% 0px" });
  useMotionValueEvent(scrollYProgress, "change", (v) => setCurrent(Math.min(LAYOUTS.length - 1, Math.round(v * (LAYOUTS.length - 1)))));
  useEffect(() => {
    if (inView) setAmbient(ambient[LAYOUTS[current]]);
  }, [current, inView]);

  return (
    <section id="layouts" ref={wrap} className="relative mt-24" style={{ height: `${LAYOUTS.length * 80}vh` }}>
      <div className="sticky top-0 flex h-screen flex-col justify-center overflow-hidden">
        <div className="mx-auto mb-8 flex w-full max-w-6xl items-end justify-between gap-6 px-5">
          <div>
            <p className="text-[13px] font-semibold uppercase tracking-[0.18em] text-muted">{t.layouts.kicker}</p>
            <h2 className="mt-2 font-display text-3xl font-semibold tracking-tight text-white sm:text-5xl">{t.layouts.title}</h2>
          </div>
          <p className="hidden shrink-0 font-mono text-[14px] text-white/50 sm:block">
            {String(current + 1).padStart(2, "0")} / {String(LAYOUTS.length).padStart(2, "0")}
          </p>
        </div>
        <motion.div animate={{ x: `-${(current * 100) / LAYOUTS.length}%` }} transition={{ type: "spring", stiffness: 90, damping: 22 }} style={{ width: `${LAYOUTS.length * 100}%` }} className="flex">
          {LAYOUTS.map((id, i) => (
            <div key={id} className="flex w-full justify-center px-5" style={{ width: `${100 / LAYOUTS.length}%` }}>
              <div className="grid w-full max-w-6xl items-center gap-8 lg:grid-cols-[1fr_280px]">
                <motion.div animate={{ scale: i === current ? 1 : 0.92, opacity: i === current ? 1 : 0.35 }} transition={{ type: "spring", stiffness: 140, damping: 24 }} className="rounded-[26px] border border-white/10 bg-black p-2">
                  <LayoutStage id={id} />
                </motion.div>
                <div>
                  <p className="font-display text-4xl font-semibold text-white">{t.layouts.items[id].name}</p>
                  <p className="mt-3 text-[16px] leading-relaxed text-muted">{t.layouts.items[id].text}</p>
                </div>
              </div>
            </div>
          ))}
        </motion.div>
        <div className="mx-auto mt-10 flex gap-2">
          {LAYOUTS.map((id, i) => (
            <span key={id} className="h-1 rounded-full bg-white transition-all duration-500" style={{ width: i === current ? 32 : 8, opacity: i === current ? 0.9 : 0.25 }} />
          ))}
        </div>
      </div>
    </section>
  );
}
