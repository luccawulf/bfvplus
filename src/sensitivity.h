// Mouse sensitivity corrections. See sensitivity.cpp for the rationale.
#pragma once

#include <windows.h>

// Config (set from config.cpp, read by the DirectInput hooks).
extern bool  g_strafeFixEnabled;    // [sensitivity] correctedLookSensitivity
extern float g_strafeFixScale;      // [sensitivity] correctedLookScale   (default 0.5)
extern bool  g_strafeFixAnyMove;    // [sensitivity] correctedLookTrigger = any (else strafe)

extern bool  g_mgInfSensEnabled;    // [sensitivity] stationaryMGInfSensitivity

// Wrap the IDirectInput8 returned by DirectInput8Create so we can see the mouse
// device get created and scale its axes. Safe to call more than once.
void installSensitivityHooks(void* dinput8);

// Patch the two InfantryControls / LandSeaControls constructor call sites so we can
// record those objects as the game builds them, and read the live mouse sensitivity
// and invert settings off them later. Needed only by stationaryMGInfSensitivity.
// Must be installed during startup, before the settings root is constructed.
void installControlSettingsHooks();
