#pragma once

// TEMPORARY Xbox diagnostics: widescreen presentation logging ("[diag-ws]"). Remove this
// file, diagws.cpp and every MiniwinDiagWs_* call once the Xbox widescreen problem is
// understood. All output is rate-limited; see diagws.cpp.

#include <SDL3/SDL.h>

class MiniwinRenderBackend;

// Which game-facing entry point submits the next draw, clear or present. The caller sets it
// immediately before calling the backend; the next draw/clear/present consumes (resets) it.
enum MiniwinDiagEntry {
	MINIWIN_DIAG_ENTRY_NONE,
	MINIWIN_DIAG_ENTRY_DEVICE,  // d3ddevice.cpp: DrawPrimitive / Viewport::Clear
	MINIWIN_DIAG_ENTRY_SURFACE, // ddsurface.cpp: software-surface composite / colour fill
	MINIWIN_DIAG_ENTRY_VIDEO,   // backends.cpp: Indeo video frame
	MINIWIN_DIAG_ENTRY_BLT,     // present via Blt to the primary surface
	MINIWIN_DIAG_ENTRY_FLIP,    // present via Flip
};

void MiniwinDiagWs_SetEntry(MiniwinDiagEntry p_entry);
void MiniwinDiagWs_Pref(const char* p_what, bool p_before, bool p_after);
void MiniwinDiagWs_Scope(bool p_begin, bool p_previousWorld, bool p_nowWorld);
void MiniwinDiagWs_Draw(bool p_world, bool p_active, Uint32 p_vertexCount, Uint32 p_indexCount);
void MiniwinDiagWs_Clear(
	bool p_world,
	bool p_active,
	const SDL_Rect* p_rect,
	int p_logicalW,
	int p_logicalH,
	bool p_color,
	bool p_depth
);
void MiniwinDiagWs_SideBars(bool p_stale);
void MiniwinDiagWs_Pillarbox(bool p_ran);
// End of UpdatePresentation: logs the full geometry whenever it differs from the last call.
void MiniwinDiagWs_Presentation(const MiniwinRenderBackend& p_backend);
// End of PresentScene: summarises (rate-limited) and resets the per-frame counters.
void MiniwinDiagWs_Present(
	const MiniwinRenderBackend& p_backend,
	bool p_worldOpen,
	bool p_worldFrame,
	bool p_barsDirty
);
// Window-geometry events seen by the main thread (resize, pixel size, display scale, fullscreen).
void MiniwinDiagWs_OnEvent(const SDL_Event& p_event);
