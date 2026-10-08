#pragma once

// АВТОГЕНЕРАЦИЯ — tools/gen_reference.py, не редактировать вручную.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace diffuri {
    namespace test_data {

        struct SystemRef {
            std::string name;
            std::string input;
            double t_end;
            double rtol;
            double atol;
            std::size_t M;
            double h_init;
            std::vector<std::pair<std::string, double>> expected;
        };

        inline const std::vector<SystemRef>& All() {
            static const std::vector<SystemRef> v = {
                {
                    "linear_decay",
                    "x' = -x\nx(0) = 1\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 0.36787944117144233}},
                },
                {
                    "oscillator",
                    "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 0.5403023058681398}, {"y", -0.8414709848078965}},
                },
                {
                    "forced_resonance",
                    "x'' = -x + sin(t)\nx(0) = 0\nx'(0) = 0\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 0.1505843394698784},
                     {"x'", 0.42073549240394825},
                     {"t", 1.0}},
                },
                {
                    "pendulum",
                    "x'' = -sin(x)\nx(0) = 0\nx'(0) = 1\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 0.8477986816771164}, {"x'", 0.5685689980951707}},
                },
                {
                    "lorenz",
                    "x' = -10*x + 10*y\ny' = 28*x - y - x*z\nz' = -8/3*z + x*y\n"
                    "x(0) = -13.76\ny(0) = -19.58\nz(0) = 27.0\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 4.08942663088452},
                     {"y", -2.0800383308405283},
                     {"z", 30.099074803850543}},
                },
                {
                    "damped",
                    "x' = y\ny' = -x - 0.1*y\nx(0) = 1\ny(0) = 0\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 0.5549917206178984}, {"y", -0.8007901073533101}},
                },
                {
                    "van_der_pol",
                    "x' = y\ny' = 0.5*(1 - x^2)*y - x\nx(0) = 2\ny(0) = 0\n",
                    1.0, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 1.334891333353608},
                     {"y", -1.1544580972665115},
                     {"q_1", 1.7819348718625907}},
                },
                {
                    "blowup_x2_partial",
                    "x' = x^2\nx(0) = 1\n",
                    0.9, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 10.000000000000002}},
                },
                {
                    "blowup_x3_partial",
                    "x' = x^3\nx(0) = 1\n",
                    0.4, 1e-10, 1e-12,
                    20, 1e-3,
                    {{"x", 2.23606797749979}, {"q_1", 5.000000000000001}},
                },
            };
            return v;
        }

    }  // namespace test_data
}  // namespace diffuri