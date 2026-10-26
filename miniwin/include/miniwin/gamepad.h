#pragma once

// [library:input] Gamepad button conventions the game hooks rely on. The emulated
// DirectInput joystick reports its buttons in the order Windows DirectInput reports an
// Xbox controller (g_joystickButtons in dinput.cpp, which checks these indices), so the
// game sees them as raw joystick button indices. Race and pause-dialog code reads the
// raw index; the menus remap it (MenuInputHandler::MapAllJoystickButtons).

// Menu (Start): pauses a race, and closes the pause dialog again.
#define MINIWIN_GAMEPAD_BUTTON_MENU 7
