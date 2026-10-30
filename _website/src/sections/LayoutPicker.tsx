import { AnimatePresence, motion, useInView } from "motion/react";
import { Check } from "lucide-react";
import { useCallback, useEffect, useRef, useState } from "react";
import { FlixMock, SnitchMock } from "../components/mocks/Mocks";
import { HintBar, Reveal, SectionHeader } from "../components/ui";
import { Wireframe, type LayoutKey } from "../components/Wireframe";
import { ambient } from "../lib/assets";
import { onPad, useAmbient } from "../lib/ambient";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";

export const LAYOUTS: LayoutKey[] = ["shelves", "xmb", "flix", "metro", "psfive", "snitch"];
const AUTO_MS = 6000;

export function LayoutStage({ id, running = true, className }: { id: LayoutKey; running?: boolean; className?: string }) {
  const { asset, t } = usePage();
  const mock = id === "flix" || id === "snitch";
  return (
    <div className={cn("relative aspect-video w-full overflow-hidden rounded-[20px] bg-black [container-type:inline-size]", className)}>
      <AnimatePresence initial={false}>
        <motion.div
          key={id}
          className="absolute inset-0"
          initial={{ opacity: 0, scale: 1.04, filter: "blur(10px)" }}
          animate={{ opacity: 1, scale: 1, filter: "blur(0px)" }}
          exit={{ opacity: 0, scale: 0.985, filter: "blur(6px)" }}
          transition={{ duration: 0.7, ease: [0.22, 1, 0.36, 1] }}
        >
          {id === "flix" ? (
            <FlixMock />
          ) : id === "snitch" ? (
            <SnitchMock />
          ) : (
            <motion.img
              src={asset(`img/shots/${id}.webp`)}
              srcSet={`${asset(`img/shots/${id}-sm.webp`)} 800w, ${asset(`img/shots/${id}.webp`)} 1600w`}
              sizes="(min-width: 1200px) 1100px, 100vw"
              alt={t.layouts.items[id].name}
              className="absolute inset-0 h-full w-full object-cover"
              initial={{ scale: 1 }}
              animate={{ scale: 1.045 }}
              transition={{ duration: 12, ease: "linear" }}
            />
          )}
        </motion.div>
      </AnimatePresence>
      {mock && (
        <span className="absolute bottom-[2.5cqw] left-[2.5cqw] rounded-full bg-black/55 px-[1.2cqw] py-[0.45cqw] text-[max(10px,1.05cqw)] font-medium text-white/75 backdrop-blur">
          {t.layouts.preview}
        </span>
      )}
    </div>
  );
}

export function LayoutPicker() {
  const { t } = usePage();
  const [index, setIndex] = useState(0);
  const [paused, setPaused] = useState(false);
  const [userPicked, setUserPicked] = useState(false);
  const section = useRef<HTMLElement>(null);
  const inView = useInView(section, { margin: "-30% 0px -30% 0px" });
  const id = LAYOUTS[index];
  useAmbient(ambient[id], inView);

  const pick = useCallback((next: number, byUser: boolean) => {
    setIndex(((next % LAYOUTS.length) + LAYOUTS.length) % LAYOUTS.length);
    if (byUser) setUserPicked(true);
  }, []);

  useEffect(() => {
    if (!inView || paused || userPicked) return;
    const timer = setTimeout(() => pick(index + 1, false), AUTO_MS);
    return () => clearTimeout(timer);
  }, [index, inView, paused, userPicked, pick]);

  useEffect(
    () =>
      onPad((action) => {
        if (!inView) return;
        if (action === "left") pick(index - 1, true);
        if (action === "right") pick(index + 1, true);
      }),
    [inView, index, pick],
  );

  return (
    <section id="layouts" ref={section} className="relative mx-auto mt-40 max-w-6xl scroll-mt-28 px-5">
      <SectionHeader kicker={t.layouts.kicker} title={t.layouts.title} lead={t.layouts.lead} />

      <Reveal className="mt-12" delay={0.1}>
        <div
          className="relative rounded-[28px] border border-white/10 bg-black p-2 shadow-[0_50px_140px_-50px_var(--ambient)] transition-shadow duration-1000 sm:p-2.5"
          onPointerEnter={() => setPaused(true)}
          onPointerLeave={() => setPaused(false)}
        >
          <LayoutStage id={id} running={inView} />
        </div>
        <div className="mt-5 flex min-h-[56px] items-start justify-between gap-6 px-1">
          <AnimatePresence mode="wait">
            <motion.p
              key={id}
              initial={{ opacity: 0, y: 8 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6 }}
              transition={{ duration: 0.3 }}
              className="max-w-2xl text-[16px] leading-relaxed text-muted"
            >
              <span className="font-display text-[18px] font-semibold text-white">{t.layouts.items[id].name}.</span> {t.layouts.items[id].text}
            </motion.p>
          </AnimatePresence>
          <HintBar items={[{ key: "← →", label: t.layouts.hint.move }]} className="hidden shrink-0 md:inline-flex" />
        </div>
      </Reveal>

      {/* The picker itself, as in Settings → Interface → TV mode layout. */}
      <div
        role="tablist"
        aria-label={t.layouts.kicker}
        className="mt-5 grid grid-cols-3 gap-3 sm:grid-cols-6"
        onKeyDown={(event) => {
          if (event.key === "ArrowRight") pick(index + 1, true);
          if (event.key === "ArrowLeft") pick(index - 1, true);
        }}
      >
        {LAYOUTS.map((layout, i) => {
          const active = i === index;
          return (
            <button
              key={layout}
              role="tab"
              aria-selected={active}
              tabIndex={active ? 0 : -1}
              onClick={() => pick(i, true)}
              className={cn(
                "group relative rounded-[18px] border bg-panel-2/70 p-2.5 pb-3 text-left transition-colors duration-300",
                active ? "border-transparent bg-panel-3" : "border-line hover:border-white/20 hover:bg-panel-2",
              )}
            >
              {active && (
                <motion.span
                  layoutId="layout-focus"
                  className="pointer-events-none absolute -inset-[3px] rounded-[21px] border-2 border-white shadow-[0_0_24px_rgb(255_255_255/0.25)]"
                  transition={{ type: "spring", stiffness: 380, damping: 32 }}
                />
              )}
              <Wireframe id={layout} active={active} />
              <span className="mt-2.5 flex items-center justify-between px-1 text-[14px] font-medium">
                <span className={active ? "text-white" : "text-white/65"}>{t.layouts.items[layout].name}</span>
                {active && <Check className="size-4 text-white" />}
              </span>
              {active && !userPicked && inView && !paused && (
                <motion.span
                  key={`p-${index}`}
                  className="absolute inset-x-3 bottom-1.5 h-[2px] origin-left rounded-full bg-white/60"
                  initial={{ scaleX: 0 }}
                  animate={{ scaleX: 1 }}
                  transition={{ duration: AUTO_MS / 1000, ease: "linear" }}
                />
              )}
            </button>
          );
        })}
      </div>

    </section>
  );
}
