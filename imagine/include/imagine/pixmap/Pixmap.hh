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
#include <imagine/pixmap/PixmapDesc.hh>
#include <imagine/util/rectangle2.h>
#include <imagine/util/algorithm.h>
#include <imagine/util/container/array.hh>

namespace IG
{

template <class Func, class SrcData = unsigned>
concept PixmapTransformFunc =
		requires (Func &&f, SrcData data){ f(data); };

// TODO: rename this class to PixmapView
class Pixmap : public PixmapDesc
{
public:
	enum PitchUnits { PIXEL_UNITS, BYTE_UNITS };
	class PitchInit
	{
	public:
		unsigned val;
		PitchUnits units;

		constexpr PitchInit(uint32_t pitchVal, PitchUnits units):
			val{pitchVal}, units{units}
			{}
	};

	constexpr Pixmap() = default;

	constexpr Pixmap(PixmapDesc desc, void *data, PitchInit pitch):
		PixmapDesc{desc},
		pitch{pitch.units == PIXEL_UNITS ? pitch.val * desc.format().bytesPerPixel() : pitch.val},
		data_{data}
		{}

	constexpr Pixmap(PixmapDesc desc, void *data):
		Pixmap{desc, data, {desc.w(), PIXEL_UNITS}}
		{}

	constexpr operator bool() const
	{
		return data_;
	}

	constexpr char *pixel(WP pos) const
	{
		return &IG::ArrayView2<char>{data(), pitchBytes()}[pos.y][format().pixelBytes(pos.x)];
	}

	constexpr char *data() const
	{
		return (char*)data_;
	}

	constexpr uint32_t pitchPixels() const
	{
		return pitch / format().bytesPerPixel();
	}

	constexpr uint32_t pitchBytes() const
	{
		return pitch;
	}

	constexpr size_t bytes() const
	{
		return pitchBytes() * h();
	}

	constexpr size_t unpaddedBytes() const
	{
		return PixmapDesc::bytes();
	}

	constexpr bool isPadded() const
	{
		return w() != pitchPixels();
	}

	constexpr uint32_t paddingPixels() const
	{
		return pitchPixels() - w();
	}

	constexpr uint32_t paddingBytes() const
	{
		return pitchBytes() - format().pixelBytes(w());
	}

	constexpr Pixmap subView(WP pos, WP size) const
	{
		//logDMsg("sub-pixmap with pos:%dx%d size:%dx%d", pos.x, pos.y, size.x, size.y);
		assumeExpr(pos.x >= 0 && pos.y >= 0);
		assumeExpr(pos.x + size.x <= (int)w() && pos.y + size.y <= (int)h());
		return Pixmap{makeNewSize(size), pixel(pos), {pitchBytes(), BYTE_UNITS}};
	}

	void write(Pixmap pixmap);
	void write(Pixmap pixmap, WP destPos);
	void writeConverted(Pixmap pixmap);
	void writeConverted(Pixmap pixmap, WP destPos);
	void clear(WP pos, WP size);
	void clear();

	void transformInPlace(PixmapTransformFunc auto &&func)
	{
		switch(format().bytesPerPixel())
		{
			case 1: return transformInPlace2<uint8_t>(func);
			case 2: return transformInPlace2<uint16_t>(func);
			case 4: return transformInPlace2<uint32_t>(func);
		}
	}

	template <class Data>
	void transformInPlace2(PixmapTransformFunc<Data> auto &&func)
	{
		auto data = (Data*)data_;
		if(!isPadded())
		{
			IG::transformN(data, w() * h(), data,
				[=](Data pixel)
				{
					return func(pixel);
				});
		}
		else
		{
			auto dataPitchPixels = pitchPixels();
			auto width = w();
			iterateTimes(h(), y)
			{
				auto lineData = data;
				IG::transformN(lineData, width, lineData,
					[=](Data pixel)
					{
						return func(pixel);
					});
				data += dataPitchPixels;
			}
		}
	}

	void writeTransformed(PixmapTransformFunc auto &&func, Pixmap pixmap)
	{
		auto srcBytesPerPixel = pixmap.format().bytesPerPixel();
		switch(format().bytesPerPixel())
		{
			bcase 1:
				switch(srcBytesPerPixel)
				{
					case 1: return writeTransformed2<uint8_t,  uint8_t>(func, pixmap);
					case 2: return writeTransformed2<uint16_t, uint8_t>(func, pixmap);
					case 4: return writeTransformed2<uint32_t, uint8_t>(func, pixmap);
				}
			bcase 2:
				switch(srcBytesPerPixel)
				{
					case 1: return writeTransformed2<uint8_t,  uint16_t>(func, pixmap);
					case 2: return writeTransformed2<uint16_t, uint16_t>(func, pixmap);
					case 4: return writeTransformed2<uint32_t, uint16_t>(func, pixmap);
				}
			bcase 4:
				switch(srcBytesPerPixel)
				{
					case 1: return writeTransformed2<uint8_t,  uint32_t>(func, pixmap);
					case 2: return writeTransformed2<uint16_t, uint32_t>(func, pixmap);
					case 4: return writeTransformed2<uint32_t, uint32_t>(func, pixmap);
				}
		}
	}

	void writeTransformed(PixmapTransformFunc auto &&func, Pixmap pixmap, WP destPos)
	{
		subView(destPos, size() - destPos).writeTransformed(func, pixmap);
	}

	template <class Src, class Dest>
	void writeTransformedDirect(PixmapTransformFunc<Src> auto &&func, Pixmap pixmap)
	{
		writeTransformed2<Src, Dest>(func, pixmap);
	}

protected:
	uint32_t pitch = 0; // in bytes
	void *data_{};

	template <class Src, class Dest>
	void writeTransformed2(PixmapTransformFunc<Src> auto &&func, Pixmap pixmap)
	{
		auto srcData = (const Src*)pixmap.data_;
		auto destData = (Dest*)data_;
		if(w() == pixmap.w() && !isPadded() && !pixmap.isPadded())
		{
			IG::transform_n_r(srcData, pixmap.w() * pixmap.h(), destData,
				[=](Src srcPixel)
				{
					return func(srcPixel);
				});
		}
		else
		{
			auto destPitchPixels = pitchPixels();
			iterateTimes(pixmap.h(), h)
			{
				auto destLineData = destData;
				auto srcLineData = srcData;
				IG::transform_n_r(srcLineData, pixmap.w(), destLineData,
					[=](Src srcPixel)
					{
						return func(srcPixel);
					});
				srcData += pixmap.pitchPixels();
				destData += destPitchPixels;
			}
		}
	}
};

}
