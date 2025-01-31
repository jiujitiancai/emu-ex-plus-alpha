/*  This file is part of EmuFramework.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#include <emuframework/OptionView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppHelper.hh>
#include <emuframework/EmuVideoLayer.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/VideoImageEffect.hh>
#include "EmuOptions.hh"
#include "private.hh"
#include <imagine/base/Screen.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gfx/Renderer.hh>
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/gui/TextTableView.hh>
#include <imagine/util/format.hh>

namespace EmuEx
{

class DetectFrameRateView final: public View, public EmuAppHelper<DetectFrameRateView>
{
public:
	using DetectFrameRateDelegate = DelegateFunc<void (IG::FloatSeconds frameTime)>;
	DetectFrameRateDelegate onDetectFrameTime;
	IG::OnFrameDelegate detectFrameRate;
	IG::FrameTime totalFrameTime{};
	IG::FrameTime lastFrameTimestamp{};
	Gfx::Text fpsText;
	unsigned allTotalFrames = 0;
	unsigned callbacks = 0;
	std::vector<IG::FrameTime> frameTimeSample{};
	bool useRenderTaskTime = false;

	DetectFrameRateView(ViewAttachParams attach): View(attach),
		fpsText{{}, &defaultFace()}
	{
		defaultFace().precacheAlphaNum(attach.renderer());
		defaultFace().precache(attach.renderer(), ".");
		fpsText.setString("Preparing to detect frame rate...");
		useRenderTaskTime = !screen()->supportsTimestamps();
		frameTimeSample.reserve(std::round(screen()->frameRate() * 2.));
	}

	~DetectFrameRateView() final
	{
		window().setIntendedFrameRate(0.);
		app().setCPUNeedsLowLatency(appContext(), false);
		window().removeOnFrame(detectFrameRate);
	}

	void place() final
	{
		fpsText.compile(renderer(), projP);
	}

	bool inputEvent(const Input::Event &e) final
	{
		if(e.keyEvent() && e.asKeyEvent().pushed(Input::DefaultKey::CANCEL))
		{
			logMsg("aborted detection");
			dismiss();
			return true;
		}
		return false;
	}

	void draw(Gfx::RendererCommands &cmds) final
	{
		using namespace IG::Gfx;
		cmds.setColor(1., 1., 1., 1.);
		cmds.setCommonProgram(CommonProgram::TEX_ALPHA, projP.makeTranslate());
		fpsText.draw(cmds, projP.alignXToPixel(projP.bounds().xCenter()),
			projP.alignYToPixel(projP.bounds().yCenter()), C2DO, projP);
	}

	bool runFrameTimeDetection(IG::FrameTime timestampDiff, double slack)
	{
		const unsigned framesToTime = frameTimeSample.capacity() * 10;
		allTotalFrames++;
		frameTimeSample.emplace_back(timestampDiff);
		if(frameTimeSample.size() == frameTimeSample.capacity())
		{
			bool stableFrameTime = true;
			IG::FrameTime frameTimeTotal{};
			{
				IG::FrameTime lastFrameTime{};
				for(auto frameTime : frameTimeSample)
				{
					frameTimeTotal += frameTime;
					if(!stableFrameTime)
						continue;
					double frameTimeDiffSecs =
						std::abs(IG::FloatSeconds(lastFrameTime - frameTime).count());
					if(lastFrameTime.count() && frameTimeDiffSecs > slack)
					{
						logMsg("frame times differed by:%f", frameTimeDiffSecs);
						stableFrameTime = false;
					}
					lastFrameTime = frameTime;
				}
			}
			IG::FloatSeconds frameTimeTotalSecs = IG::FloatSeconds(frameTimeTotal);
			IG::FloatSeconds detectedFrameTime = frameTimeTotalSecs / (double)frameTimeSample.size();
			{
				waitForDrawFinished();
				if(detectedFrameTime.count())
					fpsText.setString(fmt::format("{:.2f}fps", 1. / detectedFrameTime.count()));
				else
					fpsText.setString("0fps");
				fpsText.compile(renderer(), projP);
			}
			if(stableFrameTime)
			{
				logMsg("found frame time:%f", detectedFrameTime.count());
				onDetectFrameTime(detectedFrameTime);
				dismiss();
				return false;
			}
			frameTimeSample.erase(frameTimeSample.cbegin());
			postDraw();
		}
		else
		{
			//logMsg("waiting for capacity:%zd/%zd", frameTimeSample.size(), frameTimeSample.capacity());
		}
		if(allTotalFrames >= framesToTime)
		{
			onDetectFrameTime(IG::FloatSeconds{});
			dismiss();
			return false;
		}
		else
		{
			if(useRenderTaskTime)
				postDraw();
			return true;
		}
	}

	void onAddedToController(ViewController *, const Input::Event &e) final
	{
		lastFrameTimestamp = std::chrono::duration_cast<IG::FrameTime>(IG::steadyClockTimestamp());
		detectFrameRate =
			[this](IG::FrameParams params)
			{
				const unsigned callbacksToSkip = 10;
				callbacks++;
				if(callbacks < callbacksToSkip)
				{
					if(useRenderTaskTime)
						postDraw();
					return true;
				}
				return runFrameTimeDetection(params.timestamp() - std::exchange(lastFrameTimestamp, params.timestamp()), 0.00175);
			};
		window().addOnFrame(detectFrameRate);
		app().setCPUNeedsLowLatency(appContext(), true);
	}
};

static auto makeFrameRateStr()
{
	return fmt::format("Frame Rate: {:.2f}Hz",
		EmuSystem::frameRate(EmuSystem::VIDSYS_NATIVE_NTSC));
}

static auto makeFrameRatePALStr()
{
	return fmt::format("Frame Rate (PAL): {:.2f}Hz",
		EmuSystem::frameRate(EmuSystem::VIDSYS_PAL));
}

#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
static void setFrameInterval(int interval)
{
	optionFrameInterval = interval;
	logMsg("set frame interval: %d", int(optionFrameInterval));
}
#endif

TextMenuItem::SelectDelegate VideoOptionView::setImgEffectDel(ImageEffectId val)
{
	return [this, val]()
	{
		optionImgEffect = (unsigned)val;
		if(emuVideo().image())
		{
			videoLayer->setEffect(val, app().imageEffectPixelFormat());
			app().viewController().postDrawToEmuWindows();
		}
	};
}

TextMenuItem::SelectDelegate VideoOptionView::setOverlayEffectDel(int val)
{
	return [this, val]()
	{
		optionOverlayEffect = val;
		videoLayer->setOverlay(val);
		app().viewController().postDrawToEmuWindows();
	};
}

TextMenuItem::SelectDelegate VideoOptionView::setImgEffectPixelFormatDel(PixelFormat format)
{
	return [this, format]()
	{
		optionImageEffectPixelFormat = format;
		videoLayer->setEffectFormat(app().imageEffectPixelFormat());
		app().viewController().postDrawToEmuWindows();
	};
}

TextMenuItem::SelectDelegate VideoOptionView::setRenderPixelFormatDel(PixelFormat format)
{
	return [this, format]() { app().setRenderPixelFormat(format); };
}

static const char *autoWindowPixelFormatStr(IG::ApplicationContext ctx)
{
	return ctx.defaultWindowPixelFormat() == PIXEL_RGB565 ? "RGB565" : "RGBA8888";
}

TextMenuItem::SelectDelegate VideoOptionView::setWindowDrawableConfigDel(Gfx::DrawableConfig conf)
{
	return [this, conf]()
	{
		if(!app().setWindowDrawableConfig(conf))
		{
			app().postMessage("Restart app for option to take effect");
			return;
		}
		renderPixelFormat.updateDisplayString();
		imgEffectPixelFormat.updateDisplayString();
	};
}

TextMenuItem::SelectDelegate VideoOptionView::setImageBuffersDel(int buffers)
{
	return [this, buffers]()
	{
		optionVideoImageBuffers = buffers;
		emuVideo().setImageBuffers(buffers);
	};
}

static int aspectRatioValueIndex(double val)
{
	iterateTimes(EmuSystem::aspectRatioInfos, i)
	{
		if(val == (double)EmuSystem::aspectRatioInfo[i])
		{
			return i;
		}
	}
	return -1;
}

VideoOptionView::VideoOptionView(ViewAttachParams attach, bool customMenu):
	TableView{"Video Options", attach, item},
	textureBufferMode
	{
		"GPU Copy Mode", &defaultFace(),
		0,
		textureBufferModeItem
	},
	#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
	frameIntervalItem
	{
		{"Full", &defaultFace(), [this]() { setFrameInterval(1); }},
		{"1/2", &defaultFace(), [this]() { setFrameInterval(2); }},
		{"1/3", &defaultFace(), [this]() { setFrameInterval(3); }},
		{"1/4", &defaultFace(), [this]() { setFrameInterval(4); }},
	},
	frameInterval
	{
		"Target Frame Rate", &defaultFace(),
		optionFrameInterval - 1,
		frameIntervalItem
	},
	#endif
	dropLateFrames
	{
		"Skip Late Frames", &defaultFace(),
		(bool)optionSkipLateFrames,
		[this](BoolMenuItem &item)
		{
			optionSkipLateFrames.val = item.flipBoolValue(*this);
		}
	},
	frameRate
	{
		{}, &defaultFace(),
		[this](const Input::Event &e)
		{
			pushAndShowFrameRateSelectMenu(EmuSystem::VIDSYS_NATIVE_NTSC, e);
			postDraw();
		}
	},
	frameRatePAL
	{
		{}, &defaultFace(),
		[this](const Input::Event &e)
		{
			pushAndShowFrameRateSelectMenu(EmuSystem::VIDSYS_PAL, e);
			postDraw();
		}
	},
	aspectRatio
	{
		"Aspect Ratio", &defaultFace(),
		[this](uint32_t idx, Gfx::Text &t)
		{
			if(idx == EmuSystem::aspectRatioInfos)
			{
				t.setString(fmt::format("{:.2f}", optionAspectRatio.val));
				return true;
			}
			return false;
		},
		(int)EmuSystem::aspectRatioInfos,
		aspectRatioItem
	},
	zoomItem
	{
		{"100%",                  &defaultFace(), setZoomDel(100)},
		{"90%",                   &defaultFace(), setZoomDel(90)},
		{"80%",                   &defaultFace(), setZoomDel(80)},
		{"Integer-only",          &defaultFace(), setZoomDel(optionImageZoomIntegerOnly)},
		{"Integer-only (Height)", &defaultFace(), setZoomDel(optionImageZoomIntegerOnlyY)},
		{"Custom Value", &defaultFace(),
			[this](const Input::Event &e)
			{
				app().pushAndShowNewCollectValueInputView<int>(attachParams(), e, "Input 10 to 100", "",
					[this](EmuApp &app, auto val)
					{
						if(optionImageZoom.isValidVal(val))
						{
							app.setVideoZoom(val);
							zoom.setSelected(std::size(zoomItem) - 1, *this);
							dismissPrevious();
							return true;
						}
						else
						{
							app.postErrorMessage("Value not in range");
							return false;
						}
					});
				return false;
			}
		},
	},
	zoom
	{
		"Content Zoom", &defaultFace(),
		[this](uint32_t idx, Gfx::Text &t)
		{
			if(optionImageZoom <= 100)
			{
				t.setString(fmt::format("{}%", optionImageZoom.val));
				return true;
			}
			return false;
		},
		[]()
		{
			switch(optionImageZoom.val)
			{
				case 100: return 0;
				case 90: return 1;
				case 80: return 2;
				case optionImageZoomIntegerOnly: return 3;
				case optionImageZoomIntegerOnlyY: return 4;
				default: return 5;
			}
		}(),
		zoomItem
	},
	viewportZoomItem
	{
		{"100%", &defaultFace(), setViewportZoomDel(100)},
		{"95%", &defaultFace(),  setViewportZoomDel(95)},
		{"90%", &defaultFace(),  setViewportZoomDel(90)},
		{"Custom Value", &defaultFace(),
			[this](const Input::Event &e)
			{
				app().pushAndShowNewCollectValueInputView<int>(attachParams(), e, "Input 50 to 100", "",
					[this](EmuApp &app, auto val)
					{
						if(optionViewportZoom.isValidVal(val))
						{
							app.setViewportZoom(val);
							viewportZoom.setSelected(std::size(viewportZoomItem) - 1, *this);
							dismissPrevious();
							return true;
						}
						else
						{
							app.postErrorMessage("Value not in range");
							return false;
						}
					});
				return false;
			}
		},
	},
	viewportZoom
	{
		"App Zoom", &defaultFace(),
		[this](uint32_t idx, Gfx::Text &t)
		{
			t.setString(fmt::format("{}%", optionViewportZoom.val));
			return true;
		},
		[]()
		{
			switch(optionViewportZoom.val)
			{
				case 100: return 0;
				case 95: return 1;
				case 90: return 2;
				default: return 3;
			}
		}(),
		viewportZoomItem
	},
	imgFilter
	{
		"Image Interpolation", &defaultFace(),
		(bool)optionImgFilter,
		"None", "Linear",
		[this](BoolMenuItem &item)
		{
			optionImgFilter.val = item.flipBoolValue(*this);
			videoLayer->setLinearFilter(optionImgFilter);
			app().viewController().postDrawToEmuWindows();
		}
	},
	imgEffectItem
	{
		{"Off",         &defaultFace(), setImgEffectDel(ImageEffectId::DIRECT)},
		{"hq2x",        &defaultFace(), setImgEffectDel(ImageEffectId::HQ2X)},
		{"Scale2x",     &defaultFace(), setImgEffectDel(ImageEffectId::SCALE2X)},
		{"Prescale 2x", &defaultFace(), setImgEffectDel(ImageEffectId::PRESCALE2X)}
	},
	imgEffect
	{
		"Image Effect", &defaultFace(),
		[]()
		{
			switch((ImageEffectId)optionImgEffect.val)
			{
				default: return 0;
				case ImageEffectId::HQ2X: return 1;
				case ImageEffectId::SCALE2X: return 2;
				case ImageEffectId::PRESCALE2X: return 3;
			}
		}(),
		imgEffectItem
	},
	overlayEffectItem
	{
		{"Off",          &defaultFace(), setOverlayEffectDel(0)},
		{"Scanlines",    &defaultFace(), setOverlayEffectDel(VideoImageOverlay::SCANLINES)},
		{"Scanlines 2x", &defaultFace(), setOverlayEffectDel(VideoImageOverlay::SCANLINES_2)},
		{"CRT Mask",     &defaultFace(), setOverlayEffectDel(VideoImageOverlay::CRT)},
		{"CRT",          &defaultFace(), setOverlayEffectDel(VideoImageOverlay::CRT_RGB)},
		{"CRT 2x",       &defaultFace(), setOverlayEffectDel(VideoImageOverlay::CRT_RGB_2)}
	},
	overlayEffect
	{
		"Overlay Effect", &defaultFace(),
		[]()
		{
			switch(optionOverlayEffect)
			{
				default: return 0;
				case VideoImageOverlay::SCANLINES: return 1;
				case VideoImageOverlay::SCANLINES_2: return 2;
				case VideoImageOverlay::CRT: return 3;
				case VideoImageOverlay::CRT_RGB: return 4;
				case VideoImageOverlay::CRT_RGB_2: return 5;
			}
		}(),
		overlayEffectItem
	},
	overlayEffectLevelItem
	{
		{"100%", &defaultFace(), setOverlayEffectLevelDel(100)},
		{"75%",  &defaultFace(), setOverlayEffectLevelDel(75)},
		{"50%",  &defaultFace(), setOverlayEffectLevelDel(50)},
		{"25%",  &defaultFace(), setOverlayEffectLevelDel(25)},
		{"Custom Value", &defaultFace(),
			[this](const Input::Event &e)
			{
				app().pushAndShowNewCollectValueInputView<int>(attachParams(), e, "Input 0 to 100", "",
					[this](EmuApp &app, auto val)
					{
						if(optionOverlayEffectLevel.isValidVal(val))
						{
							app.setOverlayEffectLevel(*videoLayer, val);
							overlayEffectLevel.setSelected(std::size(overlayEffectLevelItem) - 1, *this);
							dismissPrevious();
							return true;
						}
						else
						{
							app.postErrorMessage("Value not in range");
							return false;
						}
					});
				return false;
			}
		},
	},
	overlayEffectLevel
	{
		"Overlay Effect Level", &defaultFace(),
		[this](uint32_t idx, Gfx::Text &t)
		{
			t.setString(fmt::format("{}%", optionOverlayEffectLevel.val));
			return true;
		},
		[]()
		{
			switch(optionOverlayEffectLevel)
			{
				case 100: return 0;
				case 75: return 1;
				case 50: return 2;
				case 25: return 3;
				default: return 4;
			}
		}(),
		overlayEffectLevelItem
	},
	imgEffectPixelFormatItem
	{
		{"Auto (Match display format)", &defaultFace(), setImgEffectPixelFormatDel(PIXEL_NONE)},
		{"RGBA8888",                    &defaultFace(), setImgEffectPixelFormatDel(PIXEL_RGBA8888)},
		{"RGB565",                      &defaultFace(), setImgEffectPixelFormatDel(PIXEL_RGB565)},
	},
	imgEffectPixelFormat
	{
		"Effect Color Format", &defaultFace(),
		[this](int idx, Gfx::Text &t)
		{
			if(idx == 0)
			{
				t.setString(app().imageEffectPixelFormat().name());
				return true;
			}
			else
				return false;
		},
		[]()
		{
			switch(optionImageEffectPixelFormat.val)
			{
				default: return 0;
				case PIXEL_RGBA8888: return 1;
				case PIXEL_RGB565: return 2;
			}
		}(),
		imgEffectPixelFormatItem
	},
	windowPixelFormat
	{
		"Display Color Format", &defaultFace(),
		[this](int idx, Gfx::Text &t)
		{
			if(idx == 0)
			{
				t.setString(autoWindowPixelFormatStr(appContext()));
				return true;
			}
			else
				return false;
		},
		0,
		windowPixelFormatItem
	},
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_X11
	secondDisplay
	{
		"2nd Window (for testing only)", &defaultFace(),
		false,
		[this](BoolMenuItem &item)
		{
			app().viewController().setEmuViewOnExtraWindow(item.flipBoolValue(*this), appContext().mainScreen());
		}
	},
	#endif
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_MULTI_SCREEN
	showOnSecondScreen
	{
		"External Screen", &defaultFace(),
		(bool)optionShowOnSecondScreen,
		"OS Managed", "Emu Content",
		[this](BoolMenuItem &item)
		{
			optionShowOnSecondScreen = item.flipBoolValue(*this);
			if(appContext().screens().size() > 1)
				app().viewController().setEmuViewOnExtraWindow(optionShowOnSecondScreen, *appContext().screens()[1]);
		}
	},
	#endif
	imageBuffersItem
	{
		{"Auto",                                     &defaultFace(), setImageBuffersDel(0)},
		{"1 (Syncs GPU each frame, less input lag)", &defaultFace(), setImageBuffersDel(1)},
		{"2 (More stable, may add 1 frame of lag)",  &defaultFace(), setImageBuffersDel(2)},
	},
	imageBuffers
	{
		"Image Buffers", &defaultFace(),
		[this](int idx, Gfx::Text &t)
		{
			t.setString(emuVideo().imageBuffers() == 1 ? "1" : "2");
			return true;
		},
		[]()
		{
			switch(optionVideoImageBuffers.val)
			{
				default: return 0;
				case 1: return 1;
				case 2: return 2;
			}
		}(),
		imageBuffersItem
	},
	renderPixelFormatItem
	{
		{"Auto (Match display format)", &defaultFace(), setRenderPixelFormatDel(IG::PIXEL_NONE)},
		{"RGBA8888",                    &defaultFace(), setRenderPixelFormatDel(IG::PIXEL_RGBA8888)},
		{"RGB565",                      &defaultFace(), setRenderPixelFormatDel(IG::PIXEL_RGB565)},
	},
	renderPixelFormat
	{
		"Render Color Format", &defaultFace(),
		[this](int idx, Gfx::Text &t)
		{
			if(idx == 0)
			{
				t.setString(emuVideo().internalRenderPixelFormat().name());
				return true;
			}
			return false;
		},
		[this]()
		{
			switch(app().renderPixelFormat())
			{
				default: return 0;
				case IG::PIXEL_RGBA8888: return 1;
				case IG::PIXEL_RGB565: return 2;
			}
		}(),
		renderPixelFormatItem
	},
	presentationTime
	{
		"Reduce Compositor Lag", &defaultFace(),
		app().viewController().usePresentationTime(),
		[this](BoolMenuItem &item)
		{
			app().viewController().setUsePresentationTime(item.flipBoolValue(*this));
		}
	},
	visualsHeading{"Visuals", &defaultBoldFace()},
	screenShapeHeading{"Screen Shape", &defaultBoldFace()},
	advancedHeading{"Advanced", &defaultBoldFace()},
	systemSpecificHeading{"System-specific", &defaultBoldFace()}
{
	windowPixelFormatItem.emplace_back("Auto", &defaultFace(), setWindowDrawableConfigDel({}));
	{
		auto descs = renderer().supportedDrawableConfigs();
		for(auto desc: descs)
		{
			windowPixelFormatItem.emplace_back(desc.name, &defaultFace(), setWindowDrawableConfigDel(desc.config));
		}
		windowPixelFormat.setSelected(IG::findIndex(descs, app().windowDrawableConfig()) + 1);
	}
	iterateTimes(EmuSystem::aspectRatioInfos, i)
	{
		aspectRatioItem.emplace_back(EmuSystem::aspectRatioInfo[i].name, &defaultFace(),
			[this, i]()
			{
				app().setVideoAspectRatio((double)EmuSystem::aspectRatioInfo[i]);
			});
	}
	aspectRatioItem.emplace_back("Custom Value", &defaultFace(),
		[this](const Input::Event &e)
		{
			app().pushAndShowNewCollectValueInputView<std::pair<double, double>>(attachParams(), e,
				"Input decimal or fraction", "",
				[this](EmuApp &app, auto val)
				{
					double ratio = val.first / val.second;
					if(optionAspectRatio.isValidVal(ratio))
					{
						app.setVideoAspectRatio(ratio);
						if(auto idx = aspectRatioValueIndex(ratio);
							idx != -1)
						{
							aspectRatio.setSelected(idx, *this);
						}
						else
						{
							aspectRatio.setSelected(std::size(aspectRatioItem) - 1, *this);
						}
						dismissPrevious();
						return true;
					}
					else
					{
						app.postErrorMessage("Value not in range");
						return false;
					}
				});
			return false;
		});
	if(auto idx = aspectRatioValueIndex(optionAspectRatio);
		idx != -1)
	{
		aspectRatio.setSelected(idx, *this);
	}
	textureBufferModeItem.emplace_back("Auto (Set optimal mode)", &defaultFace(),
		[this](View &view)
		{
			optionTextureBufferMode = 0;
			auto defaultMode = renderer().makeValidTextureBufferMode();
			emuVideo().setTextureBufferMode(defaultMode);
			textureBufferMode.setSelected(IG::findIndex(renderer().textureBufferModes(), defaultMode) + 1);
			view.dismiss();
			return false;
		});
	{
		auto descs = renderer().textureBufferModes();
		for(auto desc: descs)
		{
			textureBufferModeItem.emplace_back(desc.name, &defaultFace(),
				[this, mode = desc.mode]()
				{
					optionTextureBufferMode = (uint8_t)mode;
					emuVideo().setTextureBufferMode(mode);
				});
		}
		textureBufferMode.setSelected(IG::findIndex(descs, renderer().makeValidTextureBufferMode((Gfx::TextureBufferMode)optionTextureBufferMode.val)) + 1);
	}
	if(!customMenu)
	{
		loadStockItems();
	}
}

void VideoOptionView::loadStockItems()
{
	#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
	item.emplace_back(&frameInterval);
	#endif
	item.emplace_back(&dropLateFrames);
	if(!optionFrameRate.isConst)
	{
		frameRate.setName(makeFrameRateStr());
		item.emplace_back(&frameRate);
	}
	if(!optionFrameRatePAL.isConst)
	{
		frameRatePAL.setName(makeFrameRatePALStr());
		item.emplace_back(&frameRatePAL);
	}
	item.emplace_back(&visualsHeading);
	item.emplace_back(&imgFilter);
	item.emplace_back(&imgEffect);
	item.emplace_back(&overlayEffect);
	item.emplace_back(&overlayEffectLevel);
	item.emplace_back(&screenShapeHeading);
	item.emplace_back(&zoom);
	item.emplace_back(&viewportZoom);
	item.emplace_back(&aspectRatio);
	item.emplace_back(&advancedHeading);
	item.emplace_back(&textureBufferMode);
	if(windowPixelFormatItem.size() > 2)
	{
		item.emplace_back(&windowPixelFormat);
	}
	if(EmuSystem::canRenderRGBA8888)
		item.emplace_back(&renderPixelFormat);
	item.emplace_back(&imgEffectPixelFormat);
	if(!optionVideoImageBuffers.isConst)
		item.emplace_back(&imageBuffers);
	if(IG::used(presentationTime) && renderer().supportsPresentationTime())
		item.emplace_back(&presentationTime);
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_X11
	item.emplace_back(&secondDisplay);
	#endif
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_MULTI_SCREEN
	if(!optionShowOnSecondScreen.isConst)
	{
		item.emplace_back(&showOnSecondScreen);
	}
	#endif
}

void VideoOptionView::setEmuVideoLayer(EmuVideoLayer &videoLayer_)
{
	videoLayer = &videoLayer_;
}

bool VideoOptionView::onFrameTimeChange(EmuSystem::VideoSystem vidSys, IG::FloatSeconds time)
{
	auto wantedTime = time;
	if(!time.count())
	{
		wantedTime = app().viewController().emuWindowScreen()->frameTime();
	}
	if(!EmuSystem::setFrameTime(vidSys, wantedTime))
	{
		app().postMessage(4, true, fmt::format("{:.2f}Hz not in valid range", 1. / wantedTime.count()));
		return false;
	}
	EmuSystem::configFrameTime(optionSoundRate);
	if(vidSys == EmuSystem::VIDSYS_NATIVE_NTSC)
	{
		optionFrameRate = time.count();
		frameRate.compile(makeFrameRateStr(), renderer(), projP);
	}
	else
	{
		optionFrameRatePAL = time.count();
		frameRatePAL.compile(makeFrameRatePALStr(), renderer(), projP);
	}
	return true;
}

void VideoOptionView::pushAndShowFrameRateSelectMenu(EmuSystem::VideoSystem vidSys, const Input::Event &e)
{
	const bool includeFrameRateDetection = !Config::envIsIOS;
	auto multiChoiceView = makeViewWithName<TextTableView>("Frame Rate", includeFrameRateDetection ? 4 : 3);
	multiChoiceView->appendItem("Set with screen's reported rate",
		[this, vidSys](View &view)
		{
			if(!app().viewController().emuWindowScreen()->frameRateIsReliable())
			{
				#ifdef __ANDROID__
				if(appContext().androidSDK() <= 10)
				{
					app().postErrorMessage("Many Android 2.3 devices mis-report their refresh rate, "
						"using the detected or default rate may give better results");
				}
				else
				#endif
				{
					app().postErrorMessage("Reported rate potentially unreliable, "
						"using the detected or default rate may give better results");
				}
			}
			if(onFrameTimeChange(vidSys, {}))
				view.dismiss();
		});
	multiChoiceView->appendItem("Set default rate",
		[this, vidSys](View &view)
		{
			onFrameTimeChange(vidSys, EmuSystem::defaultFrameTime(vidSys));
			view.dismiss();
		});
	multiChoiceView->appendItem("Set custom rate",
		[this, vidSys](const Input::Event &e)
		{
			app().pushAndShowNewCollectValueInputView<std::pair<double, double>>(attachParams(), e,
				"Input decimal or fraction", "",
				[this, vidSys](EmuApp &, auto val)
				{
					if(onFrameTimeChange(vidSys, IG::FloatSeconds{val.second / val.first}))
					{
						dismissPrevious();
						return true;
					}
					else
						return false;
				});
		});
	if(includeFrameRateDetection)
	{
		multiChoiceView->appendItem("Detect screen's rate and set",
			[this, vidSys](const Input::Event &e)
			{
				window().setIntendedFrameRate(vidSys == EmuSystem::VIDSYS_NATIVE_NTSC ? 60. : 50.);
				auto frView = makeView<DetectFrameRateView>();
				frView->onDetectFrameTime =
					[this, vidSys](IG::FloatSeconds frameTime)
					{
						if(frameTime.count())
						{
							if(onFrameTimeChange(vidSys, frameTime))
								dismissPrevious();
						}
						else
						{
							app().postErrorMessage("Detected rate too unstable to use");
						}
					};
				pushAndShowModal(std::move(frView), e);
			});
	}
	pushAndShow(std::move(multiChoiceView), e);
}

TextMenuItem::SelectDelegate VideoOptionView::setZoomDel(uint8_t val)
{
	return [this, val]() { app().setVideoZoom(val); };
}

TextMenuItem::SelectDelegate VideoOptionView::setViewportZoomDel(uint8_t val)
{
	return [this, val]() { app().setViewportZoom(val); };
}

TextMenuItem::SelectDelegate VideoOptionView::setOverlayEffectLevelDel(uint8_t val)
{
	return [this, val]() { app().setOverlayEffectLevel(*videoLayer, val); };
}

EmuVideo &VideoOptionView::emuVideo() const
{
	return videoLayer->emuVideo();
}

}
