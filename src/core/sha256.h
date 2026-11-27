#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace counterstrikesharp {

// SHA-256 (FIPS 180-4) of data. Monocypher, the vendored crypto library, has SHA-512 only; Gamedata Feed snapshot
// hashes and key ids are SHA-256 (ADR-0006).
std::array<std::uint8_t, 32> Sha256(std::string_view data);

} // namespace counterstrikesharp
