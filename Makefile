#Cygni Makefile
#Copyright 2011 teho Labs

NAME = main

#include the toolchain settings 
include toolchainsettings

os:=${shell uname -s}

CYGWIN = 0
ifneq ($(findstring CYGWIN, ${os}), )
	CYGWIN = 1
endif

#Toolchain Command Config

ifeq ($(SERIALPROG),1)
	LINKER = bootlink.ld
	ifeq ($(WINDOWS),1)
		FLASHCMD = lmflash -q manual -i serial -p $(COMPORT) -b 115200 -r --offset=0x1000 --xfer-size=32 $(NAME).bin
	else
		FLASHCMD = sflash -p 0x1000 -c $(COMPORT) -b 115200 -s 32 $(NAME).bin
	endif
else
	LINKER = link.ld
	ifeq ($(WINDOWS),1)
		FLASHCMD = perl ./do_flash.pl $(NAME).bin
	else 
		FLASHCMD = ./do_flash.pl $(NAME).bin
	endif
endif

ifeq ($(WINDOWS),1)	
	ifeq ($(CYGWIN),1)
		CLEANCMD = rm -f $(NAME).elf $(NAME).dmp *.list *.o
	else
		CLEANCMD = del /s /q $(NAME).elf $(NAME).dmp *.list *.o
	endif	
else 
	CLEANCMD = rm -f $(NAME).elf $(NAME).dmp *.list *.o $(NAME).bin
endif

CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld -v
AR      = arm-none-eabi-ar
AS      = arm-none-eabi-as
CP      = arm-none-eabi-objcopy
OD	= arm-none-eabi-objdump

#-DUART_BUFFERED
MACROS  = -DDTARGET_IS_BLIZZARD_RA2 -DPART_LM4F120H5QR 
CFLAGS  =  -Dgcc -I$(DIR_LWIP)/include -I$(DIR_LWIP)/include/ipv4 -I$(DIR_LWIP)/include/ipv6 -I./ -I$(DIR_STELLARISWARE) -std=c99 -fno-common -O0 -g -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=softfp  -mthumb
LFLAGS  = -T $(LINKER) -nostartfiles
CPFLAGS = -Obinary
ODFLAGS	= -D

SOURCES = \
	$(NAME).c echo.c \
	$(DIR_DRIVERLIB)/uart.c \
	$(DIR_UTILS)/uartstdio.c \
	$(DIR_UTILS)/ustdlib.c\
	$(DIR_LWIP)/core/raw.c \
	$(DIR_LWIP)/core/init.c \
	$(DIR_LWIP)/core/tcp.c \
	$(DIR_LWIP)/core/udp.c \
	$(DIR_LWIP)/core/tcp_in.c \
	$(DIR_LWIP)/core/tcp_out.c \
	$(DIR_LWIP)/core/pbuf.c \
	$(DIR_LWIP)/core/sys.c \
	$(DIR_LWIP)/core/def.c \
	$(DIR_LWIP)/core/mem.c \
	$(DIR_LWIP)/core/memp.c \
	$(DIR_LWIP)/core/ipv4/ip4.c \
	$(DIR_LWIP)/core/ipv4/ip4_addr.c \
	$(DIR_LWIP)/core/ipv4/icmp.c \
	$(DIR_LWIP)/core/ipv4/ip_frag.c \
	$(DIR_LWIP)/core/ipv4/igmp.c \
	$(DIR_LWIP)/core/stats.c \
	$(DIR_LWIP)/core/inet_chksum.c \
	$(DIR_LWIP)/core/netif.c \
	$(DIR_LWIP)/core/timers.c \
	$(DIR_LWIP)/core/dhcp.c \
	$(DIR_LWIP)/netif/etharp.c \
	startup_gcc.c \
	arch/sys_arch.c \
	$(DIR_DRIVERLIB)/gcc-cm4f/libdriver-cm4f.a \
	enc28j60.c
#	httpd.c \
#	$(DIR_UIP)/uip/uip.c \
#	$(DIR_UIP)/uip/uip_timer.c \
#	$(DIR_UIP)/uip/uip_arp.c \
#	$(DIR_UIP)/uip/psock.c \
#	$(DIR_UIP)/apps/dhcpc/dhcpc.c

all: reg
	
reg: $(NAME).elf
	$(CP) $(CPFLAGS) $(NAME).elf $(NAME).bin

flash: all 
	$(FLASHCMD)

clean: 
	$(CLEANCMD)	

$(NAME).elf: $(SOURCES) link.ld 
	$(CC) $(MACROS) $(CFLAGS) $(LFLAGS) -o $(NAME).elf  $(SOURCES)


