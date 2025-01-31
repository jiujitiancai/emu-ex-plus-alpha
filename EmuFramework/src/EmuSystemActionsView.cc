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

#include <emuframework/EmuSystemActionsView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuSystem.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/CreditsView.hh>
#include <emuframework/StateSlotView.hh>
#include <emuframework/OptionView.hh>
#include <emuframework/InputManagerView.hh>
#include <emuframework/BundledGamesView.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/gui/TextEntry.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/util/format.hh>
#include <imagine/logger/logger.h>

namespace EmuEx
{

class ResetAlertView : public BaseAlertView, public EmuAppHelper<ResetAlertView>
{
public:
	ResetAlertView(ViewAttachParams attach, const char *label):
		BaseAlertView(attach, label,
			[this](const TableView &)
			{
				return 3;
			},
			[this](const TableView &, size_t idx) -> MenuItem&
			{
				switch(idx)
				{
					default: bug_unreachable("idx == %d", (int)idx); return soft;
					case 0: return soft;
					case 1: return hard;
					case 2: return cancel;
				}
			}),
		soft
		{
			"Soft Reset", &defaultFace(),
			[this]()
			{
				EmuSystem::reset(app(), EmuSystem::RESET_SOFT);
				app().viewController().showEmulation();
			}
		},
		hard
		{
			"Hard Reset", &defaultFace(),
			[this]()
			{
				EmuSystem::reset(app(), EmuSystem::RESET_HARD);
				app().viewController().showEmulation();
			}
		},
		cancel
		{
			"Cancel", &defaultFace(),
			[](){}
		}
	{}

protected:
	TextMenuItem soft, hard, cancel;
};

static auto makeStateSlotStr(int slot)
{
	return fmt::format("State Slot ({})", EmuSystem::saveSlotChar(slot));
}

void EmuSystemActionsView::onShow()
{
	TableView::onShow();
	logMsg("refreshing action menu state");
	cheats.setActive(EmuSystem::gameIsRunning());
	reset.setActive(EmuSystem::gameIsRunning());
	saveState.setActive(EmuSystem::gameIsRunning());
	loadState.setActive(EmuSystem::gameIsRunning() && EmuSystem::stateExists(appContext(), EmuSystem::saveStateSlot));
	stateSlot.compile(makeStateSlotStr(EmuSystem::saveStateSlot), renderer(), projP);
	screenshot.setActive(EmuSystem::gameIsRunning());
	#ifdef CONFIG_EMUFRAMEWORK_ADD_LAUNCHER_ICON
	addLauncherIcon.setActive(EmuSystem::gameIsRunning());
	#endif
	resetSessionOptions.setActive(app().hasSavedSessionOptions());
	close.setActive(EmuSystem::gameIsRunning());
}

void EmuSystemActionsView::loadStandardItems()
{
	if(EmuSystem::hasCheats)
	{
		item.emplace_back(&cheats);
	}
	item.emplace_back(&reset);
	item.emplace_back(&loadState);
	item.emplace_back(&saveState);
	stateSlot.setName(makeStateSlotStr(EmuSystem::saveStateSlot));
	item.emplace_back(&stateSlot);
	#ifdef CONFIG_EMUFRAMEWORK_ADD_LAUNCHER_ICON
	item.emplace_back(&addLauncherIcon);
	#endif
	item.emplace_back(&screenshot);
	item.emplace_back(&resetSessionOptions);
	item.emplace_back(&close);
}

EmuSystemActionsView::EmuSystemActionsView(ViewAttachParams attach, bool customMenu):
	TableView{"System Actions", attach, item},
	cheats
	{
		"Cheats", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(EmuSystem::gameIsRunning())
			{
				pushAndShow(EmuApp::makeView(attachParams(), EmuApp::ViewID::LIST_CHEATS), e);
			}
		}
	},
	reset
	{
		"Reset", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(EmuSystem::gameIsRunning())
			{
				if(EmuSystem::hasResetModes)
				{
					pushAndShowModal(makeView<ResetAlertView>("Really reset?"), e);
				}
				else
				{
					auto ynAlertView = makeView<YesNoAlertView>("Really reset?");
					ynAlertView->setOnYes(
						[this]()
						{
							EmuSystem::reset(app(), EmuSystem::RESET_SOFT);
							app().viewController().showEmulation();
						});
					pushAndShowModal(std::move(ynAlertView), e);
				}
			}
		}
	},
	loadState
	{
		"Load State", &defaultFace(),
		[this](TextMenuItem &item, View &, const Input::Event &e)
		{
			if(item.active() && EmuSystem::gameIsRunning())
			{
				auto ynAlertView = std::make_unique<YesNoAlertView>(attachParams(), "Really load state?");
				ynAlertView->setOnYes(
					[this]()
					{
						if(app().loadStateWithSlot(EmuSystem::saveStateSlot))
							app().viewController().showEmulation();
					});
				pushAndShowModal(std::move(ynAlertView), e);
			}
		}
	},
	saveState
	{
		"Save State", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(EmuSystem::gameIsRunning())
			{
				static auto doSaveState =
					[](EmuApp &app)
					{
						if(app.saveStateWithSlot(EmuSystem::saveStateSlot))
							app.viewController().showEmulation();
					};
				if(EmuSystem::shouldOverwriteExistingState(appContext()))
				{
					doSaveState(app());
				}
				else
				{
					auto ynAlertView = std::make_unique<YesNoAlertView>(attachParams(), "Really overwrite state?");
					ynAlertView->setOnYes(
						[this]()
						{
							doSaveState(app());
						});
					pushAndShowModal(std::move(ynAlertView), e);
				}
			}
		}
	},
	stateSlot
	{
		{}, &defaultFace(),
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<StateSlotView>(), e);
		}
	},
	#ifdef CONFIG_EMUFRAMEWORK_ADD_LAUNCHER_ICON
	addLauncherIcon
	{
		"Add Game Shortcut to Launcher", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(EmuSystem::gameIsRunning())
			{
				if(EmuSystem::contentDirectory().empty())
				{
					// shortcuts to bundled games not yet supported
					return;
				}
				app().pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "Shortcut Name", EmuSystem::contentDisplayName(),
					[this](EmuApp &app, auto str)
					{
						appContext().addLauncherIcon(str, EmuSystem::contentLocation());
						app.postMessage(2, false, fmt::format("Added shortcut:\n{}", str));
						return true;
					});
			}
			else
			{
				app().postMessage("Load a game first");
			}
		}
	},
	#endif
	screenshot
	{
		"Screenshot Next Frame", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(!EmuSystem::gameIsRunning())
				return;
			auto pathName = appContext().fileUriDisplayName(EmuSystem::contentSaveDirectory());
			if(pathName.empty())
			{
				app().postMessage("Save path isn't valid");
				return;
			}
			auto ynAlertView = makeView<YesNoAlertView>(fmt::format("Save screenshot to folder {}?", pathName));
			ynAlertView->setOnYes(
				[this]()
				{
					app().video().takeGameScreenshot();
					EmuSystem::runFrame({}, &app().video(), nullptr);
				});
			pushAndShowModal(std::move(ynAlertView), e);
		}
	},
	resetSessionOptions
	{
		"Reset Saved Options", &defaultFace(),
		[this](const Input::Event &e)
		{
			if(!app().hasSavedSessionOptions())
				return;
			auto ynAlertView = makeView<YesNoAlertView>(
				"Reset saved options for the currently running system to defaults? Some options only take effect next time the system loads.");
			ynAlertView->setOnYes(
				[this]()
				{
					resetSessionOptions.setActive(false);
					app().deleteSessionOptions();
				});
			pushAndShowModal(std::move(ynAlertView), e);
		}
	},
	close
	{
		"Close Game", &defaultFace(),
		[this](const Input::Event &e)
		{
			auto ynAlertView = makeView<YesNoAlertView>("Really close current game?");
			ynAlertView->setOnYes(
				[this]()
				{
					app().viewController().closeSystem(true); // pops any System Actions views in stack
				});
			pushAndShowModal(std::move(ynAlertView), e);
		}
	}
{
	if(!customMenu)
	{
		loadStandardItems();
	}
}

}
