#pragma once

/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/glm/ext/vector_float4.hpp>

class GLMVec4
{
public:
	constexpr GLMVec4() = default;
	constexpr GLMVec4(float x, float y, float z, float w): v{x, y, z, w} {}
	constexpr GLMVec4(glm::vec4 v): v{v} {}
	constexpr glm::vec4 vec() const { return v; }

	float &operator[](int i)
	{
		return v[i];
	}

	constexpr float const &operator[](int i) const
	{
		return v[i];
	}

	constexpr float x() const { return v.x; }
	constexpr float y() const { return v.y; }
	constexpr float z() const { return v.z; }
	constexpr float w() const { return v.w; }

protected:
	glm::vec4 v{};
};
