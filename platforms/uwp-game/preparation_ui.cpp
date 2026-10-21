// SPDX-License-Identifier: MPL-2.0
// Presentation only. USB enumeration, cancellation, preparation and game loading stay in preparation.cpp.
#include "preparation_ui.h"
#include "../uwp-extract/uwp_files.h"
#include "../../runtime/third_party/stb/stb_image.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../runtime/third_party/imgui/imstb_truetype.h"
#pragma clang diagnostic pop
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace wwhd_menu {
namespace {
struct Color {Uint8 r,g,b,a=255;};
constexpr Color background{8,28,40},surface{16,43,58},selected{18,61,72},accent{101,217,194},ink{8,44,48},white{241,247,250},muted{156,180,193},border{40,70,84};
std::string read_asset(const std::filesystem::path& path){
 wwhd_extract::InputFile in(path,std::ios::binary);if(!in)throw std::runtime_error("Menu asset missing: "+path.filename().string());
 in.seekg(0,std::ios::end);auto size=in.tellg();if(size<=0||size>16*1024*1024)throw std::runtime_error("Invalid menu asset size");
 in.seekg(0);std::string data(size_t(size),'\0');in.read(data.data(),size);if(in.gcount()!=size)throw std::runtime_error("Incomplete menu asset");return data;
}
struct Texture {
 SDL_Texture* value=nullptr;
 ~Texture(){if(value)SDL_DestroyTexture(value);}
 Texture()=default;Texture(const Texture&)=delete;Texture& operator=(const Texture&)=delete;
 void rgba(SDL_Renderer* renderer,const void* pixels,int width,int height){
  SDL_Surface* image=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,const_cast<void*>(pixels),width*4);
  if(!image)throw std::runtime_error(SDL_GetError());value=SDL_CreateTextureFromSurface(renderer,image);SDL_DestroySurface(image);
  if(!value)throw std::runtime_error(SDL_GetError());SDL_SetTextureBlendMode(value,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(value,SDL_SCALEMODE_LINEAR);
 }
 void png(SDL_Renderer* renderer,const std::filesystem::path& path){
  auto data=read_asset(path);int w=0,h=0,c=0;auto pixels=stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),int(data.size()),&w,&h,&c,4);
  if(!pixels)throw std::runtime_error("Cannot decode menu icon");
  try{rgba(renderer,pixels,w,h);}catch(...){stbi_image_free(pixels);throw;}stbi_image_free(pixels);
 }
};
struct Font {
 // Rasterize at native 1080p density while preserving the approved 16:9 layout.
 static constexpr float density=1.5f;
 Texture atlas;std::string bytes;stbtt_fontinfo info{};std::array<stbtt_packedchar,480> glyphs{};float scale=0,baseline=0,line_height=0;
 void load(SDL_Renderer* renderer,const std::filesystem::path& path,float size,float line){
  bytes=read_asset(path);if(!stbtt_InitFont(&info,reinterpret_cast<const unsigned char*>(bytes.data()),0))throw std::runtime_error("Invalid menu font");
  constexpr int width=2048,height=1024;std::vector<unsigned char> alpha(width*height);stbtt_pack_context context{};
  if(!stbtt_PackBegin(&context,alpha.data(),width,height,0,1,nullptr))throw std::runtime_error("Cannot allocate menu font atlas");
  stbtt_PackSetSkipMissingCodepoints(&context,1);
  int packed=stbtt_PackFontRange(&context,reinterpret_cast<const unsigned char*>(bytes.data()),0,STBTT_POINT_SIZE(size*density),32,int(glyphs.size()),glyphs.data());stbtt_PackEnd(&context);
  // Missing codepoints are intentionally skipped; the supported UI and Latin file names remain packed.
  if(!packed&&glyphs['A'-32].xadvance==0)throw std::runtime_error("Cannot rasterize menu font");
  std::vector<unsigned char> rgba(size_t(width)*height*4,255);for(size_t i=0;i<alpha.size();++i)rgba[i*4+3]=alpha[i];atlas.rgba(renderer,rgba.data(),width,height);
  int ascent=0,descent=0,gap=0;stbtt_GetFontVMetrics(&info,&ascent,&descent,&gap);scale=stbtt_ScaleForMappingEmToPixels(&info,size);line_height=line;baseline=(line-(ascent-descent)*scale)/2+ascent*scale;
 }
 unsigned glyph(unsigned ch)const{return ch>=32&&ch<512&&glyphs[ch-32].xadvance>0?ch:'?';}
 float width(const std::string& text)const{
  const char* p=text.c_str();unsigned previous=0;float x=0;
  while(auto ch=SDL_StepUTF8(&p,nullptr)){ch=glyph(ch);if(previous)x+=stbtt_GetCodepointKernAdvance(&info,int(previous),int(ch))*scale;x+=glyphs[ch-32].xadvance/density;previous=ch;}return x;
 }
 std::string fit(const std::string& value,float maximum,bool tail=false)const{
  if(width(value)<=maximum)return value;float available=maximum-width("...");if(available<=0)return {};
  if(tail){size_t start=0;while(start<value.size()&&width(value.substr(start))>available){++start;while(start<value.size()&&(static_cast<unsigned char>(value[start])&0xc0)==0x80)++start;}return "..."+value.substr(start);}
  const char* p=value.c_str();size_t end=0;while(*p){const char* next=p;SDL_StepUTF8(&next,nullptr);size_t n=size_t(next-value.c_str());if(width(value.substr(0,n))>available)break;end=n;p=next;}return value.substr(0,end)+"...";
 }
 void draw(SDL_Renderer* renderer,float x,float y,const std::string& text,Color color,float maximum=1200,bool tail=false)const{
  auto display=fit(text,maximum,tail);SDL_SetTextureColorMod(atlas.value,color.r,color.g,color.b);SDL_SetTextureAlphaMod(atlas.value,color.a);
  const char* p=display.c_str();float pen=0,base=0;unsigned previous=0;
  while(auto ch=SDL_StepUTF8(&p,nullptr)){
   ch=glyph(ch);if(previous)pen+=stbtt_GetCodepointKernAdvance(&info,int(previous),int(ch))*scale*density;
   stbtt_aligned_quad q{};stbtt_GetPackedQuad(glyphs.data(),2048,1024,int(ch-32),&pen,&base,&q,0);
   SDL_FRect source{q.s0*2048,q.t0*1024,(q.s1-q.s0)*2048,(q.t1-q.t0)*1024};SDL_FRect target{x+q.x0/density,y+baseline+q.y0/density,(q.x1-q.x0)/density,(q.y1-q.y0)/density};
   if(target.w>0&&target.h>0)SDL_RenderTexture(renderer,atlas.value,&source,&target);previous=ch;
  }
 }
};
}
struct Renderer::Impl {
 SDL_Renderer* renderer;
 std::array<Font,6> fonts;std::array<Texture,4> icons;
 enum Type {Label,Small,Body,Title,Hero,Action};
 Impl(SDL_Renderer* r,const std::filesystem::path& assets):renderer(r){
  fonts[Label].load(r,assets/L"Manrope-Bold.ttf",15,22);fonts[Small].load(r,assets/L"Manrope-Medium.ttf",18,26);fonts[Body].load(r,assets/L"Manrope-Regular.ttf",22,30);fonts[Title].load(r,assets/L"Manrope-SemiBold.ttf",28,36);fonts[Hero].load(r,assets/L"Manrope-Bold.ttf",48,58);fonts[Action].load(r,assets/L"Manrope-Bold.ttf",18,26);
  const wchar_t* files[]={L"connected.png",L"play.png",L"folder.png",L"disc.png"};for(size_t i=0;i<icons.size();++i)icons[i].png(r,assets/files[i]);
 }
 void color(Color c){SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);}
 void rounded(float x,float y,float w,float h,float radius,Color c){
  if(w<=0||h<=0)return;radius=(std::min)({radius,w/2,h/2});
  std::array<SDL_Vertex,37> vertices{};std::array<int,108> indices{};
  SDL_FColor f{c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f};vertices[0]={{x+w/2,y+h/2},f,{0,0}};
  for(int corner=0;corner<4;++corner)for(int step=0;step<9;++step){
   float angle=(-90.f+corner*90.f+step*90.f/8)*3.14159265359f/180;
   float cx=corner<2?x+w-radius:x+radius,cy=(corner==0||corner==3)?y+radius:y+h-radius;
   vertices[1+corner*9+step]={{cx+std::cos(angle)*radius,cy+std::sin(angle)*radius},f,{0,0}};
  }
  for(int i=0;i<36;++i){indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=(i+1)%36+1;}
  SDL_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
 }
 void panel(float x,float y,float w,float h,bool focus=false,float radius=12){rounded(x,y,w,h,radius,focus?accent:border);rounded(x+(focus?2:1),y+(focus?2:1),w-(focus?4:2),h-(focus?4:2),radius-1,focus?selected:surface);}
 void text(float x,float y,const std::string& value,Type type=Body,Color c=white,float maximum=1152,bool tail=false){fonts[type].draw(renderer,x,y,value,c,maximum,tail);}
 void right(float x,float y,const std::string& value,Type type=Small,Color c=muted){text(x-fonts[type].width(value),y,value,type,c);}
 void icon(Icon value,float x,float y){
  const SDL_FRect bounds[]={{0,0,8,8},{8,4,14,20},{2,6,28,22},{3,3,26,26}};auto rect=bounds[size_t(value)];rect.x+=x;rect.y+=y;SDL_RenderTexture(renderer,icons[size_t(value)].value,nullptr,&rect);
 }
 void button(float x,float y,float w,const std::string& label,bool primary=false,bool enabled=true){
  if(primary)rounded(x,y,w,56,8,accent);else panel(x,y,w,56,false,8);
  float width=fonts[Action].width(label);text(x+(w-width)/2,y+15,label,Action,enabled?(primary?ink:white):muted,w-12);
 }
 void header(bool connected){
  text(64,50,"WWHD",Title);rounded(174,51,59,34,6,selected);text(186,57,"UWP",Label,accent);
  std::string status=connected?"USB connected":"USB not connected";float x=1216-fonts[Small].width(status);if(connected)icon(Icon::Connected,x-18,65);text(x,55,status,Small,muted);
 }
 void heading(const std::string& title,const std::string& subtitle){text(64,116,title,Hero);text(64,182,subtitle,Body,muted);}
 void hint(float& x,const std::string& key,const std::string& label){float width=(std::max)(26.f,fonts[Label].width(key)+16);panel(x,647,width,28,false,6);text(x+(width-fonts[Label].width(key))/2,650,key,Label,accent);text(x+width+9,648,label,Small,muted);x+=width+9+fonts[Small].width(label)+28;}
 void footer(bool browsing,bool error=false){color(border);SDL_RenderLine(renderer,64,628,1216,628);float x=64;
  if(error){hint(x,"B","Close");text(64,584,"Logs: LocalState / WWHD",Small,muted);}else{hint(x,"A",browsing?"Select game":"Select");hint(x,"B","Back");hint(x,"X","Refresh");hint(x,"+","Move");}right(1216,648,"WWHD UWP");
 }
 void card(const Row& row,float y,float h,bool focus){
  panel(64,y,1152,h,focus);icon(row.icon,88,y+(h-32)/2);text(144,y+(h-70)/2,row.title,Title,white,700);text(144,y+(h-70)/2+42,row.subtitle,Small,muted,770);
  if(row.ready)right(1010,y+(h-22)/2,"READY",Label,accent);
  float button_width=row.ready?148.f:160.f;button(1192-button_width,y+(h-56)/2,button_width,focus?(row.ready?"A  Play":"A  Select"):(row.ready?"Play":"Browse"),focus);
 }
 void main(const View& view){
  heading("The Wind Waker HD","Choose your game and set sail.");
  if(view.rows.empty()){panel(64,240,1152,124);text(88,266,view.loading?"Looking for USB drives...":"No USB drive found",Title);text(88,310,"Connect your USB, then press X to refresh.",Small,muted);}
  size_t first=view.selected>=3?view.selected-2:0;float y=240;
  for(size_t i=first;i<view.rows.size()&&i<first+3;++i){float h=i==first?124.f:104.f;card(view.rows[i],y,h,i==view.selected);y+=h+16;}
  if(view.rows.size()>3)right(1216,214,std::to_string(view.selected+1)+" / "+std::to_string(view.rows.size()),Label);
  footer(false);
 }
 void browser(const View& view){
  heading("Browse USB","Select your game image. Keep both keys in the same folder.");icon(Icon::Folder,64,240);text(108,243,view.folder,Small,muted,1108,true);
  size_t first=view.selected>=3?view.selected-2:0;float y=300;
  for(size_t i=first;i<view.rows.size()&&i<first+3;++i){const auto& row=view.rows[i];bool focus=i==view.selected;panel(64,y,1152,68,focus,10);icon(row.icon,84,y+18);text(134,y+19,row.title,Body,white,800);right(1196,y+21,focus?"A  SELECT":row.tag,Small,focus?accent:muted);y+=78;}
  if(view.rows.empty())text(84,324,view.loading?"Reading folder...":"No game image in this folder.",Body,muted);
  if(view.keys_visible){icon(Icon::Disc,84,y+12);text(134,y+13,"Key files",Body);right(1196,y+15,view.keys_ready?"Both keys found":"Keys missing or ambiguous",Small,view.keys_ready?accent:muted);text(64,y+66,"Keys are detected automatically. Select the WUX or WUD to continue.",Small,muted);}
  else text(64,586,"Open a folder to find your WUX or WUD game image.",Small,muted);
  if(view.rows.size()>3)right(1216,270,std::to_string(view.selected+1)+" / "+std::to_string(view.rows.size()),Label);
  footer(true);
 }
 void progress(const View& view){
  bool cached=view.stage=="Preparing to launch...";
  heading("Preparing your game",cached?"Checking your prepared game. It will start shortly.":"This only needs to run once. Keep your USB connected.");
  panel(64,240,1152,286);icon(Icon::Disc,92,268);text(140,269,view.game.empty()?"The Wind Waker HD":view.game,Body,white,1036);
  text(92,324,view.stage,Title,white,940);right(1188,324,std::to_string(view.percent)+"%",Title,accent);
  rounded(92,394,1096,12,6,border);rounded(92,394,1096*(std::min)(100u,view.percent)/100.f,12,6,accent);text(92,430,view.detail,Small,muted,1096);
  unsigned current=view.percent>=100?4:view.percent>=95?3:view.percent>=35?2:view.percent>=25?1:0;
  const char* stages[]={"Extract","Translate","Compile","Verify","Play"};float x=64;
  for(unsigned i=0;i<5;++i){bool done=i<current;std::string value=done?stages[i]:std::to_string(i+1)+"  "+stages[i];if(done){color(accent);SDL_RenderLine(renderer,x,569,x+4,573);SDL_RenderLine(renderer,x+4,573,x+12,559);x+=19;}text(x,554,value,Small,done?accent:i==current?white:muted);x+=fonts[Small].width(value)+18;}
  text(64,652,view.cancelling?"Waiting for the current operation to finish...":"Progress follows preparation stages.",Small,muted,920);
  button(1056,637,160,view.cancelling?"Please wait":view.percent>=95?"Starting...":"B  Cancel",false,!view.cancelling&&view.percent<95);
 }
 void error(const View& view){
  heading("Preparation stopped","Check the message below, then restart to try again.");panel(64,240,1152,300);
  std::string remaining=view.error;float y=268;
  while(!remaining.empty()&&y<492){auto line=fonts[Body].fit(remaining,1096);if(fonts[Body].width(remaining)<=1096){text(92,y,remaining);break;}size_t n=line.size()>3?line.size()-3:line.size();auto space=remaining.rfind(' ',n);if(space!=std::string::npos&&space>n/2)n=space;text(92,y,remaining.substr(0,n));remaining.erase(0,n);while(!remaining.empty()&&remaining.front()==' ')remaining.erase(0,1);y+=34;}
  footer(false,true);
 }
 void draw(const View& view){color(background);SDL_RenderClear(renderer);header(view.connected);if(!view.error.empty())error(view);else if(view.running)progress(view);else if(view.browsing)browser(view);else main(view);}
};
Renderer::Renderer(SDL_Renderer* renderer,const std::filesystem::path& assets):impl(std::make_unique<Impl>(renderer,assets)){}
Renderer::~Renderer()=default;
void Renderer::draw(const View& view){impl->draw(view);}
}
