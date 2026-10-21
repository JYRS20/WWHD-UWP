// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace wwhd_menu {
enum class Icon { Connected, Play, Folder, Disc };
struct Row {std::string title,subtitle,tag;Icon icon=Icon::Folder;bool ready=false;};
struct View {
 bool browsing=false,loading=false,running=false,connected=false,cancelling=false,keys_visible=false,keys_ready=false;
 std::string folder,game,stage,detail,error;
 unsigned percent=0;size_t selected=0;
 std::vector<Row> rows;
};
class Renderer {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 Renderer(SDL_Renderer*,const std::filesystem::path& assets);
 ~Renderer();
 void draw(const View&);
};
}
