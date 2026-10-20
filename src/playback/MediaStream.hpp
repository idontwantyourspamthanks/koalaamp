// SPDX-License-Identifier: MIT
// playback — the byte-stream seam libvlc reads through its callback API.
//
// SV-2 direct play: a server track is not a local file, and materialising it in a
// temp file would defeat the point (a seek must re-request, not download the
// track first). `libvlc_media_new_callbacks` takes open/read/seek/close callbacks
// and one opaque pointer; this header is the engine-side shape of that contract,
// so the tap (interop) and the Jellyfin source (servers) meet without either
// depending on the other.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace koalaamp::playback {

/// One opened bitstream. libvlc calls read/seek from its demux thread, one at a
/// time: a single stream instance need not be reentrant.
class IMediaStream {
public:
    virtual ~IMediaStream() = default;

    /// Fills up to `len` bytes. Returns the number read, 0 at end of stream, or
    /// -1 on a non-recoverable error. `buf` is valid for `len` bytes.
    virtual std::ptrdiff_t read(unsigned char* buf, std::size_t len) = 0;

    /// Absolute seek. Returns false when the source cannot serve the offset.
    virtual bool seek(std::int64_t offset) = 0;
};

/// A re-openable source. libvlc's open callback may run more than once over a
/// media's lifetime (libvlc documents that a media may serve several player
/// instances), so `open` hands back a fresh stream each time instead of exposing
/// mutable state.
///
/// MUST outlive every media/player created from it.
class IMediaStreamSource {
public:
    virtual ~IMediaStreamSource() = default;

    /// Opens at byte 0. `size` receives the resource size, or -1 when unknown.
    /// Returns nullptr when the resource cannot be opened.
    virtual std::unique_ptr<IMediaStream> open(std::int64_t* size) = 0;
};

} // namespace koalaamp::playback
