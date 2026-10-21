// Runtime-only interface; static builds retain their original game-code linkage.
#pragma once
#ifdef WWHD_DYNAMIC_GAMECODE
#include "runtime.h"
#include "link_api.h"
#include <vector>
#include <string>
void dynamic_game_load(const std::wstring& objects,const std::wstring& rpx);
void dynamic_game_native_hooks(std::vector<WwhdRuntimeSymbol>& symbols);
void dynamic_game_bind_forwarders(WwhdLinkedImage* image);
#endif
