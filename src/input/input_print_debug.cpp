#include "input_print_debug.h"
#include "input/input.h"   // ради ToString и полного типа RawSystem
#include <iostream>

namespace diffuri {

    void PrintSystem(const RawSystem& sys, std::ostream& os) {
        os << ToString(sys);
    }

    void PrintSystem(const RawSystem& sys) {
        PrintSystem(sys, std::cout);
    }

    std::ostream& operator<<(std::ostream& os, const RawSystem& sys) {
        os << ToString(sys);
        return os;
    }

} // namespace diffuri