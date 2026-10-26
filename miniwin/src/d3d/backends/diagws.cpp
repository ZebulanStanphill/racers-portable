// TEMPORARY Xbox diagnostics for the widescreen presentation ("[diag-ws]"). Game thread
// only, except MiniwinDiagWs_OnEvent. Volume budget: one "state" line whenever the
// presentation geometry changes, the per-frame detail (2 lines) for the first few frames
// after a change / race start / race end, then one detail every few seconds while
// widescreen is on or the world is drawn, and at most c_maxDetailFrames details in total.
#include "diagws.h"

#include "miniwin.h"
#include "renderbackend.h"

#include <miniwin/miniwinapp.h>
#include <miniwin/widescreen.h>
#include <string.h>

namespace
{

constexpr int c_burstFrames = 5;
constexpr int c_maxDetailFrames = 300;
constexpr Uint64 c_periodMs = 5000;
constexpr int c_maxTokens = 48;
constexpr int c_maxWindowEvents = 40;

enum Source {
	SRC_NONE,
	SRC_DEVICE,
	SRC_SURFACE,
	SRC_VIDEO,
	SRC_COUNT,
};

// A run-length encoded trace of what happened in the frame, e.g. WB WD*120 WE UD*40 P.
struct Trace {
	struct Token {
		const char* name;
		Uint32 count;
	};
	Token tokens[c_maxTokens];
	int used;
	bool overflow;

	void Add(const char* p_name)
	{
		if (used > 0 && SDL_strcmp(tokens[used - 1].name, p_name) == 0) {
			tokens[used - 1].count++;
			return;
		}
		if (used == c_maxTokens) {
			overflow = true;
			return;
		}
		tokens[used].name = p_name;
		tokens[used].count = 1;
		used++;
	}

	void Format(char* p_out, size_t p_size) const
	{
		p_out[0] = '\0';
		for (int i = 0; i < used; i++) {
			size_t length = SDL_strlen(p_out);
			if (tokens[i].count > 1) {
				SDL_snprintf(p_out + length, p_size - length, "%s*%u ", tokens[i].name, tokens[i].count);
			}
			else {
				SDL_snprintf(p_out + length, p_size - length, "%s ", tokens[i].name);
			}
		}
		if (overflow) {
			size_t length = SDL_strlen(p_out);
			SDL_snprintf(p_out + length, p_size - length, "...");
		}
	}
};

struct Frame {
	Uint32 draws[2][SRC_COUNT]; // [world][source]
	Uint32 tris[2];
	Uint32 mappedUi;      // UI draws re-mapped into the centred 4:3 region
	Uint32 drawsInactive; // draws submitted while widescreen was not active
	Uint32 clears[2][2];  // [world][full]
	Uint32 bars;          // side-bar panels drawn (any reason)
	Uint32 staleBars;     // of which ClearStaleSideBars
	Uint32 pillarCalls;   // MiniwinWidescreen_Pillarbox calls
	Uint32 pillarRan;     // of which widescreen was active
	Uint32 scopeBegins;
	Uint32 scopeEnds;
	Trace trace;
};

struct Signature {
	int pref;
	int active;
	int drawableW, drawableH;
	int vpX, vpY, vpW, vpH;
	int sceneW, sceneH;
	int uiFraction; // x10000
	int logicalW, logicalH;
	int scale;
	int resolution;
};

Frame g_frame;
MiniwinDiagEntry g_entry = MINIWIN_DIAG_ENTRY_NONE;
Signature g_signature;
bool g_haveSignature;
const char* g_burstReason = "";
int g_burst;
int g_detailFrames;
Uint64 g_frameNumber;
Uint64 g_lastPeriodic;
bool g_prevWorldFrame;
SDL_AtomicInt g_windowEvents;

MiniwinDiagEntry TakeEntry()
{
	MiniwinDiagEntry entry = g_entry;
	g_entry = MINIWIN_DIAG_ENTRY_NONE;
	return entry;
}

Source SourceOf(MiniwinDiagEntry p_entry)
{
	switch (p_entry) {
	case MINIWIN_DIAG_ENTRY_DEVICE:
		return SRC_DEVICE;
	case MINIWIN_DIAG_ENTRY_SURFACE:
		return SRC_SURFACE;
	case MINIWIN_DIAG_ENTRY_VIDEO:
		return SRC_VIDEO;
	default:
		return SRC_NONE;
	}
}

const char* ScaleName()
{
	return MiniwinGetScaleMode() == MINIWIN_SCALE_LETTERBOX ? "letterbox" : "stretch";
}

const char* ResolutionName()
{
	return MiniwinGetRenderResolution() == MINIWIN_RESOLUTION_ORIGINAL ? "original" : "native";
}

Signature Capture(const MiniwinRenderBackend& p_backend)
{
	Signature s;
	memset(&s, 0, sizeof(s));
	s.pref = MiniwinWidescreen_Get();
	s.active = p_backend.WidescreenActive();
	s.drawableW = p_backend.m_drawableW;
	s.drawableH = p_backend.m_drawableH;
	s.vpX = p_backend.m_vpX;
	s.vpY = p_backend.m_vpY;
	s.vpW = p_backend.m_vpW;
	s.vpH = p_backend.m_vpH;
	p_backend.SceneSize(s.sceneW, s.sceneH);
	s.uiFraction = (int) SDL_lroundf(p_backend.UIWidthFraction() * 10000.f);
	s.logicalW = p_backend.m_width;
	s.logicalH = p_backend.m_height;
	s.scale = (int) MiniwinGetScaleMode();
	s.resolution = (int) MiniwinGetRenderResolution();
	return s;
}

void StartBurst(const char* p_reason)
{
	g_burst = c_burstFrames;
	g_burstReason = p_reason;
}

} // namespace

void MiniwinDiagWs_SetEntry(MiniwinDiagEntry p_entry)
{
	g_entry = p_entry;
}

void MiniwinDiagWs_Pref(const char* p_what, bool p_before, bool p_after)
{
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[diag-ws] pref %s: widescreen %s -> %s",
		p_what,
		p_before ? "on" : "off",
		p_after ? "on" : "off"
	);
	StartBurst("pref");
}

void MiniwinDiagWs_Scope(bool p_begin, bool p_previousWorld, bool p_nowWorld)
{
	if (p_begin) {
		g_frame.scopeBegins++;
		g_frame.trace.Add(p_previousWorld ? "WB(nested)" : "WB");
	}
	else {
		g_frame.scopeEnds++;
		g_frame.trace.Add(p_nowWorld ? "WE(still-world)" : "WE");
	}
}

void MiniwinDiagWs_Draw(bool p_world, bool p_active, Uint32 p_vertexCount, Uint32 p_indexCount)
{
	Source source = SourceOf(TakeEntry());
	int world = p_world ? 1 : 0;
	g_frame.draws[world][source]++;
	g_frame.tris[world] += (p_indexCount ? p_indexCount : p_vertexCount) / 3;
	if (!p_active) {
		g_frame.drawsInactive++;
	}
	else if (!p_world) {
		g_frame.mappedUi++;
	}
	g_frame.trace.Add(p_world ? "WD" : "UD");
}

void MiniwinDiagWs_Clear(
	bool p_world,
	bool p_active,
	const SDL_Rect* p_rect,
	int p_logicalW,
	int p_logicalH,
	bool p_color,
	bool p_depth
)
{
	(void) p_active;
	(void) p_color;
	(void) p_depth;
	TakeEntry();
	bool full = !p_rect || (p_rect->x == 0 && p_rect->y == 0 && p_rect->w == p_logicalW && p_rect->h == p_logicalH);
	g_frame.clears[p_world ? 1 : 0][full ? 1 : 0]++;
	g_frame.trace.Add(p_world ? (full ? "WC(full)" : "WC(rect)") : (full ? "UC(full)" : "UC(rect)"));
}

void MiniwinDiagWs_SideBars(bool p_stale)
{
	g_frame.bars++;
	if (p_stale) {
		g_frame.staleBars++;
	}
	g_frame.trace.Add(p_stale ? "SB(stale)" : "SB");
}

void MiniwinDiagWs_Pillarbox(bool p_ran)
{
	g_frame.pillarCalls++;
	if (p_ran) {
		g_frame.pillarRan++;
	}
	g_frame.trace.Add(p_ran ? "PB" : "PB(inactive)");
}

void MiniwinDiagWs_Presentation(const MiniwinRenderBackend& p_backend)
{
	Signature now = Capture(p_backend);
	if (g_haveSignature && memcmp(&now, &g_signature, sizeof(now)) == 0) {
		return;
	}
	bool activeChanged = g_haveSignature && now.active != g_signature.active;
	g_signature = now;
	g_haveSignature = true;
	SDL_LogInfo(
		LOG_CATEGORY_MINIWIN,
		"[diag-ws] state: pref=%d active=%d drawable=%dx%d vp=%d,%d %dx%d scene=%dx%d uiFrac=%.4f logical=%dx%d "
		"scale=%s res=%s raceAspect=%.4f%s",
		now.pref,
		now.active,
		now.drawableW,
		now.drawableH,
		now.vpX,
		now.vpY,
		now.vpW,
		now.vpH,
		now.sceneW,
		now.sceneH,
		(double) now.uiFraction / 10000.0,
		now.logicalW,
		now.logicalH,
		ScaleName(),
		ResolutionName(),
		(double) MiniwinWidescreen_RaceAspect(false),
		activeChanged ? " (active flipped)" : ""
	);
	StartBurst("state-change");
}

void MiniwinDiagWs_Present(const MiniwinRenderBackend& p_backend, bool p_worldOpen, bool p_worldFrame, bool p_barsDirty)
{
	MiniwinDiagEntry presentVia = TakeEntry();
	g_frame.trace.Add("P");
	g_frameNumber++;

	Uint32 worldDraws = 0;
	Uint32 uiDraws = 0;
	for (int i = 0; i < SRC_COUNT; i++) {
		worldDraws += g_frame.draws[1][i];
		uiDraws += g_frame.draws[0][i];
	}
	bool worldFrame = worldDraws > 0 || g_frame.clears[1][0] + g_frame.clears[1][1] > 0;

	if (worldFrame && !g_prevWorldFrame) {
		StartBurst("world-begin");
	}
	else if (!worldFrame && g_prevWorldFrame) {
		StartBurst("world-end");
	}
	g_prevWorldFrame = worldFrame;

	Uint64 now = SDL_GetTicks();
	const char* why = nullptr;
	if (g_burst > 0) {
		why = g_burstReason;
		g_burst--;
	}
	else if ((MiniwinWidescreen_Get() || worldFrame) && now - g_lastPeriodic >= c_periodMs) {
		why = "periodic";
	}

	if (why && g_detailFrames < c_maxDetailFrames) {
		g_detailFrames++;
		g_lastPeriodic = now;
		const char* phase = worldFrame ? (uiDraws ? "race+ui" : "world-only") : "ui";
		const char* via = presentVia == MINIWIN_DIAG_ENTRY_BLT     ? "blt"
						  : presentVia == MINIWIN_DIAG_ENTRY_FLIP  ? "flip"
						  : presentVia == MINIWIN_DIAG_ENTRY_VIDEO ? "video"
																   : "other";
		SDL_LogInfo(
			LOG_CATEGORY_MINIWIN,
			"[diag-ws] frame#%llu t=%llu why=%s phase=%s via=%s pref=%d active=%d drawable=%dx%d vp=%d,%d %dx%d "
			"uiFrac=%.4f | world: draws=%u (dev=%u surf=%u vid=%u other=%u) tris=%u clears=%u/%u(full/rect) | "
			"ui: draws=%u (dev=%u surf=%u vid=%u other=%u) tris=%u mapped=%u clears=%u/%u(full/rect) | "
			"draws-while-inactive=%u | bars: drawn=%u stale=%u pillarbox=%u/%u(ran/called) | scopes: begin=%u end=%u "
			"open-at-present=%d | worldFrame=%d barsDirty=%d",
			(unsigned long long) g_frameNumber,
			(unsigned long long) now,
			why,
			phase,
			via,
			MiniwinWidescreen_Get() ? 1 : 0,
			p_backend.WidescreenActive() ? 1 : 0,
			p_backend.m_drawableW,
			p_backend.m_drawableH,
			p_backend.m_vpX,
			p_backend.m_vpY,
			p_backend.m_vpW,
			p_backend.m_vpH,
			(double) p_backend.UIWidthFraction(),
			worldDraws,
			g_frame.draws[1][SRC_DEVICE],
			g_frame.draws[1][SRC_SURFACE],
			g_frame.draws[1][SRC_VIDEO],
			g_frame.draws[1][SRC_NONE],
			g_frame.tris[1],
			g_frame.clears[1][1],
			g_frame.clears[1][0],
			uiDraws,
			g_frame.draws[0][SRC_DEVICE],
			g_frame.draws[0][SRC_SURFACE],
			g_frame.draws[0][SRC_VIDEO],
			g_frame.draws[0][SRC_NONE],
			g_frame.tris[0],
			g_frame.mappedUi,
			g_frame.clears[0][1],
			g_frame.clears[0][0],
			g_frame.drawsInactive,
			g_frame.bars,
			g_frame.staleBars,
			g_frame.pillarRan,
			g_frame.pillarCalls,
			g_frame.scopeBegins,
			g_frame.scopeEnds,
			p_worldOpen ? 1 : 0,
			p_worldFrame ? 1 : 0,
			p_barsDirty ? 1 : 0
		);
		char trace[512];
		g_frame.trace.Format(trace, sizeof(trace));
		SDL_LogInfo(LOG_CATEGORY_MINIWIN, "[diag-ws] frame#%llu seq: %s", (unsigned long long) g_frameNumber, trace);
	}

	memset(&g_frame, 0, sizeof(g_frame));
}

void MiniwinDiagWs_OnEvent(const SDL_Event& p_event)
{
	switch (p_event.type) {
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
	case SDL_EVENT_WINDOW_MAXIMIZED:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_SHOWN:
		if (SDL_AddAtomicInt(&g_windowEvents, 1) < c_maxWindowEvents) {
			SDL_LogInfo(
				LOG_CATEGORY_MINIWIN,
				"[diag-ws] window event 0x%x data=%d,%d t=%llu",
				(unsigned) p_event.type,
				(int) p_event.window.data1,
				(int) p_event.window.data2,
				(unsigned long long) SDL_GetTicks()
			);
		}
		break;
	default:
		break;
	}
}
