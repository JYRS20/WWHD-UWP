// SPDX-License-Identifier: MPL-2.0
#include "gfx/decorative_grass_policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace decorative_grass;
int main() {
 assert(valid_distance(1000)&&valid_distance(6000)&&valid_distance(12000));
 assert(!valid_distance(999)&&!valid_distance(12001));
 assert(!valid_distance(std::numeric_limits<float>::quiet_NaN()));
 assert(!valid_distance(std::numeric_limits<float>::infinity()));
 assert(view_permits_filter(45,false));assert(view_permits_filter(30,false));
 assert(!view_permits_filter(29,false));assert(!view_permits_filter(45,true));
 assert(!view_permits_filter(0,false));assert(!view_permits_filter(std::numeric_limits<float>::quiet_NaN(),false));
 // Hysteresis must retain distant visibility until leave, and hidden geometry
 // must reappear before approaching within enter. Invalid inputs fail open.
 assert(keep_visible(7000.f*7000,true,6000,7500));
 assert(!keep_visible(7000.f*7000,false,6000,7500));
 assert(!keep_visible(7501.f*7501,true,6000,7500));
 assert(keep_visible(6000.f*6000,false,6000,7500));
 // Short-distance option: preserve nearby grass and retain a hysteresis band.
 assert(keep_visible(1900.f*1900,false,2000,2500));
 assert(keep_visible(2400.f*2400,true,2000,2500));
 assert(!keep_visible(2400.f*2400,false,2000,2500));
 assert(!keep_visible(2501.f*2501,true,2000,2500));
 for(bool previous:{false,true}) {
  assert(keep_visible(-1,previous,6000,7500));
  assert(keep_visible(std::numeric_limits<float>::quiet_NaN(),previous,6000,7500));
  assert(keep_visible(std::numeric_limits<float>::infinity(),previous,6000,7500));
  assert(keep_visible(1,previous,-1,7500));
  assert(keep_visible(1,previous,6000,5000));
 }
 const DrawKey a{4,12,1,0x10200000,0,1},b{4,24,1,0x10200100,0,1};
 DrawFilter f;f.rows={{a,true},{a,false},{b,true},{a,false}};
 // Skipped draws still consume exactly one original packed-list position.
 assert(f.observe(a));assert(!f.observe(a));assert(f.observe(b));assert(!f.observe(a));assert(f.complete());
 assert(!f.observe(a)&&!f.valid); // unexpected extra draw fails open
 DrawFilter reordered;reordered.rows={{a,false},{b,true}};
 assert(!reordered.observe(b));assert(!reordered.observe(a));assert(!reordered.observe(b));assert(!reordered.complete());
 DrawFilter missing;missing.rows={{a,true},{b,true}};
 assert(missing.observe(a));assert(!missing.complete());
 // Every argument participates: never filter another draw with the same buffer.
 for(int field=0;field<6;++field) {
  DrawKey changed=a;
  switch(field){case 0:++changed.primitive;break;case 1:++changed.count;break;case 2:++changed.type;break;
   case 3:++changed.indices;break;case 4:++changed.base;break;case 5:++changed.instances;break;}
  DrawFilter exact;exact.rows={{a,true}};assert(!exact.observe(changed)&&!exact.valid);
 }
 std::puts("decorative grass policy: thresholds, invalid input, hysteresis, packed order, exact draw identity and fail-open PASS");
}
