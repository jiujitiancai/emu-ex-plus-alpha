/*  This file is part of GBC.emu.

	GBC.emu is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	GBC.emu is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with GBC.emu.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "main"
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppInlines.hh>
#include <emuframework/EmuAudio.hh>
#include <emuframework/EmuVideo.hh>
#include <imagine/util/ScopeGuard.hh>
#include <imagine/util/format.hh>
#include <imagine/fs/FS.hh>
#include <gambatte.h>
#include <libgambatte/src/video/lcddef.h>
#include <resample/resampler.h>
#include <resample/resamplerinfo.h>
#include <main/Cheats.hh>
#include <main/Palette.hh>
#include "internal.hh"

namespace EmuEx
{

const char *EmuSystem::creditsViewStr = CREDITS_INFO_STRING "(c) 2011-2022\nRobert Broglia\nwww.explusalpha.com\n\n\nPortions (c) the\nGambatte Team\ngambatte.sourceforge.net";
gambatte::GB gbEmu;
static Resampler *resampler{};
static uint8_t activeResampler = 1;
static uint32_t totalFrames = 0;
static uint64_t totalSamples = 0;
alignas(8) static uint_least32_t frameBuffer[gambatte::lcd_hres * gambatte::lcd_vres];
static constexpr IG::WP lcdSize{gambatte::lcd_hres, gambatte::lcd_vres};
static bool useBgrOrder{};
static const GBPalette *gameBuiltinPalette{};
bool EmuSystem::hasCheats = true;
bool EmuSystem::canRenderRGB565 = false;
EmuSystem::NameFilterFunc EmuSystem::defaultFsFilter =
	[](std::string_view name)
	{
		return IG::stringEndsWithAny(name, ".gb", ".gbc", ".GB", ".GBC");
	};
EmuSystem::NameFilterFunc EmuSystem::defaultBenchmarkFsFilter = defaultFsFilter;

const BundledGameInfo &EmuSystem::bundledGameInfo(unsigned idx)
{
	static const BundledGameInfo info[]
	{
		{ "Test Game", "game.gb"	}
	};

	return info[0];
}

const char *EmuSystem::shortSystemName()
{
	return "GB";
}

const char *EmuSystem::systemName()
{
	return "Game Boy";
}

static uint_least32_t makeOutputColor(uint_least32_t rgb888)
{
	unsigned b = rgb888       & 0xFF;
	unsigned g = rgb888 >>  8 & 0xFF;
	unsigned r = rgb888 >> 16 & 0xFF;
	auto desc = useBgrOrder ? IG::PIXEL_DESC_BGRA8888.nativeOrder() : IG::PIXEL_DESC_RGBA8888_NATIVE;
	return desc.build(r, g, b, 0u);
}

void applyGBPalette()
{
	unsigned idx = optionGBPal;
	assert(idx < std::size(gbPal));
	bool useBuiltin = optionUseBuiltinGBPalette && gameBuiltinPalette;
	if(useBuiltin)
		logMsg("using built-in game palette");
	else
		logMsg("using palette index %d", idx);
	const GBPalette &pal = useBuiltin ? *gameBuiltinPalette : gbPal[idx];
	iterateTimes(4, i)
		gbEmu.setDmgPaletteColor(0, i, makeOutputColor(pal.bg[i]));
	iterateTimes(4, i)
		gbEmu.setDmgPaletteColor(1, i, makeOutputColor(pal.sp1[i]));
	iterateTimes(4, i)
		gbEmu.setDmgPaletteColor(2, i, makeOutputColor(pal.sp2[i]));
}

void EmuSystem::onOptionsLoaded(IG::ApplicationContext ctx)
{
	gbEmu.setInputGetter(&gbcInput);
	gbEmu.setStreamDelegates(
		[ctx](std::string_view basePath, std::string_view filename) -> IG::IFStream
		{
			return {ctx.openFileUri(FS::uriString(basePath, filename), IO::AccessHint::ALL, IO::OPEN_TEST)};
		},
		[ctx](std::string_view basePath, std::string_view filename) -> IG::OFStream
		{
			return {ctx.openFileUri(FS::uriString(basePath, filename), IO::OPEN_CREATE | IO::OPEN_TEST)};
		});
}

void EmuSystem::reset(ResetMode mode)
{
	assert(gameIsRunning());
	gbEmu.reset();
}

FS::FileString EmuSystem::stateFilename(int slot, std::string_view name)
{
	return IG::format<FS::FileString>("{}.0{}.gqs", name, saveSlotCharUpper(slot));
}

void EmuSystem::saveState(IG::ApplicationContext ctx, IG::CStringView path)
{
	IG::OFStream stream{ctx.openFileUri(path, IO::OPEN_CREATE)};
	if(!gbEmu.saveState(frameBuffer, gambatte::lcd_hres, stream))
		throwFileWriteError();
}

void EmuSystem::loadState(EmuApp &app, IG::CStringView path)
{
	IG::IFStream stream{app.appContext().openFileUri(path, IO::AccessHint::ALL)};
	if(!gbEmu.loadState(stream))
		throwFileReadError();
}

void EmuSystem::saveBackupMem(IG::ApplicationContext ctx)
{
	logMsg("saving battery");
	gbEmu.saveSavedata();
	writeCheatFile(ctx);
}

void EmuSystem::savePathChanged()
{
	if(gameIsRunning())
		gbEmu.setSaveDir(std::string{contentSaveDirectory()});
}

void EmuSystem::closeSystem(IG::ApplicationContext ctx)
{
	saveBackupMem(ctx);
	cheatList.clear();
	cheatsModified = false;
	gameBuiltinPalette = nullptr;
	totalFrames = 0;
	totalSamples = 0;
}

void EmuSystem::loadGame(IG::ApplicationContext ctx, IO &io, EmuSystemCreateParams, OnLoadProgressDelegate)
{
	gbEmu.setSaveDir(std::string{contentSaveDirectory()});
	auto buff = io.buffer();
	if(!buff)
	{
		throwFileReadError();
	}
	if(auto result = gbEmu.load(buff.data(), buff.size(), contentFileName().data(), optionReportAsGba ? gbEmu.GBA_CGB : 0);
		result != gambatte::LOADRES_OK)
	{
		throw std::runtime_error(gambatte::to_string(result));
	}
	if(!gbEmu.isCgb())
	{
		gameBuiltinPalette = findGbcTitlePal(gbEmu.romTitle().c_str());
		if(gameBuiltinPalette)
			logMsg("game %s has built-in palette", gbEmu.romTitle().c_str());
		applyGBPalette();
	}
	readCheatFile(ctx);
	applyCheats();
}

bool EmuSystem::onVideoRenderFormatChange(EmuVideo &video, IG::PixelFormat fmt)
{
	video.setFormat({lcdSize, fmt});
	auto isBgrOrder = fmt == IG::PIXEL_BGRA8888;
	if(isBgrOrder != useBgrOrder)
	{
		useBgrOrder = isBgrOrder;
		IG::Pixmap frameBufferPix{{lcdSize, IG::PIXEL_RGBA8888}, frameBuffer};
		frameBufferPix.transformInPlace(
			[](uint32_t srcPixel) // swap red/blue values
			{
				return (srcPixel & 0xFF000000) | ((srcPixel & 0xFF0000) >> 16) | (srcPixel & 0x00FF00) | ((srcPixel & 0x0000FF) << 16);
			});
	}
	gbEmu.refreshPalettes();
	return true;
}

void EmuSystem::configAudioRate(IG::FloatSeconds frameTime, uint32_t rate)
{
	long outputRate = std::round(rate * (59.7275 * frameTime.count()));
	long inputRate = 2097152;
	if(optionAudioResampler >= ResamplerInfo::num())
		optionAudioResampler = std::min((int)ResamplerInfo::num(), 1);
	if(!resampler || optionAudioResampler != activeResampler || resampler->outRate() != outputRate)
	{
		logMsg("setting up resampler %d for input rate %ldHz", (int)optionAudioResampler, inputRate);
		delete resampler;
		resampler = ResamplerInfo::get(optionAudioResampler).create(inputRate, outputRate, 35112 + 2064);
		activeResampler = optionAudioResampler;
	}
}

static size_t runUntilVideoFrame(gambatte::uint_least32_t *videoBuf, std::ptrdiff_t pitch,
	EmuAudio *audio, DelegateFunc<void()> videoFrameCallback)
{
	size_t samplesEmulated = 0;
	constexpr unsigned samplesPerRun = 2064;
	bool didOutputFrame;
	do
	{
		std::array<uint_least32_t, samplesPerRun+2064> snd;
		size_t samples = samplesPerRun;
		didOutputFrame = gbEmu.runFor(videoBuf, pitch, snd.data(), samples, videoFrameCallback) != -1;
		samplesEmulated += samples;
		if(audio)
		{
			constexpr size_t buffSize = (snd.size() / (2097152./48000.) + 1); // TODO: std::ceil() is constexpr with GCC but not Clang yet
			std::array<uint32_t, buffSize> destBuff;
			unsigned destFrames = resampler->resample((short*)destBuff.data(), (const short*)snd.data(), samples);
			assumeExpr(destFrames <= destBuff.size());
			audio->writeFrames(destBuff.data(), destFrames);
		}
	} while(!didOutputFrame);
	return samplesEmulated;
}

static void renderVideo(const EmuSystemTaskContext &taskCtx, EmuVideo &video)
{
	auto fmt = video.renderPixelFormat() == IG::PIXEL_FMT_BGRA8888 ? IG::PIXEL_FMT_BGRA8888 : IG::PIXEL_FMT_RGBA8888;
	IG::Pixmap frameBufferPix{{lcdSize, fmt}, frameBuffer};
	video.startFrameWithAltFormat(taskCtx, frameBufferPix);
}

void EmuSystem::runFrame(EmuSystemTaskContext taskCtx, EmuVideo *video, EmuAudio *audio)
{
	auto incFrameCountOnReturn = IG::scopeGuard([](){ totalFrames++; });
	auto currentFrame = totalSamples / 35112;
	if(totalFrames < currentFrame)
	{
		logMsg("unchanged video frame");
		if(video)
			video->startUnchangedFrame(taskCtx);
		return;
	}
	if(video)
	{
		totalSamples += runUntilVideoFrame(frameBuffer, gambatte::lcd_hres, audio,
			[&taskCtx, video]()
			{
				renderVideo(taskCtx, *video);
			});
	}
	else
	{
		totalSamples += runUntilVideoFrame(nullptr, gambatte::lcd_hres, audio, {});
	}
}

void EmuSystem::renderFramebuffer(EmuVideo &video)
{
	renderVideo({}, video);
}

void EmuApp::onCustomizeNavView(EmuApp::NavView &view)
{
	const Gfx::LGradientStopDesc navViewGrad[] =
	{
		{ .0, Gfx::VertexColorPixelFormat.build(.5, .5, .5, 1.) },
		{ .03, Gfx::VertexColorPixelFormat.build((8./255.) * .4, (232./255.) * .4, (222./255.) * .4, 1.) },
		{ .3, Gfx::VertexColorPixelFormat.build((8./255.) * .4, (232./255.) * .4, (222./255.) * .4, 1.) },
		{ .97, Gfx::VertexColorPixelFormat.build((0./255.) * .4, (77./255.) * .4, (74./255.) * .4, 1.) },
		{ 1., Gfx::VertexColorPixelFormat.build(.5, .5, .5, 1.) },
	};
	view.setBackgroundGradient(navViewGrad);
}

}

uint_least32_t gbcToRgb32(unsigned const bgr15)
{
	unsigned r = bgr15       & 0x1F;
	unsigned g = bgr15 >>  5 & 0x1F;
	unsigned b = bgr15 >> 10 & 0x1F;
	unsigned outR, outG, outB;
	if(EmuEx::optionFullGbcSaturation)
	{
		outR = (r * 255 + 15) / 31;
		outG = (g * 255 + 15) / 31;
		outB = (b * 255 + 15) / 31;
	}
	else
	{
		outR = (r * 13 + g * 2 + b) >> 1;
		outG = (g * 3 + b) << 1;
		outB = (r * 3 + g * 2 + b * 11) >> 1;
	}
	auto desc = EmuEx::useBgrOrder ? IG::PIXEL_DESC_BGRA8888.nativeOrder() : IG::PIXEL_DESC_RGBA8888_NATIVE;
	return desc.build(outR, outG, outB, 0u);
}
