#include "chessgui/core/version.hpp"

namespace chessgui::core {

std::string_view version() noexcept {
    return CHESSGUI_VERSION;
}

} // namespace chessgui::core
