#pragma once

// [library:input] Gamepad button conventions the game hooks rely on. The emulated
// DirectInput joystick reports its buttons in the order Windows DirectInput reports an
// Xbox controller (g_joystickButtons in dinput.cpp, which checks these indices), so the
// game sees them as raw joystick button indices. Race and pause-dialog code reads the
// raw index; the menus remap it (MenuInputHandler::MapAllJoystickButtons).

// B: goes back. In menus it clicks the screen's Back button and in the pause dialog it
// resumes; during a race it stays an ordinary button (look back by default).
#define MINIWIN_GAMEPAD_BUTTON_B 1
// Menu (Start): pauses a race, and closes the pause dialog again.
#define MINIWIN_GAMEPAD_BUTTON_MENU 7

// The menus' logical joystick code for back (and delete in a joystick name field).
// MenuInputHandler remaps B to it, while every other button stays the select code 4.
#define MINIWIN_GAMEPAD_MENU_BACK 5

// The default joystick binding of a race action slot (GameState::InitializeInputBindings):
// the game's slots 2 to 8 are accelerate, brake, power-up, camera, HUD, powerslide and look
// back, and this returns the raw button index of the Xbox layout (A, X, LB, Y, View, RB, B).
// Other slots return -1, which keeps the original one-button-per-slot order.
int MiniwinGamepad_DefaultRaceButton(int p_slot);
