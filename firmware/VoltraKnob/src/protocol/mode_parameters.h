#pragma once
#include <cstdint>
namespace voltra {
constexpr bool supportedMode(int mode){return mode==1||mode==2||mode==4||mode==7;}
// Read-only discovery catalog. Widths come from captured controls/catalog;
// isolated reads tolerate the firmware's actual integer reply width.
struct ModeParameter {int mode;uint16_t id;const char* label;const char* unit;};
constexpr ModeParameter MODE_PARAMETERS[]={
 {2,0x5362,"Maximum force","lb"},{2,0x53b7,"Band length","cm"},
 {2,0x53b6,"Length from ROM","bool"},{2,0x5361,"Curve","curve"},{2,0x52e3,"Inverse","bool"},
 {4,0x5103,"Damper level","level"},{4,0x5106,"Assist","bool"},
 {7,0x5350,"Concentric speed","mm/s"},{7,0x5410,"Eccentric mode","eccmode"},
 {7,0x5411,"Eccentric speed","mm/s"},{7,0x5412,"Eccentric weight","lb"},{7,0x5413,"Eccentric limit","lb"},
 {8,0x535b,"Body weight","100g"},{8,0x53d2,"Test duration","s"},{8,0x53d1,"Test metric","raw"},{8,0x5431,"Maximum force","lb"},
};
constexpr int MODE_PARAMETER_COUNT=sizeof(MODE_PARAMETERS)/sizeof(MODE_PARAMETERS[0]);
inline bool validIsokineticSetting(int index,int value){
 switch(index){case 7:return value>=100&&value<=2000;case 8:return value==0||value==1;
 case 9:return value==0||(value>=100&&value<=2000);case 10:return value>=5&&value<=100;case 11:return value>=5&&value<=200;default:return false;}
}
inline int modeParameterIndex(uint16_t id){for(int i=0;i<MODE_PARAMETER_COUNT;++i)if(MODE_PARAMETERS[i].id==id)return i;return -1;}
}
