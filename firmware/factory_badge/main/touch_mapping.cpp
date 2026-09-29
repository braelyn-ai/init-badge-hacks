#include "touch_mapping.h"
#include <nvs.h>
#include <cstring>
namespace touch_mapping {
namespace {
touchcal::WarpMap active{};
int loadedVersion = 0;
}
void load() {
    loadedVersion = 0;
    nvs_handle_t nvs;
    if (nvs_open(touchcal::kNvsNamespace,NVS_READONLY,&nvs) != ESP_OK) return;
    size_t size=0;
    if (nvs_get_blob(nvs,"record",nullptr,&size)==ESP_OK) {
        if (size==sizeof(touchcal::Record)) {
            touchcal::Record r{};
            if(nvs_get_blob(nvs,"record",&r,&size)==ESP_OK && touchcal::validateRecord(r)) { active=touchcal::warpFromV1(r); loadedVersion=1; }
        } else if(size==sizeof(touchcal::RecordV2)) {
            touchcal::RecordV2 r{};
            if(nvs_get_blob(nvs,"record",&r,&size)==ESP_OK && touchcal::validateRecordV2(r)) {
                active.affine={r.affine[0],r.affine[1],r.affine[2],r.affine[3],r.affine[4],r.affine[5]};
                active.center={r.centerResidual[0],r.centerResidual[1]};
                for(int i=0;i<3;++i)for(int j=0;j<16;++j)active.rings[i][j]={r.residuals[i][j][0],r.residuals[i][j][1]};
                loadedVersion=2;
            }
        }
    }
    nvs_close(nvs);
}
bool available(){return loadedVersion!=0;}
int version(){return loadedVersion;}
touchcal::Point map(touchcal::Point raw){return available()?touchcal::applyWarp(active,raw):raw;}
bool save(const touchcal::Affine& affine) {
    if(!touchcal::validAffine(affine))return false;
    const auto record=touchcal::makeRecord(affine);
    nvs_handle_t nvs;
    if(nvs_open(touchcal::kNvsNamespace,NVS_READWRITE,&nvs)!=ESP_OK)return false;
    bool ok=nvs_set_blob(nvs,"record",&record,sizeof(record))==ESP_OK && nvs_commit(nvs)==ESP_OK;
    touchcal::Record readback{}; size_t size=sizeof(readback);
    ok=ok && nvs_get_blob(nvs,"record",&readback,&size)==ESP_OK && size==sizeof(readback) && !std::memcmp(&record,&readback,size) && touchcal::validateRecord(readback);
    nvs_close(nvs);
    if(ok){active=touchcal::warpFromV1(readback);loadedVersion=1;}
    return ok;
}
}
