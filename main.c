#include <stdint.h>
#include "common.h"
#include "enc28j60.h"
#include "spi.h"
#include <uip/uip.h>
#include <uip/uip_arp.h>

static volatile unsigned long g_ulFlags;

volatile unsigned long g_ulTickCounter = 0;

#define UIP_PERIODIC_TIMER_MS   500
#define UIP_ARP_TIMER_MS	10000

#define FLAG_SYSTICK		0
#define FLAG_RXPKT		1
#define FLAG_TXPKT		2
#define FLAG_RXPKTPEND		3

#define SYSTICKHZ		CLOCK_CONF_SECOND
#define SYSTICKMS		(1000 / SYSTICKHZ)

void uip_log(char *msg) {
  printf("UIP: %s\n", msg);
}

static void
cpu_init(void) {
  // A safety loop in order to interrupt the MCU before setting the clock (wrongly)
  for(int i=0; i<1000000; i++);

  // Setup for 16MHZ external crystal, use 200MHz PLL and divide by 4 = 50MHz
  MAP_SysCtlClockSet(SYSCTL_SYSDIV_16 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
      SYSCTL_XTAL_16MHZ);
}

static void
uart_init(void) {
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

  // Configure PD0 and PD1 for UART
  MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
  MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
  MAP_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
  /*UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200,
                      UART_CONFIG_WLEN_8| UART_CONFIG_STOP_ONE| UART_CONFIG_PAR_NONE);*/
  UARTStdioInitExpClk(0, 115200);
}

static void
spi_init(void) {
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);

  // Configure SSI1 for SPI RAM usage
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI2);
  MAP_GPIOPinConfigure(GPIO_PB4_SSI2CLK);
  MAP_GPIOPinConfigure(GPIO_PB6_SSI2RX);
  MAP_GPIOPinConfigure(GPIO_PB7_SSI2TX);
  MAP_GPIOPinTypeSSI(GPIO_PORTB_BASE, GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_7);
  MAP_SSIConfigSetExpClk(SSI2_BASE, MAP_SysCtlClockGet(), SSI_FRF_MOTO_MODE_0,
			 SSI_MODE_MASTER, 1000000, 8);
  MAP_SSIEnable(SSI2_BASE);

  unsigned long b;
  while(MAP_SSIDataGetNonBlocking(SSI2_BASE, &b)) {}
}

static void
enc28j60_comm_init(void) {
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
  MAP_GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE, ENC_CS);
  MAP_GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, SRAM_CS);
//  MAP_GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, ENC_CS | ENC_RESET | SRAM_CS);
  MAP_GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, ENC_INT);
//
//  MAP_GPIOPinWrite(GPIO_PORTA_BASE, ENC_RESET, 0);
  MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
  MAP_GPIOPinWrite(GPIO_PORTA_BASE, SRAM_CS, SRAM_CS);
}

#define SELECT_MEM() MAP_GPIOPinWrite(GPIO_PORTA_BASE, SRAM_CS, 0)
#define DESELECT_MEM() MAP_GPIOPinWrite(GPIO_PORTA_BASE, SRAM_CS, SRAM_CS)

void spi_mem_write(uint16_t addr, const uint8_t *buf, uint16_t count) {
  SELECT_MEM();
  spi_send(0x02);
  spi_send(addr >> 8);
  spi_send(addr & 0xFF);
  for(;count>0;count--) {
    spi_send(*buf);
    buf++;
  }
  DESELECT_MEM();
}

void spi_mem_read(uint16_t addr, uint8_t *buf, uint16_t count) {
  SELECT_MEM();
  spi_send(0x03);
  spi_send(addr >> 8);
  spi_send(addr & 0xFF);
  for(;count>0;count--) {
    *buf = spi_send(0xFF);
    buf++;
  }
  DESELECT_MEM();
}



const uint8_t mac_addr[] = { 0x00, 0xC0, 0x033, 0x50, 0x48, 0x12 };

int main(void) {
  char buf[100];
  static struct uip_eth_addr eth_addr;
  uip_ipaddr_t ipaddr;

  cpu_init();
  uart_init();
  printf("Welcome\n");
  spi_init();
  enc28j60_comm_init();

  printf("Welcome\n");

  enc_init(mac_addr);

  //
  // Configure SysTick for a periodic interrupt.
  //
  MAP_SysTickPeriodSet(MAP_SysCtlClockGet() / SYSTICKHZ);
  MAP_SysTickEnable();
  MAP_SysTickIntEnable();

  uip_init();

  eth_addr.addr[0] = mac_addr[0];
  eth_addr.addr[1] = mac_addr[1];
  eth_addr.addr[2] = mac_addr[2];
  eth_addr.addr[3] = mac_addr[3];
  eth_addr.addr[4] = mac_addr[4];
  eth_addr.addr[5] = mac_addr[5];

  uip_setethaddr(eth_addr);

#define DEFAULT_IPADDR0 10
#define DEFAULT_IPADDR1 0
#define DEFAULT_IPADDR2 0
#define DEFAULT_IPADDR3 201

#define DEFAULT_NETMASK0 255
#define DEFAULT_NETMASK1 255
#define DEFAULT_NETMASK2 255
#define DEFAULT_NETMASK3 0

#undef STATIC_IP

#ifdef STATIC_IP
  uip_ipaddr(ipaddr, DEFAULT_IPADDR0, DEFAULT_IPADDR1, DEFAULT_IPADDR2,
	     DEFAULT_IPADDR3);
  uip_sethostaddr(ipaddr);
  printf("IP: %d.%d.%d.%d\n", DEFAULT_IPADDR0, DEFAULT_IPADDR1,
	     DEFAULT_IPADDR2, DEFAULT_IPADDR3);
  uip_ipaddr(ipaddr, DEFAULT_NETMASK0, DEFAULT_NETMASK1, DEFAULT_NETMASK2,
	     DEFAULT_NETMASK3);
  uip_setnetmask(ipaddr);
#else
  uip_ipaddr(ipaddr, 0, 0, 0, 0);
  uip_sethostaddr(ipaddr);
  printf("Waiting for IP address...\n");
  uip_ipaddr(ipaddr, 0, 0, 0, 0);
  uip_setnetmask(ipaddr);
#endif

  httpd_init();

#ifndef STATIC_IP
  dhcpc_init(mac_addr, 6);
  dhcpc_request();
#endif

  long lPeriodicTimer, lARPTimer;
  lPeriodicTimer = lARPTimer = 0;

  int  i = MAP_GPIOPinRead(GPIO_PORTA_BASE, ENC_INT) & ENC_INT;
  while(true) {
    i = MAP_GPIOPinRead(GPIO_PORTA_BASE, ENC_INT) & ENC_INT;
    while(i != 0 && g_ulFlags == 0) {
      i = MAP_GPIOPinRead(GPIO_PORTA_BASE, ENC_INT) & ENC_INT;
    }

    if( i == 0 ) {
      enc_action();
    }

    if(HWREGBITW(&g_ulFlags, FLAG_SYSTICK) == 1) {
      HWREGBITW(&g_ulFlags, FLAG_SYSTICK) = 0;
      lPeriodicTimer += SYSTICKMS;
      lARPTimer += SYSTICKMS;
      //printf("%d %d\n", lPeriodicTimer, lARPTimer);
    }

    if( lPeriodicTimer > UIP_PERIODIC_TIMER_MS ) {
      lPeriodicTimer = 0;
      for(int l = 0; l < UIP_CONNS; l++) {
	uip_periodic(l);

	//
	// If the above function invocation resulted in data that
	// should be sent out on the network, the global variable
	// uip_len is set to a value > 0.
	//
	if(uip_len > 0) {
	  uip_arp_out();
	  enc_send_packet(uip_buf, uip_len);
	  uip_len = 0;
	}
      }

      for(int l = 0; l < UIP_UDP_CONNS; l++) {
	uip_udp_periodic(l);
	if( uip_len > 0) {
	  uip_arp_out();
	  enc_send_packet(uip_buf, uip_len);
	  uip_len = 0;
	}
      }
    }

    if( lARPTimer > UIP_ARP_TIMER_MS) {
      lARPTimer = 0;
      uip_arp_timer();
    }

  }

  return 0;
}

uint8_t spi_send(uint8_t c) {
  unsigned long val;
  MAP_SSIDataPut(SSI2_BASE, c);
  MAP_SSIDataGet(SSI2_BASE, &val);
  return (uint8_t)val;
}


void
dhcpc_configured(const struct dhcpc_state *s)
{
    uip_sethostaddr(&s->ipaddr);
    uip_setnetmask(&s->netmask);
    uip_setdraddr(&s->default_router);
    printf("IP: %d.%d.%d.%d\n", s->ipaddr[0] & 0xff, s->ipaddr[0] >> 8,
	   s->ipaddr[1] & 0xff, s->ipaddr[1] >> 8);
}

void
SysTickIntHandler(void)
{
    //
    // Increment the system tick count.
    //
    g_ulTickCounter++;

    //
    // Indicate that a SysTick interrupt has occurred.
    //
    HWREGBITW(&g_ulFlags, FLAG_SYSTICK) = 1;
    //g_ulFlags |= FLAG_SYSTICK;
}

clock_time_t
clock_time(void)
{
    return((clock_time_t)g_ulTickCounter);
}
