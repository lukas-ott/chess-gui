#include "kiebitz/core/version.hpp"

namespace kiebitz::core {

std::string_view version() noexcept {
    return KIEBITZ_VERSION;
}

} // namespace kiebitz::core
