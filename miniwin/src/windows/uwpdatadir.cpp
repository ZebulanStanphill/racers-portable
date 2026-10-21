// [library:filesystem] Finds LEGO Racers' game files on the Xbox (UWP), where the app
// gets no command line and so no --path.

#include <SDL3/SDL.h>
#include <direct.h>
#include <miniwin/uwp.h>
#include <miniwin/windows.h>

// Searched in order. A USB drive formatted for media shows up as E:\ and is open to all
// apps; D:\DevelopmentFiles\ is Dev Mode's own folder, reachable over the network (SMB)
// and from the Device Portal.
static const char* const g_gameDataDirectories[] = {
	"E:\\racers\\",
	"D:\\DevelopmentFiles\\racers\\",
};

// A message dialog shown before the app's window is visible can fail or stay hidden behind
// the splash screen: MessageDialog.ShowAsync's documentation says to wait for the window's
// VisibilityChanged event, which comes some time after the window is activated. SDL3-uwp
// activates the window when it first pumps WinRT events, before the first SDL_AppIterate,
// but reports visibility changes only as events of an SDL window, and there is none while
// the game files are missing. So the message waits this long after the first
// SDL_AppIterate, with SDL pumping WinRT events in between: plenty for the splash screen
// to go, and it only delays an error message.
static const Uint64 c_missingGameDataMessageDelayMs = 2000;

static bool g_gameDataMissing;
static const char* g_userDataLogPath;

static bool HasGameData(const char* p_directory)
{
	char path[256];
	SDL_snprintf(path, sizeof(path), "%sLEGO.JAM", p_directory);

	// Open the file rather than only checking that it exists, so a folder the app can't
	// read is skipped (and its error logged) here instead of failing later in the game.
	SDL_IOStream* file = SDL_IOFromFile(path, "rb");
	if (!file) {
		SDL_Log("No game data in %s: %s", p_directory, SDL_GetError());
		return false;
	}

	SDL_CloseIO(file);
	return true;
}

bool MiniwinUwp_EnterGameDataDirectory()
{
	for (const char* directory : g_gameDataDirectories) {
		if (!HasGameData(directory)) {
			continue;
		}
		if (_chdir(directory) != 0) {
			SDL_Log("Cannot enter %s", directory);
			continue;
		}

		SDL_Log("Game data directory: %s", directory);
		return true;
	}

	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LEGO Racers' game files were not found");
	g_gameDataMissing = true;

	// With no game data directory to keep it in, the log goes with the saved games.
	char userDataPath[1024];
	MiniwinGetUserDataPath(userDataPath, sizeof(userDataPath));
	g_userDataLogPath = MiniwinUwp_OpenLogFile(userDataPath);
	return false;
}

bool MiniwinUwp_IsGameDataMissing()
{
	return g_gameDataMissing;
}

SDL_AppResult MiniwinUwp_ReportMissingGameData()
{
	// Set at the first call.
	static const Uint64 s_firstCallMs = SDL_GetTicks();
	if (SDL_GetTicks() - s_firstCallMs < c_missingGameDataMessageDelayMs) {
		SDL_Delay(10);
		return SDL_APP_CONTINUE;
	}

	char message[2048];
	SDL_snprintf(
		message,
		sizeof(message),
		"LEGO Racers' game files were not found.\n\n"
		"Copy the files of an installed copy of LEGO Racers (LEGO.JAM, the .TUN music files and "
		"the .AVI movies) into one of these folders, then start the game again:\n\n"
		"E:\\racers\\ (on a USB drive formatted for media)\n"
		"D:\\DevelopmentFiles\\racers\\ (Dev Mode's development files folder)"
		"%s%s",
		g_userDataLogPath ? "\n\nA log was written to " : "",
		g_userDataLogPath ? g_userDataLogPath : ""
	);
	if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LEGO Racers", message, nullptr)) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Cannot show the message about the missing game files: %s",
			SDL_GetError()
		);
	}
	return SDL_APP_SUCCESS;
}
