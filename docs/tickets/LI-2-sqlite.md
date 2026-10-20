# LI-2 · SQLite schema + FTS5 search

- **Milestone:** M2 · **Est:** 3d · **Depends:** LI-1 · **Status:** in-review

## Story
The library's durable home: the schema from spec §7.2, FTS5 over the searchable text,
incremental upserts keyed by path, browse queries for the LI-3 tree, and the stamps the
scanner uses to skip unchanged files.

## Tasks
- [x] Schema exactly as spec §7.2 (`tracks` + `tracks_fts` external-content FTS5),
      with insert/update/delete triggers keeping the index in sync.
- [x] Open/migrate: WAL, `synchronous=NORMAL`, `user_version` gate (v1 here; a future
      version refuses to downgrade rather than corrupting).
- [x] `upsert(ScannedTrack)` by unique path, `stamp(path)` for incremental scans,
      `erase(path)`, `count()`.
- [x] Search: FTS5 prefix query over title/artist/album/album_artist/genre/composer,
      repeated terms ANDed; malformed input never throws.
- [x] Browse: albums (name, album artist, year, track count), artists, and an album's
      tracks in disc/track order.
- [x] Explicit transaction API for bulk scans (scanner batches inserts).
- [x] Perf harness in tests: 100k synthetic tracks; `search` p95 below the 100 ms
      acceptance bound on this machine, inserts batched.

## Acceptance
- Round-trip: scan fixtures → upsert → reopen the same file → rows and FTS survive.
- Search finds by title, artist, album and prefix; `fixture` matches
  "Fixture Artist"; a query with quotes/punctuation returns a result, not an error.
- Album browse groups correctly and track order is disc, then track.
- `stamp()` returns the stored mtime/size; `erase()` removes the row and the FTS entry.
- 100k inserts + 100 searches finish in a test-friendly time (batched transaction).

## Out of scope
- The Winamp query grammar → SQL builder and smart views (LI-3).
- Rating/playcount write hooks (LI-3 calls them).
- Remote (server) rows — `source`/`remote_id` columns exist for Phase 2.5.

## References
- `spec.md` §7.2-7.3 — schema and search/browse contract
- `docs/tickets/LI-1-scanner.md`
