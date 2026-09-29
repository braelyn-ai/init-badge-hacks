#include "touch_mapping.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include "nvs.h"
std::vector<unsigned char> blob, pending;
bool fail_commit=false;
int nvs_open(const char* name,int,nvs_handle_t* h){assert(!std::strcmp(name,"espt-touch"));*h=1;return ESP_OK;}
int nvs_get_blob(nvs_handle_t,const char* key,void* out,size_t* size){assert(!std::strcmp(key,"record"));if(blob.empty())return 1;if(!out){*size=blob.size();return ESP_OK;}if(*size<blob.size())return 1;*size=blob.size();std::memcpy(out,blob.data(),blob.size());return ESP_OK;}
int nvs_set_blob(nvs_handle_t,const char*,const void* data,size_t size){auto* b=static_cast<const unsigned char*>(data);pending.assign(b,b+size);return ESP_OK;}
int nvs_commit(nvs_handle_t){if(fail_commit)return 1;blob=pending;return ESP_OK;}
void nvs_close(nvs_handle_t){}
template<class T>void store_record(const T& r){auto* b=reinterpret_cast<const unsigned char*>(&r);blob.assign(b,b+sizeof(r));}

using namespace touch_mapping;
uint32_t now=100;
void tap(Session& s, touchcal::Point p, bool real=true) {
 s.sample(true,real,false,0,0,now+=8);
 for(int i=0;i<20;++i)s.sample(true,real,true,int(std::lround(p.x)),int(std::lround(p.y)),now+=8);
 s.sample(true,real,false,0,0,now+=8);
}
int main(){
 touch_mapping::load(); assert(!available());
 auto r=touchcal::makeRecord({1,0,17,0,1,-11});store_record(r);load();
 assert(version()==1 && map({10,20}).x==27 && map({10,20}).y==9);
 auto warp=touchcal::zeroWarp({1,0,0,0,1,0});warp.center={3,4};
 for(auto& ring:warp.rings)for(auto& v:ring)v={3,4};
 store_record(touchcal::makeRecordV2(warp,7));load();
 assert(version()==2 && map({234,233}).x==237 && map({234,233}).y==237);
 blob.back()^=1;load();assert(!available());
 blob.resize(12);load();assert(!available());
 assert(save({1,0,17,0,1,-11}));load();assert(version()==1 && map({10,20}).x==27);
 fail_commit=true;assert(!save({1,0,30,0,1,30}));assert(map({10,20}).x==27);load();assert(map({10,20}).x==27);fail_commit=false;

 Session s;s.begin();
 tap(s,{200,200},false);assert(s.index()==0);
 // An opening contact or a bus error cannot advance calibration.
 for(int i=0;i<20;++i)s.sample(true,true,true,100,100,now+=8);
 s.sample(true,true,false,0,0,now+=8);assert(s.index()==0);
 s.sample(true,true,true,100,100,now+=8);s.sample(false,true,false,0,0,now+=8);
 s.sample(true,true,false,0,0,now+=8);assert(s.index()==0);
 // A known scale/offset maps back to native display space, including holdout targets.
 auto raw=[](touchcal::Point p){return touchcal::Point{(p.x-17)/1.08,(p.y+11)/.92};};
 for(int i=0;i<9;++i)tap(s,raw(s.target()));
 assert(s.stage()==Session::Stage::Verify);
 for(int i=0;i<5;++i)tap(s,raw(s.target()));
 assert(s.stage()==Session::Stage::Ready);
 auto mapped=touchcal::apply(s.affine(),raw({234,233}));
 assert(std::hypot(mapped.x-234,mapped.y-233)<1);
 s.saved(true);assert(s.stage()==Session::Stage::Saved);
 s.begin();for(int i=0;i<9;++i)tap(s,raw(s.target()));
 for(int i=0;i<5;++i)tap(s,{40,40});
 assert(s.stage()==Session::Stage::Failed);
 s.cancel();assert(s.stage()==Session::Stage::Idle);
 s.begin();for(int i=0;i<9;++i)tap(s,{200,200});assert(s.stage()==Session::Stage::Failed);
}
