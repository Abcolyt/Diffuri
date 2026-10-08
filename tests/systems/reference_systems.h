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
            20, 0.001,
            {{"x", 0.36787944117144233}},
        },
        {
            "oscillator",
            "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 0.5403023058681398}, {"y", -0.8414709848078965}},
        },
        {
            "forced_resonance",
            "x'' = -x + sin(t)\nx(0) = 0\nx'(0) = 0\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 0.15058433946987837}, {"x'", 0.6908866453380181}, {"t", 1.0}},
        },
        {
            "pendulum",
            "x'' = -sin(x)\nx(0) = 0\nx'(0) = 1\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 0.8477986816771174}, {"x'", 0.5685689980951748}},
        },
        {
            "lorenz",
            "x' = -10*x + 10*y\ny' = 28*x - y - x*z\nz' = -8/3*z + x*y\nx(0) = -13.76\ny(0) = -19.58\nz(0) = 27.0\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 4.089426628660528}, {"y", -2.0800383184582745}, {"z", 30.09907478577127}},
        },
        {
            "damped",
            "x' = y\ny' = -x - 0.1*y\nx(0) = 1\ny(0) = 0\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 0.5549917206179026}, {"y", -0.8007901073533087}},
        },
        {
            "van_der_pol",
            "x' = y\ny' = 0.5*(1 - x^2)*y - x\nx(0) = 2\ny(0) = 0\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 1.33489133335363}, {"y", -1.1544580972665077}, {"q_1", 1.7819348718626324}},
        },
        {
            "blowup_x2_partial",
            "x' = x^2\nx(0) = 1\n",
            0.9, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 10.000000000000002}},
        },
        {
            "blowup_x3_partial",
            "x' = x^3\nx(0) = 1\n",
            0.4, 1e-10, 1e-12,
            20, 0.001,
            {{"x", 2.2360679774997902}, {"q_1", 5.000000000000001}},
        },
        {
            "cr3bp_earth_moon",
            "u1' = -u1*u1*u1*((x+0.0121505856)*vx + y*vy)\nu2' = -u2*u2*u2*((x-0.9878494144)*vx + y*vy)\nw1' = -2*u1*u1*u1*u1*((x+0.0121505856)*vx + y*vy)\nw2' = -2*u2*u2*u2*u2*((x-0.9878494144)*vx + y*vy)\nvx' = 2*vy + x - 0.9878494144*(x+0.0121505856)*u1*w1    - 0.0121505856*(x-0.9878494144)*u2*w2\nvy' = -2*vx + y - 0.9878494144*y*u1*w1    - 0.0121505856*y*u2*w2\nx' = vx\ny' = vy\nx(0)  = 0.5\ny(0)  = 0.0\nvx(0) = 0.0\nvy(0) = 0.5\nu1(0) = 1.9525508\nw1(0) = 3.8124546\nu2(0) = 2.0498130\nw2(0) = 4.2017330\n",
            1.0, 1e-10, 1e-12,
            20, 0.001,
            {{"x", -0.04552638302818021}, {"y", -0.4268748256617191}, {"vx", 0.730714708754659}, {"vy", -0.6154873194869441}},
        },
    };
    return v;
}

}  // namespace test_data
}  // namespace diffuri
