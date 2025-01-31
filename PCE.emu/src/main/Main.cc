/*  This file is part of PCE.emu.

	PCE.emu is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	PCE.emu is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with PCE.emu.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "main"
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAudio.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/EmuInput.hh>
#include <emuframework/EmuAppInlines.hh>
#include "internal.hh"
#include <imagine/fs/FS.hh>
#include <imagine/util/ScopeGuard.hh>
#include <imagine/util/format.hh>
#include <imagine/util/string.h>
#include <mednafen/cdrom/CDInterface.h>
#include <mednafen/state-driver.h>
#include <mednafen/hash/md5.h>
#include <mednafen/MemoryStream.h>
#include <mednafen/pce_fast/pce.h>
#include <mednafen/pce_fast/huc.h>
#include <mednafen/pce_fast/vdc.h>
#include <mednafen/pce_fast/pcecd_drive.h>

namespace EmuEx
{

const char *EmuSystem::creditsViewStr = CREDITS_INFO_STRING "(c) 2011-2022\nRobert Broglia\nwww.explusalpha.com\n\nPortions (c) the\nMednafen Team\nmednafen.sourceforge.net";
FS::PathString sysCardPath{};
static std::vector<CDInterface *> CDInterfaces;
static const unsigned vidBufferX = 512, vidBufferY = 242;
alignas(8) static uint32_t pixBuff[vidBufferX*vidBufferY]{};
static IG::Pixmap mSurfacePix;
std::array<uint16, 5> inputBuff{}; // 5 gamepad buffers
static bool prevUsing263Lines = false;
IG::ApplicationContext appCtx{};

static MDFN_Surface pixmapToMDFNSurface(IG::Pixmap pix)
{
	MDFN_PixelFormat fmt =
		[&]()
		{
			switch(pix.format().id())
			{
				case IG::PIXEL_BGRA8888: return MDFN_PixelFormat::ARGB32_8888;
				case IG::PIXEL_RGBA8888: return MDFN_PixelFormat::ABGR32_8888;
				case IG::PIXEL_RGB565: return MDFN_PixelFormat::RGB16_565;
				default:
					bug_unreachable("format id == %d", pix.format().id());
					return MDFN_PixelFormat::ABGR32_8888;
			};
		}();
	return {pix.pixel({0,0}), pix.w(), pix.h(), pix.pitchPixels(), fmt};
}

bool hasHuCardExtension(std::string_view name)
{
	return IG::stringEndsWithAny(name, ".pce", ".sgx", ".PCE", ".SGX");
}

static bool hasCDExtension(std::string_view name)
{
	return IG::stringEndsWithAny(name, ".toc", ".cue", ".ccd", ".TOC", ".CUE", ".CCD");
}

static bool hasPCEWithCDExtension(std::string_view name)
{
	return hasHuCardExtension(name) || hasCDExtension(name);
}

const char *EmuSystem::shortSystemName()
{
	return "PCE-TG16";
}

const char *EmuSystem::systemName()
{
	return "PC Engine (TurboGrafx-16)";
}

EmuSystem::NameFilterFunc EmuSystem::defaultFsFilter = hasPCEWithCDExtension;
EmuSystem::NameFilterFunc EmuSystem::defaultBenchmarkFsFilter = hasHuCardExtension;

void EmuSystem::saveBackupMem(IG::ApplicationContext ctx) // for manually saving when not closing game
{
	if(gameIsRunning())
	{
		logMsg("saving backup memory");
		// TODO: fix iOS permissions if needed
		MDFN_IEN_PCE_FAST::HuC_SaveNV();
	}
}

static char saveSlotCharPCE(int slot)
{
	switch(slot)
	{
		case -1: return 'q';
		case 0 ... 9: return '0' + slot;
		default: bug_unreachable("slot == %d", slot); return 0;
	}
}

FS::FileString EmuSystem::stateFilename(int slot, std::string_view name)
{
	return IG::format<FS::FileString>("{}.{}.nc{}", name, md5_context::asciistr(MDFNGameInfo->MD5, 0), saveSlotCharPCE(slot));
}

void EmuSystem::closeSystem(IG::ApplicationContext ctx)
{
	emuSys->CloseGame();
	if(CDInterfaces.size())
	{
		assert(CDInterfaces.size() == 1);
		delete CDInterfaces[0];
		CDInterfaces.clear();
	}
}

static void writeCDMD5()
{
	CDUtility::TOC toc;
	md5_context layout_md5;

	CDInterfaces[0]->ReadTOC(&toc);

	layout_md5.starts();

	layout_md5.update_u32_as_lsb(toc.first_track);
	layout_md5.update_u32_as_lsb(toc.last_track);
	layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

	for(uint32 track = toc.first_track; track <= toc.last_track; track++)
	{
		layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
		layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
	}

	uint8 LayoutMD5[16];
	layout_md5.finish(LayoutMD5);

	memcpy(emuSys->MD5, LayoutMD5, 16);
}

unsigned EmuSystem::multiresVideoBaseX() { return 512; }

void EmuSystem::loadGame(IG::ApplicationContext ctx, IO &io, EmuSystemCreateParams, OnLoadProgressDelegate)
{
	emuSys->name = std::string{EmuSystem::contentName()};
	auto unloadCD = IG::scopeGuard(
		[]()
		{
			if(CDInterfaces.size())
			{
				assert(CDInterfaces.size() == 1);
				delete CDInterfaces[0];
				CDInterfaces.clear();
			}
		});
	if(hasCDExtension(contentFileName()))
	{
		if(contentDirectory().empty())
		{
			throwMissingContentDirError();
		}
		if(sysCardPath.empty() || !ctx.fileUriExists(sysCardPath))
		{
			throw std::runtime_error("No System Card Set");
		}
		CDInterfaces.reserve(1);
		CDInterfaces.push_back(CDInterface::Open(&NVFS, contentLocation().data(), false, 0));
		writeCDMD5();
		emuSys->LoadCD(&CDInterfaces);
		PCECD_Drive_SetDisc(false, CDInterfaces[0]);
	}
	else
	{
		static constexpr size_t maxRomSize = 0x300000;
		auto stream = std::make_unique<MemoryStream>(maxRomSize, true);
		auto size = io.read(stream->map(), stream->map_size());
		if(size <= 0)
			throwFileReadError();
		stream->setSize(size);
		MDFNFILE fp(&NVFS, std::move(stream), contentFileName().data());
		GameFile gf{fp.active_vfs(), fp.active_dir_path(), fp.stream(), fp.ext, fp.fbase};
		emuSys->Load(&gf);
	}
	//logMsg("%d input ports", MDFNGameInfo->InputInfo->InputPorts);
	iterateTimes(5, i)
	{
		emuSys->SetInput(i, "gamepad", (uint8*)&inputBuff[i]);
	}
	unloadCD.cancel();
}

bool EmuSystem::onVideoRenderFormatChange(EmuVideo &, IG::PixelFormat fmt)
{
	mSurfacePix = {{{vidBufferX, vidBufferY}, fmt}, pixBuff};
	EmulateSpecStruct espec{};
	auto mSurface = pixmapToMDFNSurface(mSurfacePix);
	espec.surface = &mSurface;
	MDFN_IEN_PCE_FAST::applyVideoFormat(&espec);
	return false;
}

void EmuSystem::configAudioRate(IG::FloatSeconds frameTime, uint32_t rate)
{
	EmulateSpecStruct espec{};
	const bool using263Lines = vce.CR & 0x04;
	prevUsing263Lines = using263Lines;
	const double rateWith263Lines = 7159090.90909090 / 455 / 263;
	const double rateWith262Lines = 7159090.90909090 / 455 / 262;
	double systemFrameRate = using263Lines ? rateWith263Lines : rateWith262Lines;
	espec.SoundRate = std::round(rate * (systemFrameRate * frameTime.count()));
	logMsg("emu sound rate:%f, 263 lines:%d", (double)espec.SoundRate, using263Lines);
	MDFN_IEN_PCE_FAST::applySoundFormat(&espec);
}

void EmuSystem::runFrame(EmuSystemTaskContext taskCtx, EmuVideo *video, EmuAudio *audio)
{
	unsigned maxFrames = 48000/54;
	int16 audioBuff[maxFrames*2];
	EmulateSpecStruct espec{};
	if(audio)
	{
		espec.SoundBuf = audioBuff;
		espec.SoundBufMaxSize = maxFrames;
		const bool using263Lines = vce.CR & 0x04;
		if(prevUsing263Lines != using263Lines) [[unlikely]]
		{
			configFrameTime(audio->format().rate);
		}
	}
	espec.taskCtx = taskCtx;
	espec.video = video;
	espec.skip = !video;
	auto mSurface = pixmapToMDFNSurface(mSurfacePix);
	espec.surface = &mSurface;
	int32 lineWidth[242];
	espec.LineWidths = lineWidth;
	emuSys->Emulate(&espec);
	if(audio)
	{
		assert((unsigned)espec.SoundBufSize <= audio->format().bytesToFrames(sizeof(audioBuff)));
		audio->writeFrames((uint8_t*)audioBuff, espec.SoundBufSize);
	}
}

void EmuSystem::reset(ResetMode mode)
{
	assert(gameIsRunning());
	MDFN_IEN_PCE_FAST::PCE_Power();
}

void EmuSystem::saveState(IG::CStringView path)
{
	if(!MDFNI_SaveState(path, 0, 0, 0, 0))
		throwFileWriteError();
}

void EmuSystem::loadState(IG::CStringView path)
{
	if(!MDFNI_LoadState(path, 0))
		throwFileReadError();
}

void EmuApp::onCustomizeNavView(EmuApp::NavView &view)
{
	const Gfx::LGradientStopDesc navViewGrad[] =
	{
		{ .0, Gfx::VertexColorPixelFormat.build(.5, .5, .5, 1.) },
		{ .03, Gfx::VertexColorPixelFormat.build((255./255.) * .4, (104./255.) * .4, (31./255.) * .4, 1.) },
		{ .3, Gfx::VertexColorPixelFormat.build((255./255.) * .4, (104./255.) * .4, (31./255.) * .4, 1.) },
		{ .97, Gfx::VertexColorPixelFormat.build((85./255.) * .4, (35./255.) * .4, (10./255.) * .4, 1.) },
		{ 1., Gfx::VertexColorPixelFormat.build(.5, .5, .5, 1.) },
	};
	view.setBackgroundGradient(navViewGrad);
}

void EmuSystem::onInit(IG::ApplicationContext ctx)
{
	appCtx = ctx;
}

}

namespace Mednafen
{

template <class Pixel>
static void renderMultiresOutput(EmulateSpecStruct spec, IG::Pixmap srcPix, int multiResOutputWidth)
{
	int pixHeight = spec.DisplayRect.h;
	auto img = spec.video->startFrameWithFormat(spec.taskCtx, {{multiResOutputWidth, pixHeight}, srcPix.format()});
	auto destPixAddr = (Pixel*)img.pixmap().data();
	auto lineWidth = spec.LineWidths + spec.DisplayRect.y;
	if(multiResOutputWidth == 1024)
	{
		// scale 256x4, 341x3 + 1x4, 512x2
		iterateTimes(pixHeight, h)
		{
			auto srcPixAddr = (Pixel*)srcPix.pixel({0,(int)h});
			int width = lineWidth[h];
			switch(width)
			{
				bdefault:
					bug_unreachable("width == %d", width);
				bcase 256:
				{
					iterateTimes(256, w)
					{
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr++;
					}
				}
				bcase 341:
				{
					iterateTimes(340, w)
					{
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr++;
					}
					*destPixAddr++ = *srcPixAddr;
					*destPixAddr++ = *srcPixAddr;
					*destPixAddr++ = *srcPixAddr;
					*destPixAddr++ = *srcPixAddr++;
				}
				bcase 512:
				{
					iterateTimes(512, w)
					{
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr++;
					}
				}
			}
			destPixAddr += img.pixmap().paddingPixels();
		}
	}
	else // 512 width
	{
		iterateTimes(pixHeight, h)
		{
			auto srcPixAddr = (Pixel*)srcPix.pixel({0,(int)h});
			int width = lineWidth[h];
			switch(width)
			{
				bdefault:
					bug_unreachable("width == %d", width);
				bcase 256:
				{
					iterateTimes(256, w)
					{
						*destPixAddr++ = *srcPixAddr;
						*destPixAddr++ = *srcPixAddr++;
					}
				}
				bcase 512:
				{
					memcpy(destPixAddr, srcPixAddr, 512 * sizeof(Pixel));
					destPixAddr += 512;
					srcPixAddr += 512;
				}
			}
			destPixAddr += img.pixmap().paddingPixels();
		}
	}
	img.endFrame();
}

void MDFND_commitVideoFrame(EmulateSpecStruct *espec)
{
	const auto spec = *espec;
	int pixHeight = spec.DisplayRect.h;
	bool uses256 = false;
	bool uses341 = false;
	bool uses512 = false;
	for(int i = spec.DisplayRect.y; i < spec.DisplayRect.y + pixHeight; i++)
	{
		int w = spec.LineWidths[i];
		assumeExpr(w == 256 || w == 341 || w == 512);
		switch(w)
		{
			bcase 256: uses256 = true;
			bcase 341: uses341 = true;
			bcase 512: uses512 = true;
		}
	}
	int pixWidth = 256;
	int multiResOutputWidth = 0;
	if(uses512)
	{
		pixWidth = 512;
		if(uses341)
		{
			multiResOutputWidth = 1024;
		}
		else if(uses256)
		{
			multiResOutputWidth = 512;
		}
	}
	else if(uses341)
	{
		pixWidth = 341;
		if(uses256)
		{
			multiResOutputWidth = 1024;
		}
	}
	IG::Pixmap srcPix = EmuEx::mSurfacePix.subView(
		{spec.DisplayRect.x, spec.DisplayRect.y},
		{pixWidth, pixHeight});
	if(multiResOutputWidth)
	{
		if(srcPix.format() == IG::PIXEL_RGB565)
			renderMultiresOutput<uint16_t>(spec, srcPix, multiResOutputWidth);
		else
			renderMultiresOutput<uint32_t>(spec, srcPix, multiResOutputWidth);
	}
	else
	{
		spec.video->startFrameWithFormat(espec->taskCtx, srcPix);
	}
}

}
