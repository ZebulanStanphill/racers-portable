#pragma once

// Xbox (UWP) support for the SDL3 application shell. A UWP app gets no command line,
// so the game files are looked for in fixed places. Only built for WINDOWS_STORE.

#ifdef WINDOWS_STORE

#include <SDL3/SDL_init.h>

// [library:filesystem] Makes the first known game data directory that holds LEGO.JAM the
// working directory: E:\racers\ (a USB drive formatted for media), then
// D:\DevelopmentFiles\racers\ (Dev Mode's development files folder). If neither has it,
// returns false; the caller should then not start the game, and leave the rest to
// MiniwinUwp_ReportMissingGameData. Main thread, after SDL_Init.
bool MiniwinUwp_EnterGameDataDirectory();

// [library:filesystem] Whether MiniwinUwp_EnterGameDataDirectory found no game files.
bool MiniwinUwp_IsGameDataMissing();

// [library:filesystem] Call from SDL_AppIterate while the game files are missing. Waits
// for the app's window to be on screen, then tells the player where to put the game files
// in a message box and returns SDL_APP_SUCCESS, to quit. Returns SDL_APP_CONTINUE while
// waiting.
SDL_AppResult MiniwinUwp_ReportMissingGameData();

#endif
