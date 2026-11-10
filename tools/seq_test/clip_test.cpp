#include "Arduino.h"
#include "sign_state.cpp"
#include <cassert>
#include <string>
#include <cstdio>
int main(){
  char out[SIGN_TEXT_MAX+1]; bool tr; uint8_t u;
  std::string ten; for(int i=0;i<10;i++) ten+="{happy}";
  u=signClip(ten.c_str(),out,&tr); printf("10 happy: units=%u trunc=%d len=%zu\n",u,tr,strlen(out)); assert(u==10&&!tr&&strcmp(out,ten.c_str())==0);
  char units[SIGN_UNITS_MAX]; assert(signUnits(out,units)==10); for(int i=0;i<10;i++) assert(units[i]==13);
  std::string many; for(int i=0;i<40;i++) many+="{diamond}";   // 360 bytes
  u=signClip(many.c_str(),out,&tr); printf("40 diamond: units=%u trunc=%d len=%zu\n",u,tr,strlen(out)); assert(tr && strlen(out)%9==0 && strlen(out)<=SIGN_TEXT_MAX);
  std::string a(70,'a'); u=signClip(a.c_str(),out,&tr); printf("70 a: units=%u trunc=%d\n",u,tr); assert(u==60&&tr&&out[0]=='A');
  u=signClip("go {happy} team {xyz}",out,&tr); printf("mixed: '%s' units=%u\n",out,u); assert(strcmp(out,"GO {happy} TEAM {XYZ}")==0);
  printf("sizeof(SignState)=%zu\nALL OK\n",sizeof(SignState));
}
