#pragma once

// [library:profiling] App-local CPU diagnostics; no game timing or state changes.
enum MiniwinRaceProfilePhase {
	MINIWIN_RACE_TICK,
	MINIWIN_RACE_STATE,
	MINIWIN_RACE_UPDATE,
	MINIWIN_RACE_DRAW,
	MINIWIN_RACE_PRESENT,
	MINIWIN_RACE_SOUND,
	MINIWIN_RACE_RACERS,
	MINIWIN_RACE_PHASE_COUNT
};
void MiniwinRaceProfile_Reset();
void MiniwinRaceProfile_BeginFrame(unsigned int p_state, bool p_paused, unsigned int p_racers);
void MiniwinRaceProfile_Begin(MiniwinRaceProfilePhase p_phase);
void MiniwinRaceProfile_End(MiniwinRaceProfilePhase p_phase);
void MiniwinRaceProfile_EndFrame();
