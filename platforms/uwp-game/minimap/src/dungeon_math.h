#ifndef GC_DUNGEON_MATH_H
#define GC_DUNGEON_MATH_H
#include "minimap_math.h"
/* Read-only public dStage actor-base layout. The keep tables store inline arrays. */
typedef struct {
    char name[8]; unsigned int parameters; float x,y,z; unsigned short rooms; short yaw; unsigned short flags,set_id;
} dungeon_actor;
typedef struct { dungeon_actor actor; unsigned char scale[4]; } dungeon_door;
static inline unsigned int dungeon_chest_number(unsigned int parameters) { return (parameters>>7)&31; }
static inline int dungeon_chest_on_room(unsigned int parameters,unsigned int rooms,int room) {
    unsigned int number=dungeon_chest_number(parameters);
    return (rooms&63)==(unsigned int)room&&number!=15&&number!=16;
}
static inline int dungeon_door_on_room(unsigned int rooms,int room) {
    return (rooms&63)==(unsigned int)room||((rooms>>6)&63)==(unsigned int)room;
}
/* Dungeon markers keep their world projection; the HUD clip crops their geometry. */
static inline minimap_point dungeon_project(float x,float z,const float* bounds) {
    minimap_point p={0,0,0,0};
    if(!minimap_finite(x)||!minimap_finite(z)||!minimap_bounds_valid(bounds))return p;
    p.x=(x-bounds[0])/(bounds[2]-bounds[0]);p.z=(z-bounds[1])/(bounds[3]-bounds[1]);
    p.valid=minimap_finite(p.x)&&minimap_finite(p.z);
    p.clipped=p.x<0||p.x>1||p.z<0||p.z>1;
    return p;
}
static inline unsigned int dungeon_u32(const unsigned char* p) {
    return ((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];
}
/* Only rooms verified in this preparation may use cached local files. */
static inline int dungeon_ready(const unsigned char* p,unsigned int size,const char* stage,int room) {
    if(size<8||p[0]!='G'||p[1]!='C'||p[2]!='R'||p[3]!='1'||room<0||room>=64)return 0;
    unsigned int n=dungeon_u32(p+4);
    if(n>64||size!=8+16*n)return 0;
    for(unsigned int i=0;i<n;++i) {
        const unsigned char* e=p+8+16*i;int same=1;
        for(int j=0;j<8;++j)if(e[j]!=(unsigned char)stage[j])same=0;
        if(same)return (dungeon_u32(e+8+4*(room/32))>>(room%32))&1u;
    }
    return 0;
}
/* GCR1, count, then ascending unique (floor u32, bounds 4*f32). */
static inline int dungeon_bounds(const unsigned char* p, unsigned int size, int floor, float* bounds) {
    if(size<8 || p[0]!='G'||p[1]!='C'||p[2]!='R'||p[3]!='1')return 0;
    unsigned int n=dungeon_u32(p+4);
    if(!n||n>10||size!=8+20*n||floor<123||floor>132)return 0;
    int chosen=-1, prior=122;
    for(unsigned int i=0;i<n;++i) {
        int f=(int)dungeon_u32(p+8+20*i);
        float checked[4];
        if(f<=prior||f>132||!minimap_decode_bounds(p+12+20*i,16,checked))return 0;
        prior=f;
        if(chosen<0 || f<=floor)chosen=(int)i;
    }
    minimap_decode_bounds(p+12+20*chosen,16,bounds);
    return (int)dungeon_u32(p+8+20*chosen);
}
#endif
