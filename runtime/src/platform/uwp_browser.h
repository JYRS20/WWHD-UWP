// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef WWHD_UWP
#include <functional>
#include <string>
namespace uwp_browser {
void choose_mod(bool folder,std::function<void(std::string)> chosen);
bool active();
void draw();
}
#endif
