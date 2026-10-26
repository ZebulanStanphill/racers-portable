// TEMPORARY Xbox diagnostics for the USB-mouse path ("[diag-mouse]"). Counters are fed from
// the main thread (raw SDL events) and the game thread (everything after the queue), under a
// mutex; the summary runs on the game thread at most once per second and prints a line only
// when its content changed since it was last printed. Totals are cumulative since startup.
#include "miniwin.h"

#include <miniwin/diagmouse.h>
#include <miniwin/dinput.h>
#include <miniwin/miniwinapp.h>
#include <stdio.h>
#include <string.h>

namespace
{

constexpr Uint64 c_intervalMs = 1000;
constexpr int c_maxLines = 1500;

enum Source {
	SRC_GLOBAL, // SDL_GLOBAL_MOUSE_ID (0): SDL reports every non-relative mouse event with it
	SRC_DEFAULT,
	SRC_TOUCH,
	SRC_PEN,
	SRC_OTHER,
	SRC_COUNT,
};

Source SourceOf(SDL_MouseID p_id)
{
	if (p_id == 0) {
		return SRC_GLOBAL;
	}
	if (p_id == 1) {
		return SRC_DEFAULT;
	}
	if (p_id == SDL_TOUCH_MOUSEID) {
		return SRC_TOUCH;
	}
	if (p_id == SDL_PEN_MOUSEID) {
		return SRC_PEN;
	}
	return SRC_OTHER;
}

struct Counts {
	Uint32 motion[SRC_COUNT];
	Uint32 zeroMotion; // motion events whose xrel and yrel are both 0
	Uint32 down[SRC_COUNT];
	Uint32 up[SRC_COUNT];
	Uint32 wheel[SRC_COUNT];
	Uint32 finger[4]; // down, motion, up, canceled
	Uint32 mouseAdded;
	Uint32 mouseRemoved;
	Uint32 windowEnter;
	Uint32 windowLeave;
	Uint32 focusGained;
	Uint32 focusLost;
	double relX;
	double relY;
	float lastX;
	float lastY;
	Uint32 lastWhich;
	Uint32 lastWindow;
	Uint32 lastState;
	Uint32 lastButton;
};

struct State {
	Counts sdl;  // raw, main thread
	Counts game; // after the queue, game thread
	Uint32 wsDrops;
	Uint32 ringX, ringY, ringZ, ringDown, ringUp;
	long long ringSumX, ringSumY;
	Uint32 dataCalls, dataNonEmpty, dataItems;
	long long dataSumX, dataSumY, dataSumZ;
	Uint32 dataButtonDown[4], dataButtonUp[4];
	Uint32 stateCalls;
	Uint32 acquires, unacquires;
	bool acquired;
	Uint32 coopCalls;
	Uint32 coopFlags;
	const void* coopWindow;
	// MenuInputDispatcher::Update
	Uint32 menuFrames;
	bool menuHardware, menuFullscreen, menuVisible, menuEnabled;
	Uint32 menuAxisFrames;
	double menuAxisX, menuAxisY;
	int menuX, menuY;
	// UpdateMousePosition
	Uint32 pollTicks;
	bool windowed;
	Uint32 absUpdates, absMoved;
	long absOsX, absOsY, absUiX, absUiY;
	bool absWidescreen;
};

State g_state;
Uint64 g_lastSummary;
int g_lines;
char g_last[8][1024];

SDL_Mutex* Mutex()
{
	static SDL_Mutex* mutex = SDL_CreateMutex();
	return mutex;
}

struct Lock {
	Lock() { SDL_LockMutex(Mutex()); }
	~Lock() { SDL_UnlockMutex(Mutex()); }
};

void Count(Counts& p_counts, const SDL_Event& p_event)
{
	switch (p_event.type) {
	case SDL_EVENT_MOUSE_MOTION: {
		const SDL_MouseMotionEvent& m = p_event.motion;
		p_counts.motion[SourceOf(m.which)]++;
		if (m.xrel == 0.f && m.yrel == 0.f) {
			p_counts.zeroMotion++;
		}
		p_counts.relX += m.xrel;
		p_counts.relY += m.yrel;
		p_counts.lastX = m.x;
		p_counts.lastY = m.y;
		p_counts.lastWhich = m.which;
		p_counts.lastWindow = m.windowID;
		p_counts.lastState = m.state;
		break;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const SDL_MouseButtonEvent& b = p_event.button;
		(b.down ? p_counts.down : p_counts.up)[SourceOf(b.which)]++;
		p_counts.lastX = b.x;
		p_counts.lastY = b.y;
		p_counts.lastWhich = b.which;
		p_counts.lastWindow = b.windowID;
		p_counts.lastButton = b.button;
		break;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		p_counts.wheel[SourceOf(p_event.wheel.which)]++;
		break;
	case SDL_EVENT_FINGER_DOWN:
		p_counts.finger[0]++;
		break;
	case SDL_EVENT_FINGER_MOTION:
		p_counts.finger[1]++;
		break;
	case SDL_EVENT_FINGER_UP:
		p_counts.finger[2]++;
		break;
	case SDL_EVENT_FINGER_CANCELED:
		p_counts.finger[3]++;
		break;
	case SDL_EVENT_MOUSE_ADDED:
		p_counts.mouseAdded++;
		break;
	case SDL_EVENT_MOUSE_REMOVED:
		p_counts.mouseRemoved++;
		break;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
		p_counts.windowEnter++;
		break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		p_counts.windowLeave++;
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		p_counts.focusGained++;
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		p_counts.focusLost++;
		break;
	default:
		break;
	}
}

// "total [global/default/touch/pen/other]"
void Split(char* p_out, size_t p_size, const Uint32 (&p_values)[SRC_COUNT])
{
	Uint32 total = 0;
	for (int i = 0; i < SRC_COUNT; i++) {
		total += p_values[i];
	}
	SDL_snprintf(
		p_out,
		p_size,
		"%u[g%u/d%u/t%u/p%u/o%u]",
		total,
		p_values[0],
		p_values[1],
		p_values[2],
		p_values[3],
		p_values[4]
	);
}

void FormatCounts(char* p_out, size_t p_size, const Counts& p_c)
{
	char motion[96];
	char down[96];
	char up[96];
	char wheel[96];
	Split(motion, sizeof(motion), p_c.motion);
	Split(down, sizeof(down), p_c.down);
	Split(up, sizeof(up), p_c.up);
	Split(wheel, sizeof(wheel), p_c.wheel);
	SDL_snprintf(
		p_out,
		p_size,
		"motion=%s zero-delta=%u button-down=%s button-up=%s wheel=%s finger(d/m/u/c)=%u/%u/%u/%u "
		"mouse(+/-)=%u/%u window(enter/leave)=%u/%u focus(+/-)=%u/%u sum-rel=(%+.1f,%+.1f) last=(%.1f,%.1f) "
		"last-which=%u last-window=%u last-state=0x%x last-button=%u",
		motion,
		p_c.zeroMotion,
		down,
		up,
		wheel,
		p_c.finger[0],
		p_c.finger[1],
		p_c.finger[2],
		p_c.finger[3],
		p_c.mouseAdded,
		p_c.mouseRemoved,
		p_c.windowEnter,
		p_c.windowLeave,
		p_c.focusGained,
		p_c.focusLost,
		p_c.relX,
		p_c.relY,
		(double) p_c.lastX,
		(double) p_c.lastY,
		p_c.lastWhich,
		p_c.lastWindow,
		p_c.lastState,
		p_c.lastButton
	);
}

// Logs p_line when it differs from what slot p_slot printed last.
void Emit(int p_slot, const char* p_name, const char* p_line)
{
	if (SDL_strcmp(g_last[p_slot], p_line) == 0) {
		return;
	}
	SDL_strlcpy(g_last[p_slot], p_line, sizeof(g_last[p_slot]));
	if (g_lines == c_maxLines) {
		g_lines++;
		SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[diag-mouse] line cap reached, no further summaries");
	}
	else if (g_lines < c_maxLines) {
		g_lines++;
		SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[diag-mouse] %s: %s", p_name, p_line);
	}
}

void FormatWindowFlags(char* p_out, size_t p_size, SDL_WindowFlags p_flags)
{
	SDL_snprintf(
		p_out,
		p_size,
		"0x%llx%s%s%s%s%s%s%s%s%s",
		(unsigned long long) p_flags,
		(p_flags & SDL_WINDOW_FULLSCREEN) ? " FULLSCREEN" : "",
		(p_flags & SDL_WINDOW_MAXIMIZED) ? " MAXIMIZED" : "",
		(p_flags & SDL_WINDOW_HIDDEN) ? " HIDDEN" : "",
		(p_flags & SDL_WINDOW_OCCLUDED) ? " OCCLUDED" : "",
		(p_flags & SDL_WINDOW_INPUT_FOCUS) ? " INPUT_FOCUS" : "",
		(p_flags & SDL_WINDOW_MOUSE_FOCUS) ? " MOUSE_FOCUS" : "",
		(p_flags & SDL_WINDOW_MOUSE_GRABBED) ? " MOUSE_GRABBED" : "",
		(p_flags & SDL_WINDOW_MOUSE_RELATIVE_MODE) ? " MOUSE_RELATIVE" : "",
		(p_flags & SDL_WINDOW_MOUSE_CAPTURE) ? " MOUSE_CAPTURE" : ""
	);
}

// SDL's own view of the mouse; queried on the main thread.
void FormatSdlState(char* p_out, size_t p_size)
{
	float x = 0.f;
	float y = 0.f;
	float gx = 0.f;
	float gy = 0.f;
	SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
	SDL_MouseButtonFlags globalButtons = SDL_GetGlobalMouseState(&gx, &gy);
	SDL_Window* mouseFocus = SDL_GetMouseFocus();
	SDL_Window* keyboardFocus = SDL_GetKeyboardFocus();
	int windowCount = 0;
	SDL_Window** windows = SDL_GetWindows(&windowCount);
	SDL_Window* window = keyboardFocus ? keyboardFocus : (windowCount > 0 ? windows[0] : nullptr);

	char flags[200] = "no-window";
	int pointsW = 0;
	int pointsH = 0;
	int pixelsW = 0;
	int pixelsH = 0;
	float density = 0.f;
	int desktopW = 0;
	int desktopH = 0;
	bool relative = false;
	bool grabbed = false;
	if (window) {
		FormatWindowFlags(flags, sizeof(flags), SDL_GetWindowFlags(window));
		SDL_GetWindowSize(window, &pointsW, &pointsH);
		SDL_GetWindowSizeInPixels(window, &pixelsW, &pixelsH);
		density = SDL_GetWindowPixelDensity(window);
		relative = SDL_GetWindowRelativeMouseMode(window);
		grabbed = SDL_GetWindowMouseGrab(window);
		const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
		if (desktop) {
			desktopW = desktop->w;
			desktopH = desktop->h;
		}
	}
	int miceCount = 0;
	SDL_MouseID* mice = SDL_GetMice(&miceCount);
	const char* firstMouse = miceCount > 0 ? SDL_GetMouseNameForID(mice[0]) : nullptr;
	int touchCount = 0;
	SDL_TouchID* touches = SDL_GetTouchDevices(&touchCount);

	SDL_snprintf(
		p_out,
		p_size,
		"window{%s} size-pt=%dx%d size-px=%dx%d density=%.2f desktop-mode=%dx%d is-fullscreen(game gate)=%d | "
		"relative-mode=%d mouse-grab=%d cursor-visible=%d | mouse-focus=%s keyboard-focus=%s | "
		"state(window)=(%.1f,%.1f) buttons=0x%x global=(%.1f,%.1f) buttons=0x%x | has-mouse=%d mice=%d%s%s "
		"touch-devices=%d | hints touch-mouse-events=%s mouse-touch-events=%s",
		flags,
		pointsW,
		pointsH,
		pixelsW,
		pixelsH,
		(double) density,
		desktopW,
		desktopH,
		MiniwinApp_IsWindowFullscreen() ? 1 : 0,
		relative ? 1 : 0,
		grabbed ? 1 : 0,
		SDL_CursorVisible() ? 1 : 0,
		mouseFocus ? "yes" : "no",
		keyboardFocus ? "yes" : "no",
		(double) x,
		(double) y,
		(unsigned) buttons,
		(double) gx,
		(double) gy,
		(unsigned) globalButtons,
		SDL_HasMouse() ? 1 : 0,
		miceCount,
		firstMouse ? " first=" : "",
		firstMouse ? firstMouse : "",
		touchCount,
		SDL_GetHint(SDL_HINT_TOUCH_MOUSE_EVENTS) ? SDL_GetHint(SDL_HINT_TOUCH_MOUSE_EVENTS) : "default",
		SDL_GetHint(SDL_HINT_MOUSE_TOUCH_EVENTS) ? SDL_GetHint(SDL_HINT_MOUSE_TOUCH_EVENTS) : "default"
	);
	SDL_free(windows);
	SDL_free(mice);
	SDL_free(touches);
}

} // namespace

void MiniwinDiagMouse_OnSdlEvent(const SDL_Event& p_event)
{
	Lock lock;
	Count(g_state.sdl, p_event);
}

void MiniwinDiagMouse_OnGameEvent(const SDL_Event& p_event)
{
	Lock lock;
	Count(g_state.game, p_event);
}

void MiniwinDiagMouse_WidescreenDrop()
{
	Lock lock;
	g_state.wsDrops++;
}

void MiniwinDiagMouse_RingPush(unsigned p_offset, unsigned p_data)
{
	Lock lock;
	switch (p_offset) {
	case DIMOFS_X:
		g_state.ringX++;
		g_state.ringSumX += (int) p_data;
		break;
	case DIMOFS_Y:
		g_state.ringY++;
		g_state.ringSumY += (int) p_data;
		break;
	case DIMOFS_Z:
		g_state.ringZ++;
		break;
	default:
		(p_data & 0x80 ? g_state.ringDown : g_state.ringUp)++;
		break;
	}
}

void MiniwinDiagMouse_DeviceData(unsigned p_requested, unsigned p_produced)
{
	(void) p_requested;
	Lock lock;
	g_state.dataCalls++;
	if (p_produced) {
		g_state.dataNonEmpty++;
		g_state.dataItems += p_produced;
	}
}

void MiniwinDiagMouse_DeviceItem(unsigned p_offset, unsigned p_data)
{
	Lock lock;
	switch (p_offset) {
	case DIMOFS_X:
		g_state.dataSumX += (int) p_data;
		break;
	case DIMOFS_Y:
		g_state.dataSumY += (int) p_data;
		break;
	case DIMOFS_Z:
		g_state.dataSumZ += (int) p_data;
		break;
	default:
		if (p_offset >= DIMOFS_BUTTON0 && p_offset <= DIMOFS_BUTTON3) {
			(p_data & 0x80 ? g_state.dataButtonDown : g_state.dataButtonUp)[p_offset - DIMOFS_BUTTON0]++;
		}
		break;
	}
}

void MiniwinDiagMouse_DeviceState(unsigned p_size)
{
	(void) p_size;
	Lock lock;
	g_state.stateCalls++;
}

void MiniwinDiagMouse_Acquire(bool p_acquired)
{
	Lock lock;
	(p_acquired ? g_state.acquires : g_state.unacquires)++;
	g_state.acquired = p_acquired;
}

void MiniwinDiagMouse_CooperativeLevel(unsigned p_flags, const void* p_window)
{
	Lock lock;
	g_state.coopCalls++;
	g_state.coopFlags = p_flags;
	g_state.coopWindow = p_window;
}

void MiniwinDiagMouse_MenuCursor(
	bool p_hardwareDevice,
	float p_axisX,
	float p_axisY,
	int p_cursorX,
	int p_cursorY,
	bool p_visible,
	bool p_enabled
)
{
	Lock lock;
	g_state.menuFrames++;
	g_state.menuHardware = p_hardwareDevice;
	g_state.menuFullscreen = MiniwinApp_IsWindowFullscreen();
	g_state.menuVisible = p_visible;
	g_state.menuEnabled = p_enabled;
	g_state.menuX = p_cursorX;
	g_state.menuY = p_cursorY;
	if (p_axisX != 0.f || p_axisY != 0.f) {
		g_state.menuAxisFrames++;
		g_state.menuAxisX += p_axisX;
		g_state.menuAxisY += p_axisY;
	}
}

void MiniwinDiagMouse_AbsoluteCursor(long p_osX, long p_osY, bool p_moved, bool p_widescreen)
{
	Lock lock;
	g_state.absUpdates++;
	g_state.absOsX = p_osX;
	g_state.absOsY = p_osY;
	g_state.absWidescreen = p_widescreen;
	if (p_moved) {
		g_state.absMoved++;
	}
}

void MiniwinDiagMouse_AbsoluteCursorUi(long p_uiX, long p_uiY)
{
	Lock lock;
	g_state.absUiX = p_uiX;
	g_state.absUiY = p_uiY;
}

void MiniwinDiagMouse_Poll(bool p_windowed)
{
	Uint64 now = SDL_GetTicks();
	{
		Lock lock;
		g_state.pollTicks++;
		g_state.windowed = p_windowed;
	}
	if (now - g_lastSummary < c_intervalMs) {
		return;
	}
	g_lastSummary = now;

	// SDL's view first: it must be read on the main thread, outside our lock.
	char sdlState[1024];
	MiniwinApp_RunOnMainThread([&sdlState]() { FormatSdlState(sdlState, sizeof(sdlState)); });

	State s;
	{
		Lock lock;
		s = g_state;
	}
	char counts[900];
	char line[1024];

	FormatCounts(counts, sizeof(counts), s.sdl);
	Emit(0, "sdl-main (raw SDL events, main thread)", counts);

	FormatCounts(counts, sizeof(counts), s.game);
	SDL_snprintf(
		line,
		sizeof(line),
		"%s | ring-pushed x=%u(%+lld) y=%u(%+lld) z=%u btn-down=%u btn-up=%u | widescreen-side-panel-drops=%u",
		counts,
		s.ringX,
		s.ringSumX,
		s.ringY,
		s.ringSumY,
		s.ringZ,
		s.ringDown,
		s.ringUp,
		s.wsDrops
	);
	Emit(1, "game-in (after queue, game thread)", line);

	SDL_snprintf(
		line,
		sizeof(line),
		"GetDeviceData non-empty-calls=%u items=%u sum-dx=%+lld sum-dy=%+lld sum-dz=%+lld "
		"btn0-3-down/up=%u/%u,%u/%u,%u/%u,%u/%u | GetDeviceState called=%d (miniwin returns all zeros for the mouse) | "
		"acquire=%u unacquire=%u acquired-now=%d | SetCooperativeLevel calls=%u last-flags=0x%x hwnd=%p",
		s.dataNonEmpty,
		s.dataItems,
		s.dataSumX,
		s.dataSumY,
		s.dataSumZ,
		s.dataButtonDown[0],
		s.dataButtonUp[0],
		s.dataButtonDown[1],
		s.dataButtonUp[1],
		s.dataButtonDown[2],
		s.dataButtonUp[2],
		s.dataButtonDown[3],
		s.dataButtonUp[3],
		s.stateCalls ? 1 : 0,
		s.acquires,
		s.unacquires,
		s.acquired ? 1 : 0,
		s.coopCalls,
		s.coopFlags,
		s.coopWindow
	);
	Emit(2, "di-read (what the game's mouse device returned)", line);

	SDL_snprintf(
		line,
		sizeof(line),
		"mode=%s | menu: seen=%d hardware-device-flag=%d window-fullscreen=%d relative-accumulation-runs=%d "
		"cursor-visible=%d cursor-enabled=%d axis-frames=%u axis-sum=(%+.0f,%+.0f) cursor-pos=(%d,%d) | "
		"absolute(UpdateMousePosition): updates=%u moved=%u last-os=(%ld,%ld) last-ui=(%ld,%ld) widescreen=%d",
		s.windowed ? "windowed(absolute mapping)" : "fullscreen(relative accumulation only)",
		s.menuFrames ? 1 : 0,
		s.menuHardware ? 1 : 0,
		s.menuFullscreen ? 1 : 0,
		(s.menuHardware && s.menuFullscreen) ? 1 : 0,
		s.menuVisible ? 1 : 0,
		s.menuEnabled ? 1 : 0,
		s.menuAxisFrames,
		s.menuAxisX,
		s.menuAxisY,
		s.menuX,
		s.menuY,
		s.absUpdates,
		s.absMoved,
		s.absOsX,
		s.absOsY,
		s.absUiX,
		s.absUiY,
		s.absWidescreen ? 1 : 0
	);
	Emit(3, "cursor", line);

	Emit(4, "sdl-state", sdlState);
}
