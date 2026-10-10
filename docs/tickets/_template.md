# Ticket template

Copy for each expanded ticket file: `docs/tickets/<ID>-<slug>.md`. Keep the index (`docs/tickets.md`) as the master for sequencing/sizing; these files are the per-task contract for whoever (or whatever) implements.

---

- **ID / title:** `<ID> · <title>`
- **Milestone:** M0-M3 · **Est:** Nd focused · **Depends:** <IDs>
- **Status:** unstarted | in-progress | blocked | in-review | done

## Story
One paragraph: the user-visible outcome, or (spikes) the decision the spike retires.

## Tasks
Checkbox list, ordered. Each item is independently committable.

## Acceptance / Definition of Done
Every item checkable by a command, a fixture run, or a diff against a baseline — no "works on my machine". Cite the exact command where possible.

## Out of scope
The adjacent temptations. Prevents scope creep better than any prose.

## References
- `spec.md` §… — the governing section
- `docs/research/…` — evidence with the load-bearing citations
- source paths (koalapaw, Webamp tables) — absolute or repo-relative
- spike reports (once they exist) — `docs/spikes/…`

## Notes / risks
Only what a implementer would otherwise get wrong. Delete this section if empty.
