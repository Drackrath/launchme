import { useEffect } from "react";

// The page glow follows whatever screen is on show, like the launcher's background follows the
// focused game.
export function setAmbient(color: string) {
  if (typeof document === "undefined") return;
  document.documentElement.style.setProperty("--ambient", color);
}

export function useAmbient(color: string, active: boolean) {
  useEffect(() => {
    if (active) setAmbient(color);
  }, [color, active]);
}

export type PadAction = "left" | "right" | "up" | "down" | "lb" | "rb" | "a" | "b";

export function onPad(handler: (action: PadAction) => void) {
  const listener = (event: Event) => handler((event as CustomEvent<PadAction>).detail);
  window.addEventListener("lm-pad", listener);
  return () => window.removeEventListener("lm-pad", listener);
}

export function emitPad(action: PadAction) {
  window.dispatchEvent(new CustomEvent<PadAction>("lm-pad", { detail: action }));
}
