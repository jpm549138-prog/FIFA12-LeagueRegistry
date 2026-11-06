#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "LeagueConfig.h"
static_assert(sizeof(void*)==4,"Win32 fixture required");
static_assert(offsetof(LeagueConfig,mappings)==0&&offsetof(LeagueConfig,count)==512,"Legacy lookup layout changed");
static int failed=0;
static void Check(bool value,const char* label){std::printf("%s %s\n",value?"PASS":"FAIL",label);if(!value)++failed;}
static bool Parse(const char* text,LeagueConfig& config){std::string s(text);std::size_t line=0;const char* reason=nullptr;return ParseLeagueConfig(s.data(),config,line,reason)==ConfigStatus::Ok;}
int main(int argc,char** argv){
    if(argc==2){
        std::ifstream f(argv[1],std::ios::binary);std::ostringstream stream;stream<<f.rdbuf();const auto bytes=stream.str();
        if(!f||bytes.size()>=65536||bytes.find('\0')!=std::string::npos||bytes.compare(0,3,"\xEF\xBB\xBF")==0)return 2;
        LeagueConfig config{};if(!Parse(bytes.c_str(),config))return 1;
        const auto layout=BuildClassifierLayout(config);
        for(int g=0;g<4;++g){std::printf("GROUP%d",g);for(std::size_t i=0;i<layout.counts[g];++i)std::printf(",%u",layout.rows[g][i]);std::printf("\n");}
        return 0;
    }
    LeagueConfig c{};
    Check(Parse("[LeagueCompatibility]\n11=4\n2=4\n[ClassifierGroup3]\n11=1\n2=1\n",c),"CUSTOM CLASSIFIER CONFIG PARSES");
    auto layout=BuildClassifierLayout(c);bool native=true;
    for(int g=0;g<4;++g)native=native&&layout.counts[g]>=NativeCounts[g]&&std::memcmp(layout.rows[g],NativeGroups[g],NativeCounts[g]*4)==0;
    for(int selected=0;selected<4;++selected){
        LeagueConfig one{};const auto input="[LeagueCompatibility]\n[ClassifierGroup"+std::to_string(selected)+"]\n500=1\n";
        native=native&&Parse(input.c_str(),one);const auto result=BuildClassifierLayout(one);
        for(int g=0;g<4;++g)native=native&&result.counts[g]==NativeCounts[g]+(g==selected?1u:0u)&&std::memcmp(result.rows[g],NativeGroups[g],NativeCounts[g]*4)==0;
    }
    Check(native&&layout.counts[0]==5&&layout.counts[1]==5&&layout.counts[2]==12&&layout.counts[3]==14,"NATIVE CLASSIFIER PRESERVED");
    int group=-1;Check(FindConfiguredGroup(c,11,group)&&group==4&&FindConfiguredGroup(c,2,group)&&group==4,"SEVEN-GROUP FALLBACK PRESERVED");
    Check(layout.rows[3][12]==11&&layout.rows[3][13]==2,"URUGUAY AND CHILE REPRESENTABLE");
    const auto other=BuildClassifierLayout(c);Check(std::memcmp(&layout,&other,sizeof(layout))==0,"DETERMINISTIC");
    bool rejected=true;
    for(const char* s:{"[LeagueCompatibility]\n2=4\n2=4\n","[LeagueCompatibility]\n[ClassifierGroup3]\n2=1\n2=1\n",
        "[LeagueCompatibility]\n[ClassifierGroup3]\n2=1\n[ClassifierGroup2]\n2=1\n",
        "[LeagueCompatibility]\n[ClassifierGroup0]\n60=1\n","[LeagueCompatibility]\n[ClassifierGroup4]\n2=1\n",
        "[LeagueCompatibility]\n[ClassifierGroup3]\n5=0\n","[LeagueCompatibility]\n5=7\n"}) {
        LeagueConfig unchanged=c;rejected=rejected&&!Parse(s,unchanged)&&std::memcmp(&unchanged,&c,sizeof(c))==0;
    }
    Check(rejected,"DUPLICATE CONFLICT ATOMIC REJECTION");
    bool unsafe=true;for(int id:{382,383,384,385,386,387,388,389,2000,2028}) {
        LeagueConfig no{};const auto text="[LeagueCompatibility]\n"+std::to_string(id)+"=4\n[ClassifierGroup3]\n"+std::to_string(id)+"=1\n";
        unsafe=unsafe&&!Parse(text.c_str(),no);
    }Check(unsafe,"RESERVED UNSAFE IDS BLOCKED");
    LeagueConfig fresh{};Check(Parse("[LeagueCompatibility]\n5=4\n[ClassifierGroup3]\n5=1\n",fresh)&&BuildClassifierLayout(fresh).rows[3][12]==5,"NEW ARBITRARY SAFE ID CONFIGURABLE");
    LeagueConfig empty{};Parse("[LeagueCompatibility]\n",empty);const auto no=BuildClassifierLayout(empty);
    Check(no.counts[3]==12&&no.counts[0]==5,"ABSENT ADDITIONS PRESERVE NATIVE TABLES");
    return failed?1:0;
}
