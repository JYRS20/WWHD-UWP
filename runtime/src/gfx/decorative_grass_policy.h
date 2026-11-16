// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace decorative_grass {
inline bool valid_distance(float distance) {
 return std::isfinite(distance)&&distance>=1000&&distance<=12000;
}
inline bool view_permits_filter(float fov,bool stereo) {
 return !stereo&&std::isfinite(fov)&&fov>=30&&fov<=110;
}
// Pure draw policy: no actor, collision or packet-list writes.
inline bool keep_visible(float distance2,bool wasVisible,float enter,float leave) {
 if(!std::isfinite(distance2)||distance2<0||!std::isfinite(enter)||
    !std::isfinite(leave)||enter<=0||leave<enter)return true;
 const float limit=wasVisible?leave:enter;
 return distance2<=limit*limit;
}
struct DrawKey {
 uint32_t primitive=0,count=0,type=0,indices=0,base=0,instances=0;
 bool operator==(const DrawKey&)const=default;
};
struct Decision {DrawKey key;bool omit=false;};
class DrawFilter {
public:
 std::vector<Decision> rows;
 size_t consumed=0;
 bool valid=true;
 bool observe(const DrawKey& key) {
  if(!valid)return false;
  if(consumed>=rows.size()||!(rows[consumed].key==key)) {valid=false;return false;}
  return rows[consumed++].omit;
 }
 bool complete()const{return valid&&consumed==rows.size();}
};
}
