import { AnimatePresence, motion } from "motion/react";
import { ChevronLeft, ChevronRight, X } from "lucide-react";
import { useEffect, useState } from "react";
import { Marquee, SectionHeader } from "../components/ui";
import { usePage } from "../lib/page";

const ROW_A = ["shelves", "psfive", "xmb", "metro", "list-plus", "shelf", "details"];
const ROW_B = ["desktop", "widgets", "glance", "search", "desktop-settings", "layouts", "companion"];
const ALL = [...ROW_A, ...ROW_B];

export function Gallery() {
  const { t, asset } = usePage();
  const [open, setOpen] = useState<number | null>(null);

  useEffect(() => {
    if (open == null) return;
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(null);
      if (event.key === "ArrowRight") setOpen((i) => (i == null ? i : (i + 1) % ALL.length));
      if (event.key === "ArrowLeft") setOpen((i) => (i == null ? i : (i - 1 + ALL.length) % ALL.length));
    };
    window.addEventListener("keydown", onKey);
    document.body.style.overflow = "hidden";
    return () => {
      window.removeEventListener("keydown", onKey);
      document.body.style.overflow = "";
    };
  }, [open]);

  const tile = (id: string) => (
    <button
      key={id}
      onClick={() => setOpen(ALL.indexOf(id))}
      className="group relative w-[360px] shrink-0 overflow-hidden rounded-2xl border border-line bg-black text-left sm:w-[440px]"
    >
      <img
        src={asset(`img/shots/${id}-sm.webp`)}
        alt={t.gallery.items[id]}
        loading="lazy"
        className="aspect-video w-full object-cover transition-transform duration-700 group-hover:scale-[1.04]"
      />
      <span className="absolute inset-x-0 bottom-0 bg-[linear-gradient(transparent,rgb(0_0_0/0.85))] px-4 pb-3 pt-10 text-[14px] font-medium text-white/90">
        {t.gallery.items[id]}
      </span>
    </button>
  );

  return (
    <section id="screens" className="relative mt-40 scroll-mt-28">
      <div className="mx-auto max-w-6xl px-5">
        <SectionHeader kicker={t.gallery.kicker} title={t.gallery.title} lead={t.gallery.lead} />
      </div>
      <div className="mt-12 flex flex-col gap-4">
        <Marquee duration={70} gap={16}>
          {ROW_A.map(tile)}
        </Marquee>
        <Marquee duration={80} gap={16} reverse>
          {ROW_B.map(tile)}
        </Marquee>
      </div>

      <AnimatePresence>
        {open != null && (
          <motion.div
            className="fixed inset-0 z-[70] flex items-center justify-center bg-black/90 p-4 backdrop-blur-sm"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            onClick={() => setOpen(null)}
            role="dialog"
            aria-modal
          >
            <AnimatePresence mode="wait">
              <motion.figure
                key={ALL[open]}
                className="relative w-full max-w-[1500px]"
                initial={{ opacity: 0, scale: 0.96 }}
                animate={{ opacity: 1, scale: 1 }}
                exit={{ opacity: 0, scale: 0.98 }}
                transition={{ duration: 0.25 }}
                onClick={(event) => event.stopPropagation()}
              >
                <img src={asset(`img/shots/${ALL[open]}.webp`)} alt={t.gallery.items[ALL[open]]} className="w-full rounded-2xl border border-white/10" />
                <figcaption className="mt-3 text-center text-[15px] text-white/75">{t.gallery.items[ALL[open]]}</figcaption>
              </motion.figure>
            </AnimatePresence>
            {[
              { icon: ChevronLeft, delta: -1, cls: "left-4" },
              { icon: ChevronRight, delta: 1, cls: "right-4" },
            ].map(({ icon: Icon, delta, cls }) => (
              <button
                key={cls}
                className={`absolute top-1/2 ${cls} flex size-12 -translate-y-1/2 items-center justify-center rounded-full border border-white/15 bg-black/60 text-white hover:bg-white/10`}
                onClick={(event) => {
                  event.stopPropagation();
                  setOpen((i) => (i == null ? i : (i + delta + ALL.length) % ALL.length));
                }}
              >
                <Icon className="size-6" />
              </button>
            ))}
            <button className="absolute right-4 top-4 flex size-11 items-center justify-center rounded-full border border-white/15 bg-black/60 text-white" aria-label={t.gallery.close}>
              <X className="size-5" />
            </button>
          </motion.div>
        )}
      </AnimatePresence>
    </section>
  );
}
