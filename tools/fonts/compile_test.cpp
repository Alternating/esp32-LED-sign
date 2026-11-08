#include <stdint.h>
#include <stdio.h>
#include "character_patterns.h"
const char CHARACTER_SET[] = {'A','Z','0','9',1,2,3,4,5,6,7,8,9,10};
int main(){
  const char* probe = "Hello, World! ?.:'\"-+=/()#$&@% *<>^v~13";
  int n=0;
  for(const char* p=probe;*p;p++){ const bool* pat=getCharacterPattern(*p); if(pat==nullptr){printf("NULL for %c\n",*p);return 1;} n+=pat[0]; }
  for(int c=0;c<256;c++){ if(getCharacterPattern((char)c)==nullptr){printf("NULL for %d\n",c);return 1;} }
  printf("ok: 13->%s 14->%s 15->%s 'v'->V:%d unknown->SPACE:%d\n",
    getCharacterPattern(13)==PATTERN_HAPPY?"HAPPY":"?", getCharacterPattern(14)==PATTERN_SAD?"SAD":"?",
    getCharacterPattern(15)==PATTERN_TEN?"TEN":"?", getCharacterPattern('v')==PATTERN_V, getCharacterPattern('~')==PATTERN_SPACE);
  printf("sizeof all patterns: %zu bytes\n", sizeof(PATTERN_A)*68);
  return 0;
}
