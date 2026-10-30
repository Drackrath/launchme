import { createContext, useContext, type ReactNode } from "react";
import { MotionConfig } from "motion/react";

// The site used to have its own "Low memory mode" switch. It's gone: the full page is always shown.
// The context stays so components can still ask; the answer is always "off".
type LowMem = { lowmem: boolean };
const LowMemContext = createContext<LowMem>({ lowmem: false });

export function LowMemProvider({ children }: { children: ReactNode }) {
  return (
    <LowMemContext.Provider value={{ lowmem: false }}>
      <MotionConfig reducedMotion="never">{children}</MotionConfig>
    </LowMemContext.Provider>
  );
}

export function useLowMem() {
  return useContext(LowMemContext);
}
