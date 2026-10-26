#pragma once

// [library:config] Game-thread widescreen preference. Set saves immediately;
// Load is called after persistent storage is mounted, before CLI overrides.
void MiniwinWidescreen_Load();
void MiniwinWidescreen_Set(bool p_enabled);
bool MiniwinWidescreen_Get();
bool MiniwinWidescreen_Active();
float MiniwinWidescreen_RaceAspect(bool p_splitScreen);

// [library:3d] Everything defaults to UI. Race-world submission (and full-width
// screen fades) open this scope; End restores the phase that was active when it
// began (UI before HUD/pause draws). Destruction also restores it.
class MiniwinWorldScope {
public:
	MiniwinWorldScope();
	~MiniwinWorldScope();
	void End();

private:
	bool m_previous = false;
	bool m_ended = false;
};

// [library:input] Inverse of the centred UI transform. Normalized window input;
// returns false outside the UI rect (callers must not dispatch a click there).
bool MiniwinWidescreen_MapUI(float p_nx, float p_ny, int p_width, int p_height, long* p_x, long* p_y);
