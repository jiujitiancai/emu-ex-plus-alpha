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

#include <imagine/util/utility.h>
#include <imagine/util/concepts.hh>
#include <cmath>

namespace IG
{

static constexpr auto radians(floating_point auto degrees)
{
	return degrees * static_cast<decltype(degrees)>(M_PI / 180.0);
}

static constexpr auto degrees(floating_point auto radians)
{
	return radians * static_cast<decltype(radians)>(180.0 / M_PI);
}

static constexpr auto pow2(auto base)
{
	return base * base;
}

// ceil/floor/round "n" to the nearest multiple "mult"
static constexpr auto ceilMult(floating_point auto n, floating_point auto mult)
{
	return std::ceil(n / mult) * mult;
}

static constexpr auto floorMult(floating_point auto n, floating_point auto mult)
{
	return std::floor(n / mult) * mult;
}

static constexpr auto roundMult(floating_point auto n, floating_point auto mult)
{
	return std::round(n / mult) * mult;
}

static bool isInRange(auto val, auto min, auto max)
{
	return val >= min && val <= max;
}

// treat zeros as positive
static constexpr auto sign(auto num)
{
	return static_cast<decltype(num)>(num >= 0 ? 1 : -1);
}

template<integral IntType, floating_point FloatType = float>
constexpr static FloatType floatScaler(uint8_t bits)
{
	assumeExpr(bits <= 64);
	if constexpr(std::is_signed_v<IntType>)
	{
		assumeExpr(bits);
		return 1ul << (bits - 1);
	}
	else
	{
		return 1ul << bits;
	}
}

template<integral IntType>
constexpr static IntType clampFromFloat(floating_point auto x, uint8_t bits)
{
	const auto scale = floatScaler<IntType, decltype(x)>(bits);
	return std::round(std::fmax(std::fmin(x * scale, scale - (decltype(x))1.), -scale));
}

template<integral IntType>
constexpr static IntType clampFromFloat(floating_point auto x)
{
	const auto scale = floatScaler<IntType, decltype(x)>(sizeof(IntType) * 8);
	return std::round(x * scale);
}

static constexpr auto wrapMax(auto x, auto max)
{
	if constexpr(std::is_floating_point_v<decltype(x)>)
	{
		return std::fmod(max + std::fmod(x, max), max);
	}
	else
	{
		return (max + (x % max)) % max;
	}
}

static constexpr auto wrapMinMax(auto x, auto min, auto max)
{
	return min + wrapMax(x - min, max - min);
}

}
