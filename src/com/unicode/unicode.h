/*
Copyright (C) 2017, 2018 Topological Manifold

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

#include <string>
#include <type_traits>

namespace unicode
{
template <typename T>
std::enable_if_t<std::is_same_v<T, char32_t>, std::string> utf32_to_number_string(T code_point);
std::string utf8_to_number_string(const std::string& s);

char32_t read_utf8_as_utf32(const std::string& s, size_t& i);
}
