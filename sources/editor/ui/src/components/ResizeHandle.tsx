import { useCallback, type KeyboardEvent, type PointerEvent } from "react";

import type { ResizeAxis } from "../ui-types";

interface ResizeHandleProps {
  axis: ResizeAxis;
  ariaLabel: string;
  onResize: (deltaPixels: number) => void;
}

/** Pointer and keyboard resize primitive. The layout owner owns constraints and persisted size. */
export function ResizeHandle({ axis, ariaLabel, onResize }: ResizeHandleProps) {
  const onPointerDown = useCallback((event: PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0) {
      return;
    }

    event.preventDefault();
    const initialPosition = axis === "vertical" ? event.clientX : event.clientY;
    const onPointerMove = (moveEvent: globalThis.PointerEvent) => {
      const nextPosition = axis === "vertical" ? moveEvent.clientX : moveEvent.clientY;
      // The workspace state re-renders between pointer events. A cumulative
      // delta keeps this gesture anchored to its initial split ratio instead
      // of applying later movement to a stale render closure.
      onResize(nextPosition - initialPosition);
    };
    const onPointerUp = () => {
      document.removeEventListener("pointermove", onPointerMove);
      document.removeEventListener("pointerup", onPointerUp);
      document.body.classList.remove("is-resizing");
    };

    document.body.classList.add("is-resizing");
    document.addEventListener("pointermove", onPointerMove);
    document.addEventListener("pointerup", onPointerUp, { once: true });
  }, [axis, onResize]);

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const previousKey = axis === "vertical" ? "ArrowLeft" : "ArrowUp";
    const nextKey = axis === "vertical" ? "ArrowRight" : "ArrowDown";
    const increment = event.shiftKey ? 24 : 8;

    if (event.key === previousKey) {
      event.preventDefault();
      onResize(-increment);
    } else if (event.key === nextKey) {
      event.preventDefault();
      onResize(increment);
    }
  };

  return (
    <div
      className={`resize-handle resize-handle--${axis}`}
      role="separator"
      aria-orientation={axis === "vertical" ? "vertical" : "horizontal"}
      aria-label={ariaLabel}
      tabIndex={0}
      onPointerDown={onPointerDown}
      onKeyDown={onKeyDown}
    />
  );
}
