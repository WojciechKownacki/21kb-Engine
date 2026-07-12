import { useId, type KeyboardEvent } from "react";

export interface TabItem {
  id: string;
  label: string;
  disabled?: boolean;
}

interface TabStripProps {
  tabs: readonly TabItem[];
  activeTabId: string;
  ariaLabel: string;
  onActivate: (tabId: string) => void;
}

/** Roving-keyboard tab control. A DockTree group supplies its panel ids as tabs. */
export function TabStrip({ tabs, activeTabId, ariaLabel, onActivate }: TabStripProps) {
  const groupId = useId();
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const currentIndex = tabs.findIndex((tab) => tab.id === activeTabId);
    if (currentIndex < 0 || !["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) {
      return;
    }

    event.preventDefault();
    const direction = event.key === "ArrowLeft" ? -1 : 1;
    let nextIndex = event.key === "Home" ? 0 : event.key === "End" ? tabs.length - 1 : currentIndex;
    if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
      for (let count = 0; count < tabs.length; count += 1) {
        nextIndex = (nextIndex + direction + tabs.length) % tabs.length;
        if (!tabs[nextIndex].disabled) {
          break;
        }
      }
    }
    const nextTab = tabs[nextIndex];
    if (!nextTab.disabled) {
      onActivate(nextTab.id);
      requestAnimationFrame(() => document.getElementById(`${groupId}-${nextTab.id}`)?.focus());
    }
  };

  return (
    <div className="tab-strip" role="tablist" aria-label={ariaLabel} onKeyDown={onKeyDown}>
      {tabs.map((tab) => (
        <button
          key={tab.id}
          id={`${groupId}-${tab.id}`}
          className={`tab-strip__tab${tab.id === activeTabId ? " is-active" : ""}`}
          type="button"
          role="tab"
          aria-selected={tab.id === activeTabId}
          tabIndex={tab.id === activeTabId ? 0 : -1}
          disabled={tab.disabled}
          onClick={() => onActivate(tab.id)}
        >
          {tab.label}
        </button>
      ))}
    </div>
  );
}
