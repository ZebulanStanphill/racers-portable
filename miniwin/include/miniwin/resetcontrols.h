#pragma once

class ControlConfigScreen;
class GolStringTable;
class MenuGameContext;
class MenuIcon;
class InputDevice;
class MenuWidget;

// [library:input] App-side adapter; keeps the extra widget outside the decomp class layout.
void MiniwinResetControls_Create(
	ControlConfigScreen* p_screen,
	GolStringTable* p_strings,
	unsigned int p_languageIndex
);
void MiniwinResetControls_Activate(
	MenuWidget* p_source,
	MenuGameContext* p_context,
	InputDevice* const* p_devices,
	const int* p_entries,
	int p_selectedDevice,
	MenuIcon* p_awaitingButton
);
void MiniwinResetControls_Destroy();
