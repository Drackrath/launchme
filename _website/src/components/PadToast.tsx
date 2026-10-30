import { AnimatePresence, motion } from "motion/react";
import { Headphones } from "lucide-react";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";

export type ToastDevice = { kind: "pad" | "headphones"; pad?: string; name: string; battery: number | null };

// The launcher's connection toast: the device's picture in a circle, the battery as a green ring.
export function PadToast({ device, title, className }: { device: ToastDevice; title: string; className?: string }) {
  const { asset } = usePage();
  const r = 46;
  const circumference = 2 * Math.PI * r;
  const fraction = device.battery == null ? 1 : Math.max(0.02, device.battery / 100);
  const ringColor = device.battery != null && device.battery <= 15 ? "#ff5d5d" : "#3ddc84";
  return (
    <div className={cn("glass flex w-[340px] items-center gap-4 rounded-[22px] border border-line p-3 pr-5 shadow-[0_30px_80px_-30px_rgb(0_0_0/0.9)]", className)}>
      <div className="relative size-[76px] shrink-0">
        <svg viewBox="0 0 100 100" className="absolute inset-0 -rotate-90">
          <circle cx="50" cy="50" r={r} fill="rgb(255 255 255 / 0.04)" stroke="rgb(255 255 255 / 0.08)" strokeWidth="4" />
          <motion.circle
            key={device.name}
            cx="50"
            cy="50"
            r={r}
            fill="none"
            stroke={ringColor}
            strokeWidth="4"
            strokeLinecap="round"
            strokeDasharray={circumference}
            initial={{ strokeDashoffset: circumference }}
            animate={{ strokeDashoffset: circumference * (1 - fraction) }}
            transition={{ duration: 1.1, ease: [0.22, 1, 0.36, 1], delay: 0.15 }}
          />
        </svg>
        <div className="absolute inset-[9px] flex items-center justify-center overflow-hidden rounded-full bg-black/40">
          <AnimatePresence mode="popLayout">
            {device.kind === "pad" && device.pad ? (
              <motion.img
                key={device.pad}
                src={asset(`img/pads/${device.pad}.webp`)}
                alt=""
                className="w-[86%] object-contain"
                initial={{ opacity: 0, scale: 0.6, rotate: -8 }}
                animate={{ opacity: 1, scale: 1, rotate: 0 }}
                exit={{ opacity: 0, scale: 0.6 }}
                transition={{ type: "spring", stiffness: 260, damping: 20 }}
              />
            ) : (
              <motion.div key="hp" initial={{ opacity: 0, scale: 0.6 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0 }}>
                <Headphones className="size-8 text-white/90" />
              </motion.div>
            )}
          </AnimatePresence>
        </div>
      </div>
      <div className="min-w-0">
        <AnimatePresence mode="wait">
          <motion.div key={device.name} initial={{ opacity: 0, x: 8 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: -8 }} transition={{ duration: 0.25 }}>
            <p className="truncate text-[15px] font-semibold text-white">{title}</p>
            <p className="truncate text-[13px] text-white/60">
              {device.name}
              {device.battery != null && <span className="text-white/45"> · {device.battery}%</span>}
            </p>
          </motion.div>
        </AnimatePresence>
      </div>
    </div>
  );
}
