#include "../src/libsystem_kernel/emulation/include/linux_premigration/signal/raw_svc.h"
#include <limits.h>
int main(void) {
 unsigned int index = 0;
 for (unsigned int i = 1; i < 128; ++i)
  if (!darling_svc_mach_index(-(int)i,128,&index) || index != i) return 1;
 for (unsigned int i = 0; i < 4; ++i) {
  const int bad[] = {INT_MIN,INT_MAX,0,-128};
  if (darling_svc_mach_index(bad[i],128,&index)) return 2;
 }
 if (darling_svc_mach_index(-1,0,&index)) return 3;
 return 0;
}
