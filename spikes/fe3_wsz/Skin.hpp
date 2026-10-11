// SPDX-License-Identifier: MIT
//
// Skin — a decoded classic `.wsz`: a case-insensitive map of sheet name
// ("MAIN", "TITLEBAR", ...) to the decoded QImage, plus sprite blitting.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <QImage>
#include <QPainter>

#include "SkinSprites.hpp"
#include "ZipReader.hpp"

namespace fe3 {

class Skin {
public:
    /// Loads and decodes every `*.bmp` in the archive. Returns nullopt when the
    /// file is not a readable ZIP.
    static std::optional<Skin> load(const std::string &path);

    /// Decoded sheet by upper-case base name ("MAIN"), or nullptr.
    const QImage *sheet(std::string_view name) const;

    /// Source rect for `spriteName` on the named sheet, if present in the table
    /// and inside the decoded sheet's bounds.
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

}  // namespace fe3
