import {
  animate,
  motion,
  useInView,
  useMotionTemplate,
  useMotionValue,
  useSpring,
  type HTMLMotionProps,
} from "motion/react";
import { useEffect, useRef, useState, type ElementType, type ReactNode } from "react";
import { cn } from "../lib/cn";

/* ------------------------------------------------------------------ Reveal */

export function Reveal({
  children,
  className,
  delay = 0,
  y = 24,
  as = "div",
  ...rest
}: { children: ReactNode; className?: string; delay?: number; y?: number; as?: "div" | "section" | "li" | "span" } & Omit<
  HTMLMotionProps<"div">,
  "children"
>) {
  const Tag = motion[as] as typeof motion.div;
  return (
    <Tag
      data-reveal
      className={className}
      initial={{ opacity: 0, y, filter: "blur(8px)" }}
      whileInView={{ opacity: 1, y: 0, filter: "blur(0px)" }}
      viewport={{ once: true, margin: "0px 0px -8% 0px" }}
      transition={{ duration: 0.7, delay, ease: [0.22, 1, 0.36, 1] }}
      {...rest}
    >
      {children}
    </Tag>
  );
}

/* ---------------------------------------------------------------- BlurText */
// After React Bits' BlurText: words come in one by one, out of a blur.
export function BlurText({
  text,
  className,
  delay = 0,
  step = 0.06,
  as: Tag = "span",
  animateOnMount = false,
  wordClassName,
}: {
  text: string;
  className?: string;
  wordClassName?: string;
  delay?: number;
  step?: number;
  as?: ElementType;
  animateOnMount?: boolean;
}) {
  const words = text.split(" ");
  const trigger = animateOnMount ? { animate: { opacity: 1, y: 0, filter: "blur(0px)" } } : { whileInView: { opacity: 1, y: 0, filter: "blur(0px)" } };
  return (
    <Tag className={className} aria-label={text}>
      {words.map((word, i) => (
        <motion.span
          key={i}
          aria-hidden
          data-reveal
          className={cn("inline-block will-change-transform", wordClassName)}
          initial={{ opacity: 0, y: "0.35em", filter: "blur(12px)" }}
          {...trigger}
          viewport={{ once: true }}
          transition={{ duration: 0.8, delay: delay + i * step, ease: [0.22, 1, 0.36, 1] }}
        >
          {word}
          {i < words.length - 1 ? "\u00a0" : ""}
        </motion.span>
      ))}
    </Tag>
  );
}

/* ------------------------------------------------------------------ Magnet */
// After React Bits' Magnet: the element leans towards the cursor.
export function Magnet({ children, strength = 0.25, className }: { children: ReactNode; strength?: number; className?: string }) {
  const ref = useRef<HTMLDivElement>(null);
  const x = useSpring(0, { stiffness: 220, damping: 18, mass: 0.4 });
  const y = useSpring(0, { stiffness: 220, damping: 18, mass: 0.4 });
  return (
    <motion.div
      ref={ref}
      className={cn("inline-block", className)}
      style={{ x, y }}
      onPointerMove={(event) => {
        if (event.pointerType !== "mouse" || !ref.current) return;
        const rect = ref.current.getBoundingClientRect();
        x.set((event.clientX - rect.left - rect.width / 2) * strength);
        y.set((event.clientY - rect.top - rect.height / 2) * strength);
      }}
      onPointerLeave={() => {
        x.set(0);
        y.set(0);
      }}
    >
      {children}
    </motion.div>
  );
}

/* ----------------------------------------------------------------- CountUp */
export function CountUp({ to, from = 0, duration = 1.6, className, suffix = "" }: { to: number; from?: number; duration?: number; className?: string; suffix?: string }) {
  const ref = useRef<HTMLSpanElement>(null);
  const inView = useInView(ref, { once: true, margin: "0px 0px -10% 0px" });
  const [value, setValue] = useState(to);
  const started = useRef(false);
  useEffect(() => {
    if (!inView || started.current) return;
    started.current = true;
    setValue(from);
    const controls = animate(from, to, {
      duration,
      ease: [0.16, 1, 0.3, 1],
      onUpdate: (latest) => setValue(Math.round(latest)),
    });
    return () => controls.stop();
  }, [inView, from, to, duration]);
  return (
    <span ref={ref} className={cn("tabular-nums", className)}>
      {value}
      {suffix}
    </span>
  );
}

/* ----------------------------------------------------------- SpotlightCard */
// After Aceternity's card spotlight: a soft light follows the cursor, the border catches it.
export function SpotlightCard({
  children,
  className,
  glow = "rgb(255 255 255 / 0.10)",
  ...rest
}: { children: ReactNode; className?: string; glow?: string } & Omit<HTMLMotionProps<"div">, "children">) {
  const mx = useMotionValue(-400);
  const my = useMotionValue(-400);
  const background = useMotionTemplate`radial-gradient(420px circle at ${mx}px ${my}px, ${glow}, transparent 70%)`;
  const border = useMotionTemplate`radial-gradient(260px circle at ${mx}px ${my}px, rgb(255 255 255 / 0.35), transparent 70%)`;
  return (
    <motion.div
      className={cn("group relative overflow-hidden rounded-[28px] border border-line bg-panel/80", className)}
      onPointerMove={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        mx.set(event.clientX - rect.left);
        my.set(event.clientY - rect.top);
      }}
      onPointerLeave={() => {
        mx.set(-400);
        my.set(-400);
      }}
      {...rest}
    >
      <motion.div
        aria-hidden
        className="pointer-events-none absolute inset-0 rounded-[inherit] opacity-0 transition-opacity duration-500 group-hover:opacity-100"
        style={{ background }}
      />
      <motion.div
        aria-hidden
        className="pointer-events-none absolute inset-0 rounded-[inherit] opacity-0 transition-opacity duration-500 group-hover:opacity-100"
        style={{
          background: border,
          WebkitMask: "linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0)",
          WebkitMaskComposite: "xor",
          maskComposite: "exclude",
          padding: 1,
        }}
      />
      {children}
    </motion.div>
  );
}

/* ----------------------------------------------------------------- Marquee */
export function Marquee({
  children,
  reverse = false,
  duration = 40,
  gap = 16,
  className,
  pauseOnHover = true,
}: {
  children: ReactNode;
  reverse?: boolean;
  duration?: number;
  gap?: number;
  className?: string;
  pauseOnHover?: boolean;
}) {
  return (
    <div className={cn("group flex overflow-hidden mask-fade-x", className)} style={{ ["--marquee-gap" as string]: `${gap}px`, ["--marquee-duration" as string]: `${duration}s` }}>
      <div
        className={cn(
          "flex w-max shrink-0",
          reverse ? "animate-marquee-reverse" : "animate-marquee",
          pauseOnHover && "group-hover:[animation-play-state:paused]",
        )}
        style={{ gap }}
      >
        {children}
        <div aria-hidden className="flex shrink-0" style={{ gap }}>
          {children}
        </div>
      </div>
    </div>
  );
}

/* -------------------------------------------------------------------- Kbd */
// The launcher's hint pill ("Enter Details").
export function Kbd({ children, className }: { children: ReactNode; className?: string }) {
  return (
    <kbd
      className={cn(
        "inline-flex h-6 min-w-6 items-center justify-center rounded-full bg-white/12 px-2 font-sans text-[12px] font-semibold text-white/90",
        className,
      )}
    >
      {children}
    </kbd>
  );
}

export function HintBar({ items, className }: { items: { key: ReactNode; label: string }[]; className?: string }) {
  return (
    <div className={cn("glass inline-flex items-center gap-4 rounded-full border border-line px-3 py-1.5 text-[13px] text-white/80", className)}>
      {items.map((item, i) => (
        <span key={i} className="inline-flex items-center gap-2">
          <Kbd>{item.key}</Kbd>
          {item.label}
        </span>
      ))}
    </div>
  );
}

/* ------------------------------------------------------------------ Toggle */
// The launcher's settings switch.
export function Toggle({ on, onChange, label, className }: { on: boolean; onChange: () => void; label: string; className?: string }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={on}
      aria-label={label}
      onClick={onChange}
      className={cn(
        "relative inline-flex h-[26px] w-[46px] shrink-0 items-center rounded-full border transition-colors duration-300",
        on ? "border-white bg-white" : "border-white/20 bg-white/10",
        className,
      )}
    >
      <motion.span
        layout
        transition={{ type: "spring", stiffness: 500, damping: 32 }}
        className={cn("block size-[18px] rounded-full", on ? "ml-auto mr-[3px] bg-ink" : "ml-[3px] bg-white/85")}
      />
    </button>
  );
}

/* -------------------------------------------------------------- GitHubIcon */
export function GitHubIcon({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} fill="currentColor" aria-hidden>
      <path d="M12 .5C5.73.5.98 5.24.98 11.52c0 4.86 3.15 8.98 7.52 10.44.55.1.75-.24.75-.53v-1.86c-3.06.67-3.71-1.3-3.71-1.3-.5-1.27-1.22-1.61-1.22-1.61-1-.68.08-.67.08-.67 1.1.08 1.68 1.13 1.68 1.13.98 1.69 2.58 1.2 3.21.92.1-.71.38-1.2.7-1.48-2.44-.28-5.01-1.22-5.01-5.43 0-1.2.43-2.18 1.13-2.95-.11-.28-.49-1.4.11-2.92 0 0 .92-.3 3.02 1.13a10.5 10.5 0 0 1 5.5 0c2.1-1.43 3.02-1.13 3.02-1.13.6 1.52.22 2.64.11 2.92.7.77 1.13 1.75 1.13 2.95 0 4.22-2.58 5.15-5.03 5.42.39.34.75 1.01.75 2.04v3.03c0 .29.2.64.76.53A10.53 10.53 0 0 0 23.02 11.5C23.02 5.24 18.27.5 12 .5Z" />
    </svg>
  );
}

export function WindowsIcon({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} fill="currentColor" aria-hidden>
      <path d="M2 4.3 10 3.2v7.7H2V4.3Zm0 15.4 8 1.1v-7.6H2v6.5Zm9 1.2 11 1.6V13.2H11v7.7Zm0-17.8v7.8h11V1.5L11 3.1Z" />
    </svg>
  );
}

export function LinuxIcon({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} fill="currentColor" aria-hidden>
      <path d="M12.5 2c-2.2 0-3.6 1.8-3.6 4.4 0 1.3.3 2.2-.4 3.4-.9 1.6-2.6 3.4-2.8 5.6-.1 1 .2 1.9.7 2.5-.6.4-1.6.6-1.8 1.4-.2.9.9 1.4 2.1 1.7 1 .2 1.8.9 2.7 1 1 .1 1.5-.6 2.2-.7.8-.1 1.6.1 2.6.1 1 0 1.5.8 2.6.7 1-.1 1.6-.9 2.6-1.5 1-.5 2.3-.8 2.4-1.7.1-.8-1-1.2-1.6-1.7.5-.8.7-1.7.4-2.7-.5-1.7-2-3.3-2.8-4.6-.8-1.3-.5-2.4-.6-3.9C16.9 3.7 15 2 12.5 2Zm-1.3 3.6c.4 0 .7.5.7 1.1 0 .4-.1.7-.3.9l-.6-.2c.1-.2.2-.4.2-.7 0-.3-.1-.6-.3-.6s-.3.3-.3.6v.2l-.5-.1c0-.7.4-1.2 1.1-1.2Zm2.5 0c.6 0 1.1.6 1.1 1.3 0 .3 0 .5-.2.7l-.4-.2c.1-.1.1-.3.1-.5 0-.4-.2-.7-.5-.7s-.5.3-.5.7c0 .1 0 .2.1.4l-.5-.2c0-.2-.1-.3-.1-.5 0-.6.4-1 .9-1Zm-1.2 2.3c.6 0 1.4.4 2.1.6.3.1.5.3.4.5-.1.3-.4.4-.8.6-.6.3-1.2.7-1.8.7-.6 0-1-.4-1.4-.7-.2-.1-.5-.3-.4-.6.1-.3.5-.4.9-.6.4-.3.6-.5 1-.5Z" />
    </svg>
  );
}

/* ---------------------------------------------------------- SectionHeader */
export function SectionHeader({ kicker, title, lead, className, center = false }: { kicker: string; title: string; lead?: string; className?: string; center?: boolean }) {
  return (
    <div className={cn("max-w-3xl", center && "mx-auto text-center", className)}>
      <Reveal>
        <p className="mb-4 inline-flex items-center gap-2 text-[13px] font-semibold uppercase tracking-[0.18em] text-muted">
          <span className="size-1.5 rounded-full bg-[var(--ambient)] shadow-[0_0_12px_var(--ambient)] transition-colors duration-1000" />
          {kicker}
        </p>
      </Reveal>
      <BlurText as="h2" text={title} className="block font-display text-4xl font-semibold leading-[1.05] tracking-tight text-balance sm:text-5xl md:text-6xl" />
      {lead && (
        <Reveal delay={0.15}>
          <p className="mt-5 text-lg leading-relaxed text-muted text-pretty">{lead}</p>
        </Reveal>
      )}
    </div>
  );
}

/* ---------------------------------------------------------------- Buttons */
export function ButtonPrimary({ href, children, className, ...rest }: { href: string; children: ReactNode; className?: string } & React.AnchorHTMLAttributes<HTMLAnchorElement>) {
  return (
    <a
      href={href}
      className={cn(
        "group relative inline-flex h-14 items-center gap-3 overflow-hidden rounded-full bg-white pl-6 pr-7 text-[16px] font-semibold text-ink shadow-[0_10px_40px_-10px_rgb(255_255_255/0.45)] transition-transform duration-300 hover:scale-[1.02] active:scale-[0.98]",
        className,
      )}
      {...rest}
    >
      <span
        aria-hidden
        className="lowmem-hide pointer-events-none absolute inset-0 -translate-x-full bg-[linear-gradient(110deg,transparent_30%,rgb(0_0_0/0.08)_50%,transparent_70%)] transition-transform duration-700 group-hover:translate-x-full"
      />
      {children}
    </a>
  );
}

export function ButtonGhost({ href, children, className, ...rest }: { href: string; children: ReactNode; className?: string } & React.AnchorHTMLAttributes<HTMLAnchorElement>) {
  return (
    <a
      href={href}
      className={cn(
        "glass inline-flex h-14 items-center gap-3 rounded-full border border-line-strong px-6 text-[15px] font-medium text-white/90 transition-colors duration-300 hover:border-white/35 hover:bg-white/10",
        className,
      )}
      {...rest}
    >
      {children}
    </a>
  );
}
