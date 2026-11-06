#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "NativeClassifiers.h"

struct LeagueMapping { int leagueId; int group; };
struct LeagueConfig {
    LeagueMapping mappings[64]{};
    std::size_t count=0;
    LeagueMapping classifiers[256]{};
    std::size_t classifierCount=0;
};
enum class ConfigStatus { Ok, Absent, Invalid };
inline char* TrimConfig(char* value) {
    while (*value==' '||*value=='\t'||*value=='\r') ++value;
    char* end=value+std::strlen(value);
    while(end>value&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='\r')) --end;
    *end='\0'; return value;
}
inline bool ParseDecimalStrict(const char* text,int minimum,int maximum,int& value) {
    if (*text=='\0') return false;
    char* end=nullptr; const long n=std::strtol(text,&end,10);
    if(end==text||*end!='\0'||n<minimum||n>maximum) return false;
    value=static_cast<int>(n); return true;
}
inline ConfigStatus ParseLeagueConfig(char* text,LeagueConfig& output,std::size_t& errorLine,const char*& reason) {
    LeagueConfig candidate{}; errorLine=0; reason="";
    int section=-2; bool sections[5]{}; std::size_t lineNumber=0;
    for(char* cursor=text;cursor!=nullptr&&*cursor!='\0';) {
        ++lineNumber; char* line=cursor; char* newline=std::strchr(cursor,'\n');
        if(newline){*newline='\0';cursor=newline+1;}else cursor=nullptr;
        line=TrimConfig(line); if(*line=='\0'||*line==';'||*line=='#')continue;
        const char* failure=nullptr;
        if(*line=='[') {
            const std::size_t length=std::strlen(line);
            if(length<2||line[length-1]!=']') failure="MALFORMED_SECTION";
            else {
                line[length-1]='\0'; const char* name=TrimConfig(line+1); section=-2;
                if(_stricmp(name,"LeagueCompatibility")==0)section=-1;
                for(int g=0;g<4;++g) { const char* names[]={"ClassifierGroup0","ClassifierGroup1","ClassifierGroup2","ClassifierGroup3"};if(_stricmp(name,names[g])==0)section=g; }
                if(section==-2) failure="UNKNOWN_SECTION";
                else if(sections[section+1])failure="DUPLICATE_SECTION";
                else sections[section+1]=true;
            }
        } else if(section==-2) failure="MAPPING_OUTSIDE_SECTION";
        else {
            char* eq=std::strchr(line,'=');int id=0,value=0;
            if(!eq||std::strchr(eq+1,'='))failure="MALFORMED_MAPPING";
            else {
                *eq='\0';
                if(!ParseDecimalStrict(TrimConfig(line),1,65535,id)||UnsafeNewLeagueId(id))failure="UNSAFE_LEAGUE_ID";
                else if(!ParseDecimalStrict(TrimConfig(eq+1),section==-1?0:1,section==-1?6:1,value))failure="INVALID_VALUE";
                else if(section==-1) {
                    for(std::size_t i=0;i<candidate.count;++i)if(candidate.mappings[i].leagueId==id)failure="DUPLICATE_LEAGUE_ID";
                    if(candidate.count==64)failure="TOO_MANY_MAPPINGS";
                    if(!failure)candidate.mappings[candidate.count++]={id,value};
                } else {
                    const int native=NativeClassifierGroup(id);
                    if(native>=0&&native!=section)failure="NATIVE_GROUP_CONFLICT";
                    for(std::size_t i=0;i<candidate.classifierCount;++i)if(candidate.classifiers[i].leagueId==id)failure="DUPLICATE_CLASSIFIER_ID";
                    if(candidate.classifierCount==256)failure="TOO_MANY_CLASSIFIER_ADDITIONS";
                    if(!failure)candidate.classifiers[candidate.classifierCount++]={id,section};
                }
            }
        }
        if(failure){errorLine=lineNumber;reason=failure;return ConfigStatus::Invalid;}
    }
    if(!sections[0]){reason="SECTION_NOT_FOUND";return ConfigStatus::Invalid;}
    // No partial publication, including when a later section is invalid.
    output=candidate; return ConfigStatus::Ok;
}
inline bool FindConfiguredGroup(const LeagueConfig& config,int id,int& group) {
    for(std::size_t i=0;i<config.count;++i)if(config.mappings[i].leagueId==id){group=config.mappings[i].group;return true;}
    return false;
}
struct ClassifierLayout { std::uint32_t rows[4][268]{}; std::size_t counts[4]{}; };
inline ClassifierLayout BuildClassifierLayout(const LeagueConfig& config) {
    ClassifierLayout result{};
    for(int g=0;g<4;++g){result.counts[g]=NativeCounts[g];std::memcpy(result.rows[g],NativeGroups[g],NativeCounts[g]*sizeof(std::uint32_t));}
    // Preserve explicit per-group order, including the approved +11,+2 suffix.
    for(int g=0;g<4;++g)for(std::size_t i=0;i<config.classifierCount;++i)
        if(config.classifiers[i].group==g&&NativeClassifierGroup(config.classifiers[i].leagueId)<0)
            result.rows[g][result.counts[g]++]=static_cast<std::uint32_t>(config.classifiers[i].leagueId);
    return result;
}
