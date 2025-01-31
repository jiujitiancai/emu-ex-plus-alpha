#pragma once

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

#include <emuframework/EmuSystem.hh>
#include <emuframework/EmuAppHelper.hh>
#include <imagine/gui/TableView.hh>
#include <imagine/gui/MenuItem.hh>
#include <imagine/audio/Manager.hh>
#include <imagine/util/container/ArrayList.hh>
#include <memory>

namespace IG
{
class TextTableView;
}

namespace IG::Gfx
{
struct DrawableConfig;
}

namespace EmuEx
{

using namespace IG;
class EmuVideoLayer;
class EmuAudio;
enum class ImageEffectId : uint8_t;

class OptionCategoryView : public TableView, public EmuAppHelper<OptionCategoryView>
{
public:
	OptionCategoryView(ViewAttachParams attach, EmuAudio &audio, EmuVideoLayer &videoLayer);

protected:
	TextMenuItem subConfig[5];
};

class VideoOptionView : public TableView, public EmuAppHelper<VideoOptionView>
{
public:
	VideoOptionView(ViewAttachParams attach, bool customMenu = false);
	void loadStockItems();
	void setEmuVideoLayer(EmuVideoLayer &videoLayer);

protected:
	static constexpr unsigned MAX_ASPECT_RATIO_ITEMS = 5;
	EmuVideoLayer *videoLayer{};

	StaticArrayList<TextMenuItem, 5> textureBufferModeItem{};
	MultiChoiceMenuItem textureBufferMode;
	#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
	TextMenuItem frameIntervalItem[4];
	MultiChoiceMenuItem frameInterval;
	#endif
	BoolMenuItem dropLateFrames;
	TextMenuItem frameRate;
	TextMenuItem frameRatePAL;
	StaticArrayList<TextMenuItem, MAX_ASPECT_RATIO_ITEMS> aspectRatioItem;
	MultiChoiceMenuItem aspectRatio;
	TextMenuItem zoomItem[6];
	MultiChoiceMenuItem zoom;
	TextMenuItem viewportZoomItem[4];
	MultiChoiceMenuItem viewportZoom;
	BoolMenuItem imgFilter;
	TextMenuItem imgEffectItem[4];
	MultiChoiceMenuItem imgEffect;
	TextMenuItem overlayEffectItem[6];
	MultiChoiceMenuItem overlayEffect;
	TextMenuItem overlayEffectLevelItem[5];
	MultiChoiceMenuItem overlayEffectLevel;
	TextMenuItem imgEffectPixelFormatItem[3];
	MultiChoiceMenuItem imgEffectPixelFormat;
	StaticArrayList<TextMenuItem, 4> windowPixelFormatItem{};
	MultiChoiceMenuItem windowPixelFormat;
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_X11
	BoolMenuItem secondDisplay;
	#endif
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_MULTI_SCREEN
	BoolMenuItem showOnSecondScreen;
	#endif
	TextMenuItem imageBuffersItem[3];
	MultiChoiceMenuItem imageBuffers;
	TextMenuItem renderPixelFormatItem[3];
	MultiChoiceMenuItem renderPixelFormat;
	IG_UseMemberIf(Config::envIsAndroid, BoolMenuItem, presentationTime);
	TextHeadingMenuItem visualsHeading;
	TextHeadingMenuItem screenShapeHeading;
	TextHeadingMenuItem advancedHeading;
	TextHeadingMenuItem systemSpecificHeading;
	StaticArrayList<MenuItem*, 31> item{};

	void pushAndShowFrameRateSelectMenu(EmuSystem::VideoSystem, const Input::Event &);
	bool onFrameTimeChange(EmuSystem::VideoSystem vidSys, IG::FloatSeconds time);
	TextMenuItem::SelectDelegate setOverlayEffectLevelDel(uint8_t val);
	TextMenuItem::SelectDelegate setZoomDel(uint8_t val);
	TextMenuItem::SelectDelegate setViewportZoomDel(uint8_t val);
	TextMenuItem::SelectDelegate setImgEffectDel(ImageEffectId val);
	TextMenuItem::SelectDelegate setOverlayEffectDel(int val);
	TextMenuItem::SelectDelegate setRenderPixelFormatDel(IG::PixelFormat);
	TextMenuItem::SelectDelegate setImgEffectPixelFormatDel(IG::PixelFormat);
	TextMenuItem::SelectDelegate setWindowDrawableConfigDel(Gfx::DrawableConfig);
	TextMenuItem::SelectDelegate setImageBuffersDel(int buffers);
	EmuVideo &emuVideo() const;
};

class AudioOptionView : public TableView, public EmuAppHelper<AudioOptionView>
{
public:
	AudioOptionView(ViewAttachParams attach, bool customMenu = false);
	void loadStockItems();
	void setEmuAudio(EmuAudio &audio);

protected:
	static constexpr unsigned MAX_APIS = 2;
	EmuAudio *audio{};

	BoolMenuItem snd;
	BoolMenuItem soundDuringFastForward;
	TextMenuItem soundVolumeItem[4];
	MultiChoiceMenuItem soundVolume;
	TextMenuItem soundBuffersItem[7];
	MultiChoiceMenuItem soundBuffers;
	BoolMenuItem addSoundBuffersOnUnderrun;
	StaticArrayList<TextMenuItem, 5> audioRateItem{};
	MultiChoiceMenuItem audioRate;
	IG_UseMemberIf(IG::Audio::Manager::HAS_SOLO_MIX, BoolMenuItem, audioSoloMix){};
	#ifdef CONFIG_AUDIO_MULTIPLE_SYSTEM_APIS
	StaticArrayList<TextMenuItem, MAX_APIS + 1> apiItem{};
	MultiChoiceMenuItem api;
	#endif
	StaticArrayList<MenuItem*, 15> item{};

	void updateRateItem();
	unsigned idxOfAPI(IG::Audio::Api api, std::vector<IG::Audio::ApiDesc> apiVec);
	TextMenuItem::SelectDelegate setRateDel(uint32_t val);
	TextMenuItem::SelectDelegate setBuffersDel(int val);
	TextMenuItem::SelectDelegate setVolumeDel(uint8_t val);
};

class SystemOptionView : public TableView, public EmuAppHelper<SystemOptionView>
{
public:
	SystemOptionView(ViewAttachParams attach, bool customMenu = false);
	void loadStockItems();

protected:
	TextMenuItem autoSaveStateItem[4];
	MultiChoiceMenuItem autoSaveState;
	BoolMenuItem confirmAutoLoadState;
	BoolMenuItem confirmOverwriteState;
	TextMenuItem savePath;
	static constexpr unsigned MIN_FAST_FORWARD_SPEED = 2;
	TextMenuItem fastForwardSpeedItem[6];
	MultiChoiceMenuItem fastForwardSpeed;
	#if defined __ANDROID__
	BoolMenuItem performanceMode;
	#endif
	StaticArrayList<MenuItem*, 24> item{};

	void onSavePathChange(std::string_view path);
	virtual bool onFirmwarePathChange(IG::CStringView path, bool isDir);
	std::unique_ptr<TextTableView> makeFirmwarePathMenu(IG::utf16String name, bool allowFiles = false, unsigned extraItemsHint = 0);
	void pushAndShowFirmwarePathMenu(IG::utf16String name, const Input::Event &, bool allowFiles = false);
	void pushAndShowFirmwareFilePathMenu(IG::utf16String name, const Input::Event &);
};

class GUIOptionView : public TableView, public EmuAppHelper<GUIOptionView>
{
public:
	GUIOptionView(ViewAttachParams attach, bool customMenu = false);
	void loadStockItems();

protected:
	BoolMenuItem pauseUnfocused;
	TextMenuItem fontSizeItem[10];
	MultiChoiceMenuItem fontSize;
	BoolMenuItem notificationIcon;
	TextMenuItem statusBarItem[3];
	MultiChoiceMenuItem statusBar;
	TextMenuItem lowProfileOSNavItem[3];
	MultiChoiceMenuItem lowProfileOSNav;
	TextMenuItem hideOSNavItem[3];
	MultiChoiceMenuItem hideOSNav;
	BoolMenuItem idleDisplayPowerSave;
	BoolMenuItem navView;
	BoolMenuItem backNav;
	BoolMenuItem systemActionsIsDefaultMenu;
	BoolMenuItem showBundledGames;
	BoolMenuItem showBluetoothScan;
	BoolMenuItem showHiddenFiles;
	TextHeadingMenuItem orientationHeading;
	TextMenuItem menuOrientationItem[Config::BASE_SUPPORTS_ORIENTATION_SENSOR ? 5 : 4];
	MultiChoiceMenuItem menuOrientation;
	TextMenuItem gameOrientationItem[Config::BASE_SUPPORTS_ORIENTATION_SENSOR ? 5 : 4];
	MultiChoiceMenuItem gameOrientation;
	StaticArrayList<MenuItem*, 21> item{};

	TextMenuItem::SelectDelegate setFontSizeDel(uint16_t val);
	TextMenuItem::SelectDelegate setMenuOrientationDel(int val);
	TextMenuItem::SelectDelegate setGameOrientationDel(int val);
	TextMenuItem::SelectDelegate setStatusBarDel(int val);
	TextMenuItem::SelectDelegate setLowProfileOSNavDel(int val);
	TextMenuItem::SelectDelegate setHideOSNavDel(int val);
};

class BiosSelectMenu : public TableView, public EmuAppHelper<BiosSelectMenu>
{
public:
	using BiosChangeDelegate = DelegateFunc<void (std::string_view displayName)>;

	BiosSelectMenu(IG::utf16String name, ViewAttachParams attach, FS::PathString *biosPathStr, BiosChangeDelegate onBiosChange,
		EmuSystem::NameFilterFunc fsFilter);

protected:
	TextMenuItem selectFile{};
	TextMenuItem unset{};
	BiosChangeDelegate onBiosChangeD{};
	FS::PathString *biosPathStr{};
	EmuSystem::NameFilterFunc fsFilter{};
};

}
