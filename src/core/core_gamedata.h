#pragma once

// Boot adapter over the Gamedata Source (core/gamedata_feed.h): feeds it the core config, the running build, the bundled
// gamedata.json and the real transport, and loads its choice into globals::gameConfig for the resolver. While the server
// runs, polls the feed for a newer snapshot for the running build (applied by the next boot).

#include <string>

#include "core/gamedata_feed.h"

namespace counterstrikesharp {

// Chooses this boot's core Gamedata and loads it into globals::gameConfig; logs one provenance line. False with the
// reason only when the chosen Gamedata cannot be read (the bundled file missing or unparseable): a feed failure never
// fails boot, it falls back to the bundled file.
bool LoadCoreGamedata(std::string& error);

// What LoadCoreGamedata booted on: the bytes (managed reads its core Gamedata from these, not from disk) and provenance.
const feed::ChosenGamedata& CoreGamedata();

// Creates the OnGamedataUpdateAvailable listener and, unless the feed is off or GamedataFeedPollMinutes is 0, starts
// polling the feed every GamedataFeedPollMinutes on a background thread. A staged update is logged once (a restart
// applies it) and raised as OnGamedataUpdateAvailable(build, seq, changes) on the game thread. Call after
// LoadCoreGamedata and before plugins register listeners.
void StartGamedataPoll();

// Stops the poll thread (waits for a poll in progress, at most its 10 s deadline) and releases the listener. Safe to call
// when the poll never started.
void StopGamedataPoll();

} // namespace counterstrikesharp
