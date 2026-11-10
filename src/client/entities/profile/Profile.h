#pragma once
// The partner's profile on the host's server: money, skills and look of its own. The progress feature
// (features/progress) keeps it and answers this.

namespace coop {

// partner: send the profile (money, skills, look) now and keep it for the next world load
void SaveProfileNow();

// the partner's profile (clothes, body, skills) is on the player, or none is coming (the host, a game of its own): what
// changes the player's body, as a skin does, may go on now
bool ProfileSettled();

}  // namespace coop
