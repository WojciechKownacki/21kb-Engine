# Security Policy

## Supported versions

| Version | Security fixes |
|---|---|
| 1.0 (branch `1.0`) | Yes |
| Earlier snapshots | No — update to the latest 1.0 release |

A fix is released for the latest 1.0 release. Games built with an affected engine version have to be
rebuilt and repackaged with the fixed engine; each advisory says which players are affected.

## Reporting a vulnerability

Please do not report security problems in public issues, discussions or pull requests.

Report privately through GitHub: open the repository's **Security** tab and choose
**Report a vulnerability**
(https://github.com/WojciechKownacki/21kb-Engine/security/advisories/new).

Include what you can of:

- the affected engine version or commit, and the component (runtime, editor, packaging scripts, tools);
- how to reproduce it: steps, a sample project, file or script, or a proof of concept;
- what an attacker gains: code execution, file access, data disclosure, crash, tampering with saves or packages.

## What happens next

| Step | Target |
|---|---|
| Acknowledge the report | 3 business days |
| First assessment and severity (CVSS 3.1) | 7 days |
| Fix released — critical | 30 days |
| Fix released — high | 60 days |
| Fix released — medium | 90 days |
| Fix released — low | next regular release |

We agree a disclosure date with the reporter: when the fix is released, and in any case no later than
90 days after the report, unless the reporter agrees to more time. Reporters are credited in the advisory
unless they ask not to be.

## Advisories and notifications

- Fixed vulnerabilities are published as GitHub Security Advisories, with a CVE identifier where one applies,
  and are listed in the release notes.
- Actively exploited vulnerabilities and severe incidents are notified to the authorities as required by the
  EU Cyber Resilience Act (Regulation (EU) 2024/2847) from 11 September 2026: an early warning within
  24 hours, a notification within 72 hours, and a final report within 14 days after a fix or mitigation is
  available.

## Scope

In scope: the engine runtime and packaged players, the editor, `kb_cli` and the other tools, the packaging
scripts, the file formats the engine reads (scenes, prefabs, packs, saves, assets) and the script sandbox.

Out of scope: games made with the engine (contact their publisher) and vulnerabilities that exist only in a
third-party component's own code (report those upstream as well; tell us so we can take the fix).

## Software bill of materials

Every packaged player contains `THIRD_PARTY_NOTICES.txt`, the license texts in `Licenses/`, and a
CycloneDX SBOM (`sbom.cdx.json`). The editor and tools' SBOM is produced by
`python scripts/third_party_notices.py --product engine --version <version> --out <directory>`. All three
come from [third_party/third_party_manifest.json](third_party/third_party_manifest.json).
