// [library:input] DirectInput 5 emulation over SDL3. Buffered keyboard/mouse device
// data is fed from the game thread's event drain (MiniwinInput_HandleEvent, called by
// Win32GolApp::Tick for every forwarded SDL event); gamepads are polled through the
// SDL3 gamepad API; the GUID_Sine force-feedback effect maps to gamepad rumble.

#include "miniwin.h"

#include <miniwin/dinput.h>
#include <miniwin/miniwinapp.h>
#include <miniwin/widescreen.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// --- SDL scancode -> DirectInput DIK_* (PC scan code set 1) ---

static Uint8 ScancodeToDik(SDL_Scancode p_scancode)
{
	switch (p_scancode) {
	case SDL_SCANCODE_ESCAPE:
		return 0x01;
	case SDL_SCANCODE_1:
		return 0x02;
	case SDL_SCANCODE_2:
		return 0x03;
	case SDL_SCANCODE_3:
		return 0x04;
	case SDL_SCANCODE_4:
		return 0x05;
	case SDL_SCANCODE_5:
		return 0x06;
	case SDL_SCANCODE_6:
		return 0x07;
	case SDL_SCANCODE_7:
		return 0x08;
	case SDL_SCANCODE_8:
		return 0x09;
	case SDL_SCANCODE_9:
		return 0x0A;
	case SDL_SCANCODE_0:
		return 0x0B;
	case SDL_SCANCODE_MINUS:
		return 0x0C;
	case SDL_SCANCODE_EQUALS:
		return 0x0D;
	case SDL_SCANCODE_BACKSPACE:
		return 0x0E;
	case SDL_SCANCODE_TAB:
		return 0x0F;
	case SDL_SCANCODE_Q:
		return 0x10;
	case SDL_SCANCODE_W:
		return 0x11;
	case SDL_SCANCODE_E:
		return 0x12;
	case SDL_SCANCODE_R:
		return 0x13;
	case SDL_SCANCODE_T:
		return 0x14;
	case SDL_SCANCODE_Y:
		return 0x15;
	case SDL_SCANCODE_U:
		return 0x16;
	case SDL_SCANCODE_I:
		return 0x17;
	case SDL_SCANCODE_O:
		return 0x18;
	case SDL_SCANCODE_P:
		return 0x19;
	case SDL_SCANCODE_LEFTBRACKET:
		return 0x1A;
	case SDL_SCANCODE_RIGHTBRACKET:
		return 0x1B;
	case SDL_SCANCODE_RETURN:
		return 0x1C;
	case SDL_SCANCODE_LCTRL:
		return 0x1D;
	case SDL_SCANCODE_A:
		return 0x1E;
	case SDL_SCANCODE_S:
		return 0x1F;
	case SDL_SCANCODE_D:
		return 0x20;
	case SDL_SCANCODE_F:
		return 0x21;
	case SDL_SCANCODE_G:
		return 0x22;
	case SDL_SCANCODE_H:
		return 0x23;
	case SDL_SCANCODE_J:
		return 0x24;
	case SDL_SCANCODE_K:
		return 0x25;
	case SDL_SCANCODE_L:
		return 0x26;
	case SDL_SCANCODE_SEMICOLON:
		return 0x27;
	case SDL_SCANCODE_APOSTROPHE:
		return 0x28;
	case SDL_SCANCODE_GRAVE:
		return 0x29;
	case SDL_SCANCODE_LSHIFT:
		return 0x2A;
	case SDL_SCANCODE_BACKSLASH:
		return 0x2B;
	case SDL_SCANCODE_Z:
		return 0x2C;
	case SDL_SCANCODE_X:
		return 0x2D;
	case SDL_SCANCODE_C:
		return 0x2E;
	case SDL_SCANCODE_V:
		return 0x2F;
	case SDL_SCANCODE_B:
		return 0x30;
	case SDL_SCANCODE_N:
		return 0x31;
	case SDL_SCANCODE_M:
		return 0x32;
	case SDL_SCANCODE_COMMA:
		return 0x33;
	case SDL_SCANCODE_PERIOD:
		return 0x34;
	case SDL_SCANCODE_SLASH:
		return 0x35;
	case SDL_SCANCODE_RSHIFT:
		return 0x36;
	case SDL_SCANCODE_KP_MULTIPLY:
		return 0x37;
	case SDL_SCANCODE_LALT:
		return 0x38;
	case SDL_SCANCODE_SPACE:
		return 0x39;
	case SDL_SCANCODE_CAPSLOCK:
		return 0x3A;
	case SDL_SCANCODE_F1:
		return 0x3B;
	case SDL_SCANCODE_F2:
		return 0x3C;
	case SDL_SCANCODE_F3:
		return 0x3D;
	case SDL_SCANCODE_F4:
		return 0x3E;
	case SDL_SCANCODE_F5:
		return 0x3F;
	case SDL_SCANCODE_F6:
		return 0x40;
	case SDL_SCANCODE_F7:
		return 0x41;
	case SDL_SCANCODE_F8:
		return 0x42;
	case SDL_SCANCODE_F9:
		return 0x43;
	case SDL_SCANCODE_F10:
		return 0x44;
	case SDL_SCANCODE_NUMLOCKCLEAR:
		return 0x45;
	case SDL_SCANCODE_SCROLLLOCK:
		return 0x46;
	case SDL_SCANCODE_KP_7:
		return 0x47;
	case SDL_SCANCODE_KP_8:
		return 0x48;
	case SDL_SCANCODE_KP_9:
		return 0x49;
	case SDL_SCANCODE_KP_MINUS:
		return 0x4A;
	case SDL_SCANCODE_KP_4:
		return 0x4B;
	case SDL_SCANCODE_KP_5:
		return 0x4C;
	case SDL_SCANCODE_KP_6:
		return 0x4D;
	case SDL_SCANCODE_KP_PLUS:
		return 0x4E;
	case SDL_SCANCODE_KP_1:
		return 0x4F;
	case SDL_SCANCODE_KP_2:
		return 0x50;
	case SDL_SCANCODE_KP_3:
		return 0x51;
	case SDL_SCANCODE_KP_0:
		return 0x52;
	case SDL_SCANCODE_KP_PERIOD:
		return 0x53;
	case SDL_SCANCODE_F11:
		return 0x57;
	case SDL_SCANCODE_F12:
		return 0x58;
	case SDL_SCANCODE_KP_ENTER:
		return 0x9C;
	case SDL_SCANCODE_RCTRL:
		return 0x9D;
	case SDL_SCANCODE_KP_DIVIDE:
		return 0xB5;
	case SDL_SCANCODE_PRINTSCREEN:
		return 0xB7;
	case SDL_SCANCODE_RALT:
		return 0xB8;
	case SDL_SCANCODE_PAUSE:
		return 0xC5;
	case SDL_SCANCODE_HOME:
		return 0xC7;
	case SDL_SCANCODE_UP:
		return 0xC8;
	case SDL_SCANCODE_PAGEUP:
		return 0xC9;
	case SDL_SCANCODE_LEFT:
		return 0xCB;
	case SDL_SCANCODE_RIGHT:
		return 0xCD;
	case SDL_SCANCODE_END:
		return 0xCF;
	case SDL_SCANCODE_DOWN:
		return 0xD0;
	case SDL_SCANCODE_PAGEDOWN:
		return 0xD1;
	case SDL_SCANCODE_INSERT:
		return 0xD2;
	case SDL_SCANCODE_DELETE:
		return 0xD3;
	case SDL_SCANCODE_LGUI:
		return 0xDB;
	case SDL_SCANCODE_RGUI:
		return 0xDC;
	case SDL_SCANCODE_APPLICATION:
		return 0xDD;
	default:
		return 0;
	}
}

static SDL_Scancode DikToScancode(Uint8 p_dik)
{
	for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
		if (ScancodeToDik((SDL_Scancode) i) == p_dik) {
			return (SDL_Scancode) i;
		}
	}
	return SDL_SCANCODE_UNKNOWN;
}

// --- Buffered device data rings (all access is on the game thread: the event drain
// pushes, PollDevices' GetDeviceData pops) ---

struct InputRing {
	DIDEVICEOBJECTDATA m_items[512];
	Uint32 m_head = 0;
	Uint32 m_count = 0;

	void Push(DWORD p_ofs, DWORD p_data)
	{
		if (m_count == SDL_arraysize(m_items)) {
			m_head = (m_head + 1) % SDL_arraysize(m_items);
			m_count--;
		}
		DIDEVICEOBJECTDATA& item = m_items[(m_head + m_count) % SDL_arraysize(m_items)];
		item.dwOfs = p_ofs;
		item.dwData = p_data;
		item.dwTimeStamp = SDL_GetTicks();
		item.dwSequence = 0;
		m_count++;
	}

	bool Pop(DIDEVICEOBJECTDATA& p_out)
	{
		if (!m_count) {
			return false;
		}
		p_out = m_items[m_head];
		m_head = (m_head + 1) % SDL_arraysize(m_items);
		m_count--;
		return true;
	}
};

static InputRing g_keyboardRing;
static InputRing g_mouseRing;

// Borrowed from the DirectInput joystick devices; only the game thread changes
// this list. Reading the raw stick here leaves lRx/lRy and race bindings intact.
static std::vector<SDL_Gamepad*> g_cursorGamepads;

bool MiniwinInput_GetMenuGamepadMotion(
	Uint32 p_elapsedMs,
	int p_logicalWidth,
	bool p_cursorActive,
	long* p_dx,
	long* p_dy
)
{
	static float remainderX = 0.0f;
	static float remainderY = 0.0f;
	*p_dx = 0;
	*p_dy = 0;
	float x = 0.0f;
	float y = 0.0f;
	float magnitudeSquared = 0.0f;
	if (p_cursorActive) {
		for (SDL_Gamepad* gamepad : g_cursorGamepads) {
			float gx = (float) SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32768.0f;
			float gy = (float) SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32768.0f;
			float squared = gx * gx + gy * gy;
			if (squared > magnitudeSquared) {
				// Use the strongest stick, so several pads cannot multiply the speed.
				x = gx;
				y = gy;
				magnitudeSquared = squared;
			}
		}
	}

	const float deadZone = 0.22f;
	float magnitude = SDL_sqrtf(magnitudeSquared);
	if (magnitude <= deadZone || p_logicalWidth <= 0) {
		remainderX = 0.0f;
		remainderY = 0.0f;
		return false;
	}
	float deflection = (SDL_min(magnitude, 1.0f) - deadZone) / (1.0f - deadZone);
	// Full stick crosses 3/4 of the menu width per second. A quadratic ramp gives
	// fine control near center; cap stalls and retain subpixel motion across frames.
	float distance =
		0.75f * (float) p_logicalWidth * deflection * deflection * (float) SDL_min(p_elapsedMs, 100u) / 1000.0f;
	remainderX += x / magnitude * distance;
	remainderY += y / magnitude * distance;
	*p_dx = (long) remainderX;
	*p_dy = (long) remainderY;
	remainderX -= (float) *p_dx;
	remainderY -= (float) *p_dy;
#ifdef WINDOWS_STORE
	static bool loggedMotion = false;
	if (!loggedMotion && (*p_dx || *p_dy)) {
		loggedMotion = true;
		SDL_LogInfo(LOG_CATEGORY_MINIWIN, "Menu right-stick cursor: first motion dx=%ld dy=%ld", *p_dx, *p_dy);
	}
#endif
	return *p_dx != 0 || *p_dy != 0;
}

static float g_mouseScaleX = 1.0f;
static float g_mouseScaleY = 1.0f;

void MiniwinInput_SetMouseScale(float p_scaleX, float p_scaleY)
{
	g_mouseScaleX = p_scaleX;
	g_mouseScaleY = p_scaleY;
}

void MiniwinInput_InjectMouseButton(bool p_down)
{
	g_mouseRing.Push(DIMOFS_BUTTON0, p_down ? 0x80 : 0x00);
}

void MiniwinInput_InjectMouseMotion(long p_dx, long p_dy)
{
	if (p_dx != 0) {
		g_mouseRing.Push(DIMOFS_X, (DWORD) (LONG) p_dx);
	}
	if (p_dy != 0) {
		g_mouseRing.Push(DIMOFS_Y, (DWORD) (LONG) p_dy);
	}
}

void MiniwinInput_HandleEvent(const SDL_Event& p_event)
{
	switch (p_event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP: {
		if (p_event.key.repeat) {
			break; // DirectInput buffered keyboards do not auto-repeat.
		}
		Uint8 dik = ScancodeToDik(p_event.key.scancode);
		if (dik) {
			g_keyboardRing.Push(dik, p_event.type == SDL_EVENT_KEY_DOWN ? 0x80 : 0x00);
		}
		break;
	}
	case SDL_EVENT_MOUSE_MOTION: {
#ifdef WINDOWS_STORE
		static bool loggedMotion = false;
		if (!loggedMotion && p_event.motion.which != SDL_TOUCH_MOUSEID) {
			loggedMotion = true;
			SDL_LogInfo(
				LOG_CATEGORY_MINIWIN,
				"UWP first mouse motion: x=%.1f y=%.1f dx=%.1f dy=%.1f",
				p_event.motion.x,
				p_event.motion.y,
				p_event.motion.xrel,
				p_event.motion.yrel
			);
		}
#endif
		// Scale the raw window-space delta into the game's cursor space (consumed by the menu
		// cursor's relative accumulation and by widget drag handlers). The fractional remainder
		// is carried across events so slow motion is not lost to integer rounding.
		static float accumX = 0.0f;
		static float accumY = 0.0f;
		accumX += p_event.motion.xrel * g_mouseScaleX;
		accumY += p_event.motion.yrel * g_mouseScaleY;
		LONG dx = (LONG) accumX;
		LONG dy = (LONG) accumY;
		accumX -= (float) dx;
		accumY -= (float) dy;
		if (dx != 0) {
			g_mouseRing.Push(DIMOFS_X, (DWORD) dx);
		}
		if (dy != 0) {
			g_mouseRing.Push(DIMOFS_Y, (DWORD) dy);
		}
		break;
	}
	case SDL_EVENT_MOUSE_WHEEL:
		if (p_event.wheel.y != 0.0f) {
			g_mouseRing.Push(DIMOFS_Z, (DWORD) (LONG) (p_event.wheel.y * 120.0f));
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
#ifdef WINDOWS_STORE
		static bool loggedButton[2] = {};
		int edge = p_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0 : 1;
		if (!loggedButton[edge] && p_event.button.which != SDL_TOUCH_MOUSEID) {
			loggedButton[edge] = true;
			SDL_LogInfo(
				LOG_CATEGORY_MINIWIN,
				"UWP first mouse button %s: button=%u",
				edge == 0 ? "down" : "up",
				(unsigned) p_event.button.button
			);
		}
#endif
		// [library:input] A press in a widescreen side panel is not a UI click. Fullscreen
		// drives the game cursor by relative motion, decoupled from the OS pointer.
		if (p_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && MiniwinWidescreen_Active()) {
			SDL_Window* window = SDL_GetWindowFromID(p_event.button.windowID);
			int width = 0;
			int height = 0;
			if (window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
				SDL_GetWindowSize(window, &width, &height);
			}
			long x;
			long y;
			if (width > 0 && height > 0 &&
				!MiniwinWidescreen_MapUI(p_event.button.x / width, p_event.button.y / height, 640, 480, &x, &y)) {
				break;
			}
		}
		DWORD ofs;
		switch (p_event.button.button) {
		case SDL_BUTTON_LEFT:
			ofs = DIMOFS_BUTTON0;
			break;
		case SDL_BUTTON_RIGHT:
			ofs = DIMOFS_BUTTON1;
			break;
		case SDL_BUTTON_MIDDLE:
			ofs = DIMOFS_BUTTON2;
			break;
		case SDL_BUTTON_X1:
			ofs = DIMOFS_BUTTON3;
			break;
		default:
			return;
		}
		g_mouseRing.Push(ofs, p_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0x80 : 0x00);
		break;
	}
	default:
		break;
	}
}

// --- Joystick instance GUIDs (synthesized from the SDL instance id) ---

static GUID JoystickGuid(SDL_JoystickID p_id)
{
	GUID guid = {};
	guid.Data1 = 0x52435253; // 'RCRS'
	guid.Data2 = (Uint16) (p_id & 0xFFFF);
	guid.Data3 = (Uint16) (p_id >> 16);
	memcpy(guid.Data4, "SDLPAD\0", 8);
	return guid;
}

static SDL_JoystickID JoystickIdFromGuid(const GUID& p_guid)
{
	if (p_guid.Data1 != 0x52435253) {
		return 0;
	}
	return (SDL_JoystickID) ((Uint32) p_guid.Data2 | ((Uint32) p_guid.Data3 << 16));
}

// The gamepad axes exposed to the game: left stick = X/Y, right stick = Rx/Ry, and
// the trigger pair combined into Z (right pulls positive, like a combined pedal axis).
static const struct {
	const GUID* m_guid;
	DWORD m_offset;
	const char* m_name;
} g_joystickAxes[] = {
	{&GUID_XAxis, DIJOFS_X, "Left Stick X"},
	{&GUID_YAxis, DIJOFS_Y, "Left Stick Y"},
	{&GUID_ZAxis, DIJOFS_Z, "Triggers"},
	{&GUID_RxAxis, DIJOFS_RX, "Right Stick X"},
	{&GUID_RyAxis, DIJOFS_RY, "Right Stick Y"},
};

// The gamepad buttons exposed to the game, in the order Windows DirectInput reports an
// Xbox controller. The game's default joystick binding gives race action slot i the
// button i - 2, so this order picks the defaults: accelerate A, brake B, power-up X,
// camera Y, HUD LB, powerslide RB, look back View. The D-pad has no entries: it is
// folded into the left stick. The names must stay short: with the axis names they have to fit NameBudget.
static const struct {
	SDL_GamepadButton m_button;
	const char* m_name;
} g_joystickButtons[] = {
	{SDL_GAMEPAD_BUTTON_SOUTH, "A"},
	{SDL_GAMEPAD_BUTTON_EAST, "B"},
	{SDL_GAMEPAD_BUTTON_WEST, "X"},
	{SDL_GAMEPAD_BUTTON_NORTH, "Y"},
	{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB"},
	{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB"},
	{SDL_GAMEPAD_BUTTON_BACK, "View"},
	{SDL_GAMEPAD_BUTTON_START, "Menu"},
	{SDL_GAMEPAD_BUTTON_LEFT_STICK, "LS"},
	{SDL_GAMEPAD_BUTTON_RIGHT_STICK, "RS"},
	{SDL_GAMEPAD_BUTTON_GUIDE, "Guide"},
	{SDL_GAMEPAD_BUTTON_MISC1, "Misc"},
	{SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, "P1"}, // Xbox Elite paddle names
	{SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, "P2"},
	{SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, "P3"},
	{SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, "P4"},
};

struct MiniwinInputDevice;

struct MiniwinInputEffect : public IDirectInputEffect {
	MiniwinInputDevice* m_device;
	DWORD m_magnitude = 0; // DirectInput units, 0..10000
	bool m_playing = false;

	explicit MiniwinInputEffect(MiniwinInputDevice* p_device, DWORD p_magnitude)
		: m_device(p_device), m_magnitude(p_magnitude)
	{
	}

	HRESULT Start(DWORD dwIterations, DWORD dwFlags) override;
	HRESULT Stop() override;
	HRESULT SetParameters(LPCDIEFFECT peff, DWORD dwFlags) override;
};

struct MiniwinInputDevice : public IDirectInputDevice2A {
	enum class Kind {
		Keyboard,
		Mouse,
		Joystick,
	};

	Kind m_kind;
	bool m_acquired = false;
	SDL_Gamepad* m_gamepad = nullptr;
	SDL_JoystickID m_joystickId = 0;
	DWORD m_deadZone = 0; // DirectInput units, 0..10000
	LONG m_rangeMin = -1000;
	LONG m_rangeMax = 1000;
	int m_nameBudget = 0; // remaining wchars in the game's control-name buffer

	explicit MiniwinInputDevice(Kind p_kind) : m_kind(p_kind) {}

	~MiniwinInputDevice() override
	{
		if (m_gamepad) {
			for (auto it = g_cursorGamepads.begin(); it != g_cursorGamepads.end(); ++it) {
				if (*it == m_gamepad) {
					g_cursorGamepads.erase(it);
					break;
				}
			}
			SDL_Gamepad* gamepad = m_gamepad;
			MiniwinApp_RunOnMainThread([gamepad]() { SDL_CloseGamepad(gamepad); });
		}
	}

	bool OpenGamepad(SDL_JoystickID p_id)
	{
		m_joystickId = p_id;
		SDL_Gamepad** result = &m_gamepad;
		MiniwinApp_RunOnMainThread([result, p_id]() { *result = SDL_OpenGamepad(p_id); });
		if (m_gamepad) {
			g_cursorGamepads.push_back(m_gamepad);
		}
		return m_gamepad != nullptr;
	}

	HRESULT QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (ppvObject && riid == IID_IDirectInputDevice2A) {
			AddRef();
			*ppvObject = this;
			return DI_OK;
		}
		if (ppvObject) {
			*ppvObject = nullptr;
		}
		return E_NOINTERFACE;
	}

	HRESULT GetDeviceInfo(LPDIDEVICEINSTANCEA pdidi) override
	{
		if (!pdidi) {
			return DIERR_GENERIC;
		}

		DWORD size = pdidi->dwSize;
		memset(pdidi, 0, sizeof(DIDEVICEINSTANCEA));
		pdidi->dwSize = size;

		switch (m_kind) {
		case Kind::Keyboard:
			pdidi->dwDevType = DIDEVTYPE_KEYBOARD;
			strcpy(pdidi->tszProductName, "Keyboard");
			strcpy(pdidi->tszInstanceName, "Keyboard");
			pdidi->guidInstance = GUID_SysKeyboard;
			break;
		case Kind::Mouse:
			pdidi->dwDevType = DIDEVTYPE_MOUSE;
			strcpy(pdidi->tszProductName, "Mouse");
			strcpy(pdidi->tszInstanceName, "Mouse");
			pdidi->guidInstance = GUID_SysMouse;
			break;
		case Kind::Joystick: {
			pdidi->dwDevType = DIDEVTYPE_JOYSTICK;
			const char* name = m_gamepad ? SDL_GetGamepadName(m_gamepad) : "Gamepad";
			SDL_strlcpy(pdidi->tszProductName, name ? name : "Gamepad", sizeof(pdidi->tszProductName));
			SDL_strlcpy(pdidi->tszInstanceName, pdidi->tszProductName, sizeof(pdidi->tszInstanceName));
			pdidi->guidInstance = JoystickGuid(m_joystickId);
			break;
		}
		}

		return DI_OK;
	}

	HRESULT GetCapabilities(LPDIDEVCAPS lpDIDevCaps) override
	{
		if (!lpDIDevCaps) {
			return DIERR_GENERIC;
		}

		DWORD size = lpDIDevCaps->dwSize;
		memset(lpDIDevCaps, 0, sizeof(DIDEVCAPS));
		lpDIDevCaps->dwSize = size;

		switch (m_kind) {
		case Kind::Keyboard:
			lpDIDevCaps->dwButtons = 256;
			break;
		case Kind::Mouse:
			lpDIDevCaps->dwAxes = 3;
			lpDIDevCaps->dwButtons = 4;
			break;
		case Kind::Joystick:
			lpDIDevCaps->dwAxes = SDL_arraysize(g_joystickAxes);
			lpDIDevCaps->dwButtons = SDL_arraysize(g_joystickButtons);
			break;
		}

		return DI_OK;
	}

	// The game stores control names in a wchar buffer sized 4*(buttons + 4*axes)
	// characters and appends without bounds checks (InputDevice::StoreString), so the
	// combined length of every name we report must stay inside that budget.
	int NameBudget() const
	{
		switch (m_kind) {
		case Kind::Keyboard:
			return 4 * 256;
		case Kind::Mouse:
			return 4 * (4 + 4 * 3);
		case Kind::Joystick:
			return 4 * ((int) SDL_arraysize(g_joystickButtons) + 4 * (int) SDL_arraysize(g_joystickAxes));
		}
		return 0;
	}

	bool EmitObject(
		LPDIENUMDEVICEOBJECTSCALLBACKA p_callback,
		LPVOID p_ref,
		const GUID& p_type,
		DWORD p_offset,
		const char* p_name
	)
	{
		DIDEVICEOBJECTINSTANCEA object;
		memset(&object, 0, sizeof(object));
		object.dwSize = sizeof(object);
		object.guidType = p_type;
		object.dwOfs = p_offset;

		int length = (int) SDL_strlen(p_name);
		if (length + 1 > m_nameBudget) {
			length = m_nameBudget - 1; // truncated (or empty once exhausted)
		}
		if (length > 0) {
			memcpy(object.tszName, p_name, (size_t) length);
			m_nameBudget -= length + 1;
		}

		return p_callback(&object, p_ref) != 0;
	}

	HRESULT EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) override
	{
		if (!lpCallback) {
			return DIERR_GENERIC;
		}

		// Button and axis enumeration share one name buffer in the game; reset the
		// budget on the first (button) pass of a device setup.
		if (dwFlags & DIDFT_BUTTON) {
			// Two trailing zeros terminate the game's name scan.
			m_nameBudget = NameBudget() - 2;
		}

		char name[64];

		if (dwFlags & DIDFT_BUTTON) {
			switch (m_kind) {
			case Kind::Keyboard:
				for (int dik = 1; dik < 256; dik++) {
					SDL_Scancode scancode = DikToScancode((Uint8) dik);
					if (scancode == SDL_SCANCODE_UNKNOWN) {
						continue;
					}

					const char* scancodeName = SDL_GetScancodeName(scancode);
					if (scancodeName && scancodeName[0]) {
						SDL_strlcpy(name, scancodeName, sizeof(name));
					}
					else {
						SDL_snprintf(name, sizeof(name), "Key %02X", dik);
					}
					if (!EmitObject(lpCallback, pvRef, GUID_Button, (DWORD) dik, name)) {
						return DI_OK;
					}
				}
				break;
			case Kind::Mouse:
				for (int i = 0; i < 4; i++) {
					SDL_snprintf(name, sizeof(name), "Button %d", i);
					if (!EmitObject(lpCallback, pvRef, GUID_Button, DIMOFS_BUTTON0 + i, name)) {
						return DI_OK;
					}
				}
				break;
			case Kind::Joystick:
				for (size_t i = 0; i < SDL_arraysize(g_joystickButtons); i++) {
					if (!EmitObject(lpCallback, pvRef, GUID_Button, DIJOFS_BUTTON(i), g_joystickButtons[i].m_name)) {
						return DI_OK;
					}
				}
				break;
			}
		}

		if (dwFlags & DIDFT_AXIS) {
			switch (m_kind) {
			case Kind::Keyboard:
				break;
			case Kind::Mouse: {
				static const struct {
					const GUID* m_guid;
					DWORD m_offset;
					const char* m_name;
				} axes[] = {
					{&GUID_XAxis, DIMOFS_X, "X-axis"},
					{&GUID_YAxis, DIMOFS_Y, "Y-axis"},
					{&GUID_ZAxis, DIMOFS_Z, "Wheel"},
				};
				for (size_t i = 0; i < SDL_arraysize(axes); i++) {
					if (!EmitObject(lpCallback, pvRef, *axes[i].m_guid, axes[i].m_offset, axes[i].m_name)) {
						return DI_OK;
					}
				}
				break;
			}
			case Kind::Joystick:
				for (size_t i = 0; i < SDL_arraysize(g_joystickAxes); i++) {
					if (!EmitObject(
							lpCallback,
							pvRef,
							*g_joystickAxes[i].m_guid,
							g_joystickAxes[i].m_offset,
							g_joystickAxes[i].m_name
						)) {
						return DI_OK;
					}
				}
				break;
			}
		}

		return DI_OK;
	}

	HRESULT GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) override
	{
		if (!pdwInOut) {
			return DIERR_GENERIC;
		}

		InputRing* ring = nullptr;
		switch (m_kind) {
		case Kind::Keyboard:
			ring = &g_keyboardRing;
			break;
		case Kind::Mouse:
			ring = &g_mouseRing;
			break;
		case Kind::Joystick:
			break; // Polled via GetDeviceState.
		}

		DWORD requested = *pdwInOut;
		DWORD produced = 0;
		if (ring && rgdod) {
			while (produced < requested && ring->Pop(rgdod[produced])) {
				produced++;
			}
		}

		*pdwInOut = produced;
		return DI_OK;
	}

	LONG ScaleAxis(Sint16 p_value) const
	{
		float normalized = (float) p_value / 32767.0f;
		if (normalized < -1.0f) {
			normalized = -1.0f;
		}

		float deadZone = (float) m_deadZone / 10000.0f;
		float magnitude = normalized < 0.0f ? -normalized : normalized;
		if (magnitude <= deadZone) {
			return (LONG) ((m_rangeMin + m_rangeMax) / 2);
		}
		if (deadZone < 1.0f) {
			magnitude = (magnitude - deadZone) / (1.0f - deadZone);
		}

		float sign = normalized < 0.0f ? -1.0f : 1.0f;
		float center = (float) (m_rangeMin + m_rangeMax) / 2.0f;
		float halfSpan = (float) (m_rangeMax - m_rangeMin) / 2.0f;
		return (LONG) (center + sign * magnitude * halfSpan);
	}

	HRESULT GetDeviceState(DWORD cbData, LPVOID lpvData) override
	{
		if (!lpvData) {
			return DIERR_GENERIC;
		}
		memset(lpvData, 0, cbData);

		if (m_kind != Kind::Joystick || !m_gamepad || cbData < sizeof(DIJOYSTATE2)) {
			return DI_OK;
		}

		DIJOYSTATE2* state = (DIJOYSTATE2*) lpvData;
		state->lX = ScaleAxis(SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTX));
		state->lY = ScaleAxis(SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTY));

		// [library:input] Menus treat every joystick button as select and navigate only
		// with axes, so fold the D-pad into the left stick and suppress its buttons below.
		bool left = SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
		bool right = SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
		bool up = SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
		bool down = SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
		if (left != right) {
			state->lX = left ? m_rangeMin : m_rangeMax;
		}
		if (up != down) {
			state->lY = up ? m_rangeMin : m_rangeMax;
		}

		state->lRx = ScaleAxis(SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
		state->lRy = ScaleAxis(SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTY));

		// Combined trigger axis: right trigger pulls positive, left negative.
		Sint16 lt = SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
		Sint16 rt = SDL_GetGamepadAxis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
		state->lZ = ScaleAxis((Sint16) ((rt - lt) / 2 * 2));

		state->rgdwPOV[0] = 0xFFFFFFFF;
		state->rgdwPOV[1] = 0xFFFFFFFF;
		state->rgdwPOV[2] = 0xFFFFFFFF;
		state->rgdwPOV[3] = 0xFFFFFFFF;

		for (size_t i = 0; i < SDL_arraysize(g_joystickButtons); i++) {
			state->rgbButtons[i] = SDL_GetGamepadButton(m_gamepad, g_joystickButtons[i].m_button) ? 0x80 : 0x00;
		}

		return DI_OK;
	}

	HRESULT Acquire() override
	{
#ifdef WINDOWS_STORE
		if (m_kind == Kind::Mouse && !m_acquired) {
			MiniwinApp_RunOnMainThread([]() {
				SDL_Window* window = SDL_GetKeyboardFocus();
				if (!window) {
					SDL_LogInfo(LOG_CATEGORY_MINIWIN, "UWP mouse configure: no window");
					return;
				}
				// Keep CoreWindow's absolute pointer/button path. SDL derives xrel/yrel
				// from it; MouseDevice::MouseMoved is used only in relative mode.
				bool ok = SDL_SetWindowRelativeMouseMode(window, false);
				SDL_LogInfo(
					LOG_CATEGORY_MINIWIN,
					"UWP mouse configured: relative=%d hidden=%d blank-cursor-resource=5000 ok=%d%s%s",
					(int) SDL_GetWindowRelativeMouseMode(window),
					(int) !SDL_CursorVisible(),
					(int) ok,
					ok ? "" : " error=",
					ok ? "" : SDL_GetError()
				);
			});
		}
#endif
		m_acquired = true;
		return DI_OK;
	}

	HRESULT Unacquire() override
	{
		m_acquired = false;
		return DI_OK;
	}

	HRESULT Poll() override { return DI_OK; }

	HRESULT SetProperty(const GUID* rguidProp, LPCDIPROPHEADER pdiph) override
	{
		if (!pdiph) {
			return DIERR_GENERIC;
		}

		if (rguidProp == DIPROP_DEADZONE) {
			m_deadZone = ((const DIPROPDWORD*) pdiph)->dwData;
		}
		else if (rguidProp == DIPROP_RANGE) {
			const DIPROPRANGE* range = (const DIPROPRANGE*) pdiph;
			m_rangeMin = range->lMin;
			m_rangeMax = range->lMax;
		}

		return DI_OK;
	}

	HRESULT CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, IUnknown* punkOuter) override
	{
		if (!ppdeff) {
			return DIERR_GENERIC;
		}
		*ppdeff = nullptr;

		if (m_kind != Kind::Joystick || !m_gamepad || !(rguid == GUID_Sine)) {
			return DIERR_GENERIC;
		}

		SDL_PropertiesID props = SDL_GetGamepadProperties(m_gamepad);
		if (!SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
			return DIERR_GENERIC;
		}

		DWORD magnitude = 5000;
		if (lpeff && lpeff->lpvTypeSpecificParams && lpeff->cbTypeSpecificParams >= sizeof(DIPERIODIC)) {
			magnitude = ((const DIPERIODIC*) lpeff->lpvTypeSpecificParams)->dwMagnitude;
		}

		*ppdeff = new MiniwinInputEffect(this, magnitude);
		return DI_OK;
	}
};

HRESULT MiniwinInputEffect::Start(DWORD, DWORD)
{
	m_playing = true;
	Uint16 strength = (Uint16) ((Uint32) m_magnitude * 65535 / 10000);
	SDL_RumbleGamepad(m_device->m_gamepad, strength, strength, 600000);
	return DI_OK;
}

HRESULT MiniwinInputEffect::Stop()
{
	m_playing = false;
	SDL_RumbleGamepad(m_device->m_gamepad, 0, 0, 0);
	return DI_OK;
}

HRESULT MiniwinInputEffect::SetParameters(LPCDIEFFECT peff, DWORD dwFlags)
{
	if ((dwFlags & DIEP_TYPESPECIFICPARAMS) && peff && peff->lpvTypeSpecificParams &&
		peff->cbTypeSpecificParams >= sizeof(DIPERIODIC)) {
		m_magnitude = ((const DIPERIODIC*) peff->lpvTypeSpecificParams)->dwMagnitude;
		if (m_playing) {
			return Start(1, 0);
		}
	}
	return DI_OK;
}

struct MiniwinDirectInput : public IDirectInputA {
	HRESULT CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICEA* lplpDirectInputDevice, IUnknown* pUnkOuter) override
	{
		if (!lplpDirectInputDevice) {
			return DIERR_GENERIC;
		}
		*lplpDirectInputDevice = nullptr;

		if (rguid == GUID_SysKeyboard) {
			*lplpDirectInputDevice = new MiniwinInputDevice(MiniwinInputDevice::Kind::Keyboard);
			return DI_OK;
		}
		if (rguid == GUID_SysMouse) {
			*lplpDirectInputDevice = new MiniwinInputDevice(MiniwinInputDevice::Kind::Mouse);
			return DI_OK;
		}

		SDL_JoystickID id = JoystickIdFromGuid(rguid);
		if (!id) {
			return DIERR_GENERIC;
		}

		MiniwinInputDevice* device = new MiniwinInputDevice(MiniwinInputDevice::Kind::Joystick);
		if (!device->OpenGamepad(id)) {
			device->Release();
			return DIERR_GENERIC;
		}

		*lplpDirectInputDevice = device;
		return DI_OK;
	}

	HRESULT EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) override
	{
		if (dwDevType != DIDEVTYPE_JOYSTICK || !lpCallback) {
			return DI_OK;
		}

		int count = 0;
		SDL_JoystickID* ids = SDL_GetGamepads(&count);
		if (!ids) {
			return DI_OK;
		}

		for (int i = 0; i < count; i++) {
			// For the force-feedback pass, report every gamepad; CreateEffect verifies
			// actual rumble support and fails safely without it.
			DIDEVICEINSTANCEA instance;
			memset(&instance, 0, sizeof(instance));
			instance.dwSize = sizeof(instance);
			instance.dwDevType = DIDEVTYPE_JOYSTICK;
			instance.guidInstance = JoystickGuid(ids[i]);

			const char* name = SDL_GetGamepadNameForID(ids[i]);
			SDL_strlcpy(instance.tszProductName, name ? name : "Gamepad", sizeof(instance.tszProductName));
			SDL_strlcpy(instance.tszInstanceName, instance.tszProductName, sizeof(instance.tszInstanceName));

			if (!lpCallback(&instance, pvRef)) {
				break;
			}
		}

		SDL_free(ids);
		return DI_OK;
	}
};

HRESULT DirectInputCreate(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUT* ppDI, IUnknown* punkOuter)
{
	if (!ppDI) {
		return DIERR_GENERIC;
	}

	*ppDI = new MiniwinDirectInput();
	return DI_OK;
}

// --- Base-interface default bodies ---

HRESULT IDirectInputA::CreateDevice(REFGUID, LPDIRECTINPUTDEVICEA*, IUnknown*)
{
	return DIERR_GENERIC;
}
HRESULT IDirectInputA::EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD)
{
	return DI_OK;
}

HRESULT IDirectInputDevice2A::Acquire()
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::Unacquire()
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT* ppdeff, IUnknown*)
{
	if (ppdeff) {
		*ppdeff = nullptr;
	}
	return DIERR_GENERIC;
}
HRESULT IDirectInputDevice2A::EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD)
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::GetCapabilities(LPDIDEVCAPS)
{
	return DIERR_GENERIC;
}
HRESULT IDirectInputDevice2A::GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD pdwInOut, DWORD)
{
	if (pdwInOut) {
		*pdwInOut = 0;
	}
	return DI_OK;
}
HRESULT IDirectInputDevice2A::GetDeviceInfo(LPDIDEVICEINSTANCEA)
{
	return DIERR_GENERIC;
}
HRESULT IDirectInputDevice2A::GetDeviceState(DWORD cbData, LPVOID lpvData)
{
	if (lpvData) {
		memset(lpvData, 0, cbData);
	}
	return DI_OK;
}
HRESULT IDirectInputDevice2A::Poll()
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::SetCooperativeLevel(HWND, DWORD)
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::SetDataFormat(LPCDIDATAFORMAT)
{
	return DI_OK;
}
HRESULT IDirectInputDevice2A::SetProperty(const GUID*, LPCDIPROPHEADER)
{
	return DI_OK;
}

HRESULT IDirectInputEffect::Start(DWORD, DWORD)
{
	return DI_OK;
}
HRESULT IDirectInputEffect::Stop()
{
	return DI_OK;
}
HRESULT IDirectInputEffect::SetParameters(LPCDIEFFECT, DWORD)
{
	return DI_OK;
}
