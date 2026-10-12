// SPDX-License-Identifier: MIT
#include "ui/EqHitRegion.hpp"

#include <algorithm>

#include "ui/EqRenderer.hpp"

namespace koalaamp::ui {

namespace {

QRect scaled(const QRect& rect, int scale) {
    return QRect(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
}

/// `eqHitTest` walks the list back-to-front (later entries win), so the list
/// runs from the window background up to the buttons on top.
const std::vector<EqHitRegion>& normalRegions() {
    static const std::vector<EqHitRegion> regions = [] {
        std::vector<EqHitRegion> out;
        out.push_back({EqHitId::TitleBar, EqLayout::titleBar, -1});
        out.push_back({EqHitId::SetMaxDb, EqLayout::plus12db, -1});
        out.push_back({EqHitId::SetUnityDb, EqLayout::zerodb, -1});
        out.push_back({EqHitId::SetMinDb, EqLayout::minus12db, -1});
        out.push_back({EqHitId::Slider,
                       QRect(EqLayout::preamp.x(), EqLayout::bandY, EqLayout::bandWidth,
                             EqLayout::bandHeight),
                       0});
        for (int band = 1; band < kEqWindowSliders; ++band) {
            out.push_back({EqHitId::Slider,
                           QRect(eqBandX(band), EqLayout::bandY, EqLayout::bandWidth,
                                 EqLayout::bandHeight),
                           band});
        }
        out.push_back({EqHitId::OnButton, EqLayout::onButton, -1});
        out.push_back({EqHitId::AutoButton, EqLayout::autoButton, -1});
        out.push_back({EqHitId::PresetsButton, EqLayout::presetsButton, -1});
        out.push_back({EqHitId::ShadeButton, EqLayout::shadeButton, -1});
        out.push_back({EqHitId::CloseButton, EqLayout::closeButton, -1});
        return out;
    }();
    return regions;
}

const std::vector<EqHitRegion>& shadeRegions() {
    static const std::vector<EqHitRegion> regions = {
        {EqHitId::TitleBar, EqLayout::shadeBackground, -1},
        {EqHitId::ShadeVolume, EqLayout::shadeVolume, -1},
        {EqHitId::ShadeBalance, EqLayout::shadeBalance, -1},
        {EqHitId::ShadeButton, EqLayout::shadeButton, -1},
        {EqHitId::CloseButton, EqLayout::closeButton, -1},
    };
    return regions;
}

}  // namespace

std::vector<EqHitRegion> eqHitRegions(const EqWindowState& state, int scale) {
    const auto& source = state.shade ? shadeRegions() : normalRegions();
    std::vector<EqHitRegion> out;
    out.reserve(source.size());
    for (const EqHitRegion& region : source) {
        out.push_back({region.id, scaled(region.rect, scale), region.sliderIndex});
    }
    return out;
}

EqHitId eqHitTest(const EqWindowState& state, const QPoint& point, int scale, int* sliderIndex) {
    if (sliderIndex != nullptr) {
        *sliderIndex = -1;
    }
    const auto& source = state.shade ? shadeRegions() : normalRegions();
    for (auto it = source.rbegin(); it != source.rend(); ++it) {
        if (scaled(it->rect, scale).contains(point)) {
            if (sliderIndex != nullptr) {
                *sliderIndex = it->sliderIndex;
            }
            return it->id;
        }
    }
    return EqHitId::None;
}

}  // namespace koalaamp::ui
