#pragma once

class OptionsScreen;

// [library:config] Game-facing widget adapter, compiled into the app so miniwin's
// render/input library does not depend on game menu classes or change their layout.
void MiniwinWidescreenOptions_Create(OptionsScreen* p_screen, unsigned int p_languageIndex);
void MiniwinWidescreenOptions_Show(bool p_show);
void MiniwinWidescreenOptions_Destroy();
