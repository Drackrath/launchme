import { CountUp, Reveal } from "../components/ui";
import { usePage } from "../lib/page";

export function Stats() {
  const { t } = usePage();
  return (
    <section className="mx-auto mt-28 max-w-6xl px-5">
      <div className="grid grid-cols-2 gap-px overflow-hidden rounded-[28px] border border-line bg-line sm:grid-cols-3 lg:grid-cols-6">
        {t.stats.map((stat, i) => (
          <div key={stat.label} className="bg-[#0b0c0f] px-6 py-7"><Reveal delay={i * 0.06}>
            <div className="font-display text-4xl font-semibold tracking-tight text-white sm:text-5xl">
              {/* VRAM counts down to zero, the rest count up. */}
              <CountUp to={stat.value} from={stat.value === 0 ? 143 : 0} suffix={stat.suffix} duration={stat.value === 0 ? 2.4 : 1.6} />
            </div>
            <p className="mt-2 text-[13.5px] leading-snug text-muted">{stat.label}</p>
          </Reveal></div>
        ))}
      </div>
    </section>
  );
}
