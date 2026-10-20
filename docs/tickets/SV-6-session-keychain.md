# SV-6 · Remember the Jellyfin session in the OS keychain

- **Milestone:** M3 · **Est:** 1d · **Depends:** SV-1, SV-5 · **Status:** done

## Story
A QuickConnect (or password) sign-in survives a restart. The access token is stored in the OS credential store, not in `servers.json`. The next launch restores the session; a 401 or removing the server drops the keychain entry and the user signs in again.

## Tasks
- [x] `SecretStore` seam plus `rememberSession` / `restoreSession` / `forgetSession`. The book stays identity-only. — `src/servers/SecretStore.{hpp,cpp}`; `server_book_tests` restores a token the book file does not contain.
- [x] OS backends: Windows Credential Manager, macOS login keychain, Linux Secret Service with KWallet when Secret Service is absent. — `PlatformSecretStore_{win,mac,linux}.cpp`. `platform_secret_tests` round-trips a value when a keyring is running and skips otherwise.
- [x] Connection dialog writes the token on login, restores it when the saved server is selected, and deletes it on Remove. Startup restores the active server so Browse works before the dialog opens. A browser 401 forgets the key. — `ConnectionDialog`, `main.cpp`; `connection_dialog_tests` signs in against the fixture, opens a second dialog, and checks the token is back and absent from `servers.json`.

## Acceptance / Definition of Done
- `./build/linux-gcc/tests/server_book_tests "keychain token"` passes.
- `./build/linux-gcc/tests/connection_dialog_tests "secret store"` passes (offscreen).
- `./build/linux-gcc/tests/platform_secret_tests` passes or skips. A skip is a missing keyring, not a failed round-trip.
- `servers.json` still contains no token (both tests above).

## Out of scope
- Persisting the token inside `servers.json` or any file the app writes itself.
- Password storage. The keychain holds the access token only.
- Plex token storage (the same seam can take it when SV-3 lands).
- Re-checking the token against the server at startup. The first 401 drops it.

## References
- `spec.md` §8.2 — QuickConnect and the credential-store rule
- `docs/tickets/SV-5-jellyfin-browser.md` — the in-memory session this extends
- Secret Service: `org.freedesktop.secrets`. KWallet: `org.kde.kwalletd6` / `org.kde.KWallet`
- Windows `CredWriteW` / `CredReadW`. macOS `SecItemAdd` / `SecItemCopyMatching`
