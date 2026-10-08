// ============================================================================
// src/pipeline/trace.cpp
// ============================================================================
#include "pipeline/trace.h"

#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace diffuri {

    std::string ToString(Stage stage) {
        switch (stage) {
        case Stage::Parsed:       return "Parsed";
        case Stage::Validated:    return "Validated";
        case Stage::Normalized:   return "Normalized";
        case Stage::OrderReduced: return "OrderReduced";
        case Stage::Polynomized:  return "Polynomized";
        case Stage::Solved:       return "Solved";
        }
        return "Unknown";
    }

    namespace {

        // Глубокое клонирование дерева: RawSystem содержит ExprPtr
        // (unique_ptr), поэтому обычное копирование системы невозможно.
        // Capture обязан сделать независимую копию, чтобы вызывающий код
        // мог продолжать мутировать свою систему.
        ExprPtr CloneExpr(const Expr& e) {
            return std::visit([&](const auto& n) -> ExprPtr {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return MakeNumber(n.value);
                }
                else if constexpr (std::is_same_v<T, Function>) {
                    return MakeFunction(n.name);
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return MakeConstant(n.name, n.value);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    return MakeDerivative(n.function_name, n.order);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    return MakeUnary(n.op, CloneExpr(*n.operand));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(n.op,
                        CloneExpr(*n.lhs),
                        CloneExpr(*n.rhs));
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    std::vector<ExprPtr> args;
                    args.reserve(n.args.size());
                    for (const auto& a : n.args) {
                        args.push_back(CloneExpr(*a));
                    }
                    return MakeCall(n.name, std::move(args));
                }
                return nullptr;
                }, e.value);
        }

        RawSystem CloneSystem(const RawSystem& sys) {
            RawSystem out;
            out.independent_variable = sys.independent_variable;
            out.functions = sys.functions;
            out.initial_conditions = sys.initial_conditions;
            out.equations.reserve(sys.equations.size());
            for (const auto& eq : sys.equations) {
                out.equations.push_back(Equation{
                    CloneExpr(*eq.lhs),
                    CloneExpr(*eq.rhs)
                    });
            }
            return out;
        }

        // Формат одной записи вспомогательной переменной:
        //   Derivative{f, k}  ->  "<name> (from f')"  (k штрихов)
        //   всё остальное     ->  "<name> = <ToString(def)>"
        void AppendAuxDefinition(std::ostream& os,
            const std::string& name,
            const Expr& def) {
            if (std::holds_alternative<Derivative>(def.value)) {
                const auto& d = std::get<Derivative>(def.value);
                os << name << " (from " << d.function_name;
                for (int i = 0; i < d.order; ++i) os << "'";
                os << ")";
            }
            else {
                os << name << " = " << ToString(def);
            }
        }

    } // namespace

    // ============================================================================
    // Capture / SetAuxiliary / Auxiliary
    // ============================================================================

    void PipelineTrace::Capture(Stage stage, const RawSystem& sys) {
        // Первый Capture(Parsed) фиксирует список исходных функций.
        // Повторный Capture(Parsed) список не перезатирает.
        if (stage == Stage::Parsed && !Has(Stage::Parsed)) {
            original_functions_ = sys.functions;
        }
        snapshots_[stage] = CloneSystem(sys);
    }

    void PipelineTrace::SetAuxiliary(Stage stage,
        std::map<std::string, ExprPtr> aux) {
        auxiliary_[stage] = std::move(aux);
    }

    const std::map<std::string, ExprPtr>&
        PipelineTrace::Auxiliary(Stage stage) const {
        // Статическая пустая карта — единый экземпляр для всех вызовов,
        // когда для стадии метаданные не установлены. Только чтение,
        // гонок нет.
        static const std::map<std::string, ExprPtr> kEmpty;
        auto it = auxiliary_.find(stage);
        return (it == auxiliary_.end()) ? kEmpty : it->second;
    }

    // ============================================================================
    // At / Has / Stages
    // ============================================================================

    const RawSystem& PipelineTrace::At(Stage stage) const {
        auto it = snapshots_.find(stage);
        if (it == snapshots_.end()) {
            throw std::out_of_range(
                "PipelineTrace::At: stage not captured: " + ToString(stage));
        }
        return it->second;
    }

    bool PipelineTrace::Has(Stage stage) const {
        return snapshots_.find(stage) != snapshots_.end();
    }

    std::vector<Stage> PipelineTrace::Stages() const {
        // std::map сортирует по ключу; значения enum-класса Stage идут
        // в порядке объявления — а он совпадает с порядком пайплайна.
        std::vector<Stage> result;
        result.reserve(snapshots_.size());
        for (const auto& kv : snapshots_) {
            result.push_back(kv.first);
        }
        return result;
    }

    // ============================================================================
    // View
    // ============================================================================

    RawSystem PipelineTrace::View(Stage stage) const {
        if (!Has(Stage::Parsed)) {
            throw std::logic_error(
                "PipelineTrace::View: Parsed not captured");
        }

        if (stage == Stage::Parsed ||
            stage == Stage::Validated ||
            stage == Stage::Normalized) {
            return CloneSystem(At(stage));
        }

        if (!Has(Stage::Normalized)) {
            throw std::logic_error(
                "PipelineTrace::View: Normalized not captured");
        }
        return CloneSystem(At(Stage::Normalized));
    }

    // ============================================================================
    // Format
    // ============================================================================

    std::string PipelineTrace::Format(Stage stage) const {
        if (!Has(Stage::Parsed)) {
            throw std::logic_error(
                "PipelineTrace::Format: Parsed not captured");
        }
        if (stage != Stage::Parsed &&
            stage != Stage::Validated &&
            stage != Stage::Normalized &&
            !Has(Stage::Normalized)) {
            throw std::logic_error(
                "PipelineTrace::Format: Normalized not captured");
        }

        std::ostringstream os;
        os << "[" << ToString(stage) << "]\n";

        auto aux_it = auxiliary_.find(stage);
        if (aux_it != auxiliary_.end() && !aux_it->second.empty()) {
            os << "# Source functions:";
            for (const auto& f : original_functions_) {
                os << " " << f;
            }
            os << "\n";

            os << "# Auxiliary:";
            bool first = true;
            for (const auto& kv : aux_it->second) {
                if (!first) os << ",";
                os << " ";
                AppendAuxDefinition(os, kv.first, *kv.second);
                first = false;
            }
            os << "\n";
        }

        os << ToString(At(stage));
        return os.str();
    }

} // namespace diffuri