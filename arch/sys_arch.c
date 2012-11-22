#include "lwip/opt.h"
#include "lwip/sys.h"

#include "inc/hw_types.h"
#include "driverlib/interrupt.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"

#if 0
sys_prot_t
sys_arch_protect(void) {
  return ((sys_prot_t)MAP_IntMasterDisable());
}

void
sys_arch_unprotect(sys_prot_t level) {
  if( !(level & 1) ) {
    MAP_IntMasterEnable();
  }
}
#endif

u32_t
sys_now(void) {
  return 0;
}
