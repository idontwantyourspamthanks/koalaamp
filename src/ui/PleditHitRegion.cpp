// SPDX-License-Identifier: MIT
#include "ui/PleditHitRegion.hpp"

#include <algorithm>

#include "ui/PleditRenderer.hpp"

namespace koalaamp::ui {

namespace {

QRect scaled(const QRect& rect, int scale) {
    return QRect(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
}

}  // namespace

std::vector<PleditHitRegion> pleditHitRegions(const PleditState& state, int scale) {
    if (state.shade) {
        return {
            {PleditHitId::TitleBar, scaled(QRect(0, 0, pleditWidth(state), kPleditShadeHeight),
                                           scale)},
            {PleditHitId::ShadeButton, scaled(pleditShadeButton(state), scale)},
            {PleditHitId::CloseButton, scaled(pleditCloseButton(state), scale)},
            {PleditHitId::ResizeGrip,
             scaled(QRect(pleditWidth(state) - 20, 3, 9, 9), scale)},
        };
    }
    std::vector<PleditHitRegion> regions = {
        {PleditHitId::TitleBar, scaled(pleditTopBar(state), scale)},
        {PleditHitId::ScrollBar, scaled(pleditScrollBar(state), scale)},
        {PleditHitId::ScrollUp, scaled(pleditScrollUp(state), scale)},
        {PleditHitId::ScrollDown, scaled(pleditScrollDown(state), scale)},
        {PleditHitId::Visualizer, scaled(pleditVisualizer(state), scale)},
        {PleditHitId::RunningTime, scaled(pleditRunningTime(state), scale)},
        {PleditHitId::MenuAdd, scaled(pleditMenuButton(state, 0), scale)},
        {PleditHitId::MenuRemove, scaled(pleditMenuButton(state, 1), scale)},
        {PleditHitId::MenuSelection, scaled(pleditMenuButton(state, 2), scale)},
        {PleditHitId::MenuMisc, scaled(pleditMenuButton(state, 3), scale)},
        {PleditHitId::ActionPrevious, scaled(pleditActionButton(state, 0), scale)},
        {PleditHitId::ActionPlay, scaled(pleditActionButton(state, 1), scale)},
        {PleditHitId::ActionPause, scaled(pleditActionButton(state, 2), scale)},
        {PleditHitId::ActionStop, scaled(pleditActionButton(state, 3), scale)},
        {PleditHitId::ActionNext, scaled(pleditActionButton(state, 4), scale)},
        {PleditHitId::ActionEject, scaled(pleditActionButton(state, 5), scale)},
        {PleditHitId::ResizeGrip, scaled(pleditResizeGrip(state), scale)},
        // Buttons sit on top of the title bar.
        {PleditHitId::ShadeButton, scaled(pleditShadeButton(state), scale)},
        {PleditHitId::CloseButton, scaled(pleditCloseButton(state), scale)},
    };
    return regions;
}

PleditHitId pleditHitTest(const PleditState& state, const QPoint& point, int scale) {
    const QPoint local(point.x() / std::max(1, scale), point.y() / std::max(1, scale));
    const auto regions = pleditHitRegions(state, 1);
    PleditHitId hit = PleditHitId::None;
    for (const auto& region : regions) {
        if (region.rect.contains(local)) {
            hit = region.id;
        }
    }
    return hit;
}

int pleditRowAt(const PleditState& state, const QPoint& point, int scale) {
    if (state.shade) {
        return -1;
    }
    const QPoint local(point.x() / std::max(1, scale), point.y() / std::max(1, scale));
    const QRect area = pleditListArea(state);
    if (!area.contains(local)) {
        return -1;
    }
    const int row = (local.y() - area.top() - kPleditListPad) / kPleditTrackHeight;
    const int index = state.scrollOffset + row;
    if (row < 0 || index >= static_cast<int>(state.entries.size())) {
        return -1;
    }
    return index;
}

int pleditMenuIndex(PleditHitId id) {
    switch (id) {
        case PleditHitId::MenuAdd: return 0;
        case PleditHitId::MenuRemove: return 1;
        case PleditHitId::MenuSelection: return 2;
        case PleditHitId::MenuMisc: return 3;
        default: return -1;
    }
}

}  // namespace koalaamp::ui
