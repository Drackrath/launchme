
export type LayoutKey = "shelves" | "xmb" | "flix" | "metro" | "psfive" | "snitch";

// The little layout drawings from the launcher's Settings → Interface picker.
export function Wireframe({ id, active }: { id: LayoutKey; active: boolean }) {
  const hi = active ? "rgb(255 255 255 / 0.9)" : "rgb(255 255 255 / 0.55)";
  const mid = "rgb(255 255 255 / 0.22)";
  const low = "rgb(255 255 255 / 0.1)";
  return (
    <svg viewBox="0 0 168 70" className="block h-auto w-full">
      <rect width="168" height="70" rx="5" fill="#0c0d10" />
      {id === "shelves" && (
        <g>
          <rect x="9" y="10" width="34" height="3" rx="1.5" fill={mid} />
          <rect x="9" y="36" width="24" height="24" rx="5" fill={hi} />
          {Array.from({ length: 8 }, (_, i) => (
            <rect key={i} x={38 + i * 16} y={46} width="13" height="13" rx="3" fill={low} />
          ))}
        </g>
      )}
      {id === "xmb" && (
        <g>
          {Array.from({ length: 10 }, (_, i) => (
            <rect key={i} x={8 + i * 16} y={8} width="11" height="9" rx="2" fill={i === 1 ? hi : low} />
          ))}
          {Array.from({ length: 4 }, (_, i) => (
            <g key={i}>
              <rect x={24} y={22 + i * 11} width="9" height="8" rx="2" fill={i === 0 ? hi : mid} />
              <rect x={38} y={25 + i * 11} width={i === 0 ? 34 : 24} height="2.5" rx="1.25" fill={i === 0 ? mid : low} />
            </g>
          ))}
        </g>
      )}
      {id === "flix" && (
        <g>
          <rect x="0" y="0" width="168" height="38" rx="5" fill="#16171b" />
          <rect x="9" y="12" width="30" height="3" rx="1.5" fill={mid} />
          <rect x="9" y="18" width="20" height="2.5" rx="1.25" fill={low} />
          <rect x="9" y="42" width="14" height="18" rx="3" fill={hi} />
          {Array.from({ length: 7 }, (_, i) => (
            <rect key={i} x={48 + i * 17} y={42} width="14" height="18" rx="3" fill={low} />
          ))}
        </g>
      )}
      {id === "metro" && (
        <g>
          <rect x="9" y="9" width="40" height="9" rx="2" fill={mid} />
          <rect x="9" y="22" width="40" height="18" rx="3" fill={low} />
          <rect x="9" y="44" width="19" height="17" rx="3" fill={low} />
          <rect x="30" y="44" width="19" height="17" rx="3" fill={low} />
          <rect x="53" y="9" width="44" height="31" rx="3" fill={hi} />
          <rect x="53" y="44" width="44" height="17" rx="3" fill={low} />
          {Array.from({ length: 6 }, (_, i) => (
            <rect key={i} x={101 + (i % 3) * 21} y={9 + Math.floor(i / 3) * 27} width="18" height="24" rx="3" fill={i === 0 ? mid : low} />
          ))}
        </g>
      )}
      {id === "psfive" && (
        <g>
          <rect x="9" y="8" width="13" height="13" rx="3" fill={hi} />
          {Array.from({ length: 8 }, (_, i) => (
            <rect key={i} x={25 + i * 10} y={10} width="8" height="8" rx="2" fill={low} />
          ))}
          <rect x="125" y="11" width="10" height="2.5" rx="1.25" fill={low} />
          <rect x="140" y="11" width="18" height="2.5" rx="1.25" fill={mid} />
          <rect x="9" y="28" width="24" height="3" rx="1.5" fill={mid} />
          <rect x="9" y="48" width="22" height="8" rx="4" fill={hi} />
          <rect x="34" y="48" width="8" height="8" rx="4" fill={mid} />
          <rect x="88" y="26" width="70" height="36" rx="5" fill={low} />
        </g>
      )}
      {id === "snitch" && (
        <g>
          <circle cx="12" cy="10" r="3" fill={mid} />
          <rect x="132" y="8.5" width="26" height="3" rx="1.5" fill={low} />
          <rect x="12" y="20" width="30" height="3" rx="1.5" fill={mid} />
          <rect x="12" y="27" width="22" height="22" rx="3" fill={hi} />
          {Array.from({ length: 5 }, (_, i) => (
            <rect key={i} x={38 + i * 25} y={27} width="22" height="22" rx="3" fill={low} />
          ))}
          {Array.from({ length: 8 }, (_, i) => (
            <circle key={i} cx={52 + i * 9} cy={59} r="2.6" fill={mid} />
          ))}
        </g>
      )}
    </svg>
  );
}
