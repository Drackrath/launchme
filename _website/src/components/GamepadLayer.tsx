import { AnimatePresence, motion } from "motion/react";
import { useEffect, useRef, useState } from "react";
import { emitPad, type PadAction } from "../lib/ambient";
import { usePage } from "../lib/page";
import { PadToast } from "./PadToast";
import { HintBar } from "./ui";

// A pad connected to the visitor's PC is greeted like the launcher greets it, and drives the
// layout picker (D-pad) and the library views (LB/RB). Up/down scroll the page.
function padPicture(id: string): { pad: string; name: string } {
  const lower = id.toLowerCase();
  if (lower.includes("dualsense") || lower.includes("0ce6") || lower.includes("0df2")) return { pad: lower.includes("0df2") ? "dualsense-edge" : "dualsense", name: lower.includes("0df2") ? "DualSense Edge" : "DualSense" };
  if (lower.includes("054c") || lower.includes("dualshock") || lower.includes("wireless controller")) return { pad: "dualshock4", name: "DualShock 4" };
  if (lower.includes("8bitdo")) return { pad: "ultimate2", name: "8BitDo" };
  if (lower.includes("pro controller") || lower.includes("057e")) return { pad: "switch-pro", name: "Switch Pro Controller" };
  if (lower.includes("steam") || lower.includes("28de")) return { pad: "steam-controller2", name: "Steam Controller" };
  if (lower.includes("xbox") || lower.includes("045e") || lower.includes("xinput")) return { pad: "xbox", name: "Xbox Controller" };
  return { pad: "generic", name: "Controller" };
}

const BUTTONS: [number, PadAction][] = [
  [14, "left"],
  [15, "right"],
  [12, "up"],
  [13, "down"],
  [4, "lb"],
  [5, "rb"],
  [0, "a"],
  [1, "b"],
];

export function GamepadLayer() {
  const { t } = usePage();
  const [toast, setToast] = useState<{ pad: string; name: string } | null>(null);
  const [connected, setConnected] = useState(false);
  const previous = useRef<Record<string, boolean>>({});

  useEffect(() => {
    if (typeof navigator === "undefined" || !("getGamepads" in navigator)) return;
    let frame = 0;
    let hide: ReturnType<typeof setTimeout> | undefined;

    const poll = () => {
      const pads = navigator.getGamepads().filter(Boolean) as Gamepad[];
      const pad = pads[0];
      if (pad) {
        const pressed: Record<string, boolean> = {};
        for (const [index, action] of BUTTONS) pressed[action] = pressed[action] || !!pad.buttons[index]?.pressed;
        pressed.left ||= pad.axes[0] < -0.6;
        pressed.right ||= pad.axes[0] > 0.6;
        pressed.up ||= pad.axes[1] < -0.6;
        pressed.down ||= pad.axes[1] > 0.6;
        for (const action of Object.keys(pressed) as PadAction[]) {
          if (pressed[action] && !previous.current[action]) handle(action);
        }
        previous.current = pressed;
      }
      frame = requestAnimationFrame(poll);
    };

    const handle = (action: PadAction) => {
      emitPad(action);
      if (action === "down") window.scrollBy({ top: window.innerHeight * 0.6, behavior: "smooth" });
      if (action === "up") window.scrollBy({ top: -window.innerHeight * 0.6, behavior: "smooth" });
      if (action === "a" && document.activeElement instanceof HTMLElement && document.activeElement !== document.body) document.activeElement.click();
    };

    const onConnect = (event: GamepadEvent) => {
      setToast(padPicture(event.gamepad.id));
      setConnected(true);
      clearTimeout(hide);
      hide = setTimeout(() => setToast(null), 4200);
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(poll);
    };
    const onDisconnect = () => {
      if (!navigator.getGamepads().some(Boolean)) {
        setConnected(false);
        cancelAnimationFrame(frame);
      }
    };
    window.addEventListener("gamepadconnected", onConnect);
    window.addEventListener("gamepaddisconnected", onDisconnect);
    return () => {
      window.removeEventListener("gamepadconnected", onConnect);
      window.removeEventListener("gamepaddisconnected", onDisconnect);
      cancelAnimationFrame(frame);
      clearTimeout(hide);
    };
  }, []);

  return (
    <>
      <AnimatePresence>
        {toast && (
          <motion.div
            className="fixed right-4 top-20 z-[80]"
            initial={{ opacity: 0, y: -20, scale: 0.95 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, y: -20, scale: 0.95 }}
            transition={{ type: "spring", stiffness: 260, damping: 22 }}
          >
            <PadToast device={{ kind: "pad", pad: toast.pad, name: toast.name, battery: null }} title={t.bento.pads.connected} />
            <p className="mt-2 text-right text-[12.5px] text-white/55">{t.pad.hint}</p>
          </motion.div>
        )}
      </AnimatePresence>
      <AnimatePresence>
        {connected && (
          <motion.div className="fixed bottom-4 left-1/2 z-[75] -translate-x-1/2" initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 20 }}>
            <HintBar
              items={[
                { key: "◀ ▶", label: t.pad.layout },
                { key: "LB RB", label: t.pad.view },
                { key: "▲ ▼", label: "Scroll" },
              ]}
            />
          </motion.div>
        )}
      </AnimatePresence>
    </>
  );
}
