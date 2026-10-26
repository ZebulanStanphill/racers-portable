// [library:input] Controls-screen button built from a copied event-button binding.
#include "image/golimage.h"
#include "input/inputdevice.h"
#include "input/joystickdevice.h"
#include "menu/menugamecontext.h"
#include "menu/screens/controlconfigscreen.h"
#include "menulanguage.h"
#include "save/gamestate.h"

#include <miniwin/gamepad.h>
#include <miniwin/resetcontrols.h>
#include <string.h>

extern const LegoU32 g_keyboardInputBindingEvents[3][9];

namespace
{
struct ResetButton {
	MenuTextButton button;
	undefined2 text[32];
};

// Owned by the controls screen, released before its root and resources.
ResetButton* g_button = nullptr;

const wchar_t* ButtonLabel(unsigned int p_languageIndex)
{
	struct Translation {
		const char* folder;
		const wchar_t* text;
	};
	static const Translation labels[] = {
		{"english", L"RESET CONTROLS"},
		{"danish", L"NULSTIL STYRING"},
		{"dutch", L"BESTURING HERSTELLEN"},
		{"finnish", L"PALAUTA OHJAIMET"},
		{"french", L"R\u00c9TABLIR LES COMMANDES"},
		{"german", L"STEUERUNG ZUR\u00dcCKSETZEN"},
		{"italian", L"RIPRISTINA COMANDI"},
		{"norwegi", L"TILBAKESTILL KONTROLLER"},
		{"spanish", L"RESTABLECER CONTROLES"},
		{"swedish", L"\u00c5TERST\u00c4LL KONTROLLER"},
	};
	const char* folder = MiniwinMenuLanguageFolder(p_languageIndex);
	for (const auto& label : labels) {
		if (strcmp(folder, label.folder) == 0) {
			return label.text;
		}
	}
	return labels[0].text;
}
} // namespace

void MiniwinResetControls_Create(ControlConfigScreen* p_screen, GolStringTable* p_strings, unsigned int p_languageIndex)
{
	delete g_button;
	g_button = new ResetButton;
	auto& button = *g_button;

	// Resolve defaults exactly as CreateTextButton does, without modifying the MIB
	// or using the Back button's name (which would attach its gamepad-B hotkey).
	auto params = *static_cast<MenuTextButton::CreateParams*>(p_screen->GetBindingEntry(0x110));
	auto* style = p_screen->GetTextButtonStyle(0x42);
	p_screen->ApplyIconDefaults(&params);
	for (int i = 0; i < 6; i++) {
		if (!params.m_stateImages[i]) {
			params.m_stateImages[i] = style->m_stateImages[i];
		}
		if (!params.m_stateFonts[i]) {
			params.m_stateFonts[i] = style->m_stateFonts[i];
		}
	}
	if (!params.m_hasStateColors && style->m_hasStateColors) {
		memcpy(params.m_stateColors, style->m_stateColors, sizeof(params.m_stateColors));
	}
	params.m_stringTable = p_strings;
	params.m_stringId = 0x69;
	params.m_id = 0x7f01;
	// Action-label column, on the same baseline as FINISH. Keep enough width for
	// all translations rather than inheriting the key-name width limit.
	auto* label = static_cast<MenuTextLabel::CreateParams*>(p_screen->GetBindingEntry(0x116));
	auto* back = static_cast<MenuTextButton::CreateParams*>(p_screen->GetBindingEntry(0x3f));
	// The text starts after the button's state image (MenuTextButton::CreateLabel).
	params.m_rect.m_left = label->m_rect.m_left - static_cast<LegoS32>(params.m_stateImages[0]->GetWidth());
	params.m_rect.m_top = back->m_rect.m_top;
	params.m_hasMaxTextWidth = TRUE;
	params.m_maxTextWidth = 0;
	button.button.Create(&params, style);

	// GolString borrows its UTF-16 code-unit buffer; retain it with the widget.
	const wchar_t* text = ButtonLabel(p_languageIndex);
	unsigned int length = 0;
	while (text[length] && length + 1 < sizeof(button.text) / sizeof(button.text[0])) {
		button.text[length] = (undefined2) text[length];
		length++;
	}
	button.text[length] = 0;
	GolString string;
	string.CopyFromBufSelection(button.text, length + 1);
	button.button.SetText(&string);
}

void MiniwinResetControls_Activate(
	MenuWidget* p_source,
	MenuGameContext* p_context,
	InputDevice* const* p_devices,
	const int* p_entries,
	int p_selectedDevice,
	MenuIcon* p_awaitingButton
)
{
	if (!g_button || p_source != &g_button->button || p_awaitingButton) {
		return;
	}

	GameState& state = p_context->m_saveSystem.GetGameState();
	InputDevice* device = p_devices[p_selectedDevice];
	unsigned int entry = p_entries[p_selectedDevice];
	bool keyboard = device->GetDeviceType() == 3;
	for (unsigned int slot = keyboard ? 0 : 2; slot < 9; slot++) {
		LegoU32 event = 0;
		if (keyboard) {
			event = g_keyboardInputBindingEvents[entry - 2][slot];
		}
		else if (slot < static_cast<JoystickDevice*>(device)->GetButtonCountFast()) {
			int index = MiniwinGamepad_DefaultRaceButton(slot);
			event = InputDevice::c_sourceJoystickButton | (index >= 0 ? index : slot - 2);
		}
		state.SetInputEvent(entry, slot, event);
	}
}

void MiniwinResetControls_Destroy()
{
	delete g_button;
	g_button = nullptr;
}
