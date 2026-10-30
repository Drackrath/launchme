import { AnimatePresence, motion, useInView } from "motion/react";
import { Clapperboard, Disc3, LayoutGrid, Library, List, ListChecks, ListVideo, PanelsTopLeft, Save, SquareStack } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { ViewMock, type ViewKey } from "../components/mocks/Views";
import { Kbd, Reveal, SectionHeader } from "../components/ui";
import { onPad } from "../lib/ambient";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";

// The order of the launcher's view drawer.
export const VIEW_KEYS: ViewKey[] = ["grid", "list", "listplus", "cinematic", "overview", "detailed", "coverflow", "shelf", "floppy", "cartridge"];
const ICONS = { grid: LayoutGrid, list: List, listplus: ListVideo, cinematic: Clapperboard, overview: PanelsTopLeft, detailed: ListChecks, coverflow: Disc3, shelf: Library, floppy: Save, cartridge: SquareStack };

export function LibraryViews() {
  const { t } = usePage();
  const [index, setIndex] = useState(0);
  const section = useRef<HTMLElement>(null);
  const inView = useInView(section, { margin: "-25% 0px -25% 0px" });
  const id = VIEW_KEYS[index];
  const step = (delta: number) => setIndex((i) => (i + delta + VIEW_KEYS.length) % VIEW_KEYS.length);

  useEffect(
    () =>
      onPad((action) => {
        if (!inView) return;
        if (action === "rb") step(1);
        if (action === "lb") step(-1);
      }),
    [inView],
  );

  return (
    <section id="views" ref={section} className="relative mx-auto mt-40 max-w-6xl scroll-mt-28 px-5">
      <SectionHeader kicker={t.views.kicker} title={t.views.title} lead={t.views.lead} />
      <Reveal className="mt-12 grid gap-4 lg:grid-cols-[260px_1fr]" delay={0.1}>
        {/* The view drawer. */}
        <div className="flex gap-1.5 overflow-x-auto rounded-[24px] border border-line bg-panel/80 p-2 no-scrollbar lg:flex-col lg:overflow-visible">
          {VIEW_KEYS.map((key, i) => {
            const Icon = ICONS[key];
            const active = i === index;
            return (
              <button
                key={key}
                onClick={() => setIndex(i)}
                className={cn(
                  "relative flex shrink-0 items-center gap-3 rounded-2xl px-4 py-2.5 text-left text-[14.5px] transition-colors",
                  active ? "text-ink" : "text-white/70 hover:bg-white/6 hover:text-white",
                )}
              >
                {active && <motion.span layoutId="view-pill" className="absolute inset-0 rounded-2xl bg-white" transition={{ type: "spring", stiffness: 420, damping: 34 }} />}
                <Icon className="relative size-[18px]" />
                <span className="relative font-medium">{t.views.items[key].name}</span>
              </button>
            );
          })}
          <div className="mt-auto hidden items-center gap-2 px-4 pb-2 pt-4 text-[12.5px] text-dim lg:flex">
            <Kbd>LB</Kbd>
            <Kbd>RB</Kbd>
            {t.views.hint}
          </div>
        </div>

        <div>
          <div className="relative rounded-[26px] border border-white/10 bg-black p-2 shadow-[0_40px_120px_-50px_rgb(255_255_255/0.18)]">
            <div className="relative aspect-video overflow-hidden rounded-[19px]">
              <AnimatePresence initial={false}>
                <motion.div
                  key={id}
                  className="absolute inset-0"
                  initial={{ opacity: 0, scale: 1.03 }}
                  animate={{ opacity: 1, scale: 1 }}
                  exit={{ opacity: 0, scale: 0.98 }}
                  transition={{ duration: 0.5, ease: [0.22, 1, 0.36, 1] }}
                >
                  <ViewMock id={id} running={inView} />
                </motion.div>
              </AnimatePresence>
            </div>
          </div>
          <AnimatePresence mode="wait">
            <motion.p
              key={id}
              initial={{ opacity: 0, y: 6 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.25 }}
              className="mt-4 px-2 text-[16px] text-muted"
            >
              <span className="font-semibold text-white">{t.views.items[id].name}.</span> {t.views.items[id].text}
            </motion.p>
          </AnimatePresence>
        </div>
      </Reveal>
    </section>
  );
}
