// SPDX-License-Identifier: MIT
//
// Skin — a decoded classic skin: a case-insensitive map of sheet name ("MAIN",
// "TITLEBAR", ...) to the decoded QImage, plus sprite blitting.
//
// Promoted from the FE-3 spike (`spikes/fe3_wsz/Skin.*`) by SE-1; the loading
// entry point is now the container-agnostic `loadSkinSet` in SkinSet.hpp.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <QImage>
#include <QPainter>

#include "skins/SkinSprites.hpp"
#include "skins/ZipReader.hpp"

namespace koalaamp::skins {

class Skin {
public:
    /// Convenience loader for a single ZIP archive (the FE-3 spike path).
    /// SE-1 callers use `loadSkinSet` for archive *and* folder containers.
    static std::optional<Skin> load(const std::string &path);

    /// Stores a decoded sheet under its upper-case base name ("MAIN"), replacing
    /// any previous sheet with that name.
    void addSheet(std::string_view name, QImage image);

    /// Decoded sheet by upper-case base name ("MAIN"), or nullptr.
    const QImage *sheet(std::string_view name) const;

    /// Decoded size of a sheet, or a null size when it is absent.
    QSize sheetSize(std::string_view name) const;

    bool empty() const { return m_sheets.empty(); }

    /// Source rect for `spriteName` on the named sheet, if present in the table
    /// and inside the decoded sheet's bounds. A sprite that runs off an
    /// undersized sheet reports nullopt, which callers render as a hole — the
    /// classic Winamp behavior for art smaller than the sprite table expects.
    std::optional<SpriteRect> spriteRect(std::string_view sheet, std::string_view spriteName) const;

    /// Copies the sprite `name` from `sheetName` onto `painter` at (x, y),
    /// clipped to `clipW` x `clipH` destination pixels (<= 0 means the full
    /// sprite). No scaling, no smoothing — exact integer blit.
    bool blit(QPainter &painter, std::string_view sheetName, std::string_view name, int x, int y,
              int clipW = 0, int clipH = 0) const;

    /// Copies an explicit source rectangle of a sheet to (dstX, dstY). Used for
    /// slider strips whose source rect is animation-dependent.
    bool blitRect(QPainter &painter, std::string_view sheetName, const QRect &src, int dstX,
                  int dstY) const;

    /// Classic skins carry no alpha; QImage::Format_RGB32 throughout.
    const std::map<std::string, QImage, std::less<>> &sheets() const { return m_sheets; }

private:
    std::map<std::string, QImage, std::less<>> m_sheets;
};

/// Upper-cases a sheet stem ("main" -> "MAIN").
std::string sheetKey(std::string_view name);

}  // namespace koalaamp::skins
