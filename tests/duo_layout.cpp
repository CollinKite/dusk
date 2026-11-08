#include "dusk/duo_layout.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using namespace dusk::duo;
    Metrics m{.width = 669, .height = 951, .left = 12, .right = 24, .bottom = 34,
              .divisionTop = 455, .divisionBottom = 495, .hingeAvailable = true};
    assert(!calculate(m, false, false).active());
    assert(!calculate(m, true, true).active());
    m.partiallyOpen = true;
    auto layout = calculate(m, true, true);
    assert(layout.active());
    assert(std::abs(layout.gameHeight * m.height - 455) < .001f);
    assert(std::abs(layout.y * m.height - 495) < .001f);
    assert(std::abs(layout.width * m.width - 633) < .001f);
    assert(std::abs((layout.y + layout.height) * m.height - 917) < .001f);
    m.closed = true;
    assert(!calculate(m, true, false).active());
    m.closed = false;
    m.hingeAvailable = false;
    assert(!calculate(m, true, true).active());
    assert(calculate(m, true, false).active());

    // Repeated rotation/folding uses current geometry and never carries a panel forward.
    for (int repeat = 0; repeat < 5; ++repeat) {
        for (auto size : {std::pair{669.f, 951.f}, {951.f, 669.f}, {466.f, 678.f}, {678.f, 466.f}}) {
            m.width = size.first; m.height = size.second; m.closed = m.width == 466 || m.height == 466;
            layout = calculate(m, true, false);
            if (m.closed || m.width >= m.height) assert(!layout.active());
            else assert(layout.active());
        }
    }
    for (int width = 200; width <= 1000; width += 20) {
        for (int height = 200; height <= 1400; height += 20) {
            m = {.width = float(width), .height = float(height), .left = 12, .right = 24, .bottom = 34};
            layout = calculate(m, true, false);
            if (!layout.active()) continue;
            assert(layout.gameHeight > 0 && layout.gameHeight < 1);
            assert(layout.y >= layout.gameHeight);
            assert(layout.x >= 0 && layout.x + layout.width <= 1.00001f);
            assert(layout.y + layout.height <= 1.00001f);
        }
    }
    m.width = std::numeric_limits<float>::quiet_NaN();
    assert(!calculate(m, true, false).active());
    m.width = 669; m.height = std::numeric_limits<float>::infinity();
    assert(!calculate(m, true, false).active());
    std::cout << "Duo layout regression checks passed\n";
}
