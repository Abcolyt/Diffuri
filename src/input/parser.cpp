// ============================================================================
// src/input/parser.cpp
//
// Реализация модуля parser: лексер + рекурсивный спуск + разбор
// уравнения и начального условия.
//
// Структура файла:
//   1. Вспомогательные утилиты (пробелы, комментарии, числа, константы,
//      вычисление константных выражений).
//   2. Токены и лексер.
//   3. Парсер (рекурсивный спуск).
//   4. Реализация ParseError.
//   5. Публичные функции: ParseExpression, ParseEquation,
//      ParseInitialCondition, IsBlankOrComment.
// ============================================================================
#include "input/parser.h"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace diffuri {

    namespace {

        // ========================================================================
        // 1. ВСПОМОГАТЕЛЬНЫЕ УТИЛИТЫ
        // ========================================================================

        bool IsSpaceChar(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool IsAlphaChar(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool IsAlphaNumChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool IsDigitChar(char c) {
            return c >= '0' && c <= '9';
        }

        // Срезает всё, начиная с первого '#', до конца строки.
        // Строковых литералов в нашем синтаксисе нет, поэтому поиск простой.
        std::string CutComment(const std::string& s) {
            auto pos = s.find('#');
            if (pos == std::string::npos) return s;
            return s.substr(0, pos);
        }

        // Обрезает пробелы с обоих концов строки.
        std::string Trim(const std::string& s) {
            std::size_t start = 0;
            std::size_t end = s.size();
            while (start < end && IsSpaceChar(s[start])) ++start;
            while (end > start && IsSpaceChar(s[end - 1])) --end;
            return s.substr(start, end - start);
        }

        // Разбор double через from_chars. Возвращает false, если строка не число.
        bool ParseDouble(const std::string& s, double& out) {
            if (s.empty()) return false;
            auto begin = s.data();
            auto end = s.data() + s.size();
            auto result = std::from_chars(begin, end, out, std::chars_format::general);
            return result.ec == std::errc{} && result.ptr == end;
        }

        // Загрузка констант: файл + extra_constants (extra имеет приоритет).
        std::map<std::string, double> LoadConstants(const ParseOptions& opts) {
            std::map<std::string, double> result;

            if (!opts.constants_file.empty()) {
                std::ifstream f(opts.constants_file);
                if (f) {
                    std::string line;
                    while (std::getline(f, line)) {
                        line = Trim(CutComment(line));
                        if (line.empty()) continue;

                        auto eq = line.find('=');
                        if (eq == std::string::npos) continue;

                        std::string name = Trim(line.substr(0, eq));
                        std::string value_str = Trim(line.substr(eq + 1));
                        if (name.empty()) continue;

                        double value = 0.0;
                        if (ParseDouble(value_str, value)) {
                            result[name] = value;
                        }
                    }
                }
            }

            for (const auto& [k, v] : opts.extra_constants) {
                result[k] = v;
            }
            return result;
        }

        // ========================================================================
        // Вычисление константного выражения.
        //
        // Нужно для правой части начального условия: пользователь может
        // написать "x(0) = -1", и парсер разберёт -1 как Binary{Sub,0,1},
        // а не как Number. Чтобы принять такое, вычисляем дерево, если оно
        // целиком состоит из чисел, констант и арифметики.
        //
        // Возвращает nullopt, если выражение не константное (есть Function,
        // Derivative, Call или деление на ноль).
        // ========================================================================
        std::optional<double> TryEvalConstNumber(const Expr& e) {
            return std::visit([&](const auto& n) -> std::optional<double> {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Number>) {
                    return n.value;
                }
                else if constexpr (std::is_same_v<T, Constant>) {
                    return n.value;
                }
                else if constexpr (std::is_same_v<T, Binary>) {
                    auto l = TryEvalConstNumber(*n.lhs);
                    auto r = TryEvalConstNumber(*n.rhs);
                    if (!l || !r) return std::nullopt;
                    switch (n.op) {
                    case Binary::Op::Add: return *l + *r;
                    case Binary::Op::Sub: return *l - *r;
                    case Binary::Op::Mul: return *l * *r;
                    case Binary::Op::Div:
                        if (*r == 0.0) return std::nullopt;
                        return *l / *r;
                    case Binary::Op::Pow:
                        // Не вычисляем Pow: целочисленная проверка
                        // показателя — задача полиномизации.
                        return std::nullopt;
                    }
                    return std::nullopt;
                }
                else {
                    // Function, Derivative, Call — не константа.
                    return std::nullopt;
                }
                }, e.value);
        }

        // ========================================================================
        // Unicode-надстрочные цифры: ⁰¹²³⁴⁵⁶⁷⁸⁹
        //
        // В UTF-8 это многобайтные последовательности. Возвращаем значение
        // цифры и количество съеденных байт, или -1, если символ не подходит.
        // ========================================================================
        int TryReadSuperscriptDigit(const std::string& s, std::size_t pos,
            std::size_t& bytes_used) {
            bytes_used = 0;
            if (pos + 2 > s.size()) return -1;

            unsigned char b0 = static_cast<unsigned char>(s[pos]);
            unsigned char b1 = static_cast<unsigned char>(s[pos + 1]);

            // Двухбайтовые: ¹²³ (U+00B9, U+00B2, U+00B3)
            if (b0 == 0xC2) {
                switch (b1) {
                case 0xB9: bytes_used = 2; return 1;
                case 0xB2: bytes_used = 2; return 2;
                case 0xB3: bytes_used = 2; return 3;
                default: break;
                }
            }

            // Трёхбайтовые: ⁰⁴⁵⁶⁷⁸⁹ (U+2070, U+2074..U+2079)
            if (pos + 3 > s.size()) return -1;
            unsigned char b2 = static_cast<unsigned char>(s[pos + 2]);
            if (b0 == 0xE2 && b1 == 0x81) {
                switch (b2) {
                case 0xB0: bytes_used = 3; return 0;
                case 0xB4: bytes_used = 3; return 4;
                case 0xB5: bytes_used = 3; return 5;
                case 0xB6: bytes_used = 3; return 6;
                case 0xB7: bytes_used = 3; return 7;
                case 0xB8: bytes_used = 3; return 8;
                case 0xB9: bytes_used = 3; return 9;
                default: break;
                }
            }

            return -1;
        }

        // ========================================================================
        // 2. ТОКЕНЫ И ЛЕКСЕР
        // ========================================================================

        enum class Tok {
            Number, Name, Derivative,
            Plus, Minus, Star, Slash, Caret,
            LParen, RParen, Comma,
            End
        };

        struct Token {
            Tok         kind = Tok::End;
            double      num = 0.0;    // для Number
            std::string text;         // для Name (имя)
            std::string func;         // для Derivative: имя функции
            int         order = 0;    // для Derivative: порядок >= 1
            int         line = 1;
            int         column = 1;
        };

        class Lexer {
        public:
            Lexer(const std::string& text, const ParseOptions& opts)
                : text_(text), opts_(opts), pos_(0), line_(1), col_(1) {
            }

            Token Next() {
                SkipWhitespace();
                int start_col = col_;

                if (pos_ >= text_.size()) {
                    return MakeToken(Tok::End, start_col);
                }

                char c = text_[pos_];

                if (IsDigitChar(c) ||
                    (c == '.' && pos_ + 1 < text_.size() && IsDigitChar(text_[pos_ + 1]))) {
                    return ReadNumber(start_col);
                }

                if (IsAlphaChar(c)) {
                    return ReadNameOrDerivative(start_col);
                }

                // Однобайтовые разделители.
                // '=' сюда не попадает: уравнения и начальные условия делятся
                // по '=' ещё до лексера (см. ParseEquation / ParseInitialCondition).
                switch (c) {
                case '+': Advance(); return MakeToken(Tok::Plus, start_col);
                case '-': Advance(); return MakeToken(Tok::Minus, start_col);
                case '*': Advance(); return MakeToken(Tok::Star, start_col);
                case '/': Advance(); return MakeToken(Tok::Slash, start_col);
                case '^': Advance(); return MakeToken(Tok::Caret, start_col);
                case '(': Advance(); return MakeToken(Tok::LParen, start_col);
                case ')': Advance(); return MakeToken(Tok::RParen, start_col);
                case ',': Advance(); return MakeToken(Tok::Comma, start_col);
                default: break;
                }

                throw ParseError(line_ + opts_.line_offset, start_col, "",
                    std::string(1, c), "неизвестный символ");
            }

        private:
            const std::string& text_;
            const ParseOptions& opts_;
            std::size_t pos_;
            int line_;
            int col_;

            void Advance() {
                if (pos_ < text_.size()) {
                    if (text_[pos_] == '\n') {
                        ++line_;
                        col_ = 1;
                    }
                    else {
                        ++col_;
                    }
                    ++pos_;
                }
            }

            void SkipWhitespace() {
                while (pos_ < text_.size() && IsSpaceChar(text_[pos_])) {
                    Advance();
                }
            }

            Token MakeToken(Tok kind, int start_col) {
                Token t;
                t.kind = kind;
                t.line = line_ + opts_.line_offset;
                t.column = start_col;
                return t;
            }

            Token ReadNumber(int start_col) {
                std::size_t start = pos_;
                while (pos_ < text_.size() &&
                    (IsDigitChar(text_[pos_]) || text_[pos_] == '.')) {
                    Advance();
                }
                // Экспонента: e/E [+/-] digits
                if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                    std::size_t save = pos_;
                    int save_col = col_;
                    Advance();
                    if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                        Advance();
                    }
                    if (pos_ < text_.size() && IsDigitChar(text_[pos_])) {
                        while (pos_ < text_.size() && IsDigitChar(text_[pos_])) {
                            Advance();
                        }
                    }
                    else {
                        // Не экспонента — откатываемся.
                        pos_ = save;
                        col_ = save_col;
                    }
                }

                std::string number_str = text_.substr(start, pos_ - start);
                double value = 0.0;
                if (!ParseDouble(number_str, value)) {
                    throw ParseError(line_ + opts_.line_offset, start_col,
                        "число", number_str,
                        "не удалось разобрать числовой литерал");
                }
                Token t = MakeToken(Tok::Number, start_col);
                t.num = value;
                return t;
            }

            std::string ReadIdentifier() {
                std::size_t start = pos_;
                while (pos_ < text_.size() && IsAlphaNumChar(text_[pos_])) {
                    Advance();
                }
                return text_.substr(start, pos_ - start);
            }

            Token ReadNameOrDerivative(int start_col) {
                // Специальный случай: 'd' в начале имени может быть Лейбницем.
                // Пробуем разобрать Лейбниц. Если паттерн не совпал — TryLeibniz
                // вернёт nullopt, и мы откатимся на обычное имя. Если паттерн
                // совпал, но нарушает правила (порядки не совпадают, не та
                // независимая переменная) — TryLeibniz бросает ParseError,
                // и он уходит наружу как настоящая ошибка.
                if (text_[pos_] == 'd') {
                    std::size_t save_pos = pos_;
                    int save_col = col_;
                    auto opt = TryLeibniz(start_col);
                    if (opt.has_value()) return *opt;
                    pos_ = save_pos;
                    col_ = save_col;
                }

                std::string name = ReadIdentifier();

                // Апострофы.
                int apos = 0;
                while (pos_ < text_.size() && text_[pos_] == '\'') {
                    ++apos;
                    Advance();
                }

                if (apos > 0) {
                    Token t = MakeToken(Tok::Derivative, start_col);
                    t.func = std::move(name);
                    t.order = apos;
                    return t;
                }

                Token t = MakeToken(Tok::Name, start_col);
                t.text = std::move(name);
                return t;
            }

            // Пробуем разобрать d^n y / dx^n. Возвращает nullopt, если паттерн
            // не совпал. Бросает ParseError, если паттерн совпал частично и
            // противоречит правилам (например, порядки не совпадают).
            std::optional<Token> TryLeibniz(int start_col) {
                std::size_t save_pos = pos_;
                int save_col = col_;

                auto rollback = [&]() {
                    pos_ = save_pos;
                    col_ = save_col;
                    };

                if (pos_ >= text_.size() || text_[pos_] != 'd') {
                    rollback();
                    return std::nullopt;
                }
                Advance(); // 'd'

                // Опционально ^N или надстрочная цифра.
                int order = 1;
                bool has_order = false;
                if (pos_ < text_.size() && text_[pos_] == '^') {
                    Advance();
                    std::size_t ds = pos_;
                    while (pos_ < text_.size() && IsDigitChar(text_[pos_])) Advance();
                    if (ds == pos_) { rollback(); return std::nullopt; }
                    order = std::stoi(text_.substr(ds, pos_ - ds));
                    has_order = true;
                }
                else {
                    std::size_t bytes = 0;
                    int d = TryReadSuperscriptDigit(text_, pos_, bytes);
                    if (d >= 0) {
                        order = d;
                        pos_ += bytes;
                        col_ += static_cast<int>(bytes);
                        has_order = true;
                    }
                }

                // Имя функции.
                if (pos_ >= text_.size() || !IsAlphaChar(text_[pos_])) {
                    rollback();
                    return std::nullopt;
                }
                std::string fname = ReadIdentifier();

                // "/d".
                if (pos_ + 1 >= text_.size() || text_[pos_] != '/' || text_[pos_ + 1] != 'd') {
                    rollback();
                    return std::nullopt;
                }
                Advance(); Advance();

                // Имя независимой переменной.
                if (pos_ >= text_.size() || !IsAlphaChar(text_[pos_])) {
                    rollback();
                    return std::nullopt;
                }
                std::string vname = ReadIdentifier();

                if (vname != opts_.independent_variable) {
                    throw ParseError(line_ + opts_.line_offset, start_col, opts_.independent_variable, vname,
                        "в знаменателе производной ожидалась независимая переменная");
                }

                // Опционально ^N или надстрочная цифра в знаменателе.
                int order_den = order;
                bool has_order_den = false;
                if (pos_ < text_.size() && text_[pos_] == '^') {
                    Advance();
                    std::size_t ds = pos_;
                    while (pos_ < text_.size() && IsDigitChar(text_[pos_])) Advance();
                    if (ds == pos_) { rollback(); return std::nullopt; }
                    order_den = std::stoi(text_.substr(ds, pos_ - ds));
                    has_order_den = true;
                }
                else {
                    std::size_t bytes = 0;
                    int d = TryReadSuperscriptDigit(text_, pos_, bytes);
                    if (d >= 0) {
                        order_den = d;
                        pos_ += bytes;
                        col_ += static_cast<int>(bytes);
                        has_order_den = true;
                    }
                }

                if (has_order && !has_order_den) {
                    throw ParseError(line_ + opts_.line_offset, col_, "^" + std::to_string(order), "",
                        "порядок в знаменателе производной отсутствует");
                }
                if (has_order_den && !has_order) {
                    throw ParseError(line_ + opts_.line_offset, col_, "", "^" + std::to_string(order_den),
                        "порядок в числителе производной отсутствует");
                }
                if (has_order && has_order_den && order != order_den) {
                    throw ParseError(line_ + opts_.line_offset, col_, std::to_string(order),
                        std::to_string(order_den),
                        "порядок в числителе и знаменателе не совпадает");
                }

                Token t = MakeToken(Tok::Derivative, start_col);
                t.func = std::move(fname);
                t.order = order;
                return t;
            }
        };

        // ========================================================================
        // 3. ПАРСЕР
        // ========================================================================

        class Parser {
        public:
            Parser(std::vector<Token> toks,
                const std::map<std::string, double>& constants)
                : toks_(std::move(toks)), pos_(0), constants_(constants) {
            }

            const Token& Current() const { return toks_[pos_]; }

            void Advance() {
                if (pos_ + 1 < toks_.size()) ++pos_;
            }

            void ExpectEnd() {
                if (Current().kind != Tok::End) {
                    throw ParseError(Current().line, Current().column,
                        "конец выражения", "",
                        "лишние символы после выражения");
                }
            }

            // expr := term (('+' | '-') term)*
            ExprPtr ParseExpr() {
                auto lhs = ParseTerm();
                while (Current().kind == Tok::Plus || Current().kind == Tok::Minus) {
                    bool is_plus = (Current().kind == Tok::Plus);
                    Advance();
                    auto rhs = ParseTerm();
                    lhs = MakeBinary(is_plus ? Binary::Op::Add : Binary::Op::Sub,
                        std::move(lhs), std::move(rhs));
                }
                return lhs;
            }

        private:
            std::vector<Token> toks_;
            std::size_t pos_;
            const std::map<std::string, double>& constants_;

            // term := unary (('*' | '/') unary)*
            ExprPtr ParseTerm() {
                auto lhs = ParseUnary();
                while (Current().kind == Tok::Star || Current().kind == Tok::Slash) {
                    bool is_mul = (Current().kind == Tok::Star);
                    Advance();
                    auto rhs = ParseUnary();
                    lhs = MakeBinary(is_mul ? Binary::Op::Mul : Binary::Op::Div,
                        std::move(lhs), std::move(rhs));
                }
                return lhs;
            }

            // unary := ('-' | '+') unary | power
            //
            // Отдельного узла Neg в дереве нет: -X разбирается как 0 - X.
            // Приоритет: унарный минус ниже ^, поэтому -x^2 читается как
            // (0 - (x^2)) — это математически корректно.
            ExprPtr ParseUnary() {
                if (Current().kind == Tok::Minus) {
                    Advance();
                    auto operand = ParseUnary();
                    return MakeBinary(Binary::Op::Sub, MakeNumber(0.0),
                        std::move(operand));
                }
                if (Current().kind == Tok::Plus) {
                    Advance();
                    return ParseUnary();
                }
                return ParsePower();
            }

            // power := primary ('^' unary)?
            //
            // Правая часть — unary, чтобы x^-2 разбиралось корректно.
            // Правая ассоциативность: x^y^z == x^(y^z).
            ExprPtr ParsePower() {
                auto base = ParsePrimary();
                if (Current().kind == Tok::Caret) {
                    Advance();
                    auto exp = ParseUnary();
                    return MakeBinary(Binary::Op::Pow, std::move(base), std::move(exp));
                }
                return base;
            }

            ExprPtr ParsePrimary() {
                const Token& t = Current();

                if (t.kind == Tok::Number) {
                    double v = t.num;
                    Advance();
                    return MakeNumber(v);
                }

                if (t.kind == Tok::Derivative) {
                    auto e = MakeDerivative(t.func, t.order);
                    Advance();
                    return e;
                }

                if (t.kind == Tok::Name) {
                    std::string name = t.text;
                    Advance();

                    if (Current().kind == Tok::LParen) {
                        Advance();
                        std::vector<ExprPtr> args;
                        if (Current().kind != Tok::RParen) {
                            args.push_back(ParseExpr());
                            while (Current().kind == Tok::Comma) {
                                Advance();
                                args.push_back(ParseExpr());
                            }
                        }
                        if (Current().kind != Tok::RParen) {
                            throw ParseError(Current().line, Current().column, ")", "",
                                "ожидалась закрывающая скобка аргументов");
                        }
                        Advance();
                        return MakeCall(std::move(name), std::move(args));
                    }

                    auto it = constants_.find(name);
                    if (it != constants_.end()) {
                        return MakeConstant(name, it->second);
                    }
                    return MakeFunction(std::move(name));
                }

                if (t.kind == Tok::LParen) {
                    Advance();
                    auto e = ParseExpr();
                    if (Current().kind != Tok::RParen) {
                        throw ParseError(Current().line, Current().column, ")", "",
                            "ожидалась закрывающая скобка");
                    }
                    Advance();
                    return e;
                }

                throw ParseError(t.line, t.column, "выражение", "",
                    "ожидалось начало выражения");
            }
        };

        // Утилита: токенизировать строку целиком.
        std::vector<Token> Tokenize(const std::string& s, const ParseOptions& opts) {
            Lexer lex(s, opts);
            std::vector<Token> toks;
            while (true) {
                Token t = lex.Next();
                bool is_end = (t.kind == Tok::End);
                toks.push_back(std::move(t));
                if (is_end) break;
            }
            return toks;
        }

        // Утилита: собрать сообщение для ParseError.
        std::string FormatParseErrorMessage(int line, int column,
            const std::string& expected,
            const std::string& found,
            const std::string& message) {
            std::ostringstream oss;
            oss << "строка " << line << ", столбец " << column << ": ";
            if (!message.empty()) oss << message;
            else oss << "синтаксическая ошибка";
            bool has_expected = !expected.empty();
            bool has_found = !found.empty();
            if (has_expected || has_found) {
                oss << " (";
                if (has_expected) oss << "ожидалось: " << expected;
                if (has_expected && has_found) oss << ", ";
                if (has_found) oss << "найдено: " << found;
                oss << ")";
            }
            return oss.str();
        }

    } // namespace

    // ============================================================================
    // 4. ParseError
    // ============================================================================

    ParseError::ParseError(int line, int column,
        std::string expected, std::string found,
        const std::string& message)
        : std::runtime_error(FormatParseErrorMessage(line, column, expected, found, message)),
        line_(line), column_(column),
        expected_(std::move(expected)), found_(std::move(found)) {
    }

    int ParseError::line() const noexcept { return line_; }
    int ParseError::column() const noexcept { return column_; }
    const std::string& ParseError::expected() const noexcept { return expected_; }
    const std::string& ParseError::found() const noexcept { return found_; }

    // ============================================================================
    // 5. ПУБЛИЧНЫЕ ФУНКЦИИ
    // ============================================================================

    ExprPtr ParseExpression(const std::string& text, const ParseOptions& opts) {
        std::string trimmed = Trim(CutComment(text));
        auto constants = LoadConstants(opts);
        auto toks = Tokenize(trimmed, opts);
        Parser p(std::move(toks), constants);
        auto e = p.ParseExpr();
        p.ExpectEnd();
        return e;
    }

    Equation ParseEquation(const std::string& text, const ParseOptions& opts) {
    std::string s = Trim(CutComment(text));
    if (s.empty()) {
        throw ParseError(1 + opts.line_offset, 1, "уравнение", "",
            "пустая строка не является уравнением");
    }

    // Ищем '=' на верхнем уровне (не внутри скобок).
    int depth = 0;
    std::size_t eq_pos = std::string::npos;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == '=' && depth == 0) {
            if (eq_pos != std::string::npos) {
                throw ParseError(1 + opts.line_offset, static_cast<int>(i + 1),
                    "единственный '='", "второй '='",
                    "на верхнем уровне допускается только один '='");
            }
            eq_pos = i;
        }
    }
    if (eq_pos == std::string::npos) {
        throw ParseError(1 + opts.line_offset, 1, "=", "",
            "уравнение должно содержать '='");
    }

    std::string lhs_text = s.substr(0, eq_pos);
    std::string rhs_text = s.substr(eq_pos + 1);

    auto constants = LoadConstants(opts);

    auto lhs_toks = Tokenize(lhs_text, opts);
    Parser lhs_parser(std::move(lhs_toks), constants);
    auto lhs = lhs_parser.ParseExpr();
    lhs_parser.ExpectEnd();

    auto rhs_toks = Tokenize(rhs_text, opts);
    Parser rhs_parser(std::move(rhs_toks), constants);
    auto rhs = rhs_parser.ParseExpr();
    rhs_parser.ExpectEnd();

    return Equation{ std::move(lhs), std::move(rhs) };
}
    InitialCondition ParseInitialCondition(const std::string& text,
        const ParseOptions& opts) {
        std::string s = Trim(CutComment(text));
        if (s.empty()) {
            throw ParseError(1 + opts.line_offset, 1, "начальное условие", "",
                "пустая строка не является начальным условием");
        }

        auto eq_pos = s.find('=');
        if (eq_pos == std::string::npos) {
            throw ParseError(1 + opts.line_offset, 1, "=", "",
                "начальное условие должно содержать '='");
        }

        std::string lhs_text = s.substr(0, eq_pos);
        std::string rhs_text = s.substr(eq_pos + 1);

        auto constants = LoadConstants(opts);

        // Левая часть: [Derivative | Name] LParen [+|-] Number RParen End
        auto lhs_toks = Tokenize(lhs_text, opts);
        if (lhs_toks.empty() || lhs_toks.back().kind != Tok::End) {
            throw ParseError(1 + opts.line_offset, 1, "начальное условие", "",
                "не удалось разобрать левую часть");
        }

        std::size_t i = 0;
        std::string fname;
        int order = 0;

        if (lhs_toks[i].kind == Tok::Derivative) {
            fname = lhs_toks[i].func;
            order = lhs_toks[i].order;
            ++i;
        }
        else if (lhs_toks[i].kind == Tok::Name) {
            fname = lhs_toks[i].text;
            order = 0;
            ++i;
        }
        else {
            throw ParseError(1 + opts.line_offset, lhs_toks[i].column,
                "имя функции", "",
                "начальное условие должно начинаться с имени функции");
        }

        if (i >= lhs_toks.size() || lhs_toks[i].kind != Tok::LParen) {
            int col = (i < lhs_toks.size()) ? lhs_toks[i].column : 1;
            throw ParseError(1 + opts.line_offset, col, "(", "",
                "ожидалась '(' после имени функции");
        }
        ++i;

        // Знак перед точкой: допускаем -2.25 и +2.25.
        bool negate_t0 = false;
        if (i < lhs_toks.size() && lhs_toks[i].kind == Tok::Minus) {
            negate_t0 = true;
            ++i;
        }
        else if (i < lhs_toks.size() && lhs_toks[i].kind == Tok::Plus) {
            ++i;
        }

        if (i >= lhs_toks.size() || lhs_toks[i].kind != Tok::Number) {
            int col = (i < lhs_toks.size()) ? lhs_toks[i].column : 1;
            throw ParseError(1 + opts.line_offset, col, "число", "",
                "ожидалось числовое значение точки");
        }
        double t0 = negate_t0 ? -lhs_toks[i].num : lhs_toks[i].num;
        ++i;

        if (i >= lhs_toks.size() || lhs_toks[i].kind != Tok::RParen) {
            int col = (i < lhs_toks.size()) ? lhs_toks[i].column : 1;
            throw ParseError(1 + opts.line_offset, col, ")", "",
                "ожидалась ')' после точки");
        }
        ++i;

        if (i >= lhs_toks.size() || lhs_toks[i].kind != Tok::End) {
            int col = (i < lhs_toks.size()) ? lhs_toks[i].column : 1;
            throw ParseError(1 + opts.line_offset, col, "конец левой части", "",
                "лишние символы после точки");
        }

        // Правая часть: должна быть числовой константой. Допускаем выражения
        // вида -1, 2+3, 0.5*pi — они вычисляются TryEvalConstNumber.
        auto rhs_toks = Tokenize(rhs_text, opts);
        Parser rhs_parser(std::move(rhs_toks), constants);
        auto rhs = rhs_parser.ParseExpr();
        rhs_parser.ExpectEnd();

        auto value_opt = TryEvalConstNumber(*rhs);
        if (!value_opt) {
            throw ParseError(1 + opts.line_offset,
                static_cast<int>(eq_pos + 2), "число", "",
                "правая часть начального условия должна быть числом");
        }
        double value = *value_opt;

        InitialCondition ic;
        ic.function_name = std::move(fname);
        ic.order = order;
        ic.t0 = t0;
        ic.value = value;
        return ic;
    }

    bool IsBlankOrComment(const std::string& line) {
        for (char c : line) {
            if (c == '#') return true;
            if (!IsSpaceChar(c)) return false;
        }
        return true;
    }

} // namespace diffuri