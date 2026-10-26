#pragma once

#include "types.h"

// The menu text folder table GameState uses (languages 0 to 8).
extern const LegoChar* g_menuLanguageDirectories[9];

inline const char* MiniwinMenuLanguageFolder(unsigned int p_languageIndex)
{
	// Index 10 (registry LangID 0x000b) is Finnish, which the menu folder table lacks.
	return p_languageIndex < 9 ? g_menuLanguageDirectories[p_languageIndex]
							   : (p_languageIndex == 10 ? "finnish" : "english");
}
