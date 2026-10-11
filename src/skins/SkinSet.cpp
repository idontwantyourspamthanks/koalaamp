// SPDX-License-Identifier: MIT
#include "skins/SkinSet.hpp"

#include <algorithm>
#include <cstdint>

#include "skins/BmpDecoder.hpp"

namespace koalaamp::skins {
namespace {

constexpr std::size_t kMaxTextBytes = 256u * 1024u;  // per unknown text member
constexpr std::size_t kMaxCursors = 512;

bool isBmp(const std::vector<std::uint8_t>& data) {
    return data.size() >= 2 && data[0] == 'B' && data[1] == 'M';
}

std::string asLatin1(const std::vector<std::uint8_t>& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::string lowerCopy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isUnknownTextMember(std::string_view basename) {
    const auto endsWith = [basename](std::string_view suffix) {
        return basename.size() >= suffix.size() &&
               basename.compare(basename.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return endsWith(".txt") || endsWith(".ini");
}

constexpr std::string_view kHandledText[] = {"viscolor.txt", "pledit.txt", "region.txt"};

bool isHandledText(std::string_view basename) {
    for (const auto name : kHandledText) {
        if (basename == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

const std::vector<SkinFileSpec>& canonicalSkinFiles() {
    static const std::vector<SkinFileSpec> kFiles = {
        {"main.bmp", true, ""},
        {"titlebar.bmp", false, ""},
        {"cbuttons.bmp", false, ""},
        {"posbar.bmp", false, ""},
        {"volume.bmp", false, ""},
        {"balance.bmp", false, ""},
        {"shufrep.bmp", false, ""},
        {"monoster.bmp", false, ""},
        {"playpaus.bmp", false, ""},
        {"numbers.bmp", false, "falls back to nums_ex.bmp"},
        {"nums_ex.bmp", false, ""},
        {"text.bmp", false, ""},
        {"pledit.bmp", false, ""},
        {"eqmain.bmp", false, ""},
        {"eq_ex.bmp", false, "EQ windowshade uses eqmain.bmp"},
        {"gen.bmp", false, ""},
        {"genex.bmp", false, ""},
        {"video.bmp", false, ""},
        {"mb.bmp", false, ""},
        {"viscolor.txt", false, "built-in 24-colour default"},
        {"pledit.txt", false, "built-in playlist colours"},
        {"region.txt", false, "rectangular window"},
    };
    return kFiles;
}

QSize referenceSheetSize(std::string_view name) {
    // Header geometry verified on base-2.91.wsz in the FE-3 spike
    // (docs/spikes/fe3-wsz-render.md §5): monoster 58x24, text 155x74,
    // eq_ex 275x82, gen 194x109.
    static const std::map<std::string_view, QSize> kSizes = {
        {"main.bmp", QSize(275, 116)},     {"titlebar.bmp", QSize(344, 87)},
        {"cbuttons.bmp", QSize(136, 36)},  {"posbar.bmp", QSize(307, 10)},
        {"volume.bmp", QSize(68, 433)},    {"balance.bmp", QSize(68, 433)},
        {"shufrep.bmp", QSize(92, 85)},    {"monoster.bmp", QSize(58, 24)},
        {"playpaus.bmp", QSize(42, 9)},    {"numbers.bmp", QSize(99, 13)},
        {"nums_ex.bmp", QSize(99, 13)},    {"text.bmp", QSize(155, 74)},
        {"pledit.bmp", QSize(280, 186)},   {"eqmain.bmp", QSize(275, 315)},
        {"eq_ex.bmp", QSize(275, 82)},     {"gen.bmp", QSize(194, 109)},
    };
    const auto it = kSizes.find(name);
    return it == kSizes.end() ? QSize() : it->second;
}

bool SkinSet::hasSheet(std::string_view name) const {
    return sheets.sheet(name) != nullptr;
}

QSize SkinSet::sheetSize(std::string_view name) const { return sheets.sheetSize(name); }

bool SkinSet::hasCursor(std::string_view name) const {
    std::string key = lowerCopy(name);
    if (key.find('.') == std::string::npos) {
        key += ".cur";
    }
    return std::any_of(cursors.begin(), cursors.end(), [&key](const std::string& c) {
        return c.size() == key.size() &&
               std::equal(c.begin(), c.end(), key.begin(), [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) ==
                          std::tolower(static_cast<unsigned char>(b));
               });
    });
}

std::string SkinSet::missingSummary() const {
    std::string out;
    for (const auto& name : missingOptional) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

void SkinSet::warn(std::string message) {
    if (std::find(warnings.begin(), warnings.end(), message) == warnings.end()) {
        warnings.push_back(std::move(message));
    }
}

std::optional<SkinSet> assembleSkinSet(const SkinContainer& container,
                                       const std::filesystem::path& source) {
    if (container.empty()) {
        return std::nullopt;
    }

    SkinSet set;
    set.info.source = source;
    set.info.name = source.filename().string();
    if (set.info.name.empty()) {
        set.info.name = source.string();
    }

    const bool hasMain = container.contains("main.bmp");
    if (!hasMain) {
        return std::nullopt;  // Winamp cannot use a skin without its background
    }

    // --- Canonical bitmaps ---------------------------------------------------
    for (const auto& spec : canonicalSkinFiles()) {
        const auto* data = container.findData(spec.name);
        const bool isBitmap = spec.name.size() >= 4 &&
                              spec.name.compare(spec.name.size() - 4, 4, ".bmp") == 0;
        if (!isBitmap) {
            continue;
        }
        if (data == nullptr) {
            set.missingOptional.emplace_back(spec.name);
            continue;
        }
        std::optional<QImage> image;
        if (isBmp(*data)) {
            image = decodeBmp(*data);
        }
        if (!image) {
            set.warn(std::string(spec.name) + " is not a decodable BMP");
            if (spec.required) {
                return std::nullopt;
            }
            continue;
        }
        const QSize decoded = image->size();
        set.sheets.addSheet(spec.name, std::move(*image));

        const QSize reference = referenceSheetSize(spec.name);
        if (reference.isValid() &&
            (decoded.width() < reference.width() || decoded.height() < reference.height())) {
            set.warn(std::string(spec.name) + " is undersized (" +
                     std::to_string(decoded.width()) + "x" + std::to_string(decoded.height()) +
                     ", expected " + std::to_string(reference.width()) + "x" +
                     std::to_string(reference.height()) + ")");
        }
    }

    // The mandatory sheet must have decoded above.
    if (!set.hasSheet("MAIN")) {
        return std::nullopt;
    }
    set.info.mainWidth = set.sheetSize("main.bmp").width();
    set.info.mainHeight = set.sheetSize("main.bmp").height();
    if (set.info.mainWidth <= 0 || set.info.mainHeight <= 0) {
        return std::nullopt;
    }

    // --- Text configuration --------------------------------------------------
    set.visColor = defaultVisColor();
    if (const auto* data = container.findData("viscolor.txt")) {
        set.visColor = parseVisColor(asLatin1(*data));
    } else {
        set.missingOptional.emplace_back("viscolor.txt");
    }

    set.pleditColors = defaultPleditColors();
    if (const auto* data = container.findData("pledit.txt")) {
        set.pleditColors = parsePledit(asLatin1(*data));
    } else {
        set.missingOptional.emplace_back("pledit.txt");
    }

    if (const auto* data = container.findData("region.txt")) {
        set.region = parseRegion(asLatin1(*data));
        if (set.region.empty()) {
            set.warn("region.txt produced no usable polygons");
        }
    } else {
        set.missingOptional.emplace_back("region.txt");
    }

    // --- Cursors + other text members ---------------------------------------
    for (const auto& entry : container.entries()) {
        const std::string& base = entry.basename;
        if (base.size() >= 4) {
            const std::string ext = base.substr(base.size() - 4);
            if ((ext == ".cur" || ext == ".ani") && set.cursors.size() < kMaxCursors) {
                std::string key(base);
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (std::find(set.cursors.begin(), set.cursors.end(), key) == set.cursors.end()) {
                    set.cursors.push_back(std::move(key));
                }
            }
        }
        if (isUnknownTextMember(base) && !isHandledText(base)) {
            if (set.otherText.count(base) != 0) {
                continue;  // first (shallowest) match already stored
            }
            if (entry.data.size() > kMaxTextBytes) {
                set.warn(base + " exceeds the 256 KiB text cap and was skipped");
                continue;
            }
            set.otherText.emplace(base, asLatin1(entry.data));
        }
    }

    std::sort(set.cursors.begin(), set.cursors.end());
    std::sort(set.missingOptional.begin(), set.missingOptional.end());
    return set;
}

std::optional<SkinSet> loadSkinSet(const std::filesystem::path& skin) {
    auto container = loadContainer(skin);
    if (!container) {
        return std::nullopt;
    }
    return assembleSkinSet(*container, skin);
}

}  // namespace koalaamp::skins
