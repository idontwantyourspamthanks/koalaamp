# AGENTS.md — working contract for AI contributors

Rules for any AI agent (or human) making changes in this repo. The full plan lives in `docs/`; this file is the operational contract.

## Read first
1. `docs/README.md` → the doc map.
2. Your ticket: `docs/tickets/<ID>-<slug>.md`. If your task has no ticket file, create one from `docs/tickets/_template.md` *before* starting work (in the same PR).
3. The governing spec section your ticket's *References* point to. **Do not redesign what the spec pins** — file an issue/PR against the doc instead.

## Clean-room rules (non-negotiable)
- **Never consult the 2024 Winamp source dump, its mirrors, or any WCL-covered code.** Not "just to check a coordinate."
- **Never copy from GPL/BSD-copyleft projects** (Qmmp, Audacious plugins, qtWasabi VM source, xnde code). Behavior reference only, black-box.
- MIT sources (Webamp, butterchurn, projectm-eval, eversolo notes) **may be read and adapted, with attribution** in the file header comment and in `LICENSES.md`.
- LGPL components (Qt, libvlc, projectM, TagLib) ship dynamically linked — never statically, never vendored into the source tree.
- The PR description must attest: "No WCL/GPL source was consulted for this change."

## Boundaries
- Your ticket's **Out of scope** section is law. Adjacent improvements go into a new ticket stub, not your diff.
- Don't expand this repo's scope beyond `docs/spec.md` without a spec PR first.

## Definition of done (per ticket file; general rules)
- Every DoD item is checked by a command, fixture, or diff — run it, paste the output in the PR.
- New behavior gets a test that would fail before the change (regression), when practical.
- ASAN/UBSAN clean for anything touching the audio path or NDE/migration parsers.
- `grep -rn "X-Emby-Token\|api_key" src/servers/` must stay empty (Jellyfin header-auth rule).
- No secrets, tokens, or `settings.json` runtime state ever committed.

## Commits & PRs
- Conventional-ish subjects: `feat(skins): port region.txt leniency cases` / `fix(dsp): clear audio-filter on DSP-mode switch`.
- One logical change per commit; tests and code in the same PR.
- Keep `docs/` updated with the change (tickets flip Status; spike reports link back).

## Repo layout
```
docs/         plan: feasibility, spec, tickets (read-only for workers unless ticket says otherwise)
src/          (future) app code — ui/, playback/, library/, servers/, skins/, interop/
tests/        (future) unit + fixture tests; corpora under tests/fixtures/
scripts/      (future) vendoring, license-audit, corpus tooling
third_party/  gitignored; vendored by script from pinned sources only
```
