// SPDX-License-Identifier: MIT
#include "skins/Skin.hpp"

#include <algorithm>
#include <cctype>
#include <span>

#include "skins/BmpDecoder.hpp"

namespace koalaamp::skins {
namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string stemOf(const std::string &basename) {
    const auto dot = basename.find_last_of('.');
    return upper(dot == std::string::npos ? basename : basename.substr(0, dot));
}

/// All ported sprite definitions for a sheet, in a uniform view.
std::span<const SpriteDef> spriteSpanFor(std::string_view sheet) {
    if (sheet == "BALANCE") return kSpritesBALANCE;
    if (sheet == "CBUTTONS") return kSpritesCBUTTONS;
    if (sheet == "MAIN") return kSpritesMAIN;
    if (sheet == "MONOSTER") return kSpritesMONOSTER;
    if (sheet == "NUMBERS") return kSpritesNUMBERS;
    if (sheet == "NUMS_EX") return kSpritesNUMS_EX;
    if (sheet == "PLAYPAUS") return kSpritesPLAYPAUS;
    if (sheet == "POSBAR") return kSpritesPOSBAR;
    if (sheet == "SHUFREP") return kSpritesSHUFREP;
    if (sheet == "TITLEBAR") return kSpritesTITLEBAR;
    if (sheet == "VOLUME") return kSpritesVOLUME;
    if (sheet == "EQ_EX") return kSpritesEQ_EX;
    if (sheet == "EQMAIN") return kSpritesEQMAIN;
    if (sheet == "PLEDIT") return kSpritesPLEDIT;
    if (sheet == "GEN") return kSpritesGEN;
    if (sheet == "GENEX") return kSpritesGENEX;
    return {};
}

}  // namespace

std::string sheetKey(std::string_view name) { return stemOf(std::string(name)); }

void Skin::addSheet(std::string_view name, QImage image) {
    m_sheets.insert_or_assign(sheetKey(name), std::move(image));
}

std::optional<Skin> Skin::load(const std::string &path) {
    auto entries = readZipFile(path);
    if (!entries) {
        return std::nullopt;
    }
    Skin skin;
    for (const auto &entry : *entries) {
        if (entry.data.size() < 2 || entry.data[0] != 'B' || entry.data[1] != 'M') {
            continue;  // not a BMP (cursors, txt, ...)
        }
        auto image = decodeBmp(entry.data);
        if (!image) {
            continue;
        }
        skin.m_sheets.emplace(stemOf(entry.basename), std::move(*image));
    }
    if (skin.m_sheets.empty()) {
        return std::nullopt;
    }
    return skin;
}

const QImage *Skin::sheet(std::string_view name) const {
    const auto it = m_sheets.find(sheetKey(name));
    return it == m_sheets.end() ? nullptr : &it->second;
}

QSize Skin::sheetSize(std::string_view name) const {
    const QImage *image = sheet(name);
    return image == nullptr ? QSize() : image->size();
}

std::optional<SpriteRect> Skin::spriteRect(std::string_view sheetName,
                                           std::string_view spriteName) const {
    const auto span = spriteSpanFor(sheetName);
    const auto *image = sheet(sheetName);
    for (const auto &def : span) {
        if (def.name == spriteName) {
            if (image && (def.rect.x < 0 || def.rect.y < 0 ||
                          def.rect.x + def.rect.w > image->width() ||
                          def.rect.y + def.rect.h > image->height())) {
                return std::nullopt;  // sprite runs off the sheet (undersized art)
            }
            return def.rect;
        }
    }
    return std::nullopt;
}

bool Skin::blit(QPainter &painter, std::string_view sheetName, std::string_view name, int x, int y,
                int clipW, int clipH) const {
    const auto *image = sheet(sheetName);
    const auto rect = spriteRect(sheetName, name);
    if (!image || !rect) {
        return false;
    }
    const int w = (clipW > 0 && clipW < rect->w) ? clipW : rect->w;
    const int h = (clipH > 0 && clipH < rect->h) ? clipH : rect->h;
    painter.drawImage(QRect(x, y, w, h), *image, QRect(rect->x, rect->y, w, h));
    return true;
}

bool Skin::blitRect(QPainter &painter, std::string_view sheetName, const QRect &src, int dstX,
                    int dstY) const {
    const auto *image = sheet(sheetName);
    if (!image || !image->rect().contains(src)) {
        return false;
    }
    painter.drawImage(QPoint(dstX, dstY), *image, src);
    return true;
}

}  // namespace koalaamp::skins
