// ============================================================================
// tests/systems/test_trajectory.cpp
//
// Тесты траектории: проверка всех промежуточных точек решения,
// а не только финальной.
//
// Типы проверок:
//   1. Все точки совпадают с аналитическим решением (для систем
//      с известным точным решением).
//   2. Инварианты сохраняются вдоль траектории (энергия осциллятора).
//   3. Траектория непрерывна: время монотонно, нет разрывов.
//
// Структура файла:
//   1. Анонимный namespace: хелперы (FindIndex, TrajectoryCase).
//   2. Тесты (TEST-макросы).
// ============================================================================

// --- Стандартная библиотека (по алфавиту) ---
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <fstream>

// --- Внутренние зависимости (по алфавиту) ---
#include "pipeline/runner.h"
#include "solver/solver.h"
#include "cli/cli.h"

// --- GTest ---
#include <gtest/gtest.h>

namespace diffuri {
    namespace {

        // ============================================================================
        // 1. АНОНИМНЫЙ NAMESPACE: хелперы
        // ============================================================================

        // Найти индекс функции по имени в sol.functions.
        // Возвращает -1, если не найдена.
        int FindIndex(const Solution& sol, const std::string& name) {
            for (std::size_t i = 0; i < sol.functions.size(); ++i) {
                if (sol.functions[i] == name) return static_cast<int>(i);
            }
            return -1;
        }

        // Описание системы с аналитическим решением для проверки траектории.
        //
        // exact принимает (t, имя_функции) и возвращает точное значение.
        // Имя функции — как оно хранится в sol.functions (например, "x_1"
        // для вспомогательной переменной от ReduceOrder, а не "x'").
        struct TrajectoryCase {
            std::string name;
            std::string input;
            double t_end;
            double rtol;
            double atol;
            std::size_t M;
            // (t, имя_функции) -> точное значение. Возвращает nullptr,
            // если для этой функции аналитического решения нет.
            std::function<const double* (double, const std::string&)> exact;
            double tol_factor;  // множитель допуска: |ошибка| <= tol_factor * (1 + |exact|)
        };

        // Стандартные опции для тестов траектории.
        SolveOptions MakeOpts(const TrajectoryCase& c) {
            SolveOptions o;
            o.t_end = c.t_end;
            o.rtol = c.rtol;
            o.atol = c.atol;
            o.M = c.M;
            return o;
        }

        // ============================================================================
        // 2. ТЕСТЫ: проверка всех точек по аналитическому решению
        // ============================================================================

        // Линейное затухание: x' = -x, x(0) = 1.
        // Точное решение: x(t) = exp(-t).
        TEST(Trajectory, LinearDecayAllPoints) {
            RunResult r = RunPipeline("x' = -x\nx(0) = 1\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0) << "function 'x' not found";

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = std::exp(-pt.t);
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // Осциллятор: x' = y, y' = -x, x(0) = 1, y(0) = 0.
        // Точное решение: x(t) = cos(t), y(t) = -sin(t).
        TEST(Trajectory, OscillatorAllPoints) {
            RunResult r = RunPipeline(
                "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double tol_x = 1e-9 * (1.0 + std::abs(std::cos(pt.t)));
                const double tol_y = 1e-9 * (1.0 + std::abs(std::sin(pt.t)));
                EXPECT_NEAR(pt.x[ix], std::cos(pt.t), tol_x)
                    << "x at point " << i << ", t = " << pt.t;
                EXPECT_NEAR(pt.x[iy], -std::sin(pt.t), tol_y)
                    << "y at point " << i << ", t = " << pt.t;
            }
        }

        // Вынужденный резонанс: x'' = -x + sin(t), x(0) = 0, x'(0) = 0.
        // После ReduceOrder: x' = x_1, x_1' = -x + sin(t).
        // После Autonomize: t' = 1 (т.к. есть sin(t)).
        // Точное решение: x(t) = (sin(t) - t*cos(t)) / 2,
        //                 x'(t) = (cos(t) + t*sin(t)) / 2.
        TEST(Trajectory, ForcedResonanceAllPoints) {
            RunResult r = RunPipeline(
                "x'' = -x + sin(t)\nx(0) = 0\nx'(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int ix1 = FindIndex(r.solution, "x_1");
            ASSERT_GE(ix, 0) << "function 'x' not found";
            ASSERT_GE(ix1, 0) << "function 'x_1' not found (expected from ReduceOrder)";

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double t = pt.t;
                // x(t) = (sin(t) - t*cos(t)) / 2
                const double expected_x = 0.5 * (std::sin(t) - t * std::cos(t));
                // x'(t) = t*sin(t) / 2
                const double expected_dx = 0.5 * t * std::sin(t);
                const double tol = 1e-9 * (1.0 + std::abs(expected_x));
                EXPECT_NEAR(pt.x[ix], expected_x, tol)
                    << "x at point " << i << ", t = " << t;
                EXPECT_NEAR(pt.x[ix1], expected_dx, tol)
                    << "x_1 (=x') at point " << i << ", t = " << t;
            }
        }

        // Задача с полюсом: x' = x^2, x(0) = 1.
        // Точное решение: x(t) = 1/(1-t), полюс в t = 1.
        // Интегрируем до t = 0.9, чтобы не дойти до полюса.
        TEST(Trajectory, BlowupX2AllPoints) {
            SolveOptions opts;
            opts.t_end = 0.9;
            opts.rtol = 1e-10;
            opts.atol = 1e-12;
            opts.M = 20;

            RunResult r = RunPipeline("x' = x^2\nx(0) = 1\n", opts);

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = 1.0 / (1.0 - pt.t);
                // Допуск шире: задача плохо обусловлена вблизи полюса.
                const double tol = 1e-7 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // ============================================================================
        // 3. ТЕСТЫ: сохранение инвариантов вдоль траектории
        // ============================================================================

        // Осциллятор: энергия E = x^2 + y^2 сохраняется.
        // Дрейф не должен превышать допуск.
        TEST(Trajectory, OscillatorEnergyConserved) {
            RunResult r = RunPipeline(
                "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);

            const auto& pts = r.solution.points;
            ASSERT_GE(pts.size(), 2u);

            const double E0 = pts.front().x[ix] * pts.front().x[ix]
                + pts.front().x[iy] * pts.front().x[iy];

            for (std::size_t i = 1; i < pts.size(); ++i) {
                const double E = pts[i].x[ix] * pts[i].x[ix]
                    + pts[i].x[iy] * pts[i].x[iy];
                const double drift = std::abs(E - E0);
                // Для осциллятора с rtol=1e-10 дрейф должен быть < 1e-8.
                EXPECT_LT(drift, 1e-8 * (1.0 + std::abs(E0)))
                    << "energy drift at point " << i << ", t = " << pts[i].t;
            }
        }

        // Задача с полюсом: обратная величина 1/x = 1 - t линейна.
        // Проверяем, что 1/x + t = 1 вдоль всей траектории.
        TEST(Trajectory, BlowupX2Invariant) {
            SolveOptions opts;
            opts.t_end = 0.9;
            opts.rtol = 1e-10;
            opts.atol = 1e-12;
            opts.M = 20;

            RunResult r = RunPipeline("x' = x^2\nx(0) = 1\n", opts);

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double invariant = 1.0 / pt.x[ix] + pt.t;  // должно быть 1
                EXPECT_NEAR(invariant, 1.0, 1e-8)
                    << "invariant 1/x + t at point " << i << ", t = " << pt.t;
            }
        }

        // ============================================================================
        // 4. ТЕСТЫ: непрерывность и монотонность траектории
        // ============================================================================

        // Время должно быть строго монотонно возрастающим,
        // все значения конечными, первая точка — начальное условие.
        TEST(Trajectory, TimeMonotonicAndFinite) {
            RunResult r = RunPipeline(
                "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n");

            const auto& pts = r.solution.points;
            ASSERT_GE(pts.size(), 2u);

            // Первая точка — начальные условия.
            EXPECT_DOUBLE_EQ(pts.front().t, 0.0);
            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);
            EXPECT_DOUBLE_EQ(pts.front().x[ix], 1.0);
            EXPECT_DOUBLE_EQ(pts.front().x[iy], 0.0);

            for (std::size_t i = 1; i < pts.size(); ++i) {
                // Время строго возрастает.
                EXPECT_GT(pts[i].t, pts[i - 1].t)
                    << "time not increasing at point " << i;
                // Все значения конечны.
                for (std::size_t j = 0; j < pts[i].x.size(); ++j) {
                    EXPECT_TRUE(std::isfinite(pts[i].x[j]))
                        << "non-finite at point " << i << ", component " << j;
                }
            }
        }

        // Финальная точка должна быть не дальше t_end.
        TEST(Trajectory, FinalTimeNotExceedTEnd) {
            const double t_end = 1.0;
            RunResult r = RunPipeline(
                "x' = -x\nx(0) = 1\n");

            EXPECT_LE(r.solution.t_final, t_end + 1e-12);
            EXPECT_GE(r.solution.points.back().t, t_end - 1e-6);
        }

        // ============================================================================
        // 5. ТЕСТ: траектория через CLI с --trajectory (опционально, если нужен
        //    E2E-тест CSV-файла на уровне unit-тестов, а не только через
        //    cli_smoke.cmake)
        // ============================================================================

        // Проверяем, что SaveTrajectory записывает все точки, а не только финальную.
        // Для этого используем SaveTrajectory напрямую.
        TEST(Trajectory, SaveTrajectoryWritesAllPoints) {
            RunResult r = RunPipeline("x' = -x\nx(0) = 1\n");

            const std::string path = "test_trajectory_output.csv";

            // Видимые функции — только исходные (без вспомогательных).
            std::vector<std::string> visible;
            for (const auto& f : r.solution.functions) {
                if (f.find("q_") != 0 && f.find("v_") != 0) {
                    visible.push_back(f);
                }
            }

            SaveTrajectory(r.solution, visible, path);

            // Читаем файл и проверяем число строк.
            std::ifstream file(path);
            ASSERT_TRUE(file.is_open()) << "cannot open " << path;

            std::size_t line_count = 0;
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) ++line_count;
            }

            // Заголовок + число точек траектории.
            EXPECT_EQ(line_count, 1 + r.solution.points.size())
                << "CSV should contain header + one line per trajectory point";

            // Удаляем временный файл.
            std::remove(path.c_str());
        }

        // ============================================================================
// Дополнительные тесты траектории по аналитическим решениям
// ============================================================================

// Затухающий осциллятор: x'' + 0.1x' + x = 0, x(0)=1, x'(0)=0.
// β = 0.05, ω_d = sqrt(1 - 0.0025).
// x(t) = e^{-βt}(cos(ω_d t) + (β/ω_d) sin(ω_d t))
        TEST(Trajectory, DampedOscillatorAllPoints) {
            RunResult r = RunPipeline(
                "x' = y\ny' = -x - 0.1*y\nx(0) = 1\ny(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);

            constexpr double beta = 0.05;
            constexpr double omega_d = 0.9987492177719090;  // sqrt(1 - 0.0025)

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double t = pt.t;
                const double decay = std::exp(-beta * t);
                const double expected_x = decay * (
                    std::cos(omega_d * t) + (beta / omega_d) * std::sin(omega_d * t));
                const double expected_y = -decay * (1.0 / omega_d) * std::sin(omega_d * t);
                const double tol = 1e-9 * (1.0 + std::abs(expected_x));
                EXPECT_NEAR(pt.x[ix], expected_x, tol)
                    << "x at point " << i << ", t = " << t;
                EXPECT_NEAR(pt.x[iy], expected_y, tol)
                    << "y at point " << i << ", t = " << t;
            }
        }

        // Экспоненциальный рост: x' = x, x(0) = 1.
        TEST(Trajectory, ExponentialGrowthAllPoints) {
            RunResult r = RunPipeline("x' = x\nx(0) = 1\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = std::exp(pt.t);
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // Квадратичное затухание: x' = -x^2, x(0) = 1.
        // x(t) = 1/(1+t). Quadratize вводит q_1 = x^2.
        TEST(Trajectory, QuadraticDecayAllPoints) {
            RunResult r = RunPipeline("x' = -x^2\nx(0) = 1\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = 1.0 / (1.0 + pt.t);
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // Осциллятор с частотой 2: x'' + 4x = 0, x(0)=1, x'(0)=0.
        // x(t) = cos(2t), x'(t) = -2 sin(2t).
        TEST(Trajectory, HarmonicOmega2AllPoints) {
            RunResult r = RunPipeline("x'' = -4*x\nx(0) = 1\nx'(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int ix1 = FindIndex(r.solution, "x_1");
            ASSERT_GE(ix, 0);
            ASSERT_GE(ix1, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double t = pt.t;
                const double expected_x = std::cos(2.0 * t);
                const double expected_dx = -2.0 * std::sin(2.0 * t);
                const double tol = 1e-9 * (1.0 + std::abs(expected_x));
                EXPECT_NEAR(pt.x[ix], expected_x, tol)
                    << "x at point " << i << ", t = " << t;
                EXPECT_NEAR(pt.x[ix1], expected_dx, tol)
                    << "x_1 at point " << i << ", t = " << t;
            }
        }

        // Вращение: x' = -2y, y' = 2x, x(0)=1, y(0)=0.
        // x(t) = cos(2t), y(t) = sin(2t).
        TEST(Trajectory, CoupledRotationAllPoints) {
            RunResult r = RunPipeline(
                "x' = -2*y\ny' = 2*x\nx(0) = 1\ny(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double t = pt.t;
                const double tol = 1e-9;
                EXPECT_NEAR(pt.x[ix], std::cos(2.0 * t), tol)
                    << "x at point " << i << ", t = " << t;
                EXPECT_NEAR(pt.x[iy], std::sin(2.0 * t), tol)
                    << "y at point " << i << ", t = " << t;
            }
        }

        // Логистическое уравнение: x' = x(1-x), x(0) = 0.5.
        // x(t) = 1/(1 + e^{-t}).
        TEST(Trajectory, LogisticAllPoints) {
            RunResult r = RunPipeline("x' = x*(1-x)\nx(0) = 0.5\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = 1.0 / (1.0 + std::exp(-pt.t));
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // Риккати: x' = 1 + x^2, x(0) = 0.
        // x(t) = tan(t). Интегрируем до t=1 (полюс в π/2 ≈ 1.57).
        TEST(Trajectory, RiccatiTanAllPoints) {
            RunResult r = RunPipeline("x' = 1 + x^2\nx(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = std::tan(pt.t);
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // 3D спираль: x' = -y, y' = x, z' = -z.
        // x(t) = cos(t), y(t) = sin(t), z(t) = 2e^{-t}.
        TEST(Trajectory, Spiral3DAllPoints) {
            RunResult r = RunPipeline(
                "x' = -y\ny' = x\nz' = -z\nx(0) = 1\ny(0) = 0\nz(0) = 2\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            const int iz = FindIndex(r.solution, "z");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);
            ASSERT_GE(iz, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double t = pt.t;
                const double tol = 1e-9;
                EXPECT_NEAR(pt.x[ix], std::cos(t), tol)
                    << "x at point " << i;
                EXPECT_NEAR(pt.x[iy], std::sin(t), tol)
                    << "y at point " << i;
                EXPECT_NEAR(pt.x[iz], 2.0 * std::exp(-t), tol)
                    << "z at point " << i;
            }
        }

        // Неавтономная система: x' = 2t, x(0) = 1.
        // x(t) = t^2 + 1. Autonomize добавит t' = 1.
        TEST(Trajectory, PolynomialSolutionAllPoints) {
            RunResult r = RunPipeline("x' = 2*t\nx(0) = 1\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0) << "function 'x' not found";

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                const double expected = pt.t * pt.t + 1.0;
                const double tol = 1e-9 * (1.0 + std::abs(expected));
                EXPECT_NEAR(pt.x[ix], expected, tol)
                    << "point " << i << ", t = " << pt.t;
            }
        }

        // ============================================================================
// Дополнительные тесты инвариантов
// ============================================================================

// Вращение: радиус x² + y² = const.
        TEST(Trajectory, CoupledRotationRadiusConserved) {
            RunResult r = RunPipeline(
                "x' = -2*y\ny' = 2*x\nx(0) = 1\ny(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const double radius_sq = r.solution.points[i].x[ix] * r.solution.points[i].x[ix]
                    + r.solution.points[i].x[iy] * r.solution.points[i].x[iy];
                EXPECT_NEAR(radius_sq, 1.0, 1e-8)
                    << "radius² drift at point " << i;
            }
        }

        // Логистическое: решение ограничено 0 < x < 1.
        TEST(Trajectory, LogisticBounded) {
            RunResult r = RunPipeline("x' = x*(1-x)\nx(0) = 0.5\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                EXPECT_GT(r.solution.points[i].x[ix], 0.0)
                    << "x should be positive at point " << i;
                EXPECT_LT(r.solution.points[i].x[ix], 1.0)
                    << "x should be < 1 at point " << i;
            }
        }

        // Риккати: монотонный рост (x' = 1 + x² > 0 всегда).
        TEST(Trajectory, RiccatiMonotonic) {
            RunResult r = RunPipeline("x' = 1 + x^2\nx(0) = 0\n");

            const int ix = FindIndex(r.solution, "x");
            ASSERT_GE(ix, 0);

            for (std::size_t i = 1; i < r.solution.points.size(); ++i) {
                EXPECT_GT(r.solution.points[i].x[ix], r.solution.points[i - 1].x[ix])
                    << "x should be strictly increasing at point " << i;
            }
        }

        // 3D спираль: z монотонно убывает, радиус в плоскости (x,y) постоянен.
        TEST(Trajectory, Spiral3DInvariants) {
            RunResult r = RunPipeline(
                "x' = -y\ny' = x\nz' = -z\nx(0) = 1\ny(0) = 0\nz(0) = 2\n");

            const int ix = FindIndex(r.solution, "x");
            const int iy = FindIndex(r.solution, "y");
            const int iz = FindIndex(r.solution, "z");
            ASSERT_GE(ix, 0);
            ASSERT_GE(iy, 0);
            ASSERT_GE(iz, 0);

            for (std::size_t i = 0; i < r.solution.points.size(); ++i) {
                const auto& pt = r.solution.points[i];
                // Радиус в плоскости (x, y) = 1.
                const double r_sq = pt.x[ix] * pt.x[ix] + pt.x[iy] * pt.x[iy];
                EXPECT_NEAR(r_sq, 1.0, 1e-8) << "radius² at point " << i;
                // z монотонно убывает.
                if (i > 0) {
                    EXPECT_LT(pt.x[iz], r.solution.points[i - 1].x[iz])
                        << "z should decrease at point " << i;
                }
                // z > 0.
                EXPECT_GT(pt.x[iz], 0.0) << "z should be positive at point " << i;
            }
        }

    } // namespace
} // namespace diffuri