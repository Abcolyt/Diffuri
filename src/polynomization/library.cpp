// ============================================================================
// src/polynomization/library.cpp
//
// Реализация FunctionLibrary: пять расширений этапа.
// ============================================================================
#include "polynomization/library.h"

namespace diffuri {

    std::optional<Expansion> FunctionLibrary::Lookup(const std::string& name) const {
        Expansion e;
        if (name == "inv") {
            e.functions = { "inv" };
            e.equations.emplace("inv",
                MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0),
                    MakeBinary(Binary::Op::Pow,
                        MakeFunction("inv"), MakeNumber(2.0))));
        }
        else if (name == "ln") {
            e.functions = { "ln", "inv" };
            e.equations.emplace("ln", MakeFunction("inv"));
            e.equations.emplace("inv",
                MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0),
                    MakeBinary(Binary::Op::Pow,
                        MakeFunction("inv"), MakeNumber(2.0))));
        }
        else if (name == "exp") {
            e.functions = { "exp" };
            e.equations.emplace("exp", MakeFunction("exp"));
        }
        else if (name == "sin" || name == "cos") {
            e.functions = { "sin", "cos" };
            e.equations.emplace("sin", MakeFunction("cos"));
            e.equations.emplace("cos",
                MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0), MakeFunction("sin")));
        }
        else if (name == "sh" || name == "ch") {
            e.functions = { "sh", "ch" };
            e.equations.emplace("sh", MakeFunction("ch"));
            e.equations.emplace("ch", MakeFunction("sh"));
        }
        else {
            return std::nullopt;
        }
        return e;
    }

    bool FunctionLibrary::Contains(const std::string& name) const {
        return name == "inv" || name == "ln" || name == "exp" ||
            name == "sin" || name == "cos" ||
            name == "sh" || name == "ch";
    }

} // namespace diffuri