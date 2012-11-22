#ifndef COMMON_H
#define COMMON_H

#include <inc/hw_types.h>
#include <inc/hw_memmap.h>
#include <driverlib/rom.h>
#include <driverlib/rom_map.h>
#include <driverlib/pin_map.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <driverlib/ssi.h>
#include <driverlib/uart.h>

#include <utils/uartstdio.h>
#include <utils/ustdlib.h>

#define printf          UARTprintf

#define SRAM_CS		GPIO_PIN_5

#define BUF	((struct uip_eth_hdr *)uip_buf)

#define HW_SSI
static __inline__ void delayMs(unsigned int delay) {
  MAP_SysCtlDelay(delay*16666);
}

#endif
