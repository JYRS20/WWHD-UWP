// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <filesystem>
struct WwhdPreparedGame { std::filesystem::path game,objects; };
// Owns its temporary SDL window; returns only after objects have been verified and linked.
WwhdPreparedGame wwhd_prepare_and_load();
