// Host adapters only: production storage functions are inserted by run.py.

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>
#include <Arduino.h>
#include "@@ROOT@@/firmware/factory_badge/main/services.h"
#include "@@ROOT@@/firmware/factory_badge/main/wifi_config.h"
#include "@@ROOT@@/firmware/factory_badge/main/social_networks.h"
#include "@@ROOT@@/firmware/factory_badge/main/portal_page.h"
// Only the cJSON tree access boundary is adapted. The production form validator
// below handles field names, duplicates, types and normalized values itself.
struct cJSON { const char* string=nullptr; const char* valuestring=nullptr; int type=1; cJSON* child=nullptr; cJSON* next=nullptr; };
bool cJSON_IsString(const cJSON* value){return value&&value->type==1;}
cJSON* cJSON_GetObjectItemCaseSensitive(cJSON* root,const char* key){for(auto item=root->child;item;item=item->next)if(item->string&&!strcmp(item->string,key))return item;return nullptr;}
static std::string root;
static bool rename_failed=false, sync_failed=false;
int checked_rename(const char* a,const char* b){return rename_failed?-1:std::rename(a,b);}
int checked_sync(int fd){return sync_failed?-1:fsync(fd);}
struct FakeLittleFS {
 bool begin(bool format,const char* base,uint8_t files,const char* label){assert(!format&&root==base&&files==4&&std::string(label)=="ffat");return true;}
 bool rename(const String& a,const String& b){return checked_rename((root+a.c_str()).c_str(),(root+b.c_str()).c_str())==0;}
} LittleFS;
static constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_FAT=0x81,ESP_PARTITION_TYPE_ANY=255,ESP_PARTITION_SUBTYPE_ANY=255;
struct esp_partition_t{uint32_t address=0x610000,size=0x9E0000;const char* label="ffat";uint8_t type=1,subtype=0x81;bool encrypted=false;};
const esp_partition_t* esp_partition_find_first(int,int,const char*){static esp_partition_t p;return &p;}
static std::vector<esp_partition_t> partitions={
 {0x9000,0x5000,"nvs",1,2},{0xe000,0x2000,"otadata",1,0},{0x10000,0x300000,"app0",0,0x10},
 {0x310000,0x300000,"app1",0,0x11},{0x610000,0x9e0000,"ffat",1,0x81},{0xff0000,0x10000,"coredump",1,3}
};
using esp_partition_iterator_t=size_t*;
esp_partition_iterator_t esp_partition_find(int,int,const char*){return new size_t(0);}
const esp_partition_t* esp_partition_get(esp_partition_iterator_t it){return &partitions[*it];}
void esp_partition_iterator_release(esp_partition_iterator_t it){delete it;}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it){if(++*it<partitions.size())return it;delete it;return nullptr;}
static constexpr int ESP_OK=0;
struct esp_vfs_littlefs_conf_t{const char* base_path=nullptr;const char* partition_label=nullptr;bool format_if_mount_failed=false,dont_mount=false;};
static bool native_mounted=false,native_mountable=false;static unsigned format_calls=0;
bool esp_littlefs_mounted(const char*){return native_mounted;}
int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t* config){assert(!config->format_if_mount_failed&&!config->dont_mount);native_mounted=native_mountable;return native_mounted?ESP_OK:-1;}
int esp_vfs_littlefs_unregister(const char*){native_mounted=false;return ESP_OK;}
int esp_littlefs_format(const char* label){assert(std::string(label)=="ffat");++format_calls;std::filesystem::remove_all(root);std::filesystem::create_directory(root);native_mountable=true;return ESP_OK;}
void* ps_malloc(size_t n){return malloc(n);}
#define CONFERENCE_STORE_ROOT root.c_str()
#include "@@ROOT@@/firmware/devices_badge/conference_profile_store.h"
using mbedtls_sha256_context=CC_SHA256_CTX;
void mbedtls_sha256_init(mbedtls_sha256_context*){}
void mbedtls_sha256_free(mbedtls_sha256_context*){}
int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){return CC_SHA256_Init(c)==1?0:-1;}
int mbedtls_sha256_update(mbedtls_sha256_context* c,const uint8_t* d,size_t n){return CC_SHA256_Update(c,d,n)==1?0:-1;}
int mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* d){return CC_SHA256_Final(d,c)==1?0:-1;}
namespace badge {
bool wifi_credentials_load(WifiCredentials& credentials) { credentials = {kEventWifiSsid, ""}; return true; }
bool wifi_credentials_custom() { return false; }
namespace {
constexpr size_t kHeader=64,kMetadata=768,kAvatarSide=160,kAvatarBytes=kAvatarSide*kAvatarSide*2;
constexpr uint8_t kMagic[8]={'I','N','I','T','C','F','0','1'};
std::string record_path,temp_path;
#define kRoot root.c_str()
#define kRecord record_path.c_str()
#define kTemporary temp_path.c_str()
std::mutex data_mutex,write_mutex,portal_mutex;
ProfileSnapshot stored;
std::string session_nonce="0123456789abcdef0123456789abcdef";
PortalSnapshot portal;bool requested=false,running=false;
ProfileResetSnapshot reset_state;
void* service_task=reinterpret_cast<void*>(1);
std::vector<uint8_t> staged_image;
std::string staged_token;
#define rename checked_rename
#define fsync checked_sync
