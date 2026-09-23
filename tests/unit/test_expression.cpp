// ============================================================================
// tests/unit/test_expression.cpp
//
// Юнит-тесты модуля expression: фабрики узлов и запросы по дереву.
//
// Про MakeCall и MakeCallArgs:
//   MakeCall("имя", std::vector<ExprPtr>)   — для динамического списка,
//                                             когда аргументы собираются в цикле;
//   MakeCallArgs("имя", arg1, arg2, ...)    — для фиксированного списка,
//                                             известного на этапе компиляции.
//
// Вариант MakeCall("имя", { arg1, arg2 }) НЕ работает: braced-init-list
// требует копирования, а unique_ptr копировать нельзя.
// ============================================================================
#include <gtest/gtest.h>

#include <variant>

#include "input/expression.h"

using namespace diffuri;

// ----------------------------------------------------------------------------
// ФАБРИКИ
// ----------------------------------------------------------------------------

TEST(Expression, MakeNumber) {
    auto e = MakeNumber(3.14);
    ASSERT_NE(e, nullptr);
    ASSERT_TRUE(std::holds_alternative<Number>(e->value));
    EXPECT_DOUBLE_EQ(std::get<Number>(e->value).value, 3.14);
}

TEST(Expression, MakeFunction) {
    auto e = MakeFunction("x");
    ASSERT_NE(e, nullptr);
    ASSERT_TRUE(std::holds_alternative<Function>(e->value));
    EXPECT_EQ(std::get<Function>(e->value).name, "x");
}

TEST(Expression, MakeConstant) {
    auto e = MakeConstant("pi", 3.14159265358979);
    ASSERT_NE(e, nullptr);
    ASSERT_TRUE(std::holds_alternative<Constant>(e->value));
    const auto& c = std::get<Constant>(e->value);
    EXPECT_EQ(c.name, "pi");
    EXPECT_DOUBLE_EQ(c.value, 3.14159265358979);
}

TEST(Expression, MakeDerivativeFirstOrder) {
    auto e = MakeDerivative("x", 1);
    ASSERT_NE(e, nullptr);
    ASSERT_TRUE(std::holds_alternative<Derivative>(e->value));
    const auto& d = std::get<Derivative>(e->value);
    EXPECT_EQ(d.function_name, "x");
    EXPECT_EQ(d.order, 1);
}

TEST(Expression, MakeDerivativeThirdOrder) {
    auto e = MakeDerivative("y", 3);
    const auto& d = std::get<Derivative>(e->value);
    EXPECT_EQ(d.function_name, "y");
    EXPECT_EQ(d.order, 3);
}

TEST(Expression, MakeBinaryAdd) {
    auto e = MakeBinary(Binary::Op::Add, MakeNumber(1.0), MakeNumber(2.0));
    ASSERT_TRUE(std::holds_alternative<Binary>(e->value));
    const auto& b = std::get<Binary>(e->value);
    EXPECT_EQ(b.op, Binary::Op::Add);
    ASSERT_NE(b.lhs, nullptr);
    ASSERT_NE(b.rhs, nullptr);
}

TEST(Expression, MakeCallNoArgs) {
    auto e = MakeCall("f", {});
    ASSERT_TRUE(std::holds_alternative<Call>(e->value));
    const auto& c = std::get<Call>(e->value);
    EXPECT_EQ(c.name, "f");
    EXPECT_TRUE(c.args.empty());
}

TEST(Expression, MakeCallTwoArgs) {
    auto e = MakeCallArgs("pow", MakeFunction("x"), MakeNumber(2.0));
    const auto& c = std::get<Call>(e->value);
    ASSERT_EQ(c.args.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Function>(c.args[0]->value));
    EXPECT_TRUE(std::holds_alternative<Number>(c.args[1]->value));
}

// ----------------------------------------------------------------------------
// IsLeaf
// ----------------------------------------------------------------------------

TEST(Expression, IsLeafTrueForLeaves) {
    EXPECT_TRUE(IsLeaf(*MakeNumber(1.0)));
    EXPECT_TRUE(IsLeaf(*MakeFunction("x")));
    EXPECT_TRUE(IsLeaf(*MakeConstant("pi", 3.14)));
    EXPECT_TRUE(IsLeaf(*MakeDerivative("x", 1)));
}

TEST(Expression, IsLeafFalseForOperators) {
    EXPECT_FALSE(IsLeaf(*MakeBinary(Binary::Op::Add,
        MakeNumber(1.0), MakeNumber(2.0))));
    EXPECT_FALSE(IsLeaf(*MakeCallArgs("sin", MakeFunction("x"))));
}

// ----------------------------------------------------------------------------
// HasDerivative
// ----------------------------------------------------------------------------

TEST(Expression, HasDerivativeFalseOnPureFunction) {
    auto e = MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("y"));
    EXPECT_FALSE(HasDerivative(*e));
}

TEST(Expression, HasDerivativeTrueOnDerivative) {
    EXPECT_TRUE(HasDerivative(*MakeDerivative("x", 1)));
}

TEST(Expression, HasDerivativeNested) {
    // sin(x) + y''
    auto e = MakeBinary(Binary::Op::Add,
        MakeCallArgs("sin", MakeFunction("x")),
        MakeDerivative("y", 2));
    EXPECT_TRUE(HasDerivative(*e));
}

// ----------------------------------------------------------------------------
// MaxDerivativeOrder
// ----------------------------------------------------------------------------

TEST(Expression, MaxDerivativeOrderZeroWithoutDerivatives) {
    EXPECT_EQ(MaxDerivativeOrder(*MakeFunction("x")), 0);
    EXPECT_EQ(MaxDerivativeOrder(*MakeNumber(1.0)), 0);
}

TEST(Expression, MaxDerivativeOrderSingleDerivative) {
    EXPECT_EQ(MaxDerivativeOrder(*MakeDerivative("x", 2)), 2);
}

TEST(Expression, MaxDerivativeOrderTakesMax) {
    // x' + y''' + z''
    auto e = MakeBinary(Binary::Op::Add,
        MakeDerivative("x", 1),
        MakeBinary(Binary::Op::Add,
            MakeDerivative("y", 3),
            MakeDerivative("z", 2)));
    EXPECT_EQ(MaxDerivativeOrder(*e), 3);
}

// ----------------------------------------------------------------------------
// CollectFunctionNames
// ----------------------------------------------------------------------------

TEST(Expression, CollectFunctionNamesSimple) {
    auto e = MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("y"));
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
}

TEST(Expression, CollectFunctionNamesDeduplicates) {
    // x + x + y
    auto e = MakeBinary(Binary::Op::Add,
        MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("x")),
        MakeFunction("y"));
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
}

TEST(Expression, CollectFunctionNamesExcludesDerivativeAndConstant) {
    // x' + pi + y
    auto e = MakeBinary(Binary::Op::Add,
        MakeDerivative("x", 1),
        MakeBinary(Binary::Op::Add,
            MakeConstant("pi", 3.14),
            MakeFunction("y")));
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "y");
}

// ----------------------------------------------------------------------------
// CollectDifferentiatedNames
// ----------------------------------------------------------------------------

TEST(Expression, CollectDifferentiatedNamesSimple) {
    // x'' + y'
    auto e = MakeBinary(Binary::Op::Add,
        MakeDerivative("x", 2),
        MakeDerivative("y", 1));
    auto names = CollectDifferentiatedNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
}

TEST(Expression, CollectDifferentiatedNamesEmptyOnPlainFunction) {
    auto e = MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("y"));
    EXPECT_TRUE(CollectDifferentiatedNames(*e).empty());
}

TEST(Expression, CollectDifferentiatedNamesDeduplicates) {
    // x' + x''
    auto e = MakeBinary(Binary::Op::Add,
        MakeDerivative("x", 1),
        MakeDerivative("x", 2));
    auto names = CollectDifferentiatedNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "x");
}

// ----------------------------------------------------------------------------
// CollectConstantNames
// ----------------------------------------------------------------------------

TEST(Expression, CollectConstantNamesSimple) {
    // pi + e
    auto e = MakeBinary(Binary::Op::Add,
        MakeConstant("pi", 3.14),
        MakeConstant("e", 2.71));
    auto names = CollectConstantNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "pi");
    EXPECT_EQ(names[1], "e");
}

TEST(Expression, CollectConstantNamesEmptyOnPlainExpression) {
    auto e = MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeNumber(1.0));
    EXPECT_TRUE(CollectConstantNames(*e).empty());
}

// ----------------------------------------------------------------------------
// ToString
// ----------------------------------------------------------------------------

TEST(Expression, ToStringNumber) {
    EXPECT_EQ(ToString(*MakeNumber(3.5)), "3.5");
}

TEST(Expression, ToStringFunction) {
    EXPECT_EQ(ToString(*MakeFunction("x")), "x");
}

TEST(Expression, ToStringDerivativeFirst) {
    EXPECT_EQ(ToString(*MakeDerivative("x", 1)), "x'");
}

TEST(Expression, ToStringDerivativeSecond) {
    EXPECT_EQ(ToString(*MakeDerivative("y", 2)), "y''");
}

TEST(Expression, ToStringBinaryAdd) {
    auto e = MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("y"));
    EXPECT_EQ(ToString(*e), "(x + y)");
}

TEST(Expression, ToStringBinaryMulNested) {
    // x + y * 2
    auto e = MakeBinary(Binary::Op::Add,
        MakeFunction("x"),
        MakeBinary(Binary::Op::Mul, MakeFunction("y"), MakeNumber(2.0)));
    EXPECT_EQ(ToString(*e), "(x + (y * 2))");
}

TEST(Expression, ToStringCall) {
    auto e = MakeCallArgs("sin", MakeFunction("x"));
    EXPECT_EQ(ToString(*e), "sin(x)");
}

TEST(Expression, ToStringCallTwoArgs) {
    auto e = MakeCallArgs("pow", MakeFunction("x"), MakeNumber(2.0));
    EXPECT_EQ(ToString(*e), "pow(x, 2)");
}

TEST(Expression, ToStringConstant) {
    EXPECT_EQ(ToString(*MakeConstant("pi", 3.14)), "pi");
}


// ============================================================================
// ДОБАВЛЕННЫЕ ТЕСТЫ
// ============================================================================

// ----------------------------------------------------------------------------
// A. Фабрики
// ----------------------------------------------------------------------------

TEST(Expression, MakeNumberZeroNegativeAndExtremes) {
    auto zero = MakeNumber(0.0);
    ASSERT_NE(zero, nullptr);
    ASSERT_TRUE(std::holds_alternative<Number>(zero->value));
    EXPECT_DOUBLE_EQ(std::get<Number>(zero->value).value, 0.0);

    auto neg = MakeNumber(-42.5);
    ASSERT_TRUE(std::holds_alternative<Number>(neg->value));
    EXPECT_DOUBLE_EQ(std::get<Number>(neg->value).value, -42.5);

    auto big = MakeNumber(1e308);
    ASSERT_TRUE(std::holds_alternative<Number>(big->value));
    EXPECT_DOUBLE_EQ(std::get<Number>(big->value).value, 1e308);

    auto small = MakeNumber(1e-308);
    ASSERT_TRUE(std::holds_alternative<Number>(small->value));
    EXPECT_DOUBLE_EQ(std::get<Number>(small->value).value, 1e-308);
}

TEST(Expression, MakeFunctionStoresEdgeNames) {
    auto empty = MakeFunction("");
    ASSERT_TRUE(std::holds_alternative<Function>(empty->value));
    EXPECT_EQ(std::get<Function>(empty->value).name, "");

    auto with_digits = MakeFunction("x1");
    ASSERT_TRUE(std::holds_alternative<Function>(with_digits->value));
    EXPECT_EQ(std::get<Function>(with_digits->value).name, "x1");

    const std::string long_name(100, 'a');
    auto long_expr = MakeFunction(long_name);
    ASSERT_TRUE(std::holds_alternative<Function>(long_expr->value));
    EXPECT_EQ(std::get<Function>(long_expr->value).name, long_name);
}

TEST(Expression, MakeConstantStoresNegativeZeroAndPrecise) {
    auto neg = MakeConstant("neg", -3.14);
    ASSERT_TRUE(std::holds_alternative<Constant>(neg->value));
    EXPECT_EQ(std::get<Constant>(neg->value).name, "neg");
    EXPECT_DOUBLE_EQ(std::get<Constant>(neg->value).value, -3.14);

    auto zero = MakeConstant("zero", 0.0);
    ASSERT_TRUE(std::holds_alternative<Constant>(zero->value));
    EXPECT_DOUBLE_EQ(std::get<Constant>(zero->value).value, 0.0);

    auto precise = MakeConstant("pi", 3.14159265358979323846);
    ASSERT_TRUE(std::holds_alternative<Constant>(precise->value));
    EXPECT_DOUBLE_EQ(std::get<Constant>(precise->value).value,
        3.14159265358979323846);
}

TEST(Expression, MakeDerivativeClampsNonPositiveOrder) {
    auto zero = MakeDerivative("x", 0);
    ASSERT_TRUE(std::holds_alternative<Derivative>(zero->value));
    EXPECT_EQ(std::get<Derivative>(zero->value).order, 1);

    auto neg = MakeDerivative("y", -5);
    ASSERT_TRUE(std::holds_alternative<Derivative>(neg->value));
    EXPECT_EQ(std::get<Derivative>(neg->value).order, 1);
}

TEST(Expression, MakeDerivativeHighOrder) {
    auto e = MakeDerivative("x", 10);
    ASSERT_TRUE(std::holds_alternative<Derivative>(e->value));
    EXPECT_EQ(std::get<Derivative>(e->value).order, 10);
    EXPECT_EQ(ToString(*e), "x" + std::string(10, '\''));
}

TEST(Expression, MakeBinaryAllOpsPreserveOperands) {
    auto add = MakeBinary(Binary::Op::Add, MakeFunction("lhs"), MakeFunction("rhs"));
    ASSERT_TRUE(std::holds_alternative<Binary>(add->value));
    EXPECT_EQ(std::get<Binary>(add->value).op, Binary::Op::Add);
    EXPECT_EQ(ToString(*add), "(lhs + rhs)");

    auto sub = MakeBinary(Binary::Op::Sub, MakeFunction("lhs"), MakeFunction("rhs"));
    ASSERT_TRUE(std::holds_alternative<Binary>(sub->value));
    EXPECT_EQ(std::get<Binary>(sub->value).op, Binary::Op::Sub);
    EXPECT_EQ(ToString(*sub), "(lhs - rhs)");

    auto mul = MakeBinary(Binary::Op::Mul, MakeFunction("lhs"), MakeFunction("rhs"));
    ASSERT_TRUE(std::holds_alternative<Binary>(mul->value));
    EXPECT_EQ(std::get<Binary>(mul->value).op, Binary::Op::Mul);
    EXPECT_EQ(ToString(*mul), "(lhs * rhs)");

    auto div = MakeBinary(Binary::Op::Div, MakeFunction("lhs"), MakeFunction("rhs"));
    ASSERT_TRUE(std::holds_alternative<Binary>(div->value));
    EXPECT_EQ(std::get<Binary>(div->value).op, Binary::Op::Div);
    EXPECT_EQ(ToString(*div), "(lhs / rhs)");

    auto pow = MakeBinary(Binary::Op::Pow, MakeFunction("lhs"), MakeFunction("rhs"));
    ASSERT_TRUE(std::holds_alternative<Binary>(pow->value));
    EXPECT_EQ(std::get<Binary>(pow->value).op, Binary::Op::Pow);
    EXPECT_EQ(ToString(*pow), "(lhs ^ rhs)");
}

TEST(Expression, MakeCallArgsZero) {
    auto e = MakeCallArgs("f");
    ASSERT_TRUE(std::holds_alternative<Call>(e->value));
    EXPECT_TRUE(std::get<Call>(e->value).args.empty());
}

TEST(Expression, MakeCallArgsOne) {
    auto e = MakeCallArgs("f", MakeFunction("x"));
    ASSERT_TRUE(std::holds_alternative<Call>(e->value));
    const auto& c = std::get<Call>(e->value);
    ASSERT_EQ(c.args.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Function>(c.args[0]->value));
}

TEST(Expression, MakeCallArgsFive) {
    auto e = MakeCallArgs("f",
        MakeNumber(1.0),
        MakeNumber(2.0),
        MakeNumber(3.0),
        MakeNumber(4.0),
        MakeNumber(5.0));
    ASSERT_TRUE(std::holds_alternative<Call>(e->value));
    const auto& c = std::get<Call>(e->value);
    ASSERT_EQ(c.args.size(), 5u);
    for (std::size_t i = 0; i < c.args.size(); ++i) {
        EXPECT_TRUE(std::holds_alternative<Number>(c.args[i]->value));
    }
}

// ----------------------------------------------------------------------------
// C. HasDerivative: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, HasDerivativeDeepLeft) {
    auto e = MakeBinary(Binary::Op::Add,
        MakeBinary(Binary::Op::Mul, MakeNumber(1.0), MakeDerivative("x", 1)),
        MakeFunction("y"));
    EXPECT_TRUE(HasDerivative(*e));
}

TEST(Expression, HasDerivativeDeepRight) {
    auto e = MakeBinary(Binary::Op::Add,
        MakeFunction("x"),
        MakeBinary(Binary::Op::Mul, MakeNumber(1.0), MakeDerivative("y", 2)));
    EXPECT_TRUE(HasDerivative(*e));
}

TEST(Expression, HasDerivativeInsideCall) {
    auto e = MakeCallArgs("sin",
        MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeDerivative("y", 1)));
    EXPECT_TRUE(HasDerivative(*e));
}

TEST(Expression, HasDerivativeMultiple) {
    auto e = MakeBinary(Binary::Op::Add,
        MakeDerivative("x", 1),
        MakeDerivative("y", 2));
    EXPECT_TRUE(HasDerivative(*e));
}

TEST(Expression, HasDerivativeNoneInNestedCall) {
    auto e = MakeCallArgs("f",
        MakeCallArgs("g", MakeFunction("x")),
        MakeNumber(1.0));
    EXPECT_FALSE(HasDerivative(*e));
}

// ----------------------------------------------------------------------------
// D. MaxDerivativeOrder: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, MaxDerivativeOrderFive) {
    EXPECT_EQ(MaxDerivativeOrder(*MakeDerivative("x", 5)), 5);
}

TEST(Expression, MaxDerivativeOrderInsideCall) {
    auto e = MakeCallArgs("f",
        MakeDerivative("x", 2),
        MakeDerivative("y", 4));
    EXPECT_EQ(MaxDerivativeOrder(*e), 4);
}

TEST(Expression, MaxDerivativeOrderNoArgsCall) {
    auto e = MakeCallArgs("f");
    EXPECT_EQ(MaxDerivativeOrder(*e), 0);
}

// ----------------------------------------------------------------------------
// E. CollectFunctionNames: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, CollectFunctionNamesEmptyOnNumber) {
    auto e = MakeNumber(1.0);
    EXPECT_TRUE(CollectFunctionNames(*e).empty());
}

TEST(Expression, CollectFunctionNamesSingle) {
    auto e = MakeFunction("x");
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "x");
}

TEST(Expression, CollectFunctionNamesInsideCall) {
    auto e = MakeCallArgs("f", MakeFunction("x"), MakeFunction("y"));
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
}

TEST(Expression, CollectFunctionNamesExcludesDerivativeOnly) {
    auto e = MakeDerivative("x", 1);
    EXPECT_TRUE(CollectFunctionNames(*e).empty());
}

TEST(Expression, CollectFunctionNamesExcludesConstantOnly) {
    auto e = MakeConstant("pi", 3.14);
    EXPECT_TRUE(CollectFunctionNames(*e).empty());
}

TEST(Expression, CollectFunctionNamesDeduplicatesInCall) {
    auto e = MakeCallArgs("f", MakeFunction("x"), MakeFunction("x"));
    auto names = CollectFunctionNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "x");
}

// ----------------------------------------------------------------------------
// F. CollectDifferentiatedNames: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, CollectDifferentiatedNamesEmptyOnNumber) {
    auto e = MakeNumber(1.0);
    EXPECT_TRUE(CollectDifferentiatedNames(*e).empty());
}

TEST(Expression, CollectDifferentiatedNamesInsideCall) {
    auto e = MakeCallArgs("f", MakeDerivative("x", 1));
    auto names = CollectDifferentiatedNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "x");
}

TEST(Expression, CollectDifferentiatedNamesMultiple) {
    auto e = MakeCallArgs("f",
        MakeDerivative("x", 1),
        MakeDerivative("y", 2));
    auto names = CollectDifferentiatedNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
}

// ----------------------------------------------------------------------------
// G. CollectConstantNames: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, CollectConstantNamesEmptyOnNumber) {
    auto e = MakeNumber(1.0);
    EXPECT_TRUE(CollectConstantNames(*e).empty());
}

TEST(Expression, CollectConstantNamesSingle) {
    auto e = MakeConstant("pi", 3.14);
    auto names = CollectConstantNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "pi");
}

TEST(Expression, CollectConstantNamesDeduplicates) {
    auto e = MakeBinary(Binary::Op::Add,
        MakeConstant("pi", 3.14),
        MakeConstant("pi", 3.14));
    auto names = CollectConstantNames(*e);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "pi");
}

TEST(Expression, CollectConstantNamesInsideCall) {
    auto e = MakeCallArgs("f",
        MakeConstant("pi", 3.14),
        MakeConstant("e", 2.71));
    auto names = CollectConstantNames(*e);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "pi");
    EXPECT_EQ(names[1], "e");
}

// ----------------------------------------------------------------------------
// H. ToString: дополнительные случаи
// ----------------------------------------------------------------------------

TEST(Expression, ToStringBinaryNestedLeft) {
    auto e = MakeBinary(Binary::Op::Mul,
        MakeBinary(Binary::Op::Add, MakeFunction("x"), MakeFunction("y")),
        MakeNumber(2.0));
    EXPECT_EQ(ToString(*e), "((x + y) * 2)");
}

TEST(Expression, ToStringDerivativeThird) {
    EXPECT_EQ(ToString(*MakeDerivative("y", 3)), "y'''");
}

TEST(Expression, ToStringDerivativeHighOrder) {
    EXPECT_EQ(ToString(*MakeDerivative("y", 10)), "y" + std::string(10, '\''));
}

TEST(Expression, ToStringCallNoArgs) {
    auto e = MakeCall("f", {});
    EXPECT_EQ(ToString(*e), "f()");
}

TEST(Expression, ToStringCallThreeArgs) {
    auto e = MakeCallArgs("f",
        MakeFunction("x"),
        MakeFunction("y"),
        MakeNumber(2.0));
    EXPECT_EQ(ToString(*e), "f(x, y, 2)");
}

TEST(Expression, ToStringNumberInteger) {
    EXPECT_EQ(ToString(*MakeNumber(2.0)), "2");
}

TEST(Expression, ToStringNumberSmall) {
    EXPECT_EQ(ToString(*MakeNumber(1e-3)), "0.001");
}

TEST(Expression, ToStringNumberLarge) {
    EXPECT_EQ(ToString(*MakeNumber(2.5e+2)), "250");
}

TEST(Expression, ToStringNumberNegative) {
    EXPECT_EQ(ToString(*MakeNumber(-3.5)), "-3.5");
}

TEST(Expression, ToStringNumberVeryLarge) {
    EXPECT_EQ(ToString(*MakeNumber(1e308)), "1e+308");
}

TEST(Expression, ToStringMixed) {
    auto e = MakeBinary(Binary::Op::Add,
        MakeCallArgs("sin", MakeFunction("x")),
        MakeBinary(Binary::Op::Mul, MakeFunction("y"), MakeNumber(2.0)));
    EXPECT_EQ(ToString(*e), "(sin(x) + (y * 2))");
}

// ----------------------------------------------------------------------------
// I. Граничные случаи
// ----------------------------------------------------------------------------

TEST(Expression, DeepTreeQueriesDoNotCrash) {
    ExprPtr e = MakeNumber(0.0);
    for (int i = 0; i < 20; ++i) {
        e = MakeBinary(Binary::Op::Add,
            std::move(e),
            MakeNumber(static_cast<double>(i)));
    }

    EXPECT_FALSE(IsLeaf(*e));
    EXPECT_FALSE(HasDerivative(*e));
    EXPECT_EQ(MaxDerivativeOrder(*e), 0);
    EXPECT_TRUE(CollectFunctionNames(*e).empty());
    EXPECT_TRUE(CollectDifferentiatedNames(*e).empty());
    EXPECT_TRUE(CollectConstantNames(*e).empty());
    EXPECT_FALSE(ToString(*e).empty());
}

TEST(Expression, CallWithManyArgs) {
    std::vector<ExprPtr> args;
    for (int i = 0; i < 100; ++i) {
        args.push_back(MakeNumber(static_cast<double>(i)));
    }

    auto e = MakeCall("f", std::move(args));
    ASSERT_TRUE(std::holds_alternative<Call>(e->value));
    EXPECT_EQ(std::get<Call>(e->value).args.size(), 100u);
    EXPECT_FALSE(IsLeaf(*e));
    EXPECT_EQ(MaxDerivativeOrder(*e), 0);
}

// ----------------------------------------------------------------------------
// J. Инварианты
// ----------------------------------------------------------------------------

TEST(Expression, MakeFunctionEmptyNameIsStored) {
    auto e = MakeFunction("");
    ASSERT_TRUE(std::holds_alternative<Function>(e->value));
    EXPECT_EQ(std::get<Function>(e->value).name, "");
    EXPECT_EQ(ToString(*e), "");
}