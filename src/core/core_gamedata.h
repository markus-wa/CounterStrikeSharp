#pragma once

// Boot adapter over the Gamedata Source (core/gamedata_feed.h): feeds it the core config, the running build, the bundled
// gamedata.json and the real transport, and loads its choice into globals::gameConfig for the resolver.

#include <string>

#include "core/gamedata_feed.h"

namespace counterstrikesharp {

// Chooses this boot's core Gamedata and loads it into globals::gameConfig; logs one provenance line. False with the
// reason only when the chosen Gamedata cannot be read (the bundled file missing or unparseable): a feed failure never
// fails boot, it falls back to the bundled file.
bool LoadCoreGamedata(std::string& error);

// What LoadCoreGamedata booted on: the bytes (managed reads its core Gamedata from these, not from disk) and provenance.
const feed::ChosenGamedata& CoreGamedata();

} // namespace counterstrikesharp
