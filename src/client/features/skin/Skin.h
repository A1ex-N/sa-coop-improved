#pragma once
// Skins: the model our player wears instead of CJ (Skin.cpp).
#include <string>
#include <vector>

namespace coop::skin {

// The chat command /skin <number | next | prev | 0>. True if the command was ours.
bool Command(const std::string& cmd, const std::vector<std::string>& args);

// Puts that model (0: CJ in his clothes) on our player now. False: the reason is in why (nothing was changed).
bool Apply(int model, std::string& why);

// Available player models in this game (including 0: CJ), and the model currently worn.
const std::vector<int>& Models();
int Current();
// Apply a choice and persist it to coop.ini, as /skin does.
bool Choose(int model, std::string& why);

}  // namespace coop::skin
