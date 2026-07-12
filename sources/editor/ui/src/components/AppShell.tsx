import type { ReactNode } from "react";

import "../styles/editor-shell.css";

interface AppShellProps {
  children: ReactNode;
  toolbar: ReactNode;
  status?: ReactNode;
}

/** Top-level editor chrome. Layout and panel state deliberately stay outside it. */
export function AppShell({ children, toolbar, status }: AppShellProps) {
  return (
    <main className="app-shell" aria-label="21KB Engine editor">
      <header className="app-shell__header">
        <div className="app-shell__brand" aria-label="21KB Engine">
          <span className="app-shell__brand-mark" aria-hidden="true">21</span>
          <span className="app-shell__brand-name">Engine</span>
        </div>
        <div className="app-shell__toolbar">{toolbar}</div>
        {status ? <div className="app-shell__status">{status}</div> : null}
      </header>
      <section className="app-shell__workspace">{children}</section>
    </main>
  );
}
