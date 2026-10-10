// SPDX-License-Identifier: MIT
// interop — thin façades over the third-party dependencies (docs/spec.md §2.2).
//
// Every external library (libvlc, projectM, TagLib, SQLite, miniaudio, libcurl,
// JSON) gets a narrow façade here with a stub implementation for unit tests,
// so no other module includes a dependency header directly. Nothing is wired
// yet: FE-2 (libvlc/Strings), FE-5 (libcurl/JSON) and FE-14 (projectM) own the
// integrations. The dependency inventory is enforced by scripts/license-audit.sh
// against deps/dependencies.json.

#pragma once

namespace koalaamp::interop {

// Façades are declared per-dependency as their tickets land.

} // namespace koalaamp::interop
