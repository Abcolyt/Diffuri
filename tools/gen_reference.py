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


import math  # уже должен быть в шапке файла

def _cr3bp_rhs(t, y):
    x, yy, vx, vy = y
    mu = 0.0121505856
    r1sq = (x + mu)**2 + yy**2
    r2sq = (x - 1 + mu)**2 + yy**2
    r1c = r1sq * math.sqrt(r1sq)
    r2c = r2sq * math.sqrt(r2sq)
    ax = 2*vy + x - (1-mu)*(x+mu)/r1c - mu*(x-1+mu)/r2c
    ay = -2*vx + yy - (1-mu)*yy/r1c - mu*yy/r2c

    return [vx, vy, ax, ay]
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
    {
    "name": "cr3bp_earth_moon",
    "input": (
        "u1' = -u1*u1*u1*((x+0.0121505856)*vx + y*vy)\n"
        "u2' = -u2*u2*u2*((x-0.9878494144)*vx + y*vy)\n"
        "w1' = -2*u1*u1*u1*u1*((x+0.0121505856)*vx + y*vy)\n"
        "w2' = -2*u2*u2*u2*u2*((x-0.9878494144)*vx + y*vy)\n"
        "vx' = 2*vy + x - 0.9878494144*(x+0.0121505856)*u1*w1"
        "    - 0.0121505856*(x-0.9878494144)*u2*w2\n"
        "vy' = -2*vx + y - 0.9878494144*y*u1*w1"
        "    - 0.0121505856*y*u2*w2\n"
        "x' = vx\n"
        "y' = vy\n"
        "x(0)  = 0.5\n"
        "y(0)  = 0.0\n"
        "vx(0) = 0.0\n"
        "vy(0) = 0.5\n"
        "u1(0) = 1.9525508\n"
        "w1(0) = 3.8124546\n"
        "u2(0) = 2.0498130\n"
        "w2(0) = 4.2017330\n"
        ),
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: _cr3bp_rhs(t, y),
        "y0": [0.5, 0.0, 0.0, 0.5],   # x, y, vx, vy
        "expected_names": ["x", "y", "vx", "vy"],
        "expected": None,  # scipy DOP853
    },
    # ============================================================================
    # ДОПОЛНИТЕЛЬНЫЕ СИСТЕМЫ С АНАЛИТИЧЕСКИМ РЕШЕНИЕМ
    # ============================================================================

    # --- 1. Исправление damped: добавляем аналитику ---
    # Было: "expected": None (scipy)
    # Стало: точное решение затухающего осциллятора
    #
    # x'' + 0.1*x' + x = 0, x(0)=1, x'(0)=0
    # β = 0.05, ω₀ = 1, ω_d = sqrt(1 - β²) = sqrt(0.9975)
    # x(t) = e^{-βt} * (cos(ω_d t) + (β/ω_d) sin(ω_d t))
    # y(t) = x'(t) = e^{-βt} * (-(β²/ω_d + ω_d) sin(ω_d t))
    #       = -e^{-βt} * (1/ω_d) * sin(ω_d t)   [упрощённо для x'(0)=0]

    {
        "name": "damped",
        "input": "x' = y\ny' = -x - 0.1*y\nx(0) = 1\ny(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -y[0] - 0.1 * y[1]],
        "y0": [1.0, 0.0],
        "expected_names": ["x", "y"],
        "expected": lambda y, t: [
            # x(t) = e^{-0.05t} * (cos(ω_d t) + (0.05/ω_d) sin(ω_d t))
            math.exp(-0.05 * t) * (
                math.cos(math.sqrt(0.9975) * t)
                + (0.05 / math.sqrt(0.9975)) * math.sin(math.sqrt(0.9975) * t)
            ),
            # y(t) = x'(t) = -e^{-0.05t} * (1/ω_d) * sin(ω_d t)
            -math.exp(-0.05 * t) * (1.0 / math.sqrt(0.9975)) * math.sin(math.sqrt(0.9975) * t),
        ],
    },

    # --- 2. Экспоненциальный рост: x' = x ---
    {
        "name": "exponential_growth",
        "input": "x' = x\nx(0) = 1\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[0]],
        "y0": [1.0],
        "expected_names": ["x"],
        "expected": lambda y, t: [math.exp(t)],
    },

    # --- 3. Квадратичное затухание: x' = -x^2 ---
    # x(t) = 1/(1+t), x(0) = 1
    {
        "name": "quadratic_decay",
        "input": "x' = -x^2\nx(0) = 1\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [-y[0] ** 2],
        "y0": [1.0],
        "expected_names": ["x", "q_1"],  # Quadratize вводит q_1 = x^2
        "expected": lambda y, t: [
            1.0 / (1.0 + t),
            1.0 / (1.0 + t) ** 2,
        ],
    },

    # --- 4. Осциллятор с частотой 2: x'' + 4x = 0 ---
    # x(t) = cos(2t), x'(t) = -2 sin(2t)
    {
        "name": "harmonic_omega2",
        "input": "x'' = -4*x\nx(0) = 1\nx'(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -4.0 * y[0]],
        "y0": [1.0, 0.0],
        "expected_names": ["x", "x'"],
        "expected": lambda y, t: [
            math.cos(2.0 * t),
            -2.0 * math.sin(2.0 * t),
        ],
    },

    # --- 5. Вращение: x' = -2y, y' = 2x ---
    # x(t) = cos(2t), y(t) = sin(2t) при x(0)=1, y(0)=0
    {
        "name": "coupled_rotation",
        "input": "x' = -2*y\ny' = 2*x\nx(0) = 1\ny(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [-2.0 * y[1], 2.0 * y[0]],
        "y0": [1.0, 0.0],
        "expected_names": ["x", "y"],
        "expected": lambda y, t: [
            math.cos(2.0 * t),
            math.sin(2.0 * t),
        ],
    },

    # --- 6. Логистическое уравнение: x' = x*(1-x) ---
    # x(t) = 1/(1 + (1/x0 - 1)*e^{-t}), x(0) = 0.5
    # => x(t) = 1/(1 + e^{-t})
    {
        "name": "logistic",
        "input": "x' = x*(1-x)\nx(0) = 0.5\n",
        "t_end": 2.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[0] * (1.0 - y[0])],
        "y0": [0.5],
        "expected_names": ["x", "q_1"],  # Quadratize вводит q_1 = x^2
        "expected": lambda y, t: [
            1.0 / (1.0 + math.exp(-t)),
            1.0 / (1.0 + math.exp(-t)) ** 2,
        ],
    },

    # --- 7. Уравнение Риккати: x' = 1 + x^2 ---
    # x(t) = tan(t), x(0) = 0. Полюс в t = π/2 ≈ 1.5708.
    # Интегрируем до t = 1.0 (безопасно).
    {
        "name": "riccati_tan",
        "input": "x' = 1 + x^2\nx(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [1.0 + y[0] ** 2],
        "y0": [0.0],
        "expected_names": ["x", "q_1"],  # Quadratize вводит q_1 = x^2
        "expected": lambda y, t: [
            math.tan(t),
            math.tan(t) ** 2,
        ],
    },

    # --- 8. Линейная 3D система: спираль в 3D ---
    # x' = -y, y' = x, z' = -z
    # x(t) = cos(t), y(t) = sin(t), z(t) = e^{-t}
    {
        "name": "spiral_3d",
        "input": "x' = -y\ny' = x\nz' = -z\nx(0) = 1\ny(0) = 0\nz(0) = 2\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [-y[1], y[0], -y[2]],
        "y0": [1.0, 0.0, 2.0],
        "expected_names": ["x", "y", "z"],
        "expected": lambda y, t: [
            math.cos(t),
            math.sin(t),
            2.0 * math.exp(-t),
        ],
    },

    # --- 9. Система с полиномиальным решением: x' = 2t ---
    # x(t) = t^2 + 1, x(0) = 1
    # Проверяет, что парсер корректно обрабатывает независимую переменную
    # в RHS (неавтономная система → Autonomize добавит t' = 1).
    {
        "name": "polynomial_solution",
        "input": "x' = 2*t\nx(0) = 1\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [2.0 * t],
        "y0": [1.0],
        "expected_names": ["x", "t"],  # Autonomize добавит t
        "expected": lambda y, t: [
            t ** 2 + 1.0,
            t,
        ],
    },

    # --- 10. Неоднородный осциллятор: x'' + x = e^t ---
    # x(t) = A cos(t) + B sin(t) + e^t / 2
    # x(0) = 0, x'(0) = 0:
    #   A + 1/2 = 0 => A = -1/2
    #   B + 1/2 = 0 => B = -1/2
    # x(t) = (e^t - cos(t) - sin(t)) / 2
    # x'(t) = (e^t + sin(t) - cos(t)) / 2
    {
        "name": "forced_exp",
        "input": "x'' = -x + exp(t)\nx(0) = 0\nx'(0) = 0\n",
        "t_end": 1.0,
        "rtol": 1e-10, "atol": 1e-12,
        "M": 20, "h_init": 1e-3,
        "rhs": lambda t, y: [y[1], -y[0] + math.exp(t)],
        "y0": [0.0, 0.0],
        "expected_names": ["x", "x'", "t", "v_1"],
        # v_1 = exp(t) от Polynomize, t от Autonomize
        "expected": lambda y, t: [
            (math.exp(t) - math.cos(t) - math.sin(t)) / 2.0,
            (math.exp(t) + math.sin(t) - math.cos(t)) / 2.0,
            t,
            math.exp(t),
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