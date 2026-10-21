// [library:config] Log file for the Xbox (UWP). There, SDL's default log output only
// reaches an attached debugger (OutputDebugString), which a player can't see on a
// console, so every log line is also written to latest-log.txt, normally in the game data
// directory, where it can be read over the network or from the Device Portal.

#include "miniwin.h"

#include <SDL3/SDL.h>
#include <miniwin/uwp.h>
#include <miniwin/windows.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <wchar.h>

// At most this much is kept of the lines logged before the game data directory is known
// (more than enough), including the marker that says when lines were dropped.
static const size_t c_pendingLogLimit = 64 * 1024;
static const char c_pendingLogTruncated[] = "[log truncated]\n";

// Guards the state below: lines are logged from the main thread, the game thread and
// SDL's own threads, while MiniwinUwp_OpenLogFile switches from the buffer to the file.
static SDL_Mutex* g_logMutex;
static bool g_logStarted;
static bool g_logPending;
static std::string g_pendingLog;
static bool g_pendingLogTruncated;
static FILE* g_logFile;
static char g_logPath[1024];

// The output function installed before ours (SDL's default), which still gets every line.
static SDL_LogOutputFunction g_nextLogOutput;
static void* g_nextLogOutputUserdata;

static const char* PriorityName(SDL_LogPriority p_priority)
{
	switch (p_priority) {
	case SDL_LOG_PRIORITY_TRACE:
		return "TRACE";
	case SDL_LOG_PRIORITY_VERBOSE:
		return "VERBOSE";
	case SDL_LOG_PRIORITY_DEBUG:
		return "DEBUG";
	case SDL_LOG_PRIORITY_INFO:
		return "INFO";
	case SDL_LOG_PRIORITY_WARN:
		return "WARN";
	case SDL_LOG_PRIORITY_ERROR:
		return "ERROR";
	case SDL_LOG_PRIORITY_CRITICAL:
		return "CRITICAL";
	default:
		return "?";
	}
}

static const char* CategoryName(int p_category)
{
	switch (p_category) {
	case SDL_LOG_CATEGORY_APPLICATION:
		return "APP";
	case SDL_LOG_CATEGORY_ERROR:
		return "ERROR";
	case SDL_LOG_CATEGORY_ASSERT:
		return "ASSERT";
	case SDL_LOG_CATEGORY_SYSTEM:
		return "SYSTEM";
	case SDL_LOG_CATEGORY_AUDIO:
		return "AUDIO";
	case SDL_LOG_CATEGORY_VIDEO:
		return "VIDEO";
	case SDL_LOG_CATEGORY_RENDER:
		return "RENDER";
	case SDL_LOG_CATEGORY_INPUT:
		return "INPUT";
	case SDL_LOG_CATEGORY_TEST:
		return "TEST";
	case SDL_LOG_CATEGORY_GPU:
		return "GPU";
	case LOG_CATEGORY_MINIWIN:
		return "MINIWIN";
	default:
		return "OTHER";
	}
}

// Appends at most p_room bytes of p_text to the pending log, without splitting a UTF-8
// sequence, and returns how much room is left.
static size_t AppendPendingText(const char* p_text, size_t p_room)
{
	size_t length = SDL_strlen(p_text);
	if (length > p_room) {
		length = p_room;
		// Back up while the cut would fall on a UTF-8 continuation byte.
		while (length > 0 && ((unsigned char) p_text[length] & 0xC0) == 0x80) {
			length--;
		}
	}

	g_pendingLog.append(p_text, length);
	return p_room - length;
}

// Called with g_logMutex held. A line that doesn't fit is cut short, then the marker is
// added and later lines are dropped.
static void AppendPendingLine(const char* p_prefix, const char* p_message)
{
	if (g_pendingLogTruncated) {
		return;
	}

	size_t room = c_pendingLogLimit - (sizeof(c_pendingLogTruncated) - 1) - g_pendingLog.size();
	if (SDL_strlen(p_prefix) + SDL_strlen(p_message) + 1 <= room) {
		g_pendingLog += p_prefix;
		g_pendingLog += p_message;
		g_pendingLog += '\n';
		return;
	}

	if (room > 1) {
		room = AppendPendingText(p_prefix, room - 1);
		AppendPendingText(p_message, room);
		g_pendingLog += '\n';
	}
	g_pendingLog += c_pendingLogTruncated;
	g_pendingLogTruncated = true;
}

static void SDLCALL WriteLogLine(void* p_userdata, int p_category, SDL_LogPriority p_priority, const char* p_message)
{
	char time[16] = "--:--:--.---";
	SDL_Time now;
	SDL_DateTime local;
	if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &local, true)) {
		SDL_snprintf(
			time,
			sizeof(time),
			"%02d:%02d:%02d.%03d",
			local.hour,
			local.minute,
			local.second,
			local.nanosecond / 1000000
		);
	}

	char prefix[64];
	SDL_snprintf(prefix, sizeof(prefix), "%s %s %s: ", time, PriorityName(p_priority), CategoryName(p_category));

	SDL_LockMutex(g_logMutex);
	if (g_logFile) {
		fputs(prefix, g_logFile);
		fputs(p_message, g_logFile);
		fputc('\n', g_logFile);
		// Flushed per line, so the log is complete up to a crash.
		fflush(g_logFile);
	}
	else if (g_logPending) {
		AppendPendingLine(prefix, p_message);
	}
	SDL_UnlockMutex(g_logMutex);

	if (g_nextLogOutput) {
		g_nextLogOutput(g_nextLogOutputUserdata, p_category, p_priority, p_message);
	}
}

void MiniwinUwp_StartLog()
{
	if (g_logStarted) {
		return;
	}
	g_logStarted = true;
	g_logPending = true;
	g_logMutex = SDL_CreateMutex();

	// Outside the application category, SDL only logs errors by default. Raise miniwin's
	// category (render backend, SDL_GPU driver) and SDL's GPU category (graphics adapter)
	// to info; both log those once at startup, not every frame. An SDL_LOGGING hint wins.
	if (!SDL_GetHint(SDL_HINT_LOGGING)) {
		SDL_SetLogPriority(LOG_CATEGORY_MINIWIN, SDL_LOG_PRIORITY_INFO);
		SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_INFO);
	}

	SDL_GetLogOutputFunction(&g_nextLogOutput, &g_nextLogOutputUserdata);
	SDL_SetLogOutputFunction(WriteLogLine, nullptr);
}

// Converts between UTF-8 (SDL) and UTF-16 (the CRT's wide-character file functions), so
// paths outside the ANSI code page work. The result is freed with SDL_free.
static wchar_t* Utf8ToWide(const char* p_string)
{
	return (wchar_t*) SDL_iconv_string("UTF-16LE", "UTF-8", p_string, SDL_strlen(p_string) + 1);
}

static char* WideToUtf8(const wchar_t* p_string)
{
	return SDL_iconv_string("UTF-8", "UTF-16LE", (const char*) p_string, (wcslen(p_string) + 1) * sizeof(wchar_t));
}

// Fills p_out with the working directory, ending with a separator; empty on failure.
static void GetWorkingDirectory(char* p_out, size_t p_size)
{
	p_out[0] = '\0';

	wchar_t* directory = _wgetcwd(nullptr, 0);
	if (!directory) {
		return;
	}

	char* utf8 = WideToUtf8(directory);
	free(directory);
	if (!utf8) {
		return;
	}

	SDL_strlcpy(p_out, utf8, p_size);
	SDL_free(utf8);

	size_t length = SDL_strlen(p_out);
	if (length > 0 && p_out[length - 1] != '\\' && length + 1 < p_size) {
		SDL_strlcat(p_out, "\\", p_size);
	}
}

static FILE* CreateLogFile(const char* p_directory, char* p_path, size_t p_pathSize)
{
	SDL_snprintf(p_path, p_pathSize, "%slatest-log.txt", p_directory);

	wchar_t* path = Utf8ToWide(p_path);
	if (!path) {
		return nullptr;
	}

	// Emptied at every launch. Others may read it while the game runs (the Device Portal,
	// a network share), but not write to it.
	FILE* file = _wfsopen(path, L"w", _SH_DENYWR);
	SDL_free(path);
	return file;
}

static void LogSystemInfo()
{
	SDL_Time now;
	SDL_DateTime local;
	if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &local, true)) {
		SDL_Log("Date: %04d-%02d-%02d", local.year, local.month, local.day);
	}

	int version = SDL_GetVersion();
	SDL_Log(
		"SDL %d.%d.%d (%s)",
		SDL_VERSIONNUM_MAJOR(version),
		SDL_VERSIONNUM_MINOR(version),
		SDL_VERSIONNUM_MICRO(version),
		SDL_GetRevision()
	);

	const char* videoDriver = SDL_GetCurrentVideoDriver();
	SDL_Log("Video driver: %s", videoDriver ? videoDriver : "none");

#ifdef SDL_PLATFORM_WINRT
	const char* deviceFamily;
	switch (SDL_GetWinRTDeviceFamily()) {
	case SDL_WINRT_DEVICEFAMILY_DESKTOP:
		deviceFamily = "desktop";
		break;
	case SDL_WINRT_DEVICEFAMILY_MOBILE:
		deviceFamily = "mobile";
		break;
	case SDL_WINRT_DEVICEFAMILY_XBOX:
		deviceFamily = "Xbox";
		break;
	default:
		deviceFamily = "unknown";
		break;
	}
	SDL_Log("Device family: %s", deviceFamily);
#endif

	// SDL3-uwp has no call for whether Game Mode's extra resources were granted; the CPU
	// cores and memory the app sees hint at it.
	SDL_Log("CPU cores: %d, RAM: %d MiB", SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM());
}

const char* MiniwinUwp_OpenLogFile(const char* p_directory)
{
	if (!g_logStarted || !g_logPending) {
		return g_logPath[0] ? g_logPath : nullptr;
	}

	char directory[1024];
	if (p_directory) {
		SDL_strlcpy(directory, p_directory, sizeof(directory));
	}
	else {
		GetWorkingDirectory(directory, sizeof(directory));
	}

	char path[1024];
	FILE* file = CreateLogFile(directory, path, sizeof(path));

	// The game data directory may be read-only; keep the log with the saved games then.
	char fallback[1024] = "";
	if (!file && !p_directory) {
		MiniwinGetUserDataPath(fallback, sizeof(fallback));
		if (fallback[0]) {
			file = CreateLogFile(fallback, path, sizeof(path));
		}
	}

	SDL_LockMutex(g_logMutex);
	if (file) {
		SDL_strlcpy(g_logPath, path, sizeof(g_logPath));
		fputs(g_pendingLog.c_str(), file);
		fflush(file);
		g_logFile = file;
	}
	g_logPending = false;
	g_pendingLog.clear();
	g_pendingLog.shrink_to_fit();
	SDL_UnlockMutex(g_logMutex);

	if (!file) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot write latest-log.txt in %s", directory);
		return nullptr;
	}

	if (fallback[0]) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot write latest-log.txt in %s", directory);
	}
	SDL_Log("Log file: %s", g_logPath);
	LogSystemInfo();
	return g_logPath;
}

void MiniwinUwp_FlushPendingLog()
{
	if (!g_logStarted || !g_logPending) {
		return;
	}

	char userDataPath[1024];
	MiniwinGetUserDataPath(userDataPath, sizeof(userDataPath));
	MiniwinUwp_OpenLogFile(userDataPath);
}
