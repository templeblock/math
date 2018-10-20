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

#include "string_vector.h"

std::vector<std::string> operator+(const std::vector<std::string>& v1, const std::vector<std::string>& v2)
{
        std::vector<std::string> res;
        res.reserve(v1.size() + v2.size());

        for (const std::string& s : v1)
        {
                res.push_back(s);
        }

        for (const std::string& s : v2)
        {
                res.push_back(s);
        }

        return res;
}

std::vector<std::string> operator+(const std::vector<std::string>& v, const std::string& s)
{
        return v + std::vector<std::string>({s});
}

std::vector<std::string> operator+(const std::string& s, const std::vector<std::string>& v)
{
        return std::vector<std::string>({s}) + v;
}

std::vector<const char*> const_char_pointer_vector(const std::vector<std::string>& v)
{
        std::vector<const char*> res;
        res.reserve(v.size());

        for (const std::string& s : v)
        {
                res.push_back(s.c_str());
        }

        return res;
}

std::vector<std::string> string_vector(const std::vector<const char*>& v)
{
        return std::vector<std::string>(std::cbegin(v), std::cend(v));
}
