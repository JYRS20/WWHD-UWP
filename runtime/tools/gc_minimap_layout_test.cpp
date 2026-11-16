// SPDX-License-Identifier: MPL-2.0
#include "mods/guest_hud.h"
#include <cassert>
#include <cmath>
using namespace guestmods::hud;
int main(){
 Store store;Command map;map.kind=Command::Rect;map.anchor=Anchor::TopLeft;map.x=28.5f;map.y=499.5f;map.w=map.h=180;
 auto list=store.begin("gc-minimap",0);assert(store.append("gc-minimap",list,map));
 auto clip=map;clip.kind=Command::ClipPush;assert(store.append("gc-minimap",list,clip));
 auto marker=map;marker.x=160;marker.y=540;marker.w=marker.h=12;assert(store.append("gc-minimap",list,marker));
 Command pop;pop.kind=Command::ClipPop;assert(store.append("gc-minimap",list,pop));
 assert(!store.avoid_bottom_left("other-mod",list,220,600,12));
 assert(store.avoid_bottom_left("gc-minimap",list,220,600,12));
 assert(!store.avoid_bottom_left("gc-minimap",list,220,600,12)); // no accumulated movement
 assert(store.commit("gc-minimap",list));auto blocks=store.snapshot(0);assert(blocks.size()==1);
 const auto& c=blocks[0]->commands;assert(c.size()==4);assert(c[0].y+c[0].h==588);
 assert(c[1].y==c[0].y);assert(c[2].y==marker.y-91.5f);assert(c[3].y==pop.y);
 auto right=store.begin("right",0);map.x=900;assert(store.append("right",right,map));assert(!store.avoid_bottom_left("right",right,220,600,12));
 auto high=store.begin("high",0);map.x=28;map.y=200;assert(store.append("high",high,map));assert(!store.avoid_bottom_left("high",high,220,600,12));
 auto empty=store.begin("empty",0);assert(!store.avoid_bottom_left("empty",empty,220,600,12));
 auto huge=store.begin("huge",0);map.y=0;map.h=700;assert(store.append("huge",huge,map));assert(!store.avoid_bottom_left("huge",huge,220,600,12));
 assert(!store.avoid_bottom_left("high",high,220,NAN,12));
}
