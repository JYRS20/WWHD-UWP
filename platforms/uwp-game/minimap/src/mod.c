/* GameCube-style minimap guest mod. No writes to game memory or embedded game data. */
#include "wwhd_guest.h"
#include "wwhd/actor.h"
#include "wwhd/functions.h"
#include "wwhd/data.h"
#include "minimap_math.h"
#include "dungeon_math.h"
WWHD_GAME_FUNC(0x0258DBF4, u8, dungeon_floor, (s32 room, f32 y));
WWHD_GAME_FUNC(0x025B8B94, s32, dungeon_event, (u32 events, u16 flag));
WWHD_GAME_FUNC(0x025B9100, s32, dungeon_item, (u32 record, u32 item));
WWHD_GAME_FUNC(0x025B8C74, s32, dungeon_chest_open, (u32 record, u32 chest));
WWHD_GAME_FUNC(0x02520864, s32, dungeon_stage_chest_open, (u32 stage, u32 chest));
WWHD_GAME_FUNC(0x025C11DC, u8*, dungeon_room_data, (u32 control, s32 room));
WWHD_GAME_FUNC(0x025C4734, u8*, dungeon_treasures, (void));
WWHD_GAME_FUNC(0x025C4740, u8*, dungeon_doors, (void));

static struct { float x, z; s16 yaw; int room, visible, dungeon, floor, compass; char stage[9]; } snapshot;
static struct { float x, bottom, side, opacity; int original, dungeons; } options;
static minimap_freshness freshness;
static unsigned long long image_epoch;
static int cached_room, initialized;
static u32 chart, frame, marker, arrow;
static float bounds[4];
static unsigned char bounds_bytes[17];
static char path[64], style[16], cached_stage[9];
static int cached_floor, cached_dungeon;
static unsigned char dungeon_bytes[209], ready_bytes[1033];
static wwhd_dungeon_room mod_room;
static u32 mod_revision;
static struct { float x,z; int type; } points[64];
static int point_count;
static int same_stage(const char* a,const char* b) {
    for(int i=0;i<9;++i) { if(a[i]!=b[i])return 0; if(!a[i])return 1; } return 0;
}
static void dungeon_path(const char* suffix,int floor) {
    int i=0;
    path[i++]='d';path[i++]='-';
    for(int j=0;snapshot.stage[j]&&j<8;++j)path[i++]=snapshot.stage[j];
    path[i++]='-';path[i++]=(char)('0'+snapshot.room/10);path[i++]=(char)('0'+snapshot.room%10);
    if(floor) { path[i++]='-';path[i++]=(char)('0'+floor/100);path[i++]=(char)('0'+floor/10%10);path[i++]=(char)('0'+floor%10); }
    for(int j=0;suffix[j];++j)path[i++]=suffix[j];path[i]=0;
}
static wwhd_hud_element element;
static const float pi = 3.14159265358979323846f;

static float bounded(double value, float low, float high, float fallback) {
    if (!(value >= low && value <= high)) return fallback;
    return (float)value;
}
static void configure(void) {
    options.dungeons = wwhd_config_bool("dungeons", 1);
    options.x = bounded(wwhd_config_float("x", 6), 0, 1200, 6);
    options.bottom = bounded(wwhd_config_float("y", 18), 0, 650, 18);
    options.side = bounded(wwhd_config_float("size", 225), 48, 600, 225);
    options.opacity = bounded(wwhd_config_float("opacity", .8), 0, 1, .8);
    if (options.x + options.side > 1280) options.x = 1280 - options.side;
    if (options.bottom + options.side > 720) options.bottom = 720 - options.side;
    wwhd_config_string("style", style, sizeof style);
    options.original = style[0]=='o' && style[1]=='r' && style[2]=='i' && style[3]=='g' &&
                       style[4]=='i' && style[5]=='n' && style[6]=='a' && style[7]=='l' && !style[8];
    initialized = 1;
}
static void room_path(int room, const char* suffix) {
    const char prefix[] = "room-";
    int i;
    for (i = 0; i < 5; ++i) path[i] = prefix[i];
    path[i++] = (char)('0' + room / 10);
    path[i++] = (char)('0' + room % 10);
    for (int j = 0; suffix[j]; ++j) path[i++] = suffix[j];
    path[i] = 0;
}
static void forget_images(void) {
    chart = frame = marker = arrow = 0;
    cached_room = 0; cached_stage[0]=0; cached_floor=0; cached_dungeon=0; mod_revision=0;
}
static void prepare_chart(int room) {
    if (cached_room == room && cached_dungeon==snapshot.dungeon &&
        (!snapshot.dungeon || (cached_floor==snapshot.floor && same_stage(cached_stage,snapshot.stage)))) return;
    if (chart) wwhd_hud_release(chart);
    chart = 0;
    cached_room = room; /* Missing/unverified data stays hidden until restart/epoch/sector change. */
    cached_dungeon=snapshot.dungeon; cached_floor=snapshot.floor;
    for(int i=0;i<9;++i)cached_stage[i]=snapshot.stage[i];
    if(snapshot.dungeon) {
        s32 ready_size=wwhd_file_read("dungeons-ready.bin",ready_bytes,sizeof ready_bytes);
        if(ready_size<0||!dungeon_ready(ready_bytes,(u32)ready_size,snapshot.stage,snapshot.room))return;
        dungeon_path(".bin",0);
        s32 bytes=wwhd_file_read(path,dungeon_bytes,sizeof dungeon_bytes);
        int floor=bytes<0?0:dungeon_bounds(dungeon_bytes,(u32)bytes,snapshot.floor,bounds);
        if(!floor)return;
        dungeon_path(options.original?"-original.png":"-vector.png",floor);
        chart=wwhd_hud_texture(WWHD_HUD_DATA,path);
    } else {
    room_path(room, ".bounds");
    s32 count = wwhd_file_read(path, bounds_bytes, sizeof bounds_bytes);
    if (count < 0 || !minimap_decode_bounds(bounds_bytes, (u32)count, bounds)) return;
    room_path(room, options.original ? "-original.png" : "-vector.png");
    chart = wwhd_hud_texture(WWHD_HUD_DATA, path);
    if (!chart) {
        room_path(room, options.original ? "-vector.png" : "-original.png");
        chart = wwhd_hud_texture(WWHD_HUD_DATA, path);
    }
    }
    if (!chart) return;
    if (!frame) frame = wwhd_hud_texture(WWHD_HUD_PACKAGE, "assets/frame.png");
    if (!marker) marker = wwhd_hud_texture(WWHD_HUD_PACKAGE, "assets/marker.png");
    if (!arrow) arrow = wwhd_hud_texture(WWHD_HUD_PACKAGE, "assets/arrow.png");
}
static void image(u32 list, u32 handle, float x, float y, float w, float h,
                  float rotation, float opacity) {
    if (!handle || opacity <= 0) return;
    element = (wwhd_hud_element){0};
    element.kind = WWHD_HUD_IMAGE;
    element.anchor = WWHD_HUD_TOP_LEFT;
    element.x=x; element.y=y; element.w=w; element.h=h;
    element.rotation=rotation;
    element.thickness=1;
    element.u1=element.v1=1;
    element.image=handle;
    element.rgba=(snapshot.dungeon&&mod_revision&&handle==chart?0x00800000u:0xffffff00u) | (u32)(opacity * 255.0f + .5f);
    wwhd_hud_emit(list, &element);
}
static void draw(u32 list) {
    unsigned long long epoch = wwhd_hud_epoch();
    if (epoch != image_epoch) { image_epoch=epoch; forget_images(); }
    /* Hidden while paused or in a menu: Link's Execute didn't run for this step. */
    if (!minimap_snapshot_current(&freshness, wwhd_logic_step())) return;
    if (!snapshot.visible || options.opacity <= 0) return;
    int custom=0;
    if(snapshot.dungeon) {
        custom=wwhd_dungeon_room_get(&mod_room,mod_revision);
        if(custom) {
            if(mod_room.image) { if(chart)wwhd_hud_release(chart);chart=mod_room.image; }
            mod_revision=mod_room.revision;
            for(int i=0;i<4;++i)bounds[i]=mod_room.bounds[i];
            if(!frame)frame=wwhd_hud_texture(WWHD_HUD_PACKAGE,"assets/frame.png");
            if(!marker)marker=wwhd_hud_texture(WWHD_HUD_PACKAGE,"assets/marker.png");
        }
    }
    if(!custom) {
        if(mod_revision) { if(chart)wwhd_hud_release(chart);chart=0;mod_revision=0;cached_room=-1; }
        prepare_chart(snapshot.room);
    }
    if (!chart) return;
    minimap_point point = snapshot.dungeon?dungeon_project(snapshot.x,snapshot.z,bounds):minimap_project(snapshot.x,snapshot.z,bounds);
    if (!point.valid) return;
    float s=options.side, x=options.x, y=720-s-options.bottom, a=options.opacity;
    image(list, chart, x+.1f*s, y+.1f*s, .8f*s, .8f*s, 0, a);
    float overlay = a < .8f ? a / .8f : 1;
    image(list, frame, x+.1f*s, y+.1f*s, .8f*s, .8f*s, 0, overlay);
    /* Game yaw zero faces +Z (map down); original marker art points up. */
    element = (wwhd_hud_element){0};
    element.kind=WWHD_HUD_CLIP_PUSH;
    element.anchor=WWHD_HUD_TOP_LEFT;
    element.x=x+.1f*s; element.y=y+.1f*s;
    element.w=element.h=.8f*s;
    element.thickness=1;
    wwhd_hud_clip(list, &element);
    if(snapshot.dungeon) {
        int count=custom?(int)mod_room.marker_count:point_count;
        for(int i=0;i<count&&i<64;++i) {
            float px=custom?mod_room.markers[i].x:points[i].x;
            float pz=custom?mod_room.markers[i].z:points[i].z;
            int type=custom?(int)mod_room.markers[i].kind:points[i].type;
            minimap_point p=dungeon_project(px,pz,bounds);
            if(!p.valid)continue;
            element=(wwhd_hud_element){0};
            element.kind=WWHD_HUD_RECT; element.anchor=WWHD_HUD_TOP_LEFT;
            element.x=x+s*(.1f+.8f*p.x)-s*.015f; element.y=y+s*(.1f+.8f*p.z)-s*.012f;
            element.w=s*.03f;element.h=s*.024f;element.thickness=1;
            element.rgba=(type==1?0xe0b03000u:custom?0xc0303000u:0xffffff00u)|(u32)(a*255);
            wwhd_hud_emit(list,&element);
        }
    }
    float extent=.8f*s*.078f;
    image(list, marker, x+s*(.1f+.8f*point.x)-extent*.5f,
          y+s*(.1f+.8f*point.z)-extent*.5f, extent, extent,
          pi-(float)snapshot.yaw*(2*pi/65536.0f), overlay);
    element = (wwhd_hud_element){0};
    element.kind=WWHD_HUD_CLIP_POP;
    element.thickness=1;
    wwhd_hud_clip(list, &element);
    if(snapshot.dungeon)return;
    image(list, arrow, x+.45f*s, y+.02f*s, .1f*s, .05f*s, pi, overlay);
    image(list, arrow, x-.005f*s, y+.475f*s, .1f*s, .05f*s, pi*.5f, overlay);
    image(list, arrow, x+.905f*s, y+.475f*s, .1f*s, .05f*s, -pi*.5f, overlay);
}
static int is_sea(const u8* stage) {
    return stage[0]=='s' && stage[1]=='e' && stage[2]=='a' &&
           (!stage[3] || (stage[3]=='_' && (stage[4]=='T' || stage[4]=='E') && !stage[5]));
}
static void capture_point(int kind,float x,float y,float z) {
    if(point_count>=64||dungeon_floor(snapshot.room,y)!=snapshot.floor)return;
    for(int i=0;i<point_count;++i)if(points[i].type==kind&&points[i].x==x&&points[i].z==z)return;
    points[point_count].x=x;points[point_count].z=z;points[point_count++].type=kind;
}
static void capture_treasures(u32 save,const u8* records,s32 count,int filter_room) {
    if(!snapshot.compass||!records||count<0||count>128)return;
    for(s32 i=0;i<count;++i) {
        const dungeon_actor* actor=(const dungeon_actor*)(records+32*i);
        u32 number=dungeon_chest_number(actor->parameters),type=actor->parameters&127;
        if(number==15||number==16||(filter_room&&!dungeon_chest_on_room(actor->parameters,actor->rooms,snapshot.room)))continue;
        int opened=(type==7||type==8)?dungeon_stage_chest_open(1,number):dungeon_chest_open(save+0x798,number);
        if(!opened)capture_point(1,actor->x,actor->y,actor->z);
    }
}
static void capture_doors(const u8* records,s32 count) {
    if(!records||count<0||count>128)return;
    for(s32 i=0;i<count;++i) {
        const dungeon_door* door=(const dungeon_door*)(records+36*i);
        if(dungeon_door_on_room(door->actor.rooms,snapshot.room))capture_point(2,door->actor.x,door->actor.y,door->actor.z);
    }
}
static void capture_dungeon_markers(u32 save,u8* play) {
    point_count=0;
    /* Stage records are kept inline; room records remain count/pointer views.
     * HD's room handlers never copy their treasure/door lists into the keep tables. */
    u8* table=dungeon_treasures();
    if(table)capture_treasures(save,table+4,*(s32*)table,1);
    table=dungeon_doors();
    if(table)capture_doors(table+4,*(s32*)table);
    u8* room=dungeon_room_data((u32)play+0x51cc,snapshot.room);
    if(!room)return;
    table=*(u8**)(room+0x40);
    if(table)capture_treasures(save,*(u8**)(table+4),*(s32*)table,0);
    table=*(u8**)(room+0x44);
    if(table)capture_doors(*(u8**)(table+4),*(s32*)table);
}
WWHD_HOOK_RETURN(WWHD_ADDR_daPy_Execute, capture_minimap, (fopAc_ac_c* actor)) {
    if (!initialized) configure();
    wwhd_hud_register(draw, WWHD_HUD_TV);
    snapshot.visible=0;
    minimap_note_capture(&freshness);
    if (options.opacity <= 0) return;
    u8* play=wwhd_play_get();
    if (!play || !actor || (u32)actor != *(u32*)(play+WWHD_PLAY_PLAYER_OFFSET) ||
        play[WWHD_PLAY_ENABLE_NEXT_STAGE_OFFSET] || play[WWHD_PLAY_EVENT_RUNNING_OFFSET]) return;
    snapshot.dungeon=!is_sea(play+WWHD_PLAY_START_STAGE_NAME_OFFSET);
    if(snapshot.dungeon && (!options.dungeons || actor->current.roomNo<0 || actor->current.roomNo>=64))return;
    int ended=0;
    for(int i=0;i<8;++i) {
        char ch=ended?0:(char)play[WWHD_PLAY_START_STAGE_NAME_OFFSET+i];
        if(!ch)ended=1;
        if(ch && !((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'))return;
        snapshot.stage[i]=ch;
    }
    snapshot.stage[8]=0;
    snapshot.floor=snapshot.dungeon?dungeon_floor(actor->current.roomNo,actor->current.pos.y):128;
    u32 save=*(u32*)&WWHD_GAME_DATA(WWHD_ADDR_dComIfG_save_info_pointer,u8);
    if(same_stage(snapshot.stage,"MajyuE") && (!save || !dungeon_event(save+0x644,0x0801)))return;
    snapshot.compass=snapshot.dungeon && save && dungeon_item(save+0x798,1);
    snapshot.x=actor->current.pos.x;
    snapshot.z=actor->current.pos.z;
    snapshot.yaw=actor->shape_angle.y;
    snapshot.room=snapshot.dungeon?actor->current.roomNo:minimap_sea_room(snapshot.x, snapshot.z);
    snapshot.visible=snapshot.dungeon?(snapshot.room>=0&&snapshot.room<64):snapshot.room!=0;
    if(snapshot.visible&&snapshot.dungeon)capture_dungeon_markers(save,play);
    else point_count=0;
}
