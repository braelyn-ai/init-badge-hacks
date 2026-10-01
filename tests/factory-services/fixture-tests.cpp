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
 Form legacy{{"name","  Sample Attendee  "},{"network","github"},{"handle","https://github.com/example"},{"image","keep"},{"imageToken",""}};
 assert(badge::profile_form(&legacy.root,previous,candidate,error));
 assert(candidate.name=="Sample Attendee"&&candidate.company==previous.company&&candidate.network==badge_social::find("github")&&candidate.handle=="example");
 Form account{{"name","Sample"},{"network","url"},{"handle","bsky.app/profile/Chantastic"},{"image","keep"},{"imageToken",""}};
 assert(badge::profile_form(&account.root,previous,candidate,error)&&candidate.network==badge_social::find("url")&&candidate.handle=="https://bsky.app/profile/Chantastic");
 account.fields[2].valuestring="not a link!";assert(!badge::profile_form(&account.root,previous,candidate,error)&&error.find("https://")!=std::string::npos);
 account.fields[1].valuestring="myspace";account.fields[2].valuestring="tom";assert(!badge::profile_form(&account.root,previous,candidate,error));
 Form full{{"name","Sample"},{"network","github"},{"handle",""},{"image","keep"},{"imageToken",""},{"company","  Research & Development  "}};
 assert(badge::profile_form(&full.root,previous,candidate,error)&&candidate.company=="Research & Development");
 assert(candidate.network==-1&&candidate.handle.empty()); // A blank username stores no account.
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
 auto imageToken=full.fields[4].string;full.fields[4].string="company";assert(!badge::profile_form(&full.root,previous,candidate,error));full.fields[4].string=imageToken;
}
void check_networks(){
 // Five named networks plus Other (URL); each accepts a handle, @handle or profile URL.
 struct Case{const char* key;const char* input;const char* handle;const char* url;};
 const Case cases[]={
  {"linkedin","https://www.linkedin.com/in/michael-chan-1234/","michael-chan-1234","https://linkedin.com/in/michael-chan-1234"},
  {"x","https://twitter.com/example_1/","example_1","https://x.com/example_1"},
  {"github","https://github.com/Octo-Cat/","Octo-Cat","https://github.com/Octo-Cat"},
  {"huggingface","julien-c","julien-c","https://huggingface.co/julien-c"},
  {"youtube","https://www.youtube.com/@GitHub","GitHub","https://youtube.com/@GitHub"},
  {"url","bsky.app/profile/chan.dev","https://bsky.app/profile/chan.dev","https://bsky.app/profile/chan.dev"},
  {"url","http://chan.dev/about","https://chan.dev/about","https://chan.dev/about"},
 };
 assert(badge_social::Count==6&&std::string(badge_social::Networks[5].key)=="url");
 for(const auto& c:cases){
  const int n=badge_social::find(c.key);assert(n>=0);
  const auto h=badge_social::handle(n,c.input);
  if(h!=c.handle)std::fprintf(stderr,"%s %s -> %s\n",c.key,c.input,h.c_str());
  assert(h==c.handle&&badge_social::url(n,h)==c.url);
 }
 const std::pair<std::string,std::string> rejected[]={{"github","-bad"},{"github","a--b"},{"github","https://github.com.evil/account"},
  {"x","way_too_long_handle"},{"youtube","ab"},{"url","not a link"},{"url","https://nodot/x"},{"url","https://a.com/\"quote"},
  {"url","https://example.com/"+std::string(170,'a')},{"bluesky","chan.dev"}};
 for(const auto& [key,input]:rejected)assert(badge_social::handle(badge_social::find(key),input).empty());
 // Other links get a photo only when they match a provider's profile form.
 const int url=badge_social::find("url");
 struct Source{const char* link;const char* provider;const char* handle;const char* label;};
 const Source sources[]={
  {"https://bsky.app/profile/Chan.Dev","bluesky","chan.dev","Bluesky"},
  {"https://gitlab.com/sytses","gitlab","sytses","GitLab"},
  {"https://bankless.substack.com/","substack","bankless","Substack"},
  {"https://dribbble.com/omidnikrah","dribbble","omidnikrah","Dribbble"},
  {"https://www.threads.net/@zuck?x=1","threads","zuck","Threads"},
  {"https://github.com/octocat","github","octocat","GitHub"},
  {"https://www.linkedin.com/in/someone/","linkedin","someone","LinkedIn"},
 };
 for(const auto& c:sources){auto s=badge_social::photo_source(url,c.link);assert(s.provider&&std::string(s.provider)==c.provider&&s.handle==c.handle&&s.label==c.label);}
 for(const char* qr_only:{"https://chan.dev/about","https://github.com/orgs/workos","https://bsky.app/search"}){
  auto s=badge_social::photo_source(url,qr_only);assert(!s.provider&&!s.label.empty());
 }
 assert(badge_social::photo_source(url,"https://www.chan.dev/").label=="chan.dev");
 auto named=badge_social::photo_source(badge_social::find("youtube"),"GitHub");assert(std::string(named.provider)=="youtube"&&named.label=="YouTube");
 assert(badge_social::find("myspace")<0&&badge_social::url(-1,"a").empty());
}
void check_metadata(){
 badge::Profile p;p.name="Attendee";p.company="Company";p.network=badge_social::find("github");p.handle="example";
 uint8_t metadata[badge::kMetadata];auto n=badge::encode(p,metadata,3);assert(n&&n<sizeof(metadata));
 badge::Profile result;assert(badge::decode(metadata,n,3,result)&&result.company==p.company&&result.network==p.network&&result.handle=="example");
 assert(!badge::encode(p,metadata,2)&&!badge::encode(p,metadata,1));assert(!badge::decode(metadata,n,4,result));
 for(size_t cut=0;cut<n;++cut)assert(!badge::decode(metadata,cut,3,result));
 metadata[n]=0;assert(!badge::decode(metadata,n+1,3,result));
 auto companyStart=n-p.company.size();metadata[companyStart]='\n';assert(!badge::decode(metadata,n,3,result));
 n=badge::encode(p,metadata,3);metadata[2+p.name.size()+2]='q';assert(!badge::decode(metadata,n,3,result)); // Unknown network key.
 badge::Profile bad=p;bad.handle="-bad";assert(!badge::encode(bad,metadata,3));
 bad=p;bad.network=-1;assert(!badge::encode(bad,metadata,3)); // A handle needs a network.
 bad.handle.clear();n=badge::encode(bad,metadata,3);assert(badge::decode(metadata,n,3,result)&&result.network==-1&&result.handle.empty());
 // Versions 1 and 2 held three URL slots; the first filled slot becomes the account.
 auto legacy=[&](uint16_t version,std::vector<std::string> fields){
  std::vector<const std::string*> pointers;for(auto& f:fields)pointers.push_back(&f);
  const size_t used=badge::put_fields(pointers.data(),pointers.size(),metadata);
  return badge::decode(metadata,used,version,result);
 };
 assert(legacy(1,{"Old","","https://x.com/example_1","https://www.linkedin.com/in/someone/"})&&result.network==badge_social::find("x")&&result.handle=="example_1"&&result.company.empty());
 assert(legacy(2,{"Old","","","https://www.linkedin.com/in/someone/","Co"})&&result.network==badge_social::find("linkedin")&&result.handle=="someone"&&result.company=="Co");
 assert(legacy(2,{"Old","","","",""})&&result.network==-1);
 assert(!legacy(2,{"Old","https://github.com/-bad","","",""}));
 assert(!legacy(1,{"Old","","","","Co"})); // v1 has exactly four fields; a fifth is trailing data.
 p.company.clear();for(int i=0;i<60;++i){p.name=i? p.name+"\xc3\xa9":"\xc3\xa9";p.company+="\xc3\xa9";}
 p.network=badge_social::find("url");p.handle="https://example.com/"+std::string(160,'a');
 assert(badge::profile_valid(p));n=badge::encode(p,metadata,3);assert(n<=badge::kMetadata&&badge::decode(metadata,n,3,result));assert(result.name==p.name&&result.company==p.company&&result.handle==p.handle);
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
 personal.network=1;personal.handle="example";
 std::string error;assert(badge::save_record(personal,jpeg.data(),jpeg.size(),true,error));
 const auto saved=badge::profile_snapshot();const auto savedBytes=bytes(badge::record_path);
 const auto legacyPath=root+"/legacy-profile-sentinel.bin";const std::vector<uint8_t> legacy={0x41,0x55,0x54,0x48};write_bytes(legacyPath,legacy);
 const auto formatsBefore=format_calls;
 auto unchanged=[&]{
  const auto now=badge::profile_snapshot();
  assert(bytes(badge::record_path)==savedBytes&&now.revision==saved.revision);
  assert(now.profile.name==personal.name&&now.profile.company==personal.company&&now.avatar==saved.avatar);
  assert(now.profile.network==personal.network&&now.profile.handle==personal.handle);
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
 assert(empty.profile.network==-1&&empty.profile.handle.empty());assert(empty.revision==saved.revision+1);
 badge::ProfileSnapshot durable;assert(badge::read_record(badge::record_path.c_str(),durable));
 assert(durable.profile.name.empty()&&durable.profile.company.empty()&&!durable.avatar);
 assert(durable.profile.network==-1&&durable.profile.handle.empty());
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
 assert(badge::read_record(badge::record_path.c_str(),imported));assert(imported.profile.name==old.name.c_str()&&imported.profile.company.empty());
 assert(imported.profile.network==badge_social::find("github")&&imported.profile.handle=="example"); // Old GitHub slot becomes the account.assert(imported.avatar&&imported.avatar->size()==25600);assert(memcmp(imported.avatar->data(),legacy.avatarPixels(),51200)==0);
 assert(bytes(badge::record_path)==legacyRecord); // Reading the old format never migrates or erases it.
 imported.ready=true;badge::stored=imported;auto next=imported.profile;next.name="Changed Native";next.network=1;next.handle="example_1";std::string error;assert(badge::save_record(next,nullptr,0,false,error));
 assert(badge::u16(bytes(badge::record_path).data()+8)==3); // Writes are single-account v3.
 next.company="Synthetic Labs";assert(badge::save_record(next,nullptr,0,false,error));assert(badge::u16(bytes(badge::record_path).data()+8)==3);
 badge::ProfileSnapshot withCompany;assert(badge::read_record(badge::record_path.c_str(),withCompany));
 assert(withCompany.profile.company==next.company&&withCompany.profile.name==next.name&&withCompany.profile.network==1&&withCompany.profile.handle=="example_1");
 assert(withCompany.avatar&&*withCompany.avatar==*imported.avatar);
 auto unchanged=bytes(badge::record_path);next.name="Failure must not commit";next.company="Also must not commit";
 for(bool* failure:{&rename_failed,&sync_failed}){*failure=true;assert(!badge::save_record(next,nullptr,0,false,error));*failure=false;assert(bytes(badge::record_path)==unchanged);assert(badge::stored.profile.name=="Changed Native"&&badge::stored.profile.company=="Synthetic Labs");assert(*badge::stored.avatar==*imported.avatar);}
 for(size_t pos:{size_t(0),size_t(8),size_t(12),size_t(16),size_t(24),size_t(32),size_t(68),unchanged.size()-1}){auto corrupt=unchanged;corrupt[pos]^=0x80;write_bytes(badge::record_path,corrupt);badge::ProfileSnapshot bad;bad.profile.company="Unchanged output";assert(!badge::read_record(badge::record_path.c_str(),bad));assert(bad.profile.company=="Unchanged output");}write_bytes(badge::record_path,unchanged);
 assert(badge::save_record(next,nullptr,0,true,error));badge::ProfileSnapshot noPhoto;assert(badge::read_record(badge::record_path.c_str(),noPhoto)&&!noPhoto.avatar&&noPhoto.profile.company==next.company);
 next.company.clear();assert(badge::save_record(next,nullptr,0,false,error));assert(badge::u16(bytes(badge::record_path).data()+8)==3);
 std::string name,url;assert(badge::name_valid("  Jos\xc3\xa9  ",name)&&name=="Jos\xc3\xa9");assert(!badge::name_valid("line\nbreak",name));assert(!badge::name_valid(std::string("a\0b",3),name));(void)url;
 check_metadata();check_form();check_networks();check_prefill();check_reset(jpeg);
 assert(badge::profile_initialize_for_conference()&&format_calls==0);
 badge::stored.ready=false;badge::portal.active=true;assert(!badge::profile_initialize_for_conference()&&format_calls==0);badge::portal.active=false;
 for(size_t i=0;i<partitions.size();++i){partitions[i].address++;assert(!badge::profile_initialize_for_conference()&&format_calls==0);partitions[i].address--;}
 partitions.push_back(partitions.back());assert(!badge::profile_initialize_for_conference()&&format_calls==0);partitions.pop_back();
 native_mountable=true;assert(badge::profile_initialize_for_conference()&&format_calls==0&&badge::stored.profile.name==next.name);
 badge::stored.ready=false;native_mountable=false;native_mounted=false;assert(badge::profile_initialize_for_conference()&&format_calls==1&&badge::stored.profile.name.empty());
 assert(badge::profile_initialize_for_conference()&&format_calls==1);
 std::filesystem::remove_all(root);puts("Native services: legacy v1/v2 three-slot to one-account migration, v3 record, five networks plus Other links with provider recognition, company bounds/schema/prefill, corruption, atomic failures, image removal, queued profile reset and initialization guards passed");
}
