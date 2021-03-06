/*
Copyright (C) 2017-2019 Topological Manifold

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "error.h"
#include "matrix.h"

// Для случаев, когда последняя строка матрицы состоит из нулей с последней единицей.
template <size_t N, typename T>
class MatrixMulVector
{
        Matrix<N, N, T> m_mtx;

public:
        MatrixMulVector(const Matrix<N, N, T>& m) : m_mtx(m)
        {
                if (m_mtx[N - 1][N - 1] != 1)
                {
                        error("Wrong matrix for matrix-vector multiplier");
                }

                for (unsigned i = 0; i < N - 1; ++i)
                {
                        if (m_mtx[N - 1][i] != 0)
                        {
                                error("Wrong matrix for matrix-vector multiplier");
                        }
                }
        }

        Vector<N - 1, T> operator()(const Vector<N - 1, T>& v) const
        {
                Vector<N - 1, T> res;

                for (unsigned row = 0; row < N - 1; ++row)
                {
                        res[row] = m_mtx[row][N - 1];
                        for (unsigned col = 0; col < N - 1; ++col)
                        {
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ == 8
                                // При использовании GCC 8.1 функция fma работает неправильно
                                res[row] += m_mtx[row][col] * v[col];
#else
                                res[row] = any_fma(m_mtx[row][col], v[col], res[row]);
#endif
                        }
                }

                return res;
        }
};

template <typename T>
Matrix<4, 4, T> look_at(const Vector<3, T>& eye, const Vector<3, T>& center, const Vector<3, T>& up)
{
        Vector<3, T> f = normalize(center - eye);
        Vector<3, T> s = normalize(cross(f, up));
        Vector<3, T> u = normalize(cross(s, f));

        Matrix<4, 4, T> m;

        m[0] = Vector<4, T>(s[0], s[1], s[2], -dot(s, eye));
        m[1] = Vector<4, T>(u[0], u[1], u[2], -dot(u, eye));
        m[2] = Vector<4, T>(-f[0], -f[1], -f[2], dot(f, eye));
        m[3] = Vector<4, T>(0, 0, 0, 1);

        return m;
}

template <typename T, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
constexpr Matrix<4, 4, T> ortho_opengl(T1 left, T2 right, T3 bottom, T4 top, T5 near, T6 far)
{
        // OpenGL
        // X направо [-1, 1], Y вверх [-1, 1], Z в экран [-1, 1]

        T left_t = left;
        T right_t = right;
        T bottom_t = bottom;
        T top_t = top;
        T near_t = near;
        T far_t = far;

        Matrix<4, 4, T> m(1);

        m[0][0] = 2 / (right_t - left_t);
        m[1][1] = 2 / (top_t - bottom_t);
        m[2][2] = 2 / (far_t - near_t);

        m[0][3] = -(right_t + left_t) / (right_t - left_t);
        m[1][3] = -(top_t + bottom_t) / (top_t - bottom_t);
        m[2][3] = -(far_t + near_t) / (far_t - near_t);

        return m;
}

template <typename T, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
constexpr Matrix<4, 4, T> ortho_vulkan(T1 left, T2 right, T3 bottom, T4 top, T5 near, T6 far)
{
        // Vulkan
        // X направо [-1, 1], Y вниз [-1, 1], Z в экран [0, 1]

        T left_t = left;
        T right_t = right;
        T bottom_t = bottom;
        T top_t = top;
        T near_t = near;
        T far_t = far;

        Matrix<4, 4, T> m(1);

        m[0][0] = 2 / (right_t - left_t);
        m[1][1] = 2 / (bottom_t - top_t);
        m[2][2] = 1 / (far_t - near_t);

        m[0][3] = -(right_t + left_t) / (right_t - left_t);
        m[1][3] = -(bottom_t + top_t) / (bottom_t - top_t);
        m[2][3] = -near_t / (far_t - near_t);

        return m;
}

template <size_t N, typename T>
constexpr Matrix<N + 1, N + 1, T> scale(const Vector<N, T>& v)
{
        Matrix<N + 1, N + 1, T> m(1);
        for (unsigned i = 0; i < N; ++i)
        {
                m[i][i] = v[i];
        }
        return m;
}

template <typename T, typename... V>
constexpr Matrix<sizeof...(V) + 1, sizeof...(V) + 1, T> scale(V... v)
{
        return scale(Vector<sizeof...(V), T>(v...));
}

template <size_t N, typename T>
constexpr Matrix<N + 1, N + 1, T> translate(const Vector<N, T>& v)
{
        Matrix<N + 1, N + 1, T> m(1);
        for (unsigned i = 0; i < N; ++i)
        {
                m[i][N] = v[i];
        }
        return m;
}

template <typename T, typename... V>
constexpr Matrix<sizeof...(V) + 1, sizeof...(V) + 1, T> translate(V... v)
{
        return translate(Vector<sizeof...(V), T>(v...));
}
