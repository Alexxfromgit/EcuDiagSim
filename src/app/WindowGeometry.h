#pragma once

#include <QRect>
#include <QSize>

#include <algorithm>

/// Where the main window opens and the smallest size it may be resized to.
struct InitialWindowGeometry
{
    QRect geometry;
    QSize minimum;
};

/// Fits the window into the available area of its screen (the work area without taskbar or dock):
/// the preferred size shrinks to leave a margin, the result is centred, and it never extends past
/// the available area. On a screen smaller than the window's minimum size the minimum gives way,
/// so a 1024x600 display with a taskbar still shows the whole window.
inline InitialWindowGeometry fitWindowToScreen(const QRect &available, QSize preferred, QSize minimum,
    QSize margin = QSize(40, 60))
{
    const QSize minSize(std::min(minimum.width(), available.width()), std::min(minimum.height(), available.height()));
    const int w = std::clamp(std::min(preferred.width(), available.width() - margin.width()), minSize.width(),
        available.width());
    const int h = std::clamp(std::min(preferred.height(), available.height() - margin.height()), minSize.height(),
        available.height());
    const QRect geometry(available.x() + (available.width() - w) / 2, available.y() + (available.height() - h) / 2,
        w, h);
    return {geometry, minSize};
}
