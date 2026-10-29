// [library:profiling] Coarse CPU measurements, owned by LEGORacers.exe rather
// than the renderer DLL. Nested sound/racer time is part of Update, not additive.
#include <SDL3/SDL.h>
#include <miniwin/raceprofile.h>

namespace
{
struct Profile {
	bool enabled = false;
	unsigned int state = 0;
	bool paused = false;
	unsigned int racers = 0;
	Uint64 start = 0, frameStart = 0, frames = 0, frameNs = 0, worstNs = 0, steps = 0;
	Uint64 begin[MINIWIN_RACE_PHASE_COUNT] = {};
	Uint64 total[MINIWIN_RACE_PHASE_COUNT] = {};
};
Profile s_profile;
} // namespace

void MiniwinRaceProfile_Reset()
{
	s_profile = {};
#ifdef WINDOWS_STORE
	s_profile.enabled = true;
#else
	s_profile.enabled = SDL_getenv("RACERS_GL_STATS") != nullptr;
#endif
}

void MiniwinRaceProfile_BeginFrame(unsigned int p_state, bool p_paused, unsigned int p_racers)
{
	if (!s_profile.enabled) {
		return;
	}
	if (s_profile.state != p_state || s_profile.paused != p_paused || s_profile.racers != p_racers) {
		MiniwinRaceProfile_Reset();
		s_profile.state = p_state;
		s_profile.paused = p_paused;
		s_profile.racers = p_racers;
	}
	s_profile.frameStart = SDL_GetTicksNS();
	if (!s_profile.start) {
		s_profile.start = s_profile.frameStart;
	}
}

void MiniwinRaceProfile_Begin(MiniwinRaceProfilePhase p_phase)
{
	if (s_profile.enabled) {
		s_profile.begin[p_phase] = SDL_GetTicksNS();
	}
}

void MiniwinRaceProfile_End(MiniwinRaceProfilePhase p_phase)
{
	if (s_profile.enabled) {
		s_profile.total[p_phase] += SDL_GetTicksNS() - s_profile.begin[p_phase];
		if (p_phase == MINIWIN_RACE_RACERS) {
			s_profile.steps++;
		}
	}
}

void MiniwinRaceProfile_EndFrame()
{
	Profile& p = s_profile;
	if (!p.enabled) {
		return;
	}
	Uint64 now = SDL_GetTicksNS();
	Uint64 elapsed = now - p.frameStart;
	p.frameNs += elapsed;
	p.worstNs = SDL_max(p.worstNs, elapsed);
	p.frames++;
	if (now - p.start < 5000000000ull) {
		return;
	}
	double perFrameMs = 1.0 / ((double) p.frames * 1000000.0);
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"Race CPU stats: state %u, paused %d, racers %u, %.1f FPS, frame mean/worst %.2f/%.2f ms; "
		"ms/frame Tick %.2f, state/input %.2f, Update %.2f, Draw %.2f, PresentFrame %.2f; "
		"within Update: sound manager %.2f, racer steps %.2f ms, %.2f steps/frame",
		p.state,
		(int) p.paused,
		p.racers,
		p.frames * 1000000000.0 / (double) (now - p.start),
		p.frameNs * perFrameMs,
		p.worstNs / 1000000.0,
		p.total[MINIWIN_RACE_TICK] * perFrameMs,
		p.total[MINIWIN_RACE_STATE] * perFrameMs,
		p.total[MINIWIN_RACE_UPDATE] * perFrameMs,
		p.total[MINIWIN_RACE_DRAW] * perFrameMs,
		p.total[MINIWIN_RACE_PRESENT] * perFrameMs,
		p.total[MINIWIN_RACE_SOUND] * perFrameMs,
		p.total[MINIWIN_RACE_RACERS] * perFrameMs,
		p.steps / (double) p.frames
	);
	unsigned int state = p.state, racers = p.racers;
	bool paused = p.paused;
	MiniwinRaceProfile_Reset();
	p.state = state;
	p.paused = paused;
	p.racers = racers;
}
