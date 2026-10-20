# LI-1 · Scanner + TagLib pipeline

- **Milestone:** M2 · **Est:** 5d · **Depends:** FE-4 · **Status:** in-review

## Story
Point the player at folders and get a library: a recursive scanner that reads tags
through TagLib, detects changes by mtime/size, skips hidden/ignored trees, and keeps
watching for creates/modifies/deletes. This is the data source LI-2 stores and LI-3
renders.

## Tasks
- [x] Root walk: whitelist `mp3 flac ogg oga opus m4a aac alac wav wv ape mpc it mod s3m xm`;
      hidden files/dirs skipped; a directory containing `.ignore` is skipped.
- [x] TagLib metadata: title, artist, album artist, album, genre, year, track, disc,
      composer, publisher, comment, BPM; duration/bitrate/sample-rate/channels from
      `AudioProperties`; ReplayGain/R128 from the tag properties.
- [x] Files TagLib cannot parse (MOD family) still enter the library with
      filename-derived metadata rather than disappearing.
- [x] Incremental rescan: the caller supplies the DB's `(mtime, size)` stamp; unchanged
      files are skipped, changed files are re-read, new files are added.
- [x] Folder art detection (`folder.*`, `cover.*`, `front.*`) recorded on the track.
- [x] Polling watcher (default 2 s) with debounce; detects create/modify/delete and
      reports after the tree settles. Cross-platform, Qt-free.
- [x] Fixture corpus + tests: tagged FLAC/MP3/OGG, untagged WAV, hidden and `.ignore`
      trees, art detection, incremental scan, watcher latency.

## Acceptance
- Scanning the fixture tree yields exactly the whitelisted, non-ignored files; every
  tag field maps; ReplayGain values parse (`-6.5 dB` → −6.5).
- A second scan with the DB stamps reports `unchanged` and emits no tracks; touching
  a file turns it into `updated`.
- The watcher reports a created file within one interval (≤ 5 s), debounced once.
- Cancellation stops the scan and reports `cancelled`.
- No Qt, no libvlc: `koalaamp_library` stays a headless data module.

## Out of scope
- Writing tags, cuesheets, embedded-art extraction (display art is LI-3/cover work).
- libvlc pre-scan for formats without TagLib audio properties (duration stays 0; the
  DB tolerates it).
- The library window and smart views (LI-3).

## References
- `spec.md` §7.1 — scanner contract and field set
- `docs/research/formats.md` — tag-format edge cases
