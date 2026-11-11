#pragma once

namespace coop::downed {

// CPlayerInfo::KillPlayer (hooked in Missions.cpp) asks first: true means "we lie down, a partner can still bring us
// up": the game does not get its death (no WASTED, no hospital, the mission goes on) until the time is out or nobody
// is left to help.
bool OnKillPlayer();

// our player lies down now (sent to the others in OnFootState::F2_DOWNED)
bool IsDown();

}  // namespace coop::downed
