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

#include <imagine/config/defs.hh>
#include <imagine/font/Font.hh>
#include <imagine/gfx/PixmapTexture.hh>
#include <imagine/util/container/VMemArray.hh>
#include <system_error>

namespace IG
{
class GenericIO;
}

namespace IG::Gfx
{

class Renderer;

static constexpr auto glyphCommonTextureSampler = CommonTextureSampler::NO_MIP_CLAMP;

struct GlyphEntry
{
	Gfx::PixmapTexture glyph_{};
	IG::GlyphMetrics metrics{};

	constexpr const Gfx::PixmapTexture &glyph() const { return glyph_; }
};

class GlyphTextureSet
{
public:
	static constexpr bool supportsUnicode = Config::UNICODE_CHARS;

	constexpr GlyphTextureSet() = default;
	GlyphTextureSet(Renderer &, IG::Font, IG::FontSettings);
	IG::FontSettings fontSettings() const;
	bool setFontSettings(Renderer &r, IG::FontSettings set);
	unsigned precache(Renderer &r, std::string_view string);
	unsigned precacheAlphaNum(Renderer &r)
	{
		return precache(r, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	}
	GlyphEntry *glyphEntry(Renderer &r, int c, bool allowCache = true);
	uint32_t nominalHeight() const;
	void freeCaches(uint32_t rangeToFreeBits);
	void freeCaches() { freeCaches(~0); }

private:
	IG::Font font{};
	IG::VMemArray<GlyphEntry> glyphTable{};
	IG::FontSettings settings{};
	IG::FontSize faceSize{};
	uint32_t nominalHeight_ = 0;
	uint32_t usedGlyphTableBits = 0;

	void calcNominalHeight(Renderer &r);
	void resetGlyphTable();
	std::errc cacheChar(Renderer &r, int c, int tableIdx);
};

}
