// SPDX-License-Identifier: MIT
#include "ui/SkinHitRegion.hpp"

#include <algorithm>

#include <QPolygon>

#include "ui/PlayerRenderer.hpp"

namespace koalaamp::ui {

namespace {

using L = MainLayout;
using S = ShadeLayout;

QRect scaled(const QRect& rect, int scale) {
    return QRect(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
}

/// Hot areas of the full (unshaded) window, in the paint order used by
/// `hitTest`: later entries win, so the list runs back-to-front.
const std::vector<HitRegion>& normalRegions() {
    static const std::vector<HitRegion> regions = {
        {HitId::TitleBar, L::titleBar},
        {HitId::Marquee, QRect(L::marquee.x(), L::marquee.y(), L::marquee.width(), 12)},
        {HitId::ClutterO, QRect(L::clutterBar.x(), L::clutterBar.y() + L::clutterO.y(), 8, 8)},
        {HitId::ClutterA, QRect(L::clutterBar.x(), L::clutterBar.y() + L::clutterA.y(), 8, 7)},
        {HitId::ClutterI, QRect(L::clutterBar.x(), L::clutterBar.y() + L::clutterI.y(), 8, 7)},
        {HitId::ClutterD, QRect(L::clutterBar.x(), L::clutterBar.y() + L::clutterD.y(), 8, 8)},
        {HitId::ClutterV, QRect(L::clutterBar.x(), L::clutterBar.y() + L::clutterV.y(), 8, 7)},
        {HitId::PlayPauseIndicator, L::playPause},
        {HitId::Time, L::time},
        {HitId::Visualizer, L::visualizer},
        {HitId::Kbps, L::kbps},
        {HitId::Khz, L::khz},
        {HitId::MonoStereo, QRect(L::mono.x(), L::mono.y(), 57, 12)},
        {HitId::Volume, L::volume},
        {HitId::Balance, L::balance},
        {HitId::EqButton, L::eqButton},
        {HitId::PlButton, L::plButton},
        {HitId::Position, L::position},
        {HitId::Previous, L::previous},
        {HitId::Play, L::play},
        {HitId::Pause, L::pause},
        {HitId::Stop, L::stop},
        {HitId::Next, L::next},
        {HitId::Eject, L::eject},
        {HitId::Shuffle, L::shuffle},
        {HitId::Repeat, L::repeat},
        // Window buttons last: they sit on top of the title bar.
        {HitId::OptionButton, L::option},
        {HitId::MinimizeButton, L::minimize},
        {HitId::ShadeButton, L::shade},
        {HitId::CloseButton, L::close},
    };
    return regions;
}

/// Hot areas of the windowshade bar (275x14). The transport buttons are drawn
/// by the art, but their hit areas are still live (Webamp keeps the same
/// `.actions` handlers at shade coordinates).
const std::vector<HitRegion>& shadeRegions() {
    static const std::vector<HitRegion> regions = {
        {HitId::TitleBar, S::background},
        {HitId::ShadeMiniTime, S::miniTime},
        {HitId::ShadePosition, S::position},
        {HitId::Previous, QRect(169, 2, 7, 10)},
        {HitId::Play, QRect(176, 2, 10, 10)},
        {HitId::Pause, QRect(186, 2, 9, 10)},
        {HitId::Stop, QRect(195, 2, 9, 10)},
        {HitId::Next, QRect(204, 2, 10, 10)},
        {HitId::Eject, QRect(215, 2, 10, 10)},
        {HitId::OptionButton, L::option},
        {HitId::MinimizeButton, L::minimize},
        {HitId::ShadeButton, L::shade},
        {HitId::CloseButton, L::close},
    };
    return regions;
}

const char* sectionName(bool shade) { return shade ? "windowshade" : "normal"; }

bool pointInPolygon(const std::vector<skins::RegionPoint>& points, const QPoint& p) {
    bool inside = false;
    const auto count = static_cast<int>(points.size());
    for (int i = 0, j = count - 1; i < count; j = i++) {
        const auto& a = points[static_cast<std::size_t>(i)];
        const auto& b = points[static_cast<std::size_t>(j)];
        const bool crosses = ((a.y > p.y()) != (b.y > p.y()));
        if (!crosses) {
            continue;
        }
        const double x = static_cast<double>(b.x - a.x) * (p.y() - a.y) /
                             static_cast<double>(b.y - a.y) +
                         a.x;
        if (p.x() < x) {
            inside = !inside;
        }
    }
    return inside;
}

const std::vector<skins::RegionPolygon>* polygonsFor(const skins::Region& region, bool shade) {
    const auto it = region.sections.find(sectionName(shade));
    if (it == region.sections.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace

std::vector<HitRegion> hitRegions(const PlayerState& state, int scale) {
    const auto& source = state.shade ? shadeRegions() : normalRegions();
    std::vector<HitRegion> result;
    result.reserve(source.size());
    for (const auto& region : source) {
        result.push_back({region.id, scaled(region.rect, scale)});
    }
    return result;
}

HitId hitTest(const PlayerState& state, const QPoint& point, int scale) {
    const auto& source = state.shade ? shadeRegions() : normalRegions();
    for (auto it = source.rbegin(); it != source.rend(); ++it) {
        if (scaled(it->rect, scale).contains(point)) {
            return it->id;
        }
    }
    return HitId::None;
}

QRegion buildWindowMask(const skins::Region& region, bool shade, const QSize& size) {
    const auto* polygons = polygonsFor(region, shade);
    if (polygons == nullptr) {
        return {};
    }
    QRegion mask;
    for (const auto& polygon : *polygons) {
        if (polygon.points.size() < 3) {
            continue;  // Webamp drops degenerate polygons
        }
        QPolygon poly;
        poly.reserve(static_cast<int>(polygon.points.size()));
        for (const auto& p : polygon.points) {
            poly << QPoint(p.x, p.y);
        }
        mask = mask.united(QRegion(poly));
    }
    return mask.intersected(QRegion(0, 0, size.width(), size.height()));
}

bool maskContains(const skins::Region& region, bool shade, const QPoint& point) {
    const auto* polygons = polygonsFor(region, shade);
    if (polygons == nullptr) {
        return true;  // unmasked: everything is clickable
    }
    for (const auto& polygon : *polygons) {
        if (polygon.points.size() >= 3 && pointInPolygon(polygon.points, point)) {
            return true;
        }
    }
    return false;
}

}  // namespace koalaamp::ui
