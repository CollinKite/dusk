#pragma once

#include <algorithm>
#include <cmath>

namespace dusk::duo {
struct Metrics {
    float width = 0, height = 0;
    float left = 0, right = 0, bottom = 0;
    float divisionTop = 0, divisionBottom = 0;
    bool hingeAvailable = false, closed = false, partiallyOpen = false;
};
struct Layout {
    float gameHeight = 1;
    float x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Layout&) const = default;
    bool active() const { return width > 0 && height > 0; }
};

// All output coordinates are fractions of this view, never device dimensions.
// Manual mode also works without a hinge; automatic mode requires its real state.
inline Layout calculate(const Metrics& m, bool enabled, bool automatic) {
    if (!enabled || !std::isfinite(m.width) || !std::isfinite(m.height) ||
        m.width <= 0 || m.height <= m.width || m.closed ||
        (automatic && (!m.hingeAvailable || !m.partiallyOpen))) return {};
    float top = m.height * .5f, bottom = top;
    if (m.divisionTop > m.height * .2f && m.divisionBottom < m.height * .8f &&
        m.divisionBottom >= m.divisionTop) {
        top = m.divisionTop;
        bottom = m.divisionBottom;
    }
    const float left = std::clamp(m.left, 0.f, m.width);
    const float right = std::clamp(m.right, 0.f, m.width - left);
    const float height = m.height - bottom - std::max(m.bottom, 0.f);
    // Fall back to the whole game view if either panel is too small for controls.
    if (m.width - left - right < 280 || top < 200 || height < 200) return {};
    return {top / m.height, left / m.width, bottom / m.height,
            (m.width - left - right) / m.width, height / m.height};
}
} // namespace dusk::duo
