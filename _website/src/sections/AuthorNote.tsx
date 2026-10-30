import { Bot } from "lucide-react";
import { LogoSymbol } from "../components/Logo";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";

// The honest note: vibecoded, free, personal, shared.
export function AuthorNote({ compact = false, className }: { compact?: boolean; className?: string }) {
  const { t } = usePage();
  return (
    <div
      className={cn(
        "glass relative overflow-hidden rounded-[26px] border border-line text-left",
        compact ? "p-5 sm:p-6" : "p-7 sm:p-9",
        className,
      )}
    >
      <div aria-hidden className="lowmem-hide pointer-events-none absolute -right-16 -top-16 size-48 rounded-full bg-[radial-gradient(closest-side,var(--ambient),transparent)] opacity-50 blur-2xl transition-[background] duration-1000" />
      <div className="relative flex gap-4 sm:gap-5">
        <div className="relative mt-0.5 flex size-11 shrink-0 items-center justify-center rounded-2xl border border-line-strong bg-white/5">
          <LogoSymbol className="size-6 text-white" />
          <span className="absolute -bottom-1.5 -right-1.5 flex size-5 items-center justify-center rounded-full border border-line-strong bg-panel-3">
            <Bot className="size-3 text-white/80" />
          </span>
        </div>
        <div className="min-w-0">
          <p className="text-[12px] font-semibold uppercase tracking-[0.16em] text-dim">{t.note.kicker}</p>
          <p className={cn("mt-1 font-display font-semibold tracking-tight text-white", compact ? "text-[19px]" : "text-2xl")}>{t.note.title}</p>
          {t.note.body.map((paragraph, i) => (
            <p key={i} className={cn("mt-2 leading-relaxed text-white/70", compact ? "text-[14.5px]" : "text-[16px]")}>
              {paragraph}
            </p>
          ))}
          <p className="mt-3 text-right font-display text-[14px] italic text-white/50">{t.note.sign}</p>
        </div>
      </div>
    </div>
  );
}
