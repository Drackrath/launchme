import { AnimatePresence, motion } from "motion/react";
import { ArrowUpRight, Bug, Download, FolderOpen, Plus, Terminal } from "lucide-react";
import { useState } from "react";
import { LogoFull } from "../components/Logo";
import { ButtonGhost, ButtonPrimary, DiscordIcon, GitHubIcon, Magnet, Reveal, SectionHeader, WindowsIcon } from "../components/ui";
import { cn } from "../lib/cn";
import { usePage } from "../lib/page";
import {
  CHANGELOG_URL,
  DISCORD_URL,
  formatDate,
  formatSize,
  GITHUB_URL,
  ISSUES_URL,
  LATEST_URL,
  LICENSE_URL,
  RELEASES_URL,
  SDK_URL,
} from "../lib/release";

/* --------------------------------------------------------------- Install */

export function Install() {
  const { t, release } = usePage();
  const icons = [Download, FolderOpen, WindowsIcon];
  return (
    <section id="install" className="relative mx-auto mt-40 max-w-6xl scroll-mt-28 px-5">
      <SectionHeader kicker={t.install.kicker} title={t.install.title} />
      <div className="mt-12 grid gap-4 md:grid-cols-3">
        {t.install.steps.map((step, i) => {
          const Icon = icons[i];
          return (
            <Reveal key={step.title} delay={i * 0.1} className="relative overflow-hidden rounded-[26px] border border-line bg-panel/70 p-7">
              <span className="absolute -right-3 -top-6 font-display text-[120px] font-bold leading-none text-white/[0.04]">{i + 1}</span>
              <span className="flex size-11 items-center justify-center rounded-2xl border border-line-strong bg-white/5">
                <Icon className="size-5 text-white" />
              </span>
              <p className="mt-5 font-display text-xl font-semibold text-white">{step.title}</p>
              <p className="mt-1.5 text-[15px] text-muted">{step.text}</p>
              {i === 0 && release.windows && (
                <a href={release.windows.url} className="mt-4 inline-flex items-center gap-1.5 font-mono text-[12.5px] text-white/80 hover:text-white hover:underline">
                  {release.windows.name} <span className="text-white/40">· {formatSize(release.windows.size)}</span>
                </a>
              )}
              {i === 2 && <p className="mt-4 font-mono text-[12.5px] text-white/60">%LOCALAPPDATA%\Launchme</p>}
            </Reveal>
          );
        })}
      </div>
      <Reveal delay={0.2}>
        <p className="mt-6 max-w-3xl text-[14.5px] leading-relaxed text-dim">{t.install.keys}</p>
      </Reveal>
    </section>
  );
}

/* ------------------------------------------------------------------- FAQ */

export function Faq() {
  const { t } = usePage();
  const [open, setOpen] = useState<number | null>(0);
  return (
    <section id="faq" className="relative mx-auto mt-40 grid max-w-6xl scroll-mt-28 gap-10 px-5 lg:grid-cols-[0.8fr_1.2fr]">
      <SectionHeader kicker={t.faq.kicker} title={t.faq.title} />
      <div className="divide-y divide-line rounded-[26px] border border-line bg-panel/60">
        {t.faq.items.map((item, i) => (
          <div key={item.q}>
            <button className="flex w-full items-center justify-between gap-4 px-6 py-5 text-left" onClick={() => setOpen(open === i ? null : i)} aria-expanded={open === i}>
              <span className="text-[16.5px] font-medium text-white">{item.q}</span>
              <motion.span animate={{ rotate: open === i ? 45 : 0 }} className="flex size-8 shrink-0 items-center justify-center rounded-full border border-line text-white/70">
                <Plus className="size-4" />
              </motion.span>
            </button>
            <AnimatePresence initial={false}>
              {open === i && (
                <motion.div initial={{ height: 0, opacity: 0 }} animate={{ height: "auto", opacity: 1 }} exit={{ height: 0, opacity: 0 }} transition={{ duration: 0.3, ease: [0.22, 1, 0.36, 1] }} className="overflow-hidden">
                  <p className="px-6 pb-5 text-[15px] leading-relaxed text-muted">{item.a}</p>
                </motion.div>
              )}
            </AnimatePresence>
          </div>
        ))}
      </div>
    </section>
  );
}

/* ------------------------------------------------------------------- CTA */

export function FinalCta() {
  const { t, release, lang } = usePage();
  return (
    <section className="relative mx-auto mt-40 max-w-6xl px-5">
      <Reveal className="relative overflow-hidden rounded-[36px] border border-line bg-panel px-6 py-20 text-center sm:px-12">
        <div aria-hidden className="pointer-events-none absolute inset-0">
          <div className="dots-bg absolute inset-0 [mask-image:radial-gradient(ellipse_at_center,#000,transparent_70%)]" />
          <div className="lowmem-hide absolute left-1/2 top-1/2 h-[420px] w-[720px] -translate-x-1/2 -translate-y-1/2 rounded-full bg-[radial-gradient(closest-side,var(--ambient),transparent)] opacity-50 blur-3xl transition-[background] duration-1000" />
        </div>
        <div className="relative">
          <h2 className="font-display text-5xl font-semibold tracking-tight text-white sm:text-7xl">{t.cta.title}</h2>
          <p className="mx-auto mt-5 max-w-xl text-lg text-muted">{t.cta.text}</p>
          <div className="mt-10 flex flex-wrap items-center justify-center gap-3">
            <Magnet strength={0.18}>
              <ButtonPrimary href={release.windows?.url ?? LATEST_URL}>
                <WindowsIcon className="size-5" />
                {t.hero.downloadWin}
                <span className="text-[13px] font-normal text-ink/55">v{release.version}</span>
              </ButtonPrimary>
            </Magnet>
            <ButtonGhost href={release.linux?.url ?? LATEST_URL}>
              <Terminal className="size-[18px]" />
              {t.hero.downloadLinux}
            </ButtonGhost>
            <ButtonGhost href={ISSUES_URL} target="_blank" rel="noreferrer">
              <Bug className="size-[18px]" />
              {t.cta.issue}
            </ButtonGhost>
          </div>
          {release.date && (
            <p className="mt-5 text-[13px] text-dim">
              {release.tag} · {t.hero.released} {formatDate(release.date, lang)}
            </p>
          )}
        </div>
      </Reveal>
    </section>
  );
}

/* ---------------------------------------------------------------- Footer */

export function Footer() {
  const { t, href, lang, layout } = usePage();
  const f = t.footer;
  const project = [
    [f.links.source, GITHUB_URL],
    [f.links.releases, RELEASES_URL],
    [f.links.changelog, CHANGELOG_URL],
    [f.links.issues, ISSUES_URL],
    [f.links.discord, DISCORD_URL],
    [f.links.sdk, SDK_URL],
    [f.links.license, LICENSE_URL],
  ];
  return (
    <footer className="relative mt-40 border-t border-line">
      <div className="mx-auto grid max-w-6xl gap-12 px-5 py-16 md:grid-cols-[1.4fr_1fr_1fr]">
        <div>
          <LogoFull className="h-7 w-auto text-white" />
          <p className="mt-5 max-w-sm text-[15px] leading-relaxed text-muted">{f.tagline}</p>
          <a
            href={GITHUB_URL}
            target="_blank"
            rel="noreferrer"
            className="mt-6 inline-flex h-11 items-center gap-2.5 rounded-full border border-line-strong bg-white/5 px-5 text-[14px] text-white transition-colors hover:bg-white/10"
          >
            <GitHubIcon className="size-4" /> uxandai/launchme <ArrowUpRight className="size-4 text-white/50" />
          </a>
          <a
            href={DISCORD_URL}
            target="_blank"
            rel="noreferrer"
            className="ml-3 mt-6 inline-flex h-11 items-center gap-2.5 rounded-full border border-line-strong bg-white/5 px-5 text-[14px] text-white transition-colors hover:bg-white/10"
          >
            <DiscordIcon className="size-4" /> {f.discord} <ArrowUpRight className="size-4 text-white/50" />
          </a>
        </div>
        <div>
          <p className="text-[12.5px] font-semibold uppercase tracking-[0.14em] text-dim">{f.project}</p>
          <ul className="mt-4 space-y-2.5">
            {project.map(([label, url]) => (
              <li key={label}>
                <a href={url} target="_blank" rel="noreferrer" className="text-[15px] text-white/70 transition-colors hover:text-white">
                  {label}
                </a>
              </li>
            ))}
          </ul>
        </div>
        <div>
          <p className="text-[12.5px] font-semibold uppercase tracking-[0.14em] text-dim">{f.site}</p>
          <ul className="mt-4 space-y-2.5">
            <li>
              <a href={href(lang === "en" ? "pl" : "en", layout)} className="text-[15px] text-white/70 hover:text-white">
                {f.lang}
              </a>
            </li>
            <li>
              <a href={href(lang, layout === "v1" ? "v2" : "v1")} className="text-[15px] text-white/70 hover:text-white">
                {layout === "v1" ? f.layout : f.layoutBack}
              </a>
            </li>

            <li>
              <a href="#top" className={cn("text-[15px] text-white/70 hover:text-white")}>
                ↑ Top
              </a>
            </li>
          </ul>
        </div>
      </div>
      <div className="mx-auto max-w-6xl border-t border-line px-5 py-8 text-[12.5px] leading-relaxed text-dim">
        <p>{f.legal}</p>
        <p className="mt-2">{f.copyright} · GPL-3.0-or-later</p>
      </div>
      <div aria-hidden className="pointer-events-none mx-auto h-[clamp(70px,11vw,150px)] max-w-6xl select-none overflow-hidden px-5">
        <LogoFull className="w-full text-white/[0.05]" />
      </div>
    </footer>
  );
}
