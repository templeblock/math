/*
Copyright (C) 2017 Topological Manifold

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
#ifndef OBJ_SURFACE_H
#define OBJ_SURFACE_H

#include "obj.h"

#include "geometry/vec.h"
#include "progress/progress.h"

#include <memory>

std::unique_ptr<IObj> create_surface_for_obj(const IObj* obj, ProgressRatio* progress);

std::unique_ptr<IObj> create_obj_for_facets(const std::vector<glm::vec3>& points, const std::vector<Vector<3, double>>& normals,
                                            const std::vector<std::array<int, 3>>& facets);

#endif
