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

#include <emuframework/EmuApp.hh>
#include "EmuOptions.hh"
#include "privateInput.hh"
#include "configFile.hh"
#include <imagine/base/ApplicationContext.hh>
#include <imagine/io/FileIO.hh>
#include <imagine/fs/FS.hh>
#include <imagine/input/config.hh>
#include <imagine/util/ScopeGuard.hh>

namespace EmuEx
{

static constexpr unsigned KEY_CONFIGS_HARD_LIMIT = 256;
static constexpr unsigned INPUT_DEVICE_CONFIGS_HARD_LIMIT = 256;

static bool windowPixelFormatIsValid(uint8_t val)
{
	switch(val)
	{
		case IG::PIXEL_RGB565:
		case IG::PIXEL_RGBA8888:
			return true;
		default: return false;
	}
}

static bool renderPixelFormatIsValid(IG::PixelFormat val)
{
	return windowPixelFormatIsValid(val);
}

static bool colorSpaceIsValid(Gfx::ColorSpace val)
{
	return val == Gfx::ColorSpace::SRGB;
}

static bool readKeyConfig(KeyConfigContainer &customKeyConfigs,
	IO &io, uint16_t &size, std::span<const KeyCategory> categorySpan)
{
	auto confs = io.get<uint8_t>(); // TODO: unused currently, use to pre-allocate memory for configs
	size--;
	if(!size)
		return false;

	while(size)
	{
		KeyConfig keyConf{};

		keyConf.map = (Input::Map)io.get<uint8_t>();
		size--;
		if(!size)
			return false;

		auto nameLen = io.get<uint8_t>();
		size--;
		if(size < nameLen)
			return false;

		if(io.readSized(keyConf.name, nameLen) != nameLen)
			return false;
		size -= nameLen;
		if(!size)
			return false;

		auto categories = io.get<uint8_t>();
		size--;
		if(categories > categorySpan.size())
		{
			return false;
		}

		iterateTimes(categories, i)
		{
			if(!size)
				return false;

			auto categoryIdx = io.get<uint8_t>();
			size--;
			if(categoryIdx >= categorySpan.size())
			{
				return false;
			}
			if(size < 2)
			{
				return false;
			}
			auto &cat = categorySpan[categoryIdx];
			auto catSize = io.get<uint16_t>();
			size -= 2;
			if(size < catSize)
				return false;

			if(catSize > cat.keys * sizeof(KeyConfig::Key))
				return false;
			auto key = keyConf.key(cat);
			if(io.read(key, catSize) != catSize)
				return false;
			size -= catSize;

			// verify keys
			{
				const auto keyMax = Input::KeyEvent::mapNumKeys(keyConf.map);
				iterateTimes(cat.keys, i)
				{
					if(key[i] >= keyMax)
					{
						logWarn("key code 0x%X out of range for map type %d", key[i], (int)keyConf.map);
						key[i] = 0;
					}
				}
			}

			logMsg("read category %d", categoryIdx);
		}

		logMsg("read key config %s", keyConf.name.data());
		customKeyConfigs.emplace_back(std::make_unique<KeyConfig>(keyConf));

		if(customKeyConfigs.size() == KEY_CONFIGS_HARD_LIMIT)
		{
			logWarn("reached custom key config hard limit:%d", KEY_CONFIGS_HARD_LIMIT);
			break;
		}
	}
	return true;
}

static OptionBase *cfgFileOption[] =
{
	&optionAutoSaveState,
	&optionConfirmAutoLoadState,
	&optionSound,
	&optionSoundVolume,
	&optionSoundRate,
	&optionAspectRatio,
	&optionImageZoom,
	&optionViewportZoom,
	#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_MULTI_SCREEN
	&optionShowOnSecondScreen,
	#endif
	&optionImgFilter,
	&optionImgEffect,
	&optionImageEffectPixelFormat,
	&optionVideoImageBuffers,
	&optionOverlayEffect,
	&optionOverlayEffectLevel,
	#if 0
	&optionRelPointerDecel,
	#endif
	&optionFontSize,
	&optionPauseUnfocused,
	&optionGameOrientation,
	&optionMenuOrientation,
	&optionConfirmOverwriteState,
	&optionFastForwardSpeed,
	#ifdef CONFIG_INPUT_DEVICE_HOTSWAP
	&optionNotifyInputDeviceChange,
	#endif
	#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
	&optionFrameInterval,
	#endif
	&optionSkipLateFrames,
	&optionFrameRate,
	&optionFrameRatePAL,
	&optionNotificationIcon,
	&optionTitleBar,
	&optionIdleDisplayPowerSave,
	&optionHideStatusBar,
	&optionSystemActionsIsDefaultMenu,
	&optionTextureBufferMode,
	#if defined __ANDROID__
	&optionLowProfileOSNav,
	&optionHideOSNav,
	&optionSustainedPerformanceMode,
	#endif
	#ifdef CONFIG_BLUETOOTH
	&optionKeepBluetoothActive,
	&optionShowBluetoothScan,
		#ifdef CONFIG_BLUETOOTH_SCAN_CACHE_USAGE
		&optionBlueToothScanCache,
		#endif
	#endif
	&optionSoundBuffers,
	&optionAddSoundBuffersOnUnderrun,
	#ifdef CONFIG_AUDIO_MULTIPLE_SYSTEM_APIS
	&optionAudioAPI,
	#endif
	&optionShowBundledGames,
};

void EmuApp::saveConfigFile(IO &io)
{
	if(!io)
	{
		logMsg("not writing config file");
		return;
	}
	writeConfigHeader(io);

	for(auto &e : cfgFileOption)
	{
		if(!e->isDefault())
		{
			io.write((uint16_t)e->ioSize());
			e->writeToIO(io);
		}
	}

	writeRecentContent(io);
	writeOptionValue(io, CFGKEY_BACK_NAVIGATION, viewManager.needsBackControlOption());
	writeOptionValue(io, CFGKEY_SWAPPED_GAMEPAD_CONFIM, swappedConfirmKeysOption());
	writeOptionValue(io, CFGKEY_AUDIO_SOLO_MIX, audioManager().soloMixOption());
	writeOptionValue(io, CFGKEY_WINDOW_PIXEL_FORMAT, windowDrawablePixelFormatOption());
	writeOptionValue(io, CFGKEY_VIDEO_COLOR_SPACE, windowDrawableColorSpaceOption());
	writeOptionValue(io, CFGKEY_RENDER_PIXEL_FORMAT, renderPixelFormatOption());
	if(showHiddenFilesInPicker()) writeOptionValue(io, CFGKEY_SHOW_HIDDEN_FILES, true);
	if constexpr(Config::EmuFramework::MOGA_INPUT)
	{
		if(mogaManagerPtr)
			writeOptionValue(io, CFGKEY_MOGA_INPUT_SYSTEM, true);
	}
	vController.writeConfig(io);
	viewController().writeConfig(io);

	if(customKeyConfigs.size())
	{
		auto categories = inputControlCategories();
		bool writeCategory[customKeyConfigs.size()][categories.size()];
		uint8_t writeCategories[customKeyConfigs.size()];
		std::fill_n(writeCategories, customKeyConfigs.size(), 0);
		// compute total size
		static_assert(sizeof(KeyConfig::name) <= 255, "key config name array is too large");
		unsigned bytes = 2; // config key size
		bytes += 1; // number of configs
		for(uint8_t configs = 0; auto &ePtr : customKeyConfigs)
		{
			auto &e = *ePtr;
			bytes += 1; // input map type
			bytes += 1; // name string length
			bytes += e.name.size(); // name string
			bytes += 1; // number of categories present
			for(auto &cat : inputControlCategories())
			{
				bool write{};
				const auto key = e.key(cat);
				iterateTimes(cat.keys, k)
				{
					if(key[k]) // check if category has any keys defined
					{
						write = true;
						break;
					}
				}
				writeCategory[configs][std::distance(inputControlCategories().data(), &cat)] = write;
				if(!write)
				{
					logMsg("category:%s of key conf:%s skipped", cat.name, e.name.data());
					continue;
				}
				writeCategories[configs]++;
				bytes += 1; // category index
				bytes += 2; // category key array size
				bytes += cat.keys * sizeof(KeyConfig::Key); // keys array
			}
			configs++;
		}
		if(bytes > 0xFFFF)
		{
			bug_unreachable("excessive key config size, should not happen");
		}
		// write to config file
		logMsg("saving %d key configs, %d bytes", (int)customKeyConfigs.size(), bytes);
		io.write(uint16_t(bytes));
		io.write((uint16_t)CFGKEY_INPUT_KEY_CONFIGS);
		io.write((uint8_t)customKeyConfigs.size());
		for(uint8_t configs = 0; auto &ePtr : customKeyConfigs)
		{
			auto &e = *ePtr;
			logMsg("writing config %s", e.name.data());
			io.write(uint8_t(e.map));
			uint8_t nameLen = e.name.size();
			io.write(nameLen);
			io.write(e.name.data(), nameLen);
			io.write(writeCategories[configs]);
			for(auto &cat : inputControlCategories())
			{
				uint8_t catIdx = std::distance(inputControlCategories().data(), &cat);
				if(!writeCategory[configs][catIdx])
					continue;
				io.write((uint8_t)catIdx);
				uint16_t catSize = cat.keys * sizeof(KeyConfig::Key);
				io.write(catSize);
				io.write(e.key(cat), catSize);
			}
			configs++;
		}
	}

	if(savedInputDevs.size())
	{
		// input device configs must be saved after key configs since
		// they reference the key configs when read back from the config file

		// compute total size
		unsigned bytes = 2; // config key size
		bytes += 1; // number of configs
		for(auto &ePtr : savedInputDevs)
		{
			auto &e = *ePtr;
			bytes += 1; // device id
			bytes += 1; // enabled
			bytes += 1; // player
			bytes += 1; // mapJoystickAxis1ToDpad
			#ifdef CONFIG_INPUT_ICADE
			bytes += 1; // iCade mode
			#endif
			bytes += 1; // name string length
			bytes += std::min((size_t)256, e.name.size()); // name string
			bytes += 1; // key config map
			if(e.keyConf)
			{
				bytes += 1; // name of key config string length
				bytes += e.keyConf->name.size(); // name of key config string
			}
		}
		if(bytes > 0xFFFF)
		{
			bug_unreachable("excessive input device config size, should not happen");
		}
		// write to config file
		logMsg("saving %d input device configs, %d bytes", (int)savedInputDevs.size(), bytes);
		io.write((uint16_t)bytes);
		io.write((uint16_t)CFGKEY_INPUT_DEVICE_CONFIGS);
		io.write((uint8_t)savedInputDevs.size());
		for(auto &ePtr : savedInputDevs)
		{
			auto &e = *ePtr;
			logMsg("writing config %s, id %d", e.name.data(), e.enumId);
			uint8_t enumIdWithFlags = e.enumId;
			if(e.handleUnboundEvents)
				enumIdWithFlags |= e.HANDLE_UNBOUND_EVENTS_FLAG;
			io.write((uint8_t)enumIdWithFlags);
			io.write((uint8_t)e.enabled);
			io.write((uint8_t)e.player);
			io.write((uint8_t)e.joystickAxisAsDpadBits);
			#ifdef CONFIG_INPUT_ICADE
			io.write((uint8_t)e.iCadeMode);
			#endif
			uint8_t nameLen = std::min((size_t)256, e.name.size());
			io.write(nameLen);
			io.write(e.name.data(), nameLen);
			uint8_t keyConfMap = e.keyConf ? (uint8_t)e.keyConf->map : 0;
			io.write(keyConfMap);
			if(keyConfMap)
			{
				logMsg("has key conf %s, map %d", e.keyConf->name.data(), keyConfMap);
				uint8_t keyConfNameLen = e.keyConf->name.size();
				io.write(keyConfNameLen);
				io.write(e.keyConf->name.data(), keyConfNameLen);
			}
		}
	}

	writeStringOptionValue(io, CFGKEY_LAST_DIR, contentSearchPath());
	writeStringOptionValue(io, CFGKEY_SAVE_PATH, EmuSystem::userSaveDirectory());

	EmuSystem::writeConfig(io);
}

EmuApp::ConfigParams EmuApp::loadConfigFile(IG::ApplicationContext ctx)
{
	auto configFilePath = FS::pathString(ctx.supportPath(), "config");
	// move config files from old locations
	if(Config::envIsLinux)
	{
		auto oldConfigFilePath = FS::pathString(ctx.assetPath(), "config");
		if(FS::exists(oldConfigFilePath))
		{
			logMsg("moving config file from app path to support path");
			FS::rename(oldConfigFilePath, configFilePath);
		}
	}
	#ifdef CONFIG_BASE_IOS
	if(ctx.isSystemApp())
	{
		const char *oldConfigDir = "/User/Library/Preferences/explusalpha.com";
		auto oldConfigFilePath = FS::pathString(oldConfigDir, EmuSystem::configFilename);
		if(FS::exists(oldConfigFilePath))
		{
			logMsg("moving config file from prefs path to support path");
			FS::rename(oldConfigFilePath, configFilePath);
		}
		if(!FS::directoryItems(oldConfigDir))
		{
			logMsg("removing old empty config directory");
			FS::remove(oldConfigDir);
		}
	}
	#endif
	auto configBuff = FileUtils::bufferFromPath(configFilePath, IO::OPEN_TEST);
	if(!configBuff)
		return {};
	ConfigParams appConfig{};
	Gfx::DrawableConfig pendingWindowDrawableConf{};
	readConfigKeys(std::move(configBuff),
		[&](uint16_t key, uint16_t size, IO &io)
		{
			switch(key)
			{
				default:
				{
					if(EmuSystem::readConfig(io, key, size))
					{
						break;
					}
					if(vController.readConfig(io, key, size))
					{
						break;
					}
					logMsg("skipping unknown key %u", (unsigned)key);
				}
				bcase CFGKEY_SOUND: optionSound.readFromIO(io, size);
				bcase CFGKEY_SOUND_RATE: optionSoundRate.readFromIO(io, size);
				bcase CFGKEY_AUTO_SAVE_STATE: optionAutoSaveState.readFromIO(io, size);
				bcase CFGKEY_CONFIRM_AUTO_LOAD_STATE: optionConfirmAutoLoadState.readFromIO(io, size);
				#if defined CONFIG_BASE_SCREEN_FRAME_INTERVAL
				bcase CFGKEY_FRAME_INTERVAL: optionFrameInterval.readFromIO(io, size);
				#endif
				bcase CFGKEY_SKIP_LATE_FRAMES: optionSkipLateFrames.readFromIO(io, size);
				bcase CFGKEY_FRAME_RATE: optionFrameRate.readFromIO(io, size);
				bcase CFGKEY_FRAME_RATE_PAL: optionFrameRatePAL.readFromIO(io, size);
				bcase CFGKEY_LAST_DIR:
					readStringOptionValue<FS::PathString>(io, size,
						[&](auto &path)
						{
							if(ctx.permissionIsRestricted(IG::Permission::WRITE_EXT_STORAGE) && path[0] == '/')
							{
								logWarn("not restoring content dir due to storage permission restriction");
								return;
							}
							setContentSearchPath(path);
						});
				bcase CFGKEY_FONT_Y_SIZE: optionFontSize.readFromIO(io, size);
				bcase CFGKEY_GAME_ORIENTATION: optionGameOrientation.readFromIO(io, size);
				bcase CFGKEY_MENU_ORIENTATION: optionMenuOrientation.readFromIO(io, size);
				bcase CFGKEY_GAME_IMG_FILTER: optionImgFilter.readFromIO(io, size);
				bcase CFGKEY_GAME_ASPECT_RATIO: optionAspectRatio.readFromIO(io, size);
				bcase CFGKEY_IMAGE_ZOOM: optionImageZoom.readFromIO(io, size);
				bcase CFGKEY_VIEWPORT_ZOOM: optionViewportZoom.readFromIO(io, size);
				#if defined CONFIG_BASE_MULTI_WINDOW && defined CONFIG_BASE_MULTI_SCREEN
				bcase CFGKEY_SHOW_ON_2ND_SCREEN: optionShowOnSecondScreen.readFromIO(io, size);
				#endif
				bcase CFGKEY_IMAGE_EFFECT: optionImgEffect.readFromIO(io, size);
				bcase CFGKEY_IMAGE_EFFECT_PIXEL_FORMAT: optionImageEffectPixelFormat.readFromIO(io, size);
				bcase CFGKEY_RENDER_PIXEL_FORMAT: setRenderPixelFormat(readOptionValue<IG::PixelFormat>(io, size, renderPixelFormatIsValid));
				bcase CFGKEY_VIDEO_IMAGE_BUFFERS: optionVideoImageBuffers.readFromIO(io, size);
				bcase CFGKEY_OVERLAY_EFFECT: optionOverlayEffect.readFromIO(io, size);
				bcase CFGKEY_OVERLAY_EFFECT_LEVEL: optionOverlayEffectLevel.readFromIO(io, size);
				bcase CFGKEY_TOUCH_CONTROL_VIRBRATE: vController.setVibrateOnTouchInput(*this, readOptionValue<bool>(io, size));
				bcase CFGKEY_RECENT_GAMES: readRecentContent(ctx, io, size);
				bcase CFGKEY_SWAPPED_GAMEPAD_CONFIM: setSwappedConfirmKeys(readOptionValue<bool>(io, size));
				bcase CFGKEY_PAUSE_UNFOCUSED: optionPauseUnfocused.readFromIO(io, size);
				bcase CFGKEY_NOTIFICATION_ICON: optionNotificationIcon.readFromIO(io, size);
				bcase CFGKEY_TITLE_BAR: optionTitleBar.readFromIO(io, size);
				bcase CFGKEY_BACK_NAVIGATION: appConfig.setBackNavigation(readOptionValue<bool>(io, size));
				bcase CFGKEY_SYSTEM_ACTIONS_IS_DEFAULT_MENU: optionSystemActionsIsDefaultMenu.readFromIO(io, size);
				bcase CFGKEY_IDLE_DISPLAY_POWER_SAVE: optionIdleDisplayPowerSave.readFromIO(io, size);
				bcase CFGKEY_HIDE_STATUS_BAR: optionHideStatusBar.readFromIO(io, size);
				bcase CFGKEY_CONFIRM_OVERWRITE_STATE: optionConfirmOverwriteState.readFromIO(io, size);
				bcase CFGKEY_FAST_FORWARD_SPEED: optionFastForwardSpeed.readFromIO(io, size);
				#ifdef CONFIG_INPUT_DEVICE_HOTSWAP
				bcase CFGKEY_NOTIFY_INPUT_DEVICE_CHANGE: optionNotifyInputDeviceChange.readFromIO(io, size);
				#endif
				bcase CFGKEY_MOGA_INPUT_SYSTEM:
					if constexpr(Config::EmuFramework::MOGA_INPUT)
					{
						if(readOptionValue<bool>(io, size).value_or(false))
						{
							setMogaManagerActive(true, false);
						}
					}
				bcase CFGKEY_TEXTURE_BUFFER_MODE: optionTextureBufferMode.readFromIO(io, size);
				#if defined __ANDROID__
				bcase CFGKEY_LOW_PROFILE_OS_NAV: optionLowProfileOSNav.readFromIO(io, size);
				bcase CFGKEY_HIDE_OS_NAV: optionHideOSNav.readFromIO(io, size);
				//bcase CFGKEY_REL_POINTER_DECEL: optionRelPointerDecel.readFromIO(io, size);
				bcase CFGKEY_SUSTAINED_PERFORMANCE_MODE: optionSustainedPerformanceMode.readFromIO(io, size);
				#endif
				#ifdef CONFIG_BLUETOOTH
				bcase CFGKEY_KEEP_BLUETOOTH_ACTIVE: optionKeepBluetoothActive.readFromIO(io, size);
				bcase CFGKEY_SHOW_BLUETOOTH_SCAN: optionShowBluetoothScan.readFromIO(io, size);
					#ifdef CONFIG_BLUETOOTH_SCAN_CACHE_USAGE
					bcase CFGKEY_BLUETOOTH_SCAN_CACHE: optionBlueToothScanCache.readFromIO(io, size);
					#endif
				#endif
				bcase CFGKEY_SOUND_BUFFERS: optionSoundBuffers.readFromIO(io, size);
				bcase CFGKEY_SOUND_VOLUME: optionSoundVolume.readFromIO(io, size);
				bcase CFGKEY_ADD_SOUND_BUFFERS_ON_UNDERRUN: optionAddSoundBuffersOnUnderrun.readFromIO(io, size);
				bcase CFGKEY_AUDIO_SOLO_MIX: audioManager().setSoloMix(readOptionValue<bool>(io, size));
				#ifdef CONFIG_AUDIO_MULTIPLE_SYSTEM_APIS
				bcase CFGKEY_AUDIO_API: optionAudioAPI.readFromIO(io, size);
				#endif
				bcase CFGKEY_SAVE_PATH:
					readStringOptionValue<FS::PathString>(io, size,
						[&](auto &path)
						{
							if(ctx.permissionIsRestricted(IG::Permission::WRITE_EXT_STORAGE) && path[0] == '/')
							{
								logWarn("not restoring save dir due to storage permission restriction");
								return;
							}
							EmuSystem::setUserSaveDirectory(ctx, path);
						});
				bcase CFGKEY_SHOW_BUNDLED_GAMES:
				{
					if(EmuSystem::hasBundledGames)
						optionShowBundledGames.readFromIO(io, size);
				}
				bcase CFGKEY_WINDOW_PIXEL_FORMAT: pendingWindowDrawableConf.pixelFormat = readOptionValue<IG::PixelFormat>(io, size, windowPixelFormatIsValid).value_or(IG::PixelFormat{});
				bcase CFGKEY_VIDEO_COLOR_SPACE: pendingWindowDrawableConf.colorSpace = readOptionValue<Gfx::ColorSpace>(io, size, colorSpaceIsValid).value_or(Gfx::ColorSpace{});
				bcase CFGKEY_SHOW_HIDDEN_FILES: setShowHiddenFilesInPicker(readOptionValue<bool>(io, size).value_or(false));
				bcase CFGKEY_RENDERER_PRESENTATION_TIME: appConfig.setRendererPresentationTime(readOptionValue<bool>(io, size).value_or(true));
				bcase CFGKEY_INPUT_KEY_CONFIGS:
				{
					if(!readKeyConfig(customKeyConfigs, io, size, inputControlCategories()))
					{
						logErr("error reading key configs from file");
					}
					if(size)
					{
						// skip leftover bytes
						logWarn("%d bytes leftover reading key configs", size);
					}
				}
				bcase CFGKEY_INPUT_DEVICE_CONFIGS:
				{
					auto confs = io.get<uint8_t>(); // TODO: unused currently, use to pre-allocate memory for configs
					size--;
					if(!size)
						break;

					while(size)
					{
						InputDeviceSavedConfig devConf;

						auto enumIdWithFlags = io.get<uint8_t>();
						size--;
						if(!size)
							break;
						devConf.handleUnboundEvents = enumIdWithFlags & devConf.HANDLE_UNBOUND_EVENTS_FLAG;
						devConf.enumId = enumIdWithFlags & devConf.ENUM_ID_MASK;

						devConf.enabled = io.get<uint8_t>();
						size--;
						if(!size)
							break;

						devConf.player = io.get<uint8_t>();
						if(devConf.player != InputDeviceConfig::PLAYER_MULTI && devConf.player > EmuSystem::maxPlayers)
						{
							logWarn("player %d out of range", devConf.player);
							devConf.player = 0;
						}
						size--;
						if(!size)
							break;

						devConf.joystickAxisAsDpadBits = io.get<uint8_t>();
						size--;
						if(!size)
							break;

						#ifdef CONFIG_INPUT_ICADE
						devConf.iCadeMode = io.get<uint8_t>();
						size--;
						if(!size)
							break;
						#endif

						auto nameLen = io.get<uint8_t>();
						size--;
						if(size < nameLen)
							break;

						io.readSized(devConf.name, nameLen);
						size -= nameLen;
						if(!size)
							break;

						auto keyConfMap = Input::validateMap(io.get<uint8_t>());
						size--;

						if(keyConfMap != Input::Map::UNKNOWN)
						{
							if(!size)
								break;

							auto keyConfNameLen = io.get<uint8_t>();
							size--;
							if(size < keyConfNameLen)
								break;

							if(keyConfNameLen > devConf.name.max_size()-1)
								break;
							char keyConfName[keyConfNameLen + 1];
							if(io.read(keyConfName, keyConfNameLen) != keyConfNameLen)
								break;
							keyConfName[keyConfNameLen] = '\0';
							size -= keyConfNameLen;

							for(auto &ePtr : customKeyConfigs)
							{
								auto &e = *ePtr;
								if(e.map == keyConfMap && e.name == keyConfName)
								{
									logMsg("found referenced custom key config %s while reading input device config", keyConfName);
									devConf.keyConf = &e;
									break;
								}
							}

							if(!devConf.keyConf) // check built-in configs after user-defined ones
							{
								unsigned defaultConfs = 0;
								auto defaultConf = KeyConfig::defaultConfigsForInputMap(keyConfMap, defaultConfs);
								iterateTimes(defaultConfs, c)
								{
									if(defaultConf[c].name == keyConfName)
									{
										logMsg("found referenced built-in key config %s while reading input device config", keyConfName);
										devConf.keyConf = &defaultConf[c];
										break;
									}
								}
							}
						}

						logMsg("read input device config %s, id %d", devConf.name.data(), devConf.enumId);
						savedInputDevs.emplace_back(std::make_unique<InputDeviceSavedConfig>(devConf));

						if(savedInputDevs.size() == INPUT_DEVICE_CONFIGS_HARD_LIMIT)
						{
							logWarn("reached input device config hard limit:%d", INPUT_DEVICE_CONFIGS_HARD_LIMIT);
							break;
						}
					}
					if(size)
					{
						// skip leftover bytes
						logWarn("%d bytes leftover reading input device configs", size);
					}
				}
			}
		});

	// apply any pending read options
	if(pendingWindowDrawableConf.colorSpace != Gfx::ColorSpace{} && pendingWindowDrawableConf.pixelFormat != IG::PIXEL_RGBA8888)
		pendingWindowDrawableConf.colorSpace = {};
	windowDrawableConf = pendingWindowDrawableConf;

	return appConfig;
}

void EmuApp::saveConfigFile(IG::ApplicationContext ctx)
{
	auto configFilePath = FS::pathString(ctx.supportPath(), "config");
	try
	{
		saveConfigFile(FileIO::create(configFilePath));
	}
	catch(...)
	{
		logErr("error writing config file");
	}
}

}
