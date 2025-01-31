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

#include <emuframework/EmuInputView.hh>
#include <emuframework/VController.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/EmuVideoLayer.hh>
#include <emuframework/FilePicker.hh>
#include "EmuOptions.hh"
#include "private.hh"
#include "privateInput.hh"
#include <imagine/gui/AlertView.hh>
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/util/format.hh>
#include <imagine/util/variant.hh>

namespace EmuEx
{

EmuInputView::EmuInputView() {}

EmuInputView::EmuInputView(ViewAttachParams attach, VController &vCtrl, EmuVideoLayer &videoLayer)
	: View(attach), vController{&vCtrl},
		videoLayer{&videoLayer}
{}

void EmuInputView::draw(Gfx::RendererCommands &cmds)
{
	#ifdef CONFIG_EMUFRAMEWORK_VCONTROLS
	cmds.loadTransform(projP.makeTranslate());
	vController->draw(cmds, ffToggleActive);
	#endif
}

void EmuInputView::place()
{
	#ifdef CONFIG_EMUFRAMEWORK_VCONTROLS
	vController->place();
	#endif
}

void EmuInputView::resetInput()
{
	#ifdef CONFIG_EMUFRAMEWORK_VCONTROLS
	vController->resetInput();
	#endif
	ffToggleActive = false;
}

void EmuInputView::updateFastforward()
{
	app().viewController().setFastForwardActive(ffToggleActive);
}

bool EmuInputView::inputEvent(const Input::Event &e)
{
	return visit(overloaded
	{
		[&](const Input::MotionEvent &motionEv)
		{
			if(!motionEv.isAbsolute())
				return false;
			if(motionEv.pushed() && vController->menuHitTest(motionEv.pos()))
			{
				app().showLastViewFromSystem(attachParams(), motionEv);
				return true;
			}
			else if(motionEv.pushed() && vController->fastForwardHitTest(motionEv.pos()))
			{
				ffToggleActive ^= true;
				updateFastforward();
			}
			else
			{
				vController->pointerInputEvent(motionEv, videoLayer->gameRect());
			}
			return false;
		},
		[&](const Input::KeyEvent &keyEv)
		{
			if(vController->keyInput(keyEv))
				return true;
			auto &emuApp = app();
			auto &devData = inputDevData(*keyEv.device());
			const auto &actionTable = devData.actionTable;
			if(!actionTable.size()) [[unlikely]]
				return false;
			assumeExpr(keyEv.device());
			const auto &actionGroup = actionTable[keyEv.mapKey()];
			//logMsg("player %d input %s", player, Input::buttonName(keyEv.map, keyEv.button));
			bool isPushed = keyEv.pushed();
			bool isRepeated = keyEv.repeated();
			bool didAction = false;
			iterateTimes(InputDeviceData::maxKeyActions, i)
			{
				auto action = actionGroup[i];
				if(action != 0)
				{
					using namespace Controls;
					didAction = true;
					action--;

					switch(action)
					{
						bcase guiKeyIdxFastForward:
						{
							if(isRepeated)
								continue;
							ffToggleActive = keyEv.pushed();
							updateFastforward();
							logMsg("fast-forward state:%d", ffToggleActive);
						}

						bcase guiKeyIdxLoadGame:
						if(isPushed)
						{
							logMsg("show load game view from key event");
							emuApp.viewController().popToRoot();
							pushAndShow(EmuFilePicker::makeForLoading(attachParams(), e), e, false);
							return true;
						}

						bcase guiKeyIdxMenu:
						if(isPushed)
						{
							logMsg("show system actions view from key event");
							emuApp.showSystemActionsViewFromSystem(attachParams(), e);
							return true;
						}

						bcase guiKeyIdxSaveState:
						if(isPushed)
						{
							static auto doSaveState =
								[](EmuApp &app, bool notify)
								{
									if(app.saveStateWithSlot(EmuSystem::saveStateSlot) && notify)
									{
										app.postMessage("State Saved");
									}
								};

							if(EmuSystem::shouldOverwriteExistingState(appContext()))
							{
								emuApp.syncEmulationThread();
								doSaveState(emuApp, optionConfirmOverwriteState);
							}
							else
							{
								auto ynAlertView = makeView<YesNoAlertView>("Really Overwrite State?");
								ynAlertView->setOnYes(
									[this]()
									{
										doSaveState(app(), false);
										app().viewController().showEmulation();
									});
								ynAlertView->setOnNo(
									[this]()
									{
										app().viewController().showEmulation();
									});
								pushAndShowModal(std::move(ynAlertView), e);
							}
							return true;
						}

						bcase guiKeyIdxLoadState:
						if(isPushed)
						{
							emuApp.syncEmulationThread();
							emuApp.loadStateWithSlot(EmuSystem::saveStateSlot);
							return true;
						}

						bcase guiKeyIdxDecStateSlot:
						if(isPushed)
						{
							EmuSystem::saveStateSlot--;
							if(EmuSystem::saveStateSlot < -1)
								EmuSystem::saveStateSlot = 9;
							emuApp.postMessage(1, false, fmt::format("State Slot: {}", stateNameStr(EmuSystem::saveStateSlot)));
						}

						bcase guiKeyIdxIncStateSlot:
						if(isPushed)
						{
							auto prevSlot = EmuSystem::saveStateSlot;
							EmuSystem::saveStateSlot++;
							if(EmuSystem::saveStateSlot > 9)
								EmuSystem::saveStateSlot = -1;
							emuApp.postMessage(1, false, fmt::format("State Slot: {}", stateNameStr(EmuSystem::saveStateSlot)));
						}

						bcase guiKeyIdxGameScreenshot:
						if(isPushed)
						{
							videoLayer->emuVideo().takeGameScreenshot();
							return true;
						}

						bcase guiKeyIdxToggleFastForward:
						if(isPushed)
						{
							if(keyEv.repeated())
								continue;
							ffToggleActive = !ffToggleActive;
							updateFastforward();
							logMsg("fast-forward state:%d", ffToggleActive);
						}

						bcase guiKeyIdxExit:
						if(isPushed)
						{
							logMsg("show last view from key event");
							emuApp.showLastViewFromSystem(attachParams(), e);
							return true;
						}

						bdefault:
						{
							if(isRepeated)
							{
								continue;
							}
							//logMsg("action %d, %d", emuKey, state);
							bool turbo;
							unsigned sysAction = EmuSystem::translateInputAction(action, turbo);
							//logMsg("action %d -> %d, pushed %d", action, sysAction, keyEv.state() == Input::PUSHED);
							if(turbo)
							{
								if(isPushed)
								{
									emuApp.addTurboInputEvent(sysAction);
								}
								else
								{
									emuApp.removeTurboInputEvent(sysAction);
								}
							}
							EmuSystem::handleInputAction(&emuApp, keyEv.state(), sysAction, keyEv.metaKeyBits());
						}
					}
				}
				else
					break;
			}
			return didAction
				|| keyEv.isGamepad() // consume all gamepad events
				|| devData.devConf.shouldConsumeUnboundKeys();
		}
	}, e.asVariant());
}

}
