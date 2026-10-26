#pragma once

// Xbox (UWP) support for the SDL3 application shell. A UWP app gets no command line,
// so the game files are looked for in fixed places, and SDL's log only reaches a
// debugger, so it is also written to a file. Only built for WINDOWS_STORE.

#ifdef WINDOWS_STORE

#include <SDL3/SDL_init.h>

// [library:filesystem] Makes the first known game data directory that holds LEGO.JAM the
// working directory: E:\racers\ (a USB drive formatted for media), then
// D:\DevelopmentFiles\racers\ (Dev Mode's development files folder). If neither has it,
// writes the log file to the user data directory and returns false; the caller should then
// not start the game, and leave the rest to MiniwinUwp_ReportMissingGameData. Main thread,
// after SDL_Init and MiniwinUwp_StartLog.
bool MiniwinUwp_EnterGameDataDirectory();

// [library:filesystem] Whether MiniwinUwp_EnterGameDataDirectory found no game files.
bool MiniwinUwp_IsGameDataMissing();

// [library:filesystem] Call from SDL_AppIterate while the game files are missing. Waits
// for the app's window to be on screen, then tells the player where to put the game files
// (and where the log is) in a message box and returns SDL_APP_SUCCESS, to quit. Returns
// SDL_APP_CONTINUE while waiting.
SDL_AppResult MiniwinUwp_ReportMissingGameData();

// [library:config] Installs an SDL log output function that also writes every log line
// (time, priority, category, message) to latest-log.txt, and raises miniwin's and SDL's
// GPU log categories to info. Lines are kept in memory until MiniwinUwp_OpenLogFile.
// Call first thing at startup.
void MiniwinUwp_StartLog();

// [library:config] Creates latest-log.txt, emptied at every launch, in p_directory
// (ending with a separator), or in the working directory (the game data directory) when
// p_directory is null, falling back to the user data directory if that fails. Writes the
// lines logged so far, then a few lines about the system. Returns the log file's path, or
// null if it couldn't be created. Later calls only return the path.
const char* MiniwinUwp_OpenLogFile(const char* p_directory);

// [library:config] Call from SDL_AppQuit. If latest-log.txt hasn't been created yet (the
// app is quitting before it got to the game files, say because SDL_Init failed), creates it
// in the user data directory, so the lines logged so far aren't lost.
void MiniwinUwp_FlushPendingLog();

// [library:3d] Temporary Xbox capability diagnostic. Call after opening latest-log.txt,
// before starting the game renderer. Logs failures and always returns to the caller.
void MiniwinUwp_ProbeDirect3D();

#endif
