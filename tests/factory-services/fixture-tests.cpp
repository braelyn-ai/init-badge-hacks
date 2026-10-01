#undef rename
#undef fsync
}
ProfileSnapshot profile_snapshot(){std::lock_guard<std::mutex> lock(data_mutex);return stored;}
// NATIVE_RESET_API_HERE
// NATIVE_INITIALIZER_HERE
}
std::vector<uint8_t> bytes(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write_bytes(const std::string& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());}
struct Form {
 std::vector<cJSON> fields; cJSON root;
 Form(std::initializer_list<std::pair<const char*,const char*>> values){
  for(auto value:values)fields.push_back({value.first,value.second});
  for(size_t i=1;i<fields.size();++i)fields[i-1].next=&fields[i];
  root.child=fields.empty()?nullptr:&fields[0];
 }
};
void check_form(){
 badge::Profile previous;previous.company="Keep this company";badge::Profile candidate;std::string error;
 Form legacy{{"name","  Sample Attendee  "},{"github","example"},{"x",""},{"linkedin",""},{"image","keep"},{"imageToken",""}};
 assert(badge::profile_form(&legacy.root,previous,candidate,error));
 assert(candidate.name=="Sample Attendee"&&candidate.company==previous.company&&candidate.urls[0]=="https://github.com/example");
 Form full{{"name","Sample"},{"github",""},{"x",""},{"linkedin",""},{"image","keep"},{"imageToken",""},{"company","  Research & Development  "}};
 assert(badge::profile_form(&full.root,previous,candidate,error)&&candidate.company=="Research & Development");
 auto company=&full.fields.back();company->valuestring="";
 assert(badge::profile_form(&full.root,previous,candidate,error)&&candidate.company.empty());
 company->type=2;assert(!badge::profile_form(&full.root,previous,candidate,error));company->type=1;
 const std::vector<std::string> rejected={std::string(61,'x'),std::string(121,'x'),"line\nbreak",std::string("bad\xc0\xaf"),std::string("bad\xed\xa0\x80"),std::string("bad\xe2\x80\xae")};
 for(const auto& value:rejected){company->valuestring=value.c_str();assert(!badge::profile_form(&full.root,previous,candidate,error));assert(candidate.company.empty());}
 std::string unicode;for(int i=0;i<60;++i)unicode+="\xc3\xa9";
 company->valuestring=unicode.c_str();assert(badge::profile_form(&full.root,previous,candidate,error)&&candidate.company==unicode);
 company->string="unexpected";assert(!badge::profile_form(&full.root,previous,candidate,error));
 company->string="name";assert(!badge::profile_form(&full.root,previous,candidate,error)); // Duplicate required key.
 company->string="company";company->valuestring="Good";
 cJSON duplicate{"company","Another"};company->next=&duplicate;assert(!badge::profile_form(&full.root,previous,candidate,error));company->next=nullptr;
 full.fields[0].type=2;assert(!badge::profile_form(&full.root,previous,candidate,error));full.fields[0].type=1;
 auto imageToken=full.fields[5].string;full.fields[5].string="company";assert(!badge::profile_form(&full.root,previous,candidate,error));full.fields[5].string=imageToken;
}
void check_photo_handles(){
 // Photo requests use the bare handle recovered from each canonical profile URL.
 const char* inputs[3]={"Octo-Cat","@Chan_Tastic","https://linkedin.com/in/michael-chan-1234/"};
 const char* handles[3]={"Octo-Cat","Chan_Tastic","michael-chan-1234"};
 for(unsigned i=0;i<3;++i){std::string url;assert(badge::social_url(i,inputs[i],url));assert(badge::social_handle(i,url)==handles[i]);}
 assert(badge::social_handle(0,"").empty()&&badge::social_handle(1,"https://github.com/x").empty()&&badge::social_handle(3,"https://x.com/a").empty());
}
void check_metadata(){
 badge::Profile p;p.name="Attendee";p.company="Company";p.urls[0]="https://github.com/example";
 uint8_t metadata[badge::kMetadata];auto n=badge::encode(p,metadata,2);assert(n&&n<sizeof(metadata));
 badge::Profile result;assert(badge::decode(metadata,n,2,result)&&result.company==p.company);
 assert(!badge::encode(p,metadata,1));assert(!badge::decode(metadata,n,1,result));assert(!badge::decode(metadata,n,3,result));
 for(size_t cut=0;cut<n;++cut)assert(!badge::decode(metadata,cut,2,result));
 metadata[n]=0;assert(!badge::decode(metadata,n+1,2,result));
 auto companyStart=n-p.company.size();metadata[companyStart]=0;assert(!badge::decode(metadata,n,2,result));
 n=badge::encode(p,metadata,2);metadata[companyStart]='\n';assert(!badge::decode(metadata,n,2,result));
 n=badge::encode(p,metadata,2);badge::put16(metadata+companyStart-2,0xffff);assert(!badge::decode(metadata,n,2,result));
 p.company.clear();n=badge::encode(p,metadata,1);assert(badge::decode(metadata,n,1,result)&&result.company.empty());
 assert(!badge::decode(metadata,n,2,result)); // Company length is mandatory in v2.
 n=badge::encode(p,metadata,2);assert(badge::decode(metadata,n,2,result)&&result.company.empty());
 for(int i=0;i<60;++i){p.name=i? p.name+"\xc3\xa9":"\xc3\xa9";p.company+="\xc3\xa9";}
 p.urls[0]="https://github.com/"+std::string(39,'a');p.urls[1]="https://x.com/"+std::string(15,'b');p.urls[2]="https://www.linkedin.com/in/"+std::string(100,'c')+"/";
 assert(badge::profile_valid(p));n=badge::encode(p,metadata,2);assert(n<=badge::kMetadata&&badge::decode(metadata,n,2,result));assert(result.name==p.name&&result.company==p.company);
}
void check_prefill(){
 auto previous=badge::stored;
 badge::stored.profile.name="Name {{COMPANY}} & <\"'";
 badge::stored.profile.company="Company {{NAME}} {{GITHUB}} & <\"'";
 auto page=badge::page();
 assert(page.find("value=\"Name {{COMPANY}} &amp; &lt;&quot;&#39;\"")!=std::string::npos);
 assert(page.find("value=\"Company {{NAME}} {{GITHUB}} &amp; &lt;&quot;&#39;\"")!=std::string::npos);
 assert(page.find("id=\"company\" maxlength=\"120\" autocomplete=\"organization\"")!=std::string::npos);
 assert(page.find("const nonce='"+badge::session_nonce+"'")!=std::string::npos);
 badge::stored=previous;
}
void check_reset(const std::vector<uint8_t>& jpeg){
 using State=badge::ProfileResetState;
 auto original=badge::stored;auto originalBytes=bytes(badge::record_path);
 badge::Profile personal;personal.name="Reset Attendee";personal.company="Reset Company";
 personal.urls[0]="https://github.com/example";personal.urls[1]="https://x.com/example";personal.urls[2]="https://www.linkedin.com/in/example/";
 std::string error;assert(badge::save_record(personal,jpeg.data(),jpeg.size(),true,error));
 const auto saved=badge::profile_snapshot();const auto savedBytes=bytes(badge::record_path);
 const auto legacyPath=root+"/legacy-profile-sentinel.bin";const std::vector<uint8_t> legacy={0x41,0x55,0x54,0x48};write_bytes(legacyPath,legacy);
 const auto formatsBefore=format_calls;
 auto unchanged=[&]{
  const auto now=badge::profile_snapshot();
  assert(bytes(badge::record_path)==savedBytes&&now.revision==saved.revision);
  assert(now.profile.name==personal.name&&now.profile.company==personal.company&&now.avatar==saved.avatar);
  for(unsigned i=0;i<3;++i)assert(now.profile.urls[i]==personal.urls[i]);
  assert(bytes(legacyPath)==legacy&&format_calls==formatsBefore);
 };
 // Setup and upload staging must be completely gone before queueing a reset.
 for(bool* busy:{&badge::portal.active,&badge::portal.starting,&badge::requested,&badge::running}){
  *busy=true;assert(!badge::profile_reset_request(error)&&!error.empty());*busy=false;unchanged();
 }
 badge::staged_image={1,2,3};assert(!badge::profile_reset_request(error));badge::staged_image.clear();
 badge::staged_token="staged";assert(!badge::profile_reset_request(error));badge::staged_token.clear();
 badge::service_task=nullptr;assert(!badge::profile_reset_request(error));badge::service_task=reinterpret_cast<void*>(1);
 badge::stored.ready=false;assert(!badge::profile_reset_request(error));badge::stored.ready=true;
 assert(badge::profile_reset_snapshot().state==State::Idle);unchanged();
 // Acceptance is not completion, and duplicate requests cannot replace the job.
 assert(badge::profile_reset_request(error)&&error.empty());assert(badge::profile_reset_snapshot().state==State::Pending);unchanged();
 assert(!badge::profile_reset_request(error)&&badge::profile_reset_snapshot().state==State::Pending);
 // Recheck the exclusion at execution as well; failure never retries itself.
 badge::portal.active=true;badge::process_profile_reset();badge::portal.active=false;
 assert(badge::profile_reset_snapshot().state==State::Failed&&!badge::profile_reset_snapshot().error.empty());unchanged();
 badge::process_profile_reset();assert(badge::profile_reset_snapshot().state==State::Failed);unchanged();
 for(bool* failure:{&rename_failed,&sync_failed}){
  assert(badge::profile_reset_request(error));*failure=true;badge::process_profile_reset();*failure=false;
  assert(badge::profile_reset_snapshot().state==State::Failed&&!badge::profile_reset_snapshot().error.empty());unchanged();
  badge::process_profile_reset();unchanged(); // Explicit retry is required.
 }
 const auto normalTemporary=badge::temp_path;badge::temp_path=root+"/missing/record.tmp";
 assert(badge::profile_reset_request(error));badge::process_profile_reset();badge::temp_path=normalTemporary;
 assert(badge::profile_reset_snapshot().state==State::Failed);unchanged();
 assert(badge::profile_reset_request(error));badge::process_profile_reset();
 assert(badge::profile_reset_snapshot().state==State::Succeeded&&badge::profile_reset_snapshot().error.empty());
 auto empty=badge::profile_snapshot();assert(empty.ready&&empty.profile.name.empty()&&empty.profile.company.empty()&&!empty.avatar);
 for(auto& url:empty.profile.urls)assert(url.empty());assert(empty.revision==saved.revision+1);
 badge::ProfileSnapshot durable;assert(badge::read_record(badge::record_path.c_str(),durable));
 assert(durable.profile.name.empty()&&durable.profile.company.empty()&&!durable.avatar);
 for(auto& url:durable.profile.urls)assert(url.empty());
 const auto emptyBytes=bytes(badge::record_path);badge::process_profile_reset();
 assert(bytes(badge::record_path)==emptyBytes&&badge::profile_snapshot().revision==empty.revision);
 assert(bytes(legacyPath)==legacy&&format_calls==formatsBefore);
 // The fixture restores its own synthetic state for the unrelated mount checks.
 write_bytes(badge::record_path,originalBytes);badge::stored=original;badge::reset_state={};
}
int main(int argc,char**argv){
 assert(argc==2);char tmp[]="/tmp/native-badge-profile-XXXXXX";root=mkdtemp(tmp);badge::record_path=root+"/conference-manual-v1.bin";badge::temp_path=root+"/conference-manual-v1.tmp";
 auto jpeg=bytes(argv[1]);assert(!jpeg.empty());
 ConferenceProfileStore legacy;assert(legacy.begin());ConferenceProfile old;old.name="Synthetic Native";old.urls[0]="https://github.com/example";assert(legacy.save(old,jpeg.data(),jpeg.size(),true));
 const auto legacyRecord=bytes(badge::record_path);
 badge::ProfileSnapshot imported;imported.profile.company="Must clear on legacy read";
 assert(badge::read_record(badge::record_path.c_str(),imported));assert(imported.profile.name==old.name.c_str()&&imported.profile.company.empty());assert(imported.avatar&&imported.avatar->size()==25600);assert(memcmp(imported.avatar->data(),legacy.avatarPixels(),51200)==0);
 assert(bytes(badge::record_path)==legacyRecord); // Reading the old format never migrates or erases it.
 imported.ready=true;badge::stored=imported;auto next=imported.profile;next.name="Changed Native";next.urls[1]="https://x.com/example_1";std::string error;assert(badge::save_record(next,nullptr,0,false,error));
 {ConferenceProfileStore loaded;assert(loaded.begin());assert(loaded.profile().name==next.name.c_str());assert(loaded.profile().urls[1]==next.urls[1].c_str());assert(loaded.avatarPixels());}
 assert(badge::u16(bytes(badge::record_path).data()+8)==1);
 next.company="Synthetic Labs";assert(badge::save_record(next,nullptr,0,false,error));assert(badge::u16(bytes(badge::record_path).data()+8)==2);
 badge::ProfileSnapshot withCompany;assert(badge::read_record(badge::record_path.c_str(),withCompany));
 assert(withCompany.profile.company==next.company&&withCompany.profile.name==next.name&&withCompany.profile.urls[0]==next.urls[0]&&withCompany.profile.urls[1]==next.urls[1]);
 assert(withCompany.avatar&&*withCompany.avatar==*imported.avatar);
 auto unchanged=bytes(badge::record_path);next.name="Failure must not commit";next.company="Also must not commit";
 for(bool* failure:{&rename_failed,&sync_failed}){*failure=true;assert(!badge::save_record(next,nullptr,0,false,error));*failure=false;assert(bytes(badge::record_path)==unchanged);assert(badge::stored.profile.name=="Changed Native"&&badge::stored.profile.company=="Synthetic Labs");assert(*badge::stored.avatar==*imported.avatar);}
 for(size_t pos:{size_t(0),size_t(8),size_t(12),size_t(16),size_t(24),size_t(32),size_t(68),unchanged.size()-1}){auto corrupt=unchanged;corrupt[pos]^=0x80;write_bytes(badge::record_path,corrupt);badge::ProfileSnapshot bad;bad.profile.company="Unchanged output";assert(!badge::read_record(badge::record_path.c_str(),bad));assert(bad.profile.company=="Unchanged output");}write_bytes(badge::record_path,unchanged);
 assert(badge::save_record(next,nullptr,0,true,error));badge::ProfileSnapshot noPhoto;assert(badge::read_record(badge::record_path.c_str(),noPhoto)&&!noPhoto.avatar&&noPhoto.profile.company==next.company);
 next.company.clear();assert(badge::save_record(next,nullptr,0,false,error));assert(badge::u16(bytes(badge::record_path).data()+8)==1);
 {ConferenceProfileStore loaded;assert(loaded.begin());assert(loaded.profile().name==next.name.c_str());assert(!loaded.avatarPixels());}
 std::string name,url;assert(badge::name_valid("  Jos\xc3\xa9  ",name)&&name=="Jos\xc3\xa9");assert(!badge::name_valid("line\nbreak",name));assert(!badge::name_valid(std::string("a\0b",3),name));assert(badge::social_url(1,"https://twitter.com/example_1/",url)&&url=="https://x.com/example_1");assert(!badge::social_url(0,"https://github.com.evil/account",url));
 check_metadata();check_form();check_photo_handles();check_prefill();check_reset(jpeg);
 assert(badge::profile_initialize_for_conference()&&format_calls==0);
 badge::stored.ready=false;badge::portal.active=true;assert(!badge::profile_initialize_for_conference()&&format_calls==0);badge::portal.active=false;
 for(size_t i=0;i<partitions.size();++i){partitions[i].address++;assert(!badge::profile_initialize_for_conference()&&format_calls==0);partitions[i].address--;}
 partitions.push_back(partitions.back());assert(!badge::profile_initialize_for_conference()&&format_calls==0);partitions.pop_back();
 native_mountable=true;assert(badge::profile_initialize_for_conference()&&format_calls==0&&badge::stored.profile.name==next.name);
 badge::stored.ready=false;native_mountable=false;native_mounted=false;assert(badge::profile_initialize_for_conference()&&format_calls==1&&badge::stored.profile.name.empty());
 assert(badge::profile_initialize_for_conference()&&format_calls==1);
 std::filesystem::remove_all(root);puts("Native services: legacy/v2 fields+avatar compatibility, company bounds/schema/prefill, corruption, atomic failures, image removal, queued profile reset and initialization guards passed");
}
