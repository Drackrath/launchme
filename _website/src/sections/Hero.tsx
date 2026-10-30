import { motion, useScroll, useSpring, useTransform } from "motion/react";
import { Play, Terminal } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { PadToast, type ToastDevice } from "../components/PadToast";
import { BlurText, ButtonGhost, ButtonPrimary, GitHubIcon, Magnet, WindowsIcon } from "../components/ui";
import { usePage } from "../lib/page";
import { formatDate, formatSize, GITHUB_URL, LATEST_URL } from "../lib/release";
import { AuthorNote } from "./AuthorNote";
import { HERO_VIDEO } from "../config";

const DEVICES: ToastDevice[] = [
  { kind: "pad", pad: "dualsense", name: "DualSense", battery: 85 },
  { kind: "pad", pad: "xbox", name: "Xbox Series X|S", battery: null },
  { kind: "headphones", name: "Audio-Technica ATH-M50xBT2", battery: 65 },
  { kind: "pad", pad: "ultimate2", name: "8BitDo Ultimate 2", battery: 40 },
  { kind: "pad", pad: "steam-controller2", name: "Steam Controller", battery: 100 },
];

export function Hero() {
  const { t, release, asset, lang } = usePage();
  const frame = useRef<HTMLDivElement>(null);
  const { scrollYProgress } = useScroll({ target: frame, offset: ["start end", "center center"] });
  const smooth = useSpring(scrollYProgress, { stiffness: 120, damping: 30, mass: 0.4 });
  const rotateX = useTransform(smooth, [0, 1], [26, 0]);
  const scale = useTransform(smooth, [0, 1], [0.9, 1]);
  const y = useTransform(smooth, [0, 1], [-30, 0]);

  const [playing, setPlaying] = useState(false);
  const [deviceIndex, setDeviceIndex] = useState(0);
  const [toastVisible, setToastVisible] = useState(false);
  useEffect(() => {
    const show = setTimeout(() => setToastVisible(true), 1600);
    const cycle = setInterval(() => setDeviceIndex((i) => (i + 1) % DEVICES.length), 3600);
    return () => {
      clearTimeout(show);
      clearInterval(cycle);
    };
  }, []);
  const device = DEVICES[deviceIndex];

  return (
    <section className="relative overflow-x-clip pt-36 pb-10 sm:pt-44">
      {/* Ambient light: follows the active layout's colours further down the page. */}
      <div aria-hidden className="pointer-events-none absolute inset-0 -z-10">
        <div className="dots-bg absolute inset-0 [mask-image:radial-gradient(ellipse_70%_55%_at_50%_20%,#000,transparent)]" />
        <div className="lowmem-hide absolute -top-40 left-1/2 h-[720px] w-[1100px] -translate-x-1/2 rounded-full bg-[radial-gradient(closest-side,var(--ambient),transparent)] opacity-40 blur-3xl transition-[background] duration-1000" />
        <div className="lowmem-hide absolute top-10 -left-40 h-[520px] w-[520px] animate-float rounded-full bg-[radial-gradient(closest-side,rgb(90_110_255/0.35),transparent)] blur-3xl" />
        <div className="lowmem-hide absolute top-40 -right-40 h-[520px] w-[520px] animate-float rounded-full bg-[radial-gradient(closest-side,rgb(255_120_80/0.25),transparent)] blur-3xl [animation-delay:-3s]" />
        {/* Spotlight beam, after Aceternity's Spotlight. */}
        <motion.div
          className="lowmem-hide absolute -top-[30%] left-[8%] h-[160%] w-[38%] origin-top rotate-[-28deg] bg-[linear-gradient(180deg,rgb(255_255_255/0.16),transparent_70%)] blur-2xl"
          initial={{ opacity: 0, x: -120 }}
          animate={{ opacity: 1, x: 0 }}
          transition={{ duration: 1.8, delay: 0.4, ease: "easeOut" }}
        />
      </div>

      <div className="mx-auto flex max-w-6xl flex-col items-center px-5 text-center">

        <h1 className="font-display text-[clamp(2.6rem,6.6vw,5.8rem)] font-semibold leading-[1] tracking-[-0.035em] text-balance">
          <BlurText text={t.hero.titleA} className="block" wordClassName="text-gradient pb-[0.08em]" animateOnMount delay={0.15} />
          <BlurText text={t.hero.titleB} className="block text-white/45" animateOnMount delay={0.45} />
        </h1>

        <motion.p
          initial={{ opacity: 0, y: 16 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.8, delay: 0.75 }}
          className="mt-8 max-w-2xl text-[18px] leading-relaxed text-muted text-pretty sm:text-[19px]"
        >
          {t.hero.lead}
        </motion.p>

        <motion.div
          initial={{ opacity: 0, y: 16 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.8, delay: 0.95 }}
          className="mt-10 flex flex-wrap items-center justify-center gap-3"
        >
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
            <span className="rounded-full bg-amber-400/15 px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-amber-300">{t.hero.experimental}</span>
          </ButtonGhost>
          <ButtonGhost href={GITHUB_URL} target="_blank" rel="noreferrer" className="px-5">
            <GitHubIcon className="size-[18px]" />
            <span className="sr-only sm:not-sr-only">{t.hero.github}</span>
          </ButtonGhost>
        </motion.div>
        <motion.p initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ delay: 1.2 }} className="mt-4 text-[13px] text-dim">
          {t.hero.noInstaller}
          {release.date && (
            <>
              {" "}
              · {t.hero.released} {formatDate(release.date, lang)}
            </>
          )}
        </motion.p>

        <motion.div initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} transition={{ delay: 1.25, duration: 0.8 }} className="mt-20 mb-6 w-full max-w-2xl">
          <AuthorNote compact />
        </motion.div>
      </div>

      {/* The screen, tilted like a TV seen from the couch; it straightens as you scroll. */}
      <div ref={frame} className="relative mx-auto mt-24 max-w-[min(880px,calc((100vh-120px)*16/9))] px-4 [perspective:1600px]">
        <motion.div style={{ rotateX, scale, y }} className="relative origin-top transform-gpu">
          <div className="relative rounded-[30px] border border-white/12 bg-black p-2 shadow-[0_60px_160px_-40px_var(--ambient),0_40px_80px_-30px_rgb(0_0_0/0.9)] transition-shadow duration-1000 sm:p-3">
            <div className="relative overflow-hidden rounded-[22px]">
              {HERO_VIDEO && playing ? (
                <video className="block aspect-video w-full bg-black" autoPlay controls playsInline poster={asset(HERO_VIDEO.poster)}>
                  {HERO_VIDEO.webm && <source src={asset(HERO_VIDEO.webm)} type="video/webm" />}
                  {HERO_VIDEO.mp4 && <source src={asset(HERO_VIDEO.mp4)} type="video/mp4" />}
                </video>
              ) : (
              <img
                src={asset("img/shots/shelves.webp")}
                srcSet={`${asset("img/shots/shelves-sm.webp")} 800w, ${asset("img/shots/shelves.webp")} 1600w, ${asset("img/shots/shelves-xl.webp")} 3840w`}
                sizes="(min-width: 960px) 880px, 100vw"
                width={1600}
                height={900}
                alt="launchme, Default layout"
                className="block aspect-video w-full object-cover"
                fetchPriority="high"
              />
              )}
              {HERO_VIDEO && !playing && (
                <button
                  type="button"
                  onClick={() => setPlaying(true)}
                  className="absolute left-1/2 top-1/2 flex size-20 -translate-x-1/2 -translate-y-1/2 items-center justify-center rounded-full bg-white text-ink shadow-[0_20px_60px_-10px_rgb(0_0_0/0.8)] transition-transform hover:scale-105"
                  aria-label="Play"
                >
                  <Play className="ml-1 size-8 fill-current" />
                </button>
              )}
              {!playing && <div className="pointer-events-none absolute inset-0 bg-[linear-gradient(180deg,transparent_70%,rgb(0_0_0/0.35))]" />}
            </div>
          </div>

          <motion.div
            className="absolute -top-7 right-2 hidden sm:block md:-right-24"
            initial={false}
            animate={toastVisible ? { opacity: 1, y: 0, scale: 1 } : { opacity: 0, y: -16, scale: 0.96 }}
            transition={{ type: "spring", stiffness: 200, damping: 22 }}
          >
            <PadToast device={device} title={device.kind === "pad" ? t.bento.pads.connected : t.bento.pads.headphones} />
          </motion.div>

          <motion.div
            className="glass absolute -bottom-6 left-4 hidden items-center gap-3 rounded-2xl border border-line px-4 py-3 sm:flex md:-left-20"
            initial={{ opacity: 0, x: -20 }}
            whileInView={{ opacity: 1, x: 0 }}
            viewport={{ once: true }}
            transition={{ delay: 0.6, duration: 0.7 }}
          >
            <span className="relative flex size-2.5">
              <span className="absolute inline-flex size-full animate-ping rounded-full bg-ok opacity-60" />
              <span className="relative inline-flex size-2.5 rounded-full bg-ok" />
            </span>
            <span className="text-left text-[13px] leading-tight">
              <span className="block font-semibold text-white">VRAM · 0 MB</span>
              <span className="text-white/55">{t.bento.vram.running}</span>
            </span>
          </motion.div>


        </motion.div>
        <div aria-hidden className="lowmem-hide pointer-events-none absolute inset-x-10 -bottom-24 h-40 bg-[radial-gradient(closest-side,var(--ambient),transparent)] opacity-40 blur-3xl transition-[background] duration-1000" />
      </div>
    </section>
  );
}
