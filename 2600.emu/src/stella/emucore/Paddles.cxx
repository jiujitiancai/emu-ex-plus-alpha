//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2021 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include <cmath>

#include "Event.hxx"
#include "Paddles.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Paddles::Paddles(Jack jack, const Event& event, const System& system,
                 bool swappaddle, bool swapaxis, bool swapdir, bool altmap)
  : Controller(jack, event, system, Controller::Type::Paddles)
{
  // We must start with a physical valid resistance (e.g. 0);
  // see commit 38b452e1a047a0dca38c5bcce7c271d40f76736e for more information
  setPin(AnalogPin::Five, AnalogReadout::connectToVcc());
  setPin(AnalogPin::Nine, AnalogReadout::connectToVcc());

  // The following logic reflects that mapping paddles to different
  // devices can be extremely complex
  // As well, while many paddle games have horizontal movement of
  // objects (which maps nicely to horizontal movement of the joystick
  // or mouse), others have vertical movement
  // This vertical handling is taken care of by swapping the axes
  // On the other hand, some games treat paddle resistance differently,
  // (ie, increasing resistance can move an object right instead of left)
  // This is taken care of by swapping the direction of movement
  // Arrgh, did I mention that paddles are complex ...

  // As much as possible, precompute which events we care about for
  // a given port; this will speed up processing in update()

  // Consider whether this is the left or right port
  if(myJack == Jack::Left)
  {
    if(!altmap)
    {
      // First paddle is left A, second is left B
      myAAxisValue = Event::LeftPaddleAAnalog;
      myBAxisValue = Event::LeftPaddleBAnalog;
      myLeftAFireEvent = Event::LeftPaddleAFire;
      myLeftBFireEvent = Event::LeftPaddleBFire;

      // These can be affected by changes in axis orientation
      myLeftADecEvent = Event::LeftPaddleADecrease;
      myLeftAIncEvent = Event::LeftPaddleAIncrease;
      myLeftBDecEvent = Event::LeftPaddleBDecrease;
      myLeftBIncEvent = Event::LeftPaddleBIncrease;
    }
    else
    {
      // First paddle is QT 3A, second is QT 3B (fire buttons only)
      myLeftAFireEvent = Event::QTPaddle3AFire;
      myLeftBFireEvent = Event::QTPaddle3BFire;

      myAAxisValue = myBAxisValue =
        myLeftADecEvent = myLeftAIncEvent =
        myLeftBDecEvent = myLeftBIncEvent = Event::NoType;
    }
  }
  else    // Jack is right port
  {
    if(!altmap)
    {
      // First paddle is right A, second is right B
      myAAxisValue = Event::RightPaddleAAnalog;
      myBAxisValue = Event::RightPaddleBAnalog;
      myLeftAFireEvent = Event::RightPaddleAFire;
      myLeftBFireEvent = Event::RightPaddleBFire;

      // These can be affected by changes in axis orientation
      myLeftADecEvent = Event::RightPaddleADecrease;
      myLeftAIncEvent = Event::RightPaddleAIncrease;
      myLeftBDecEvent = Event::RightPaddleBDecrease;
      myLeftBIncEvent = Event::RightPaddleBIncrease;
    }
    else
    {
      // First paddle is QT 4A, second is QT 4B (fire buttons only)
      myLeftAFireEvent = Event::QTPaddle4AFire;
      myLeftBFireEvent = Event::QTPaddle4BFire;

      myAAxisValue = myBAxisValue =
        myLeftADecEvent = myLeftAIncEvent =
        myLeftBDecEvent = myLeftBIncEvent = Event::NoType;
    }
  }

  // Some games swap the paddles
  if(swappaddle)
  {
    // First paddle is right A|B, second is left A|B
    swapEvents(myAAxisValue, myBAxisValue);
    swapEvents(myLeftAFireEvent, myLeftBFireEvent);
    swapEvents(myLeftADecEvent, myLeftBDecEvent);
    swapEvents(myLeftAIncEvent, myLeftBIncEvent);
  }

  // Direction of movement can be swapped
  // That is, moving in a certain direction on an axis can
  // result in either increasing or decreasing paddle movement
  if(swapdir)
  {
    swapEvents(myLeftADecEvent, myLeftAIncEvent);
    swapEvents(myLeftBDecEvent, myLeftBIncEvent);
  }

  // The following are independent of whether or not the port
  // is left or right
  MOUSE_SENSITIVITY = swapdir ? -abs(MOUSE_SENSITIVITY) :
                                 abs(MOUSE_SENSITIVITY);
  if(!swapaxis)
  {
    myAxisMouseMotion = Event::MouseAxisXMove;
    myAxisDigitalZero = 0;
    myAxisDigitalOne  = 1;
  }
  else
  {
    myAxisMouseMotion = Event::MouseAxisYMove;
    myAxisDigitalZero = 1;
    myAxisDigitalOne  = 0;
  }

  // Digital pins 1, 2 and 6 are not connected
  setPin(DigitalPin::One, true);
  setPin(DigitalPin::Two, true);
  setPin(DigitalPin::Six, true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::swapEvents(Event::Type& event1, Event::Type& event2)
{
  Event::Type swappedEvent;

  swappedEvent = event1;
  event1 = event2;
  event2 = swappedEvent;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::update()
{
  setPin(DigitalPin::Three, true);
  setPin(DigitalPin::Four, true);

  // Digital events (from keyboard or joystick hats & buttons)
  bool firePressedA = myEvent.get(myLeftAFireEvent) != 0;
  bool firePressedB = myEvent.get(myLeftBFireEvent) != 0;

  // Paddle movement is a very difficult thing to accurately emulate,
  // since it originally came from an analog device that had very
  // peculiar behaviour
  // Compounding the problem is the fact that we'd like to emulate
  // movement with 'digital' data (like from a keyboard or a digital
  // joystick axis), but also from a mouse (relative values)
  // and Stelladaptor-like devices (absolute analog values clamped to
  // a certain range)
  // And to top it all off, we don't want one devices input to conflict
  // with the others ...

  if(!updateAnalogAxes())
  {
    updateMouse(firePressedA, firePressedB);
    updateDigitalAxes();

    // Only change state if the charge has actually changed
    if(myCharge[1] != myLastCharge[1])
    {
      setPin(AnalogPin::Five, AnalogReadout::connectToVcc(MAX_RESISTANCE * (myCharge[1] / double(TRIGMAX))));
      myLastCharge[1] = myCharge[1];
    }
    if(myCharge[0] != myLastCharge[0])
    {
      setPin(AnalogPin::Nine, AnalogReadout::connectToVcc(MAX_RESISTANCE * (myCharge[0] / double(TRIGMAX))));
      myLastCharge[0] = myCharge[0];
    }
  }

  setPin(DigitalPin::Four, !getAutoFireState(firePressedA));
  setPin(DigitalPin::Three, !getAutoFireStateP1(firePressedB));
}

AnalogReadout::Connection Paddles::getReadOut(int lastAxis, int& newAxis, int center)
{
  const float range = ANALOG_RANGE - analogDeadZone() * 2;

  // dead zone, ignore changes inside the dead zone
  if(newAxis > analogDeadZone())
    newAxis -= analogDeadZone();
  else if(newAxis < -analogDeadZone())
    newAxis += analogDeadZone();
  else
    newAxis = 0; // treat any dead zone value as zero

  static constexpr std::array<float, MAX_DEJITTER - MIN_DEJITTER + 1> bFac = {
    // higher values mean more dejitter strength
    0.f, // off
    0.50f, 0.59f, 0.67f, 0.74f, 0.80f,
    0.85f, 0.89f, 0.92f, 0.94f, 0.95f
  };
  static constexpr std::array<float, MAX_DEJITTER - MIN_DEJITTER + 1> dFac = {
    // lower values mean more dejitter strength
    1.f, // off
    1.0f / 181, 1.0f / 256, 1.0f / 362, 1.0f / 512, 1.0f / 724,
    1.0f / 1024, 1.0f / 1448, 1.0f / 2048, 1.0f / 2896, 1.0f / 4096
  };
  const float baseFactor = bFac[DEJITTER_BASE];
  const float diffFactor = dFac[DEJITTER_DIFF];

  // dejitter, suppress small changes only
  float dejitter = powf(baseFactor, std::abs(newAxis - lastAxis) * diffFactor);
  int newVal = newAxis * (1 - dejitter) + lastAxis * dejitter;

  // only use new dejittered value for larger differences
  if(abs(newVal - newAxis) > 10)
    newAxis = newVal;

  // apply linearity
  float linearVal = newAxis / (range / 2); // scale to -1.0..+1.0

  if(newAxis >= 0)
    linearVal = powf(std::abs(linearVal), LINEARITY);
  else
    linearVal = -powf(std::abs(linearVal), LINEARITY);

  newAxis = linearVal * (range / 2); // scale back to ANALOG_RANGE

  // scale axis to range including dead zone
  const Int32 scaledAxis = newAxis * ANALOG_RANGE / range;

  // scale result
  return AnalogReadout::connectToVcc(MAX_RESISTANCE *
        BSPF::clamp((ANALOG_MAX_VALUE - (scaledAxis * SENSITIVITY + center)) /
        float(ANALOG_RANGE), 0.F, 1.F));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Paddles::updateAnalogAxes()
{
  // Analog axis events from Stelladaptor-like devices,
  // (which includes analog USB controllers)
  // These devices generate data in the range -32768 to 32767,
  // so we have to scale appropriately
  // Since these events are generated and stored indefinitely,
  // we only process the first one we see (when it differs from
  // previous values by a pre-defined amount)
  // Otherwise, it would always override input from digital and mouse


  int sa_xaxis = myEvent.get(myAAxisValue);
  int sa_yaxis = myEvent.get(myBAxisValue);
  bool sa_changed = false;

  //if(abs(myLastAxisX - sa_xaxis) > 10)
  {
    setPin(AnalogPin::Nine, getReadOut(myLastAxisX, sa_xaxis, XCENTER));
    sa_changed = true;
  }

  //if(abs(myLastAxisY - sa_yaxis) > 10)
  {
    setPin(AnalogPin::Five, getReadOut(myLastAxisY, sa_yaxis, YCENTER));
    sa_changed = true;
  }
  myLastAxisX = sa_xaxis;
  myLastAxisY = sa_yaxis;

  return sa_changed;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::updateMouse(bool& firePressedA, bool& firePressedB)
{
  // Mouse motion events give relative movement
  // That is, they're only relevant if they're non-zero
  if(myMPaddleID > -1)
  {
    // We're in auto mode, where a single axis is used for one paddle only
    myCharge[myMPaddleID] = BSPF::clamp(myCharge[myMPaddleID] -
                                        (myEvent.get(myAxisMouseMotion) * MOUSE_SENSITIVITY),
                                        TRIGMIN, TRIGRANGE);

    if(myMPaddleID == 0)
      firePressedA = firePressedA
        || myEvent.get(Event::MouseButtonLeftValue)
        || myEvent.get(Event::MouseButtonRightValue);
    else
      firePressedB = firePressedB
        || myEvent.get(Event::MouseButtonLeftValue)
        || myEvent.get(Event::MouseButtonRightValue);
  }
  else
  {
    // Test for 'untied' mouse axis mode, where each axis is potentially
    // mapped to a separate paddle
    if(myMPaddleIDX > -1)
    {
      myCharge[myMPaddleIDX] = BSPF::clamp(myCharge[myMPaddleIDX] -
                                           (myEvent.get(Event::MouseAxisXMove) * MOUSE_SENSITIVITY),
                                           TRIGMIN, TRIGRANGE);
      if(myMPaddleIDX == 0)
        firePressedA = firePressedA
          || myEvent.get(Event::MouseButtonLeftValue);
      else
        firePressedB = firePressedB
          || myEvent.get(Event::MouseButtonLeftValue);
    }
    if(myMPaddleIDY > -1)
    {
      myCharge[myMPaddleIDY] = BSPF::clamp(myCharge[myMPaddleIDY] -
                                           (myEvent.get(Event::MouseAxisYMove) * MOUSE_SENSITIVITY),
                                           TRIGMIN, TRIGRANGE);
      if(myMPaddleIDY == 0)
        firePressedA = firePressedA
          || myEvent.get(Event::MouseButtonRightValue);
      else
        firePressedB = firePressedB
          || myEvent.get(Event::MouseButtonRightValue);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::updateDigitalAxes()
{
  // Finally, consider digital input, where movement happens
  // until a digital event is released
  if(myKeyRepeatA)
  {
    myPaddleRepeatA++;
    if(myPaddleRepeatA > DIGITAL_SENSITIVITY)
      myPaddleRepeatA = DIGITAL_DISTANCE;
  }
  if(myKeyRepeatB)
  {
    myPaddleRepeatB++;
    if(myPaddleRepeatB > DIGITAL_SENSITIVITY)
      myPaddleRepeatB = DIGITAL_DISTANCE;
  }

  myKeyRepeatA = false;
  myKeyRepeatB = false;

  if(myEvent.get(myLeftADecEvent))
  {
    myKeyRepeatA = true;
    if(myCharge[myAxisDigitalZero] > myPaddleRepeatA)
      myCharge[myAxisDigitalZero] -= myPaddleRepeatA;
  }
  if(myEvent.get(myLeftAIncEvent))
  {
    myKeyRepeatA = true;
    if((myCharge[myAxisDigitalZero] + myPaddleRepeatA) < TRIGRANGE)
      myCharge[myAxisDigitalZero] += myPaddleRepeatA;
  }
  if(myEvent.get(myLeftBDecEvent))
  {
    myKeyRepeatB = true;
    if(myCharge[myAxisDigitalOne] > myPaddleRepeatB)
      myCharge[myAxisDigitalOne] -= myPaddleRepeatB;
  }
  if(myEvent.get(myLeftBIncEvent))
  {
    myKeyRepeatB = true;
    if((myCharge[myAxisDigitalOne] + myPaddleRepeatB) < TRIGRANGE)
      myCharge[myAxisDigitalOne] += myPaddleRepeatB;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Paddles::setMouseControl(
    Controller::Type xtype, int xid, Controller::Type ytype, int yid)
{
  // In 'automatic' mode, both axes on the mouse map to a single paddle,
  // and the paddle axis and direction settings are taken into account
  // This overrides any other mode
  if(xtype == Controller::Type::Paddles && ytype == Controller::Type::Paddles && xid == yid)
  {
    myMPaddleID = ((myJack == Jack::Left && (xid == 0 || xid == 1)) ||
                   (myJack == Jack::Right && (xid == 2 || xid == 3))
                  ) ? xid & 0x01 : -1;
    myMPaddleIDX = myMPaddleIDY = -1;
  }
  else
  {
    // The following is somewhat complex, but we need to pre-process as much
    // as possible, so that ::update() can run quickly
    myMPaddleID = -1;
    if(myJack == Jack::Left)
    {
      if(xtype == Controller::Type::Paddles)
        myMPaddleIDX = (xid == 0 || xid == 1) ? xid & 0x01 : -1;
      if(ytype == Controller::Type::Paddles)
        myMPaddleIDY = (yid == 0 || yid == 1) ? yid & 0x01 : -1;
    }
    else if(myJack == Jack::Right)
    {
      if(xtype == Controller::Type::Paddles)
        myMPaddleIDX = (xid == 2 || xid == 3) ? xid & 0x01 : -1;
      if(ytype == Controller::Type::Paddles)
        myMPaddleIDY = (yid == 2 || yid == 3) ? yid & 0x01 : -1;
    }
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setAnalogXCenter(int xcenter)
{
  // convert into ~5 pixel steps
  XCENTER = BSPF::clamp(xcenter, MIN_ANALOG_CENTER, MAX_ANALOG_CENTER) * 860;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setAnalogYCenter(int ycenter)
{
  // convert into ~5 pixel steps
  YCENTER = BSPF::clamp(ycenter, MIN_ANALOG_CENTER, MAX_ANALOG_CENTER) * 860;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float Paddles::setAnalogSensitivity(int sensitivity)
{
  return SENSITIVITY = analogSensitivityValue(sensitivity);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float Paddles::analogSensitivityValue(int sensitivity)
{
  // BASE_ANALOG_SENSE * (1.1 ^ 20) = 1.0
  return BASE_ANALOG_SENSE * std::pow(1.1F,
    static_cast<float>(BSPF::clamp(sensitivity, MIN_ANALOG_SENSE, MAX_ANALOG_SENSE)));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setAnalogLinearity(int linearity)
{
  LINEARITY = 100.f / BSPF::clamp(linearity, MIN_ANALOG_LINEARITY, MAX_ANALOG_LINEARITY);
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setDejitterBase(int strength)
{
  DEJITTER_BASE = BSPF::clamp(strength, MIN_DEJITTER, MAX_DEJITTER);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setDejitterDiff(int strength)
{
  DEJITTER_DIFF = BSPF::clamp(strength, MIN_DEJITTER, MAX_DEJITTER);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setDigitalSensitivity(int sensitivity)
{
  DIGITAL_SENSITIVITY = BSPF::clamp(sensitivity, MIN_DIGITAL_SENSE, MAX_DIGITAL_SENSE);
  DIGITAL_DISTANCE = 20 + (DIGITAL_SENSITIVITY << 3);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Paddles::setDigitalPaddleRange(int range)
{
  range = BSPF::clamp(range, MIN_MOUSE_RANGE, MAX_MOUSE_RANGE);
  TRIGRANGE = int(TRIGMAX * (range / 100.0));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int Paddles::XCENTER = 0;
int Paddles::YCENTER = 0;
float Paddles::SENSITIVITY = 1.0;
float Paddles::LINEARITY = 1.0;
int Paddles::DEJITTER_BASE = 0;
int Paddles::DEJITTER_DIFF = 0;
int Paddles::TRIGRANGE = Paddles::TRIGMAX;

int Paddles::DIGITAL_SENSITIVITY = -1;
int Paddles::DIGITAL_DISTANCE = -1;
