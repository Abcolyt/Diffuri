#!/usr/bin/env python3
# ============================================================================
# tools/gen_reference.py
#
# Генератор tests/systems/reference_systems.h.
# Запускать вручную из корня проекта:
#     python tools/gen_reference.py
# ============================================================================
import math

import numpy as np
from scipy.integrate import solve_ivp


# Каждая система:
#   expected_names  — метки, как их печатает FormatSolved:
#                     x' для x_1 (OrderReducer aux), q_N для Quadratize,
#                     t для независимой переменной; v_* скрыты.
#   expected        — функция(y_scipy, t_end) -> список эталонных значений.
SYSTEMS = [
    {
        "name": "linear_decay",
        "input": "x' = -x\nx(0) = 1\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [-y[0]],
        "y0": [1.0],
        "expected_names": ["x"],
        "expected": lambda y, t: [math.exp(-t)],
    },
    {
        "name": "oscillator",
        "input": "x' = y\ny' = -x\nx(0) = 1\ny(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -y[0]],
        "y0": [1.0, 0.0],
        "expected_names": ["x", "y"],
        "expected": lambda y, t: [math.cos(t), -math.sin(t)],
    },
    {
        "name": "forced_resonance",
        "input": "x'' = -x + sin(t)\nx(0) = 0\nx'(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -y[0] + math.sin(t)],
        "y0": [0.0, 0.0],
        "expected_names": ["x", "x'", "t"],
        # Точное решение x'' + x = sin t, x(0)=x'(0)=0:
        #   x(t) = (sin t - t cos t) / 2
        "expected": lambda y, t: [
            0.5 * (math.sin(t) - t * math.cos(t)),
            0.5 * (math.cos(t) + t * math.sin(t)),
            t,
        ],
    },
    {
        "name": "pendulum",
        "input": "x'' = -sin(x)\nx(0) = 0\nx'(0) = 1\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -math.sin(y[0])],
        "y0": [0.0, 1.0],
        "expected_names": ["x", "x'"],
        "expected": None,  # scipy DOP853
    },
    {
        "name": "lorenz",
        "input": (
            "x' = -10*x + 10*y\n"
            "y' = 28*x - y - x*z\n"
            "z' = -8/3*z + x*y\n"
            "x(0) = -13.76\n"
            "y(0) = -19.58\n"
            "z(0) = 27.0\n"
        ),
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [
            -10.0 * y[0] + 10.0 * y[1],
            28.0 * y[0] - y[1] - y[0] * y[2],
            -8.0 / 3.0 * y[2] + y[0] * y[1],
        ],
        "y0": [-13.76, -19.58, 27.0],
        "expected_names": ["x", "y", "z"],
        "expected": None,
    },
    {
        "name": "damped",
        "input": "x' = y\ny' = -x - 0.1*y\nx(0) = 1\ny(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -y[0] - 0.1 * y[1]],
        "y0": [1.0, 0.0],
        "expected_names": ["x", "y"],
        "expected": None,
    },
    {
        "name": "van_der_pol",
        "input": (
            "x' = y\n"
            "y' = 0.5*(1 - x^2)*y - x\n"
            "x(0) = 2\n"
            "y(0) = 0\n"
        ),
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [
            y[1],
            0.5 * (1.0 - y[0] ** 2) * y[1] - y[0],
        ],
        "y0": [2.0, 0.0],
        # Quadratize вводит q_1 = x^2, она печатается.
        "expected_names": ["x", "y", "q_1"],
        "expected": lambda y, t: [y[0], y[1], y[0] ** 2],
    },
    {
        "name": "blowup_x2_partial",
        "input": "x' = x^2\nx(0) = 1\n",
        "t_end": 0.9,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[0] ** 2],
        "y0": [1.0],
        "expected_names": ["x"],
        # x' = x^2, x(0)=1 -> x(t) = 1/(1-t); полюс в t=1.
        "expected": lambda y, t: [1.0 / (1.0 - t)],
    },
    {
        "name": "blowup_x3_partial",
        "input": "x' = x^3\nx(0) = 1\n",
        "t_end": 0.4,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[0] ** 3],
        "y0": [1.0],
        # Quadratize вводит q_1 = x^2, она печатается.
        "expected_names": ["x", "q_1"],
        # x' = x^3, x(0)=1 -> x(t) = 1/sqrt(1-2t); полюс в t=0.5.
        # q_1 = x^2 = 1/(1-2t).
        "expected": lambda y, t: [
            1.0 / math.sqrt(1.0 - 2.0 * t),
            1.0 / (1.0 - 2.0 * t),
        ],
    },
]


def render(systems):
    lines = []
    lines.append('#pragma once')
    lines.append('')
    lines.append('// АВТОГЕНЕРАЦИЯ — tools/gen_reference.py, не редактировать вручную.')
    lines.append('')
    lines.append('#include <cstddef>')
    lines.append('#include <string>')
    lines.append('#include <utility>')
    lines.append('#include <vector>')
    lines.append('')
    lines.append('namespace diffuri {')
    lines.append('namespace test_data {')
    lines.append('')
    lines.append('struct SystemRef {')
    lines.append('    std::string name;')
    lines.append('    std::string input;')
    lines.append('    double t_end;')
    lines.append('    double rtol;')
    lines.append('    double atol;')
    lines.append('    std::size_t M;')
    lines.append('    double h_init;')
    lines.append('    std::vector<std::pair<std::string, double>> expected;')
    lines.append('};')
    lines.append('')
    lines.append('inline const std::vector<SystemRef>& All() {')
    lines.append('    static const std::vector<SystemRef> v = {')

    for s in systems:
        sol = solve_ivp(s["rhs"], [0.0, s["t_end"]], s["y0"],
                        method='DOP853', rtol=1e-13, atol=1e-15)
        y_end = sol.y[:, -1]
        if s["expected"] is not None:
            values = s["expected"](y_end, s["t_end"])
        else:
            values = list(y_end)

        if len(values) != len(s["expected_names"]):
            raise RuntimeError(
                f'{s["name"]}: expected_names has {len(s["expected_names"])} '
                f'entries, expected() returned {len(values)}'
            )

        exp_entries = ", ".join(
            f'{{"{n}", {float(v)!r}}}' for n, v in zip(s["expected_names"], values)
        )
        lines.append('        {')
        lines.append(f'            "{s["name"]}",')
        lines.append(f'            {s["input"]!r},')
        lines.append(f'            {s["t_end"]!r}, {s["rtol"]!r}, {s["atol"]!r},')
        lines.append(f'            {s["M"]}, {s["h_init"]!r},')
        lines.append(f'            {{{exp_entries}}},')
        lines.append('        },')

    lines.append('    };')
    lines.append('    return v;')
    lines.append('}')
    lines.append('')
    lines.append('}  // namespace test_data')
    lines.append('}  // namespace diffuri')
    lines.append('')
    return '\n'.join(lines)


if __name__ == '__main__':
    with open('tests/systems/reference_systems.h', 'w', encoding='utf-8') as f:
        f.write(render(SYSTEMS))