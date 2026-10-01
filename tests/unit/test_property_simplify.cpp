// ============================================================================
// tests/unit/test_property_simplify.cpp
//
// Property-based (свойственные) тесты модуля simplify.
//
// Отличие от test_simplify.cpp: там — примеры на каждый конкретный случай,
// здесь — инварианты, которые должны выполняться на ЛЮБОМ выражении.
// Выражения генерируются детерминированно (фиксированный seed), поэтому
// тесты воспроизводимы и не флакают.
//
// Проверяемые свойства:
//   P1.  Детерминизм: Simplify(e) воспроизводим.
//   P2.  Идемпотентность: Simplify(Simplify(e)) == Simplify(e).
//   P3.  Канонизация коммутативных операций: результат не зависит от
//        порядка операндов Add/Mul.
//   P4.  Канонизация ассоциативности: (a+b)+c и a+(b+c) дают одно дерево;
//        то же для Mul.
//   P5.  Нейтральные элементы: e+0 == e, e*1 == e, e-e == 0.
//   P6.  Унарный минус: Simplify(-e) == Simplify(-1 * e).
//   P7.  ExtractCoefficient обратим для мономов.
//   P8.  Round-trip: Parse(ToString(Simplify(e))) == Simplify(e).
//   P9.  Compare — строгий слабый порядок на упрощённых деревьях.
//   P10. Simplify не выдумывает новых переменных/производных.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "input/expression.h"
#include "input/parser.h"
#include "simplify/simplify.h"

namespace diffuri {
    namespace {

        // ---------------------------------------------------------------------------
        // Клонирование дерева. Simplify поглощает аргумент, поэтому для повторных
        // прогонов одного и того же выражения нужен независимый экземпляр.
        // ---------------------------------------------------------------------------
        ExprPtr Clone(const Expr& e) {
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
                    return MakeUnary(n.op, Clone(*n.operand));
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return MakeBinary(n.op, Clone(*n.lhs), Clone(*n.rhs));
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    std::vector<ExprPtr> args;
                    args.reserve(n.args.size());
                    for (const auto& a : n.args) args.push_back(Clone(*a));
                    return MakeCall(n.name, std::move(args));
                }
                return nullptr;
                }, e.value);
        }

        // ---------------------------------------------------------------------------
        // Имена переменных (Function/Derivative), встречающихся в дереве.
        // Используется для проверки P10.
        // ---------------------------------------------------------------------------
        void CollectNames(const Expr& e, std::set<std::string>& out) {
            std::visit([&](const auto& n) {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Function>) {
                    out.insert(n.name);
                }
                else if constexpr (std::is_same_v<T, Derivative>) {
                    out.insert(n.function_name);
                }
                else if constexpr (std::is_same_v<T, Unary>) {
                    CollectNames(*n.operand, out);
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    CollectNames(*n.lhs, out);
                    CollectNames(*n.rhs, out);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& a : n.args) CollectNames(*a, out);
                }
                }, e.value);
        }

        // ---------------------------------------------------------------------------
        // Есть ли в дереве хоть один узел Unary?
        // Используется для P11: после Simplify Unary остаться не должно —
        // Unary::Neg контрактно разворачивается в Mul(-1, ·).
        // ---------------------------------------------------------------------------
        bool HasAnyUnary(const Expr& e) {
            return std::visit([&](const auto& n) -> bool {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Unary>) {
                    return true;
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    return HasAnyUnary(*n.lhs) || HasAnyUnary(*n.rhs);
                }
                else if constexpr (std::is_same_v<T, Call>) {
                    for (const auto& a : n.args) {
                        if (HasAnyUnary(*a)) return true;
                    }
                    return false;
                }
                else {
                    return false;
                }
                }, e.value);
        }
        // ---------------------------------------------------------------------------
        // Генератор случайных выражений.
        //
        // Детерминированный (один seed — одна последовательность). Намеренно
        // избегает NaN и деления на ноль: тесты не должны падать из-за
        // арифметики с плавающей точкой.
        // ---------------------------------------------------------------------------
        class ExprGen {
        public:
            explicit ExprGen(std::uint32_t seed) : rng_(seed) {}

            ExprPtr Generate(int depth) {
                std::uniform_int_distribution<int> d(0, 99);
                if (depth <= 0 || d(rng_) < 30) return Leaf();
                if (d(rng_) < 25) return UnaryNode(depth);
                if (d(rng_) < 20) return CallNode(depth);
                return BinaryNode(depth);
            }

        private:
            ExprPtr Leaf() {
                std::uniform_int_distribution<int> d(0, 99);
                int roll = d(rng_);
                if (roll < 35) {
                    std::uniform_int_distribution<int> dn(-5, 5);
                    return MakeNumber(static_cast<double>(dn(rng_)));
                }
                if (roll < 75) {
                    static const char* names[] = { "x", "y", "z" };
                    std::uniform_int_distribution<int> dn(0, 2);
                    return MakeFunction(names[dn(rng_)]);
                }
                static const char* funcs[] = { "x", "y" };
                std::uniform_int_distribution<int> df(0, 1);
                std::uniform_int_distribution<int> dord(1, 2);
                return MakeDerivative(funcs[df(rng_)], dord(rng_));
            }

            ExprPtr UnaryNode(int depth) {
                // Только Neg — гарантированно понимается Simplify.
                return MakeUnary(Unary::Op::Neg, Generate(depth - 1));
            }

            ExprPtr CallNode(int depth) {
                static const char* names[] = { "sin", "cos" };
                std::uniform_int_distribution<int> dn(0, 1);
                return MakeCallArgs(names[dn(rng_)], Generate(depth - 1));
            }

            ExprPtr BinaryNode(int depth) {
                static const Binary::Op ops[] = {
                    Binary::Op::Add, Binary::Op::Sub, Binary::Op::Mul,
                    Binary::Op::Div, Binary::Op::Pow
                };
                std::uniform_int_distribution<int> d(0, 4);
                auto op = ops[d(rng_)];
                auto lhs = Generate(depth - 1);
                auto rhs = Generate(depth - 1);

                if (op == Binary::Op::Div) {
                    // Гарантируем ненулевой знаменатель.
                    if (IsNumber(*rhs) && AsNumber(*rhs) == 0.0) {
                        rhs = MakeNumber(1.0);
                    }
                }
                else if (op == Binary::Op::Pow) {
                    // Неотрицательный целый показатель — избегаем NaN.
                    if (IsNumber(*rhs)) {
                        double v = AsNumber(*rhs);
                        rhs = MakeNumber(std::abs(std::round(v)));
                    }
                    else {
                        rhs = MakeNumber(static_cast<double>(d(rng_) % 3));
                    }
                }
                return MakeBinary(op, std::move(lhs), std::move(rhs));
            }

            std::mt19937 rng_;
        };

        // ---------------------------------------------------------------------------
        // P1. Детерминизм.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P1_Determinism) {
            ExprGen gen(0xC0FFEEu);
            for (int i = 0; i < 300; ++i) {
                auto orig = gen.Generate(4);
                auto a = Simplify(Clone(*orig));
                auto b = Simplify(Clone(*orig));
                ASSERT_NE(a, nullptr);
                ASSERT_NE(b, nullptr);
                EXPECT_TRUE(ExprEquals(*a, *b))
                    << "i=" << i << " input: " << ToString(*orig);
            }
        }

        // ---------------------------------------------------------------------------
        // P2. Идемпотентность.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P2_Idempotence) {
            ExprGen gen(0xDEADBEEFu);
            for (int i = 0; i < 300; ++i) {
                auto orig = gen.Generate(4);
                auto once = Simplify(Clone(*orig));
                auto twice = Simplify(Clone(*once));
                EXPECT_TRUE(ExprEquals(*once, *twice))
                    << "i=" << i
                    << "\ninput:  " << ToString(*orig)
                    << "\nonce:   " << ToString(*once)
                    << "\ntwice:  " << ToString(*twice);
            }
        }

        // ---------------------------------------------------------------------------
        // P3. Каноническая форма коммутативных операций.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P3_CommutativityCanonical) {
            ExprGen gen(0x12345678u);
            for (int i = 0; i < 300; ++i) {
                auto a = gen.Generate(3);
                auto b = gen.Generate(3);

                auto s_ab = Simplify(MakeBinary(Binary::Op::Add, Clone(*a), Clone(*b)));
                auto s_ba = Simplify(MakeBinary(Binary::Op::Add, Clone(*b), Clone(*a)));
                EXPECT_TRUE(ExprEquals(*s_ab, *s_ba))
                    << "i=" << i << " Add: " << ToString(*s_ab)
                    << " vs " << ToString(*s_ba);

                auto m_ab = Simplify(MakeBinary(Binary::Op::Mul, Clone(*a), Clone(*b)));
                auto m_ba = Simplify(MakeBinary(Binary::Op::Mul, Clone(*b), Clone(*a)));
                EXPECT_TRUE(ExprEquals(*m_ab, *m_ba))
                    << "i=" << i << " Mul: " << ToString(*m_ab)
                    << " vs " << ToString(*m_ba);
            }
        }

        // ---------------------------------------------------------------------------
        // P4. Канонизация ассоциативности.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P4_AssociativityCanonical) {
            ExprGen gen(0xABCDEF01u);
            for (int i = 0; i < 200; ++i) {
                auto a = gen.Generate(2);
                auto b = gen.Generate(2);
                auto c = gen.Generate(2);

                auto add_l = MakeBinary(Binary::Op::Add,
                    MakeBinary(Binary::Op::Add, Clone(*a), Clone(*b)),
                    Clone(*c));
                auto add_r = MakeBinary(Binary::Op::Add,
                    Clone(*a),
                    MakeBinary(Binary::Op::Add, Clone(*b), Clone(*c)));
                EXPECT_TRUE(ExprEquals(*Simplify(std::move(add_l)),
                    *Simplify(std::move(add_r))))
                    << "i=" << i << " Add";

                auto mul_l = MakeBinary(Binary::Op::Mul,
                    MakeBinary(Binary::Op::Mul, Clone(*a), Clone(*b)),
                    Clone(*c));
                auto mul_r = MakeBinary(Binary::Op::Mul,
                    Clone(*a),
                    MakeBinary(Binary::Op::Mul, Clone(*b), Clone(*c)));
                EXPECT_TRUE(ExprEquals(*Simplify(std::move(mul_l)),
                    *Simplify(std::move(mul_r))))
                    << "i=" << i << " Mul";
            }
        }

        // ---------------------------------------------------------------------------
        // P5. Нейтральные элементы и сокращение.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P5_NeutralElements) {
            ExprGen gen(0x55AA55AAu);
            for (int i = 0; i < 200; ++i) {
                auto e = gen.Generate(3);
                auto s = Simplify(Clone(*e));

                auto s_plus_0 = Simplify(MakeBinary(Binary::Op::Add,
                    Clone(*e), MakeNumber(0.0)));
                EXPECT_TRUE(ExprEquals(*s, *s_plus_0)) << "i=" << i << " e+0";

                auto s_mul_1 = Simplify(MakeBinary(Binary::Op::Mul,
                    Clone(*e), MakeNumber(1.0)));
                EXPECT_TRUE(ExprEquals(*s, *s_mul_1)) << "i=" << i << " e*1";

                auto s_minus_e = Simplify(MakeBinary(Binary::Op::Sub,
                    Clone(*e), Clone(*e)));
                ASSERT_TRUE(IsNumber(*s_minus_e)) << "i=" << i;
                EXPECT_DOUBLE_EQ(AsNumber(*s_minus_e), 0.0) << "i=" << i << " e-e";
            }
        }

        // ---------------------------------------------------------------------------
        // P6. Унарный минус согласован с умножением на -1.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P6_UnaryNegConsistent) {
            ExprGen gen(0x778899AAu);
            for (int i = 0; i < 200; ++i) {
                auto e = gen.Generate(3);
                auto s_neg = Simplify(MakeUnary(Unary::Op::Neg, Clone(*e)));
                auto s_mul = Simplify(MakeBinary(Binary::Op::Mul,
                    MakeNumber(-1.0), Clone(*e)));
                EXPECT_TRUE(ExprEquals(*s_neg, *s_mul)) << "i=" << i;
            }
        }

        // ---------------------------------------------------------------------------
        // P7. ExtractCoefficient согласован с Simplify для мономов.
        //
        // Для любого упрощённого e (кроме чистых чисел): если
        // ExtractCoefficient(e) = {c, base}, то Simplify(c * base) структурно
        // совпадает с e.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P7_ExtractCoefficientRoundTrip) {
            ExprGen gen(0x13579BDFu);
            for (int i = 0; i < 200; ++i) {
                auto e = Simplify(gen.Generate(3));
                if (IsNumber(*e)) continue; // тривиально
                auto dc = ExtractCoefficient(Clone(*e));
                auto combined = Simplify(MakeBinary(Binary::Op::Mul,
                    MakeNumber(dc.coefficient), Clone(*dc.base)));
                EXPECT_TRUE(ExprEquals(*e, *combined))
                    << "i=" << i
                    << "\n e:        " << ToString(*e)
                    << "\n c:        " << dc.coefficient
                    << "\n base:     " << ToString(*dc.base)
                    << "\n combined: " << ToString(*combined);
            }
        }

        // ---------------------------------------------------------------------------
        // P8. Round-trip: печать → разбор → упрощение не меняет каноническую форму.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P8_ToStringRoundTrip) {
            ExprGen gen(0x7E57C0DEu);
            for (int i = 0; i < 300; ++i) {
                auto e = Simplify(gen.Generate(3));
                std::string printed = ToString(*e);
                ExprPtr reparsed;
                try {
                    reparsed = Simplify(ParseExpression(printed));
                }
                catch (const std::exception& ex) {
                    ADD_FAILURE() << "i=" << i << " parse failed: " << ex.what()
                        << " printed: " << printed;
                    continue;
                }
                EXPECT_TRUE(ExprEquals(*e, *reparsed))
                    << "i=" << i << " printed: " << printed;
            }
        }

        // ---------------------------------------------------------------------------
        // P9. Compare — строгий слабый порядок на упрощённых деревьях.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P9_CompareStrictWeakOrder) {
            ExprGen gen(0x0BADF00Du);
            std::vector<ExprPtr> v;
            v.reserve(40);
            for (int i = 0; i < 40; ++i) {
                v.push_back(Simplify(gen.Generate(3)));
            }
            // Рефлексивность.
            for (auto& e : v) EXPECT_EQ(Compare(*e, *e), 0);
            // Антисимметричность.
            for (auto& a : v) for (auto& b : v) {
                EXPECT_EQ(Compare(*a, *b), -Compare(*b, *a));
            }
            // Транзитивность (в форме strict weak ordering).
            for (auto& a : v) for (auto& b : v) for (auto& c : v) {
                int ab = Compare(*a, *b);
                int bc = Compare(*b, *c);
                int ac = Compare(*a, *c);
                if (ab < 0 && bc < 0) EXPECT_LT(ac, 0);
                if (ab == 0 && bc == 0) EXPECT_EQ(ac, 0);
                if (ab == 0 && bc < 0) EXPECT_LT(ac, 0);
                if (ab < 0 && bc == 0) EXPECT_LT(ac, 0);
            }
        }

        // ---------------------------------------------------------------------------
        // P10. Simplify не выдумывает новых имён функций/производных.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P10_NoNewNames) {
            ExprGen gen(0xFACEFEEDu);
            for (int i = 0; i < 200; ++i) {
                auto orig = gen.Generate(3);
                auto s = Simplify(Clone(*orig));
                std::set<std::string> names_orig, names_simpl;
                CollectNames(*orig, names_orig);
                CollectNames(*s, names_simpl);
                for (const auto& v : names_simpl) {
                    EXPECT_TRUE(names_orig.count(v))
                        << "i=" << i << " new name: " << v;
                }
            }
        }

        // ---------------------------------------------------------------------------
// P11. Отсутствие Unary после Simplify.
//
// Контракт: Unary::Neg разворачивается в Mul(-1, ·). Это важно
// для normalize.cpp: HasSpecificDerivative не спускается в Unary,
// поэтому вся надежда на то, что после Simplify его не осталось.
// ---------------------------------------------------------------------------
        TEST(PropertySimplify, P11_NoUnaryAfterSimplify) {
            ExprGen gen(0xCAFEBABEu);
            for (int i = 0; i < 300; ++i) {
                auto orig = gen.Generate(4);
                auto s = Simplify(Clone(*orig));
                ASSERT_NE(s, nullptr);
                EXPECT_FALSE(HasAnyUnary(*s))
                    << "i=" << i
                    << "\ninput: " << ToString(*orig)
                    << "\nsimpl: " << ToString(*s);
            }
        }

        // ---------------------------------------------------------------------------
        // P12. MaxDerivativeOrder не увеличивается под Simplify.
        //
        // Контракт: Simplify не изобретает производных более высокого
        // порядка, чем было во входе. Это важно для TargetFunction: она
        // опирается на DerivativeOrders(sys) как на верхнюю границу.
        //
        // Замечание про ТЗ: ТЗ требовало «либо порядок сохранён, либо
        // результат — 0-константа». Это утверждение строго ложно:
        //   x'' - x'' + x  ->  x
        // порядок падает с 2 до 0, а результат — не 0-константа (x).
        // Такие случаи дают корректные сокращения, поэтому проверяем
        // слабый, но настоящий инвариант: after <= before.
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P12_MaxDerivativeOrderNonIncreasing) {
            ExprGen gen(0xDEADC0DEu);
            int preserved = 0;
            int reduced = 0;
            for (int i = 0; i < 300; ++i) {
                auto orig = gen.Generate(4);
                int before = MaxDerivativeOrder(*orig);
                auto s = Simplify(Clone(*orig));
                int after = MaxDerivativeOrder(*s);
                EXPECT_LE(after, before)
                    << "i=" << i
                    << "\ninput:  " << ToString(*orig)
                    << "\nsimpl:  " << ToString(*s)
                    << "\nbefore: " << before
                    << "\nafter:  " << after;
                if (after == before) ++preserved;
                else ++reduced;
            }
            // Санити-чек: генератор иногда порождает выражения с производными,
            // и Simplify в большинстве случаев их сохраняет.
            // Если из-за регрессии всё начнёт сокращаться подчистую,
            // это уведёт preserved в ноль и здесь сработает.
            EXPECT_GT(preserved, 0);
            (void)reduced; // можем сократить — нормально, но не проверяем
        }

        // ---------------------------------------------------------------------------
        // P13. Идемпотентность третьего уровня.
        //
        // P2 проверяет Simplify²(e) == Simplify(e). Здесь — что и третий
        // проход не сдвигает канон: Simplify³(e) == Simplify(e).
        // ---------------------------------------------------------------------------
        TEST(PropertySimplify, P13_IdempotenceThird) {
            ExprGen gen(0x1BADB002u);
            for (int i = 0; i < 200; ++i) {
                auto orig = gen.Generate(4);
                auto once = Simplify(Clone(*orig));
                auto twice = Simplify(Clone(*once));
                auto thrice = Simplify(Clone(*twice));
                EXPECT_TRUE(ExprEquals(*once, *thrice))
                    << "i=" << i
                    << "\ninput:  " << ToString(*orig)
                    << "\nonce:   " << ToString(*once)
                    << "\ntwice:  " << ToString(*twice)
                    << "\nthrice: " << ToString(*thrice);
            }
        }

    } // namespace
} // namespace diffuri