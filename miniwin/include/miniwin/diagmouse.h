#pragma once

// TEMPORARY Xbox diagnostics: the mouse path from SDL events to the game's cursor
// ("[diag-mouse]" log lines, summarised at most once per second and only when something
// changed). Remove this file, diagmouse.cpp and every MiniwinDiagMouse_* call once the
// Xbox USB-mouse problem is understood.

#include <SDL3/SDL.h>

// Main thread: every SDL event as it enters the miniwin queue (MiniwinApp_PushEvent).
void MiniwinDiagMouse_OnSdlEvent(const SDL_Event& p_event);
// Game thread: every event drained from the queue (MiniwinInput_HandleEvent).
void MiniwinDiagMouse_OnGameEvent(const SDL_Event& p_event);
// A mouse press in a widescreen side panel that was not forwarded to the game.
void MiniwinDiagMouse_WidescreenDrop();
// An entry pushed into the buffered mouse ring (what GetDeviceData will return).
void MiniwinDiagMouse_RingPush(unsigned p_offset, unsigned p_data);
// The game's DirectInput mouse device.
void MiniwinDiagMouse_DeviceData(unsigned p_requested, unsigned p_produced);
void MiniwinDiagMouse_DeviceItem(unsigned p_offset, unsigned p_data);
void MiniwinDiagMouse_DeviceState(unsigned p_size);
void MiniwinDiagMouse_Acquire(bool p_acquired);
void MiniwinDiagMouse_CooperativeLevel(unsigned p_flags, const void* p_window);
// MenuInputDispatcher::Update, once per menu frame: the relative-motion path (fullscreen).
void MiniwinDiagMouse_MenuCursor(
	bool p_hardwareDevice,
	float p_axisX,
	float p_axisY,
	int p_cursorX,
	int p_cursorY,
	bool p_visible,
	bool p_enabled
);
// Win32GolApp::UpdateMousePosition, once per tick: the absolute-position path (windowed
// only). Also drives the once-per-second summary.
void MiniwinDiagMouse_Poll(bool p_windowed);
void MiniwinDiagMouse_AbsoluteCursor(long p_osX, long p_osY, bool p_moved, bool p_widescreen);
// The game-space position about to be passed to OnCursorMoved (only when the OS cursor moved).
void MiniwinDiagMouse_AbsoluteCursorUi(long p_uiX, long p_uiY);
