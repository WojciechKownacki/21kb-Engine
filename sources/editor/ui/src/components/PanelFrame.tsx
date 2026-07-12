import { useId, type ReactNode } from "react";

import type { PanelActionCallbacks, PanelId } from "../ui-types";

interface PanelFrameProps extends PanelActionCallbacks {
  panelId: PanelId;
  title: string;
  children: ReactNode;
  active?: boolean;
  tabs?: ReactNode;
  className?: string;
}

/** Accessible chrome shared by docked and floating panel hosts. */
export function PanelFrame({
  panelId,
  title,
  children,
  active = false,
  tabs,
  className = "",
  onClose,
  onDetach,
}: PanelFrameProps) {
  const titleId = useId();
  const classes = `panel-frame${active ? " is-active" : ""}${className ? ` ${className}` : ""}`;

  return (
    <section className={classes} aria-labelledby={titleId} data-panel-id={panelId}>
      <header className="panel-frame__header">
        <h2 id={titleId} className="panel-frame__title">{title}</h2>
        <div className="panel-frame__actions">
          {onDetach ? (
            <button className="panel-frame__action panel-frame__action--detach" type="button" onClick={() => onDetach(panelId)} aria-label={`Detach ${title}`} title={`Detach ${title}`}>
              <span aria-hidden="true" />
            </button>
          ) : null}
          {onClose ? (
            <button className="panel-frame__action panel-frame__action--close" type="button" onClick={() => onClose(panelId)} aria-label={`Close ${title}`} title={`Close ${title}`}>
              <span aria-hidden="true" />
            </button>
          ) : null}
        </div>
      </header>
      {tabs ? <div className="panel-frame__tabs">{tabs}</div> : null}
      <div className="panel-frame__body">{children}</div>
    </section>
  );
}
