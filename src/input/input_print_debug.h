#pragma once

#include <iosfwd>

namespace diffuri {

    struct RawSystem;  // forward-декларация, без включения input.h

    // Печать в произвольный поток
    void PrintSystem(const RawSystem& sys, std::ostream& os);

    // Печать в std::cout
    void PrintSystem(const RawSystem& sys);

    // Идиоматичный вариант: std::cout << sys;
    std::ostream& operator<<(std::ostream& os, const RawSystem& sys);

} // namespace diffuri