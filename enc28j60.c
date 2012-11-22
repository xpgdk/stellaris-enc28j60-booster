#include "enc28j60.h"
#include "enc28j60reg.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "spi.h"

#include <netif/etharp.h>

#define TX_START	(0x1FFF - 0x600)
#define RX_END		(TX_START-1)

static uint8_t enc_current_bank;
static uint16_t enc_next_packet;

/* Internal low-level register access functions*/
static uint8_t enc_rcr(uint8_t reg);
static void enc_wcr(uint8_t reg, uint8_t val);
static uint8_t enc_rcr_m(uint8_t reg);
static void enc_rbm(uint8_t *buf, uint16_t count);
static void enc_wbm(const uint8_t *buf, uint16_t count);
static void enc_bfs(uint8_t reg, uint8_t mask);
static void enc_bfc(uint8_t reg, uint8_t mask);
static void enc_switch_bank(uint8_t new_bank);

/* Internal high-level register access functions*/
static uint8_t enc_read_reg(uint8_t reg, uint8_t bank);
static void enc_write_reg(uint8_t reg, uint8_t bank, uint8_t value);
static uint8_t enc_read_mreg(uint8_t reg, uint8_t bank);
static void enc_set_bits(uint8_t reg, uint8_t bank, uint8_t mask);
static void enc_clear_bits(uint8_t reg, uint8_t bank, uint8_t mask);

/* Macros for accessing registers.
 * These macros should be used instead of calling the functions directly.
 * They simply pass the register's bank as an argument, so the caller
 * doesn't have to deal with that.
 */
#define READ_REG(reg) enc_read_reg(reg, reg ## _BANK)
#define WRITE_REG(reg, value) enc_write_reg(reg, reg ## _BANK, value)
#define READ_MREG(reg) enc_read_mreg(reg, reg ## _BANK)
#define SET_REG_BITS(reg, mask) enc_set_bits(reg, reg ## _BANK, mask)
#define CLEAR_REG_BITS(reg, mask) enc_clear_bits(reg, reg ## _BANK, mask)


static uint16_t enc_phy_read(uint8_t addr);
static void enc_set_rx_area(uint16_t start, uint16_t end);
static void enc_set_mac_addr(const uint8_t *mac_addr);
static void enc_receive_packet(void);


void enc_reset(void) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);

	spi_send(0xFF);

	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Read Control Register (RCR)
 */
uint8_t enc_rcr(uint8_t reg) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(reg);
	uint8_t b = spi_send(0xFF); // Dummy

	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
	return b;
}

/**
 * Write Control Register (WCR)
 */
void enc_wcr(uint8_t reg, uint8_t val) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(0x40 | reg);
	spi_send(val);
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Read Control Register for MAC an MII registers.
 * Reading MAC and MII registers produces an initial dummy
 * byte. Presumably because it takes longer to fetch the values
 * of those registers.
 */
uint8_t enc_rcr_m(uint8_t reg) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(reg);
	spi_send(0xFF);
	uint8_t b = spi_send(0xFF); // Dummy
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
	return b;
}


/**
 * Read Buffer Memory.
 */
void enc_rbm(uint8_t *buf, uint16_t count) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(0x20 | 0x1A);
	int i;
	for (i = 0; i < count; i++) {
		*buf = spi_send(0xFF);
		buf++;
	}
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Write Buffer Memory.
 */
void enc_wbm(const uint8_t *buf, uint16_t count) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(0x60 | 0x1A);
	int i;
	for (i = 0; i < count; i++) {
		spi_send(*buf);
		buf++;
	}
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Bit Field Set.
 * Set the bits of argument 'mask' in the register 'reg'.
 * Not valid for MAC and MII registers.
 */
void enc_bfs(uint8_t reg, uint8_t mask) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(0x80 | reg);
	spi_send(mask);
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Bit Field Clear.
 * Clear the bits of argument 'mask' in the register 'reg'.
 * Not valid for MAC and MII registers.
 */
void enc_bfc(uint8_t reg, uint8_t mask) {
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, 0);
	spi_send(0xA0 | reg);
	spi_send(mask);
	MAP_GPIOPinWrite(ENC_CS_PORT, ENC_CS, ENC_CS);
}

/**
 * Switch memory bank to 'new_bank'
 */
void enc_switch_bank(uint8_t new_bank) {
	if (new_bank == enc_current_bank || new_bank == ANY_BANK) {
		return;
	}
	uint8_t econ1 = enc_rcr(ENC_ECON1);

	econ1 &= ~ENC_ECON1_BSEL_MASK;
	econ1 |= (new_bank & ENC_ECON1_BSEL_MASK) << ENC_ECON1_BSEL_SHIFT;
	enc_wcr(ENC_ECON1, econ1);
	enc_current_bank = new_bank;
}


/**
 * High level register read. Switches bank as appropriate.
 */
uint8_t enc_read_reg(uint8_t reg, uint8_t bank) {
	if (bank != enc_current_bank) {
		enc_switch_bank(bank);
	}

	return enc_rcr(reg);
}

/**
 * High level bit field set. Switches bank as appropriate.
 */
void enc_set_bits(uint8_t reg, uint8_t bank, uint8_t mask) {
	if (bank != enc_current_bank) {
		enc_switch_bank(bank);
	}

	enc_bfs(reg, mask);
}

/**
 * High level bit field clear. Switches bank as appropriate.
 */
void enc_clear_bits(uint8_t reg, uint8_t bank, uint8_t mask) {
	if (bank != enc_current_bank) {
		enc_switch_bank(bank);
	}

	enc_bfc(reg, mask);
}

/**
 * High level MAC/MII register read. Switches bank as appropriate.
 */
uint8_t enc_read_mreg(uint8_t reg, uint8_t bank) {
	if (bank != enc_current_bank) {
		enc_switch_bank(bank);
	}

	return enc_rcr_m(reg);
}

/**
 * High level register write. Switches bank as appropriate.
 */
void enc_write_reg(uint8_t reg, uint8_t bank, uint8_t value) {
	if (bank != enc_current_bank) {
		enc_switch_bank(bank);
	}

	enc_wcr(reg, value);
}

/**
 * Read value from PHY address.
 * Reading procedure is described in ENC28J60 datasheet
 * section 3.3.
 */
uint16_t enc_phy_read(uint8_t addr) {
	/*
	 1. Write the address of the PHY register to read
	 from into the MIREGADR register.*/
	WRITE_REG(ENC_MIREGADR, addr);

	/*2. Set the MICMD.MIIRD bit. The read operation
	 begins and the MISTAT.BUSY bit is set.*/
	WRITE_REG(ENC_MICMD, 0x1);

	/*3. Wait 10.24 μs. Poll the MISTAT.BUSY bit to be
	 certain that the operation is complete. While
	 busy, the host controller should not start any
	 MIISCAN operations or write to the MIWRH
	 register.
	 When the MAC has obtained the register
	 contents, the BUSY bit will clear itself.*/

	/* Assuming that we are running at 1MHz, a single cycle is
	 * 1 us */
	MAP_SysCtlDelay(((MAP_SysCtlClockGet()/3)/1000));

	uint8_t stat;
	do {
		stat = READ_MREG(ENC_MISTAT);
	} while (stat & ENC_MISTAT_BUSY);

	/*4. Clear the MICMD.MIIRD bit.*/
	WRITE_REG(ENC_MICMD, 0x00);

	/*5. Read the desired data from the MIRDL and
	 MIRDH registers. The order that these bytes are
	 accessed is unimportant.
	 */
	uint16_t ret;
	ret = READ_MREG(ENC_MIRDL) & 0xFF;
	ret |= READ_MREG(ENC_MIRDH) << 8;

	return ret;
}

/**
 * Write value to PHY address.
 * Reading procedure is described in ENC28J60 datasheet
 * section 3.3.
 */
void enc_phy_write(uint8_t addr, uint16_t value) {
	WRITE_REG(ENC_MIREGADR, addr);
	WRITE_REG(ENC_MIWRL, value & 0xFF);
	WRITE_REG(ENC_MIWRH, value >> 8);

	MAP_SysCtlDelay(((MAP_SysCtlClockGet()/3)/1000));

	uint8_t stat;
	do {
		stat = READ_MREG(ENC_MISTAT);
	} while (stat & ENC_MISTAT_BUSY);
}

/**
 * Set the memory area to use for receiving packets.
 */
void enc_set_rx_area(uint16_t start, uint16_t end) {
	WRITE_REG(ENC_ERXSTL, start & 0xFF);
	WRITE_REG(ENC_ERXSTH, (start >> 8) & 0xFFF);

	WRITE_REG(ENC_ERXNDL, end & 0xFF);
	WRITE_REG(ENC_ERXNDH, (end >> 8) & 0xFFF);

	WRITE_REG(ENC_ERXRDPTL, start & 0xFF);
	WRITE_REG(ENC_ERXRDPTH, (start >> 8) & 0xFFF);
}

/**
 * Set the MAC address.
 */
void enc_set_mac_addr(const uint8_t *mac_addr) {
	WRITE_REG(ENC_MAADR1, mac_addr[0]);
	WRITE_REG(ENC_MAADR2, mac_addr[1]);
	WRITE_REG(ENC_MAADR3, mac_addr[2]);
	WRITE_REG(ENC_MAADR4, mac_addr[3]);
	WRITE_REG(ENC_MAADR5, mac_addr[4]);
	WRITE_REG(ENC_MAADR6, mac_addr[5]);
}

/**
 * Read the MAC address.
 */
void enc_get_mac_addr(uint8_t *mac_addr) {
  mac_addr[0] = READ_REG(ENC_MAADR1);
  mac_addr[1] = READ_REG(ENC_MAADR2);
  mac_addr[2] = READ_REG(ENC_MAADR3);
  mac_addr[3] = READ_REG(ENC_MAADR4);
  mac_addr[4] = READ_REG(ENC_MAADR5);
  mac_addr[5] = READ_REG(ENC_MAADR6);
}

static err_t
enc_low_level_output(struct netif *netif, struct pbuf *p)
{
  uint8_t frame[1514];
  uint8_t *frame_ptr = &frame[0];
  struct pbuf *b;

  for(b = p; b != NULL; b = b->next) {
    //printf("Copying %d bytes from %p to %p\n", b->len, b->payload, frame_ptr);
    memcpy(frame_ptr, b->payload, b->len);
    frame_ptr += b->len;
  }

  enc_send_packet(frame, p->tot_len);

  return ERR_OK;
}

static struct pbuf*
enc_low_level_input(struct netif *netif) {
  struct pbuf *p, *q;
  /* Receive a single packet */
  uint8_t header[6];
  uint8_t *status = header + 2;
  
  WRITE_REG(ENC_ERDPTL, enc_next_packet & 0xFF);
  WRITE_REG(ENC_ERDPTH, (enc_next_packet >> 8) & 0xFF);
  enc_rbm(header, 6);
  
  /* Update next packet pointer */
  enc_next_packet = header[0] | (header[1] << 8);
  
  uint16_t data_count = status[0] | (status[1] << 8);
  if (status[2] & (1 << 7)) {
    uint8_t frame[1514];
    uint8_t *frame_ptr = &frame[0];
    enc_rbm(frame, data_count);

    p = pbuf_alloc(PBUF_LINK, data_count, PBUF_POOL);
    //printf("p: %p\n", p);
    if( p != NULL ) {
      for(q = p; q != NULL; q = q->next) {
	memcpy(q->payload, frame_ptr, q->len);
	frame_ptr += q->len;
      }
    } else {
      printf("Error!\n");
    }
  }

  uint16_t erxst = READ_REG(ENC_ERXSTL) | (READ_REG(ENC_ERXSTH) << 8);

  /* Mark packet as read */
  if (enc_next_packet == erxst) {
    WRITE_REG(ENC_ERXRDPTL, READ_REG(ENC_ERXNDL));
    WRITE_REG(ENC_ERXRDPTH, READ_REG(ENC_ERXNDH));
  } else {
    WRITE_REG(ENC_ERXRDPTL, (enc_next_packet-1) & 0xFF);
    WRITE_REG(ENC_ERXRDPTH, ((enc_next_packet-1) >> 8) & 0xFF);
  }
  SET_REG_BITS(ENC_ECON2, ENC_ECON2_PKTDEC);
  return p;
}

#ifdef LWIP_NETIF_STATUS_CALLBACK
void
enc28j60_status_callback(struct netif *netif)
{
  if( netif->flags & NETIF_FLAG_UP ) {
    printf("IP: %d.%d.%d.%d\n",
	   ip4_addr1_16(&netif->ip_addr),
	   ip4_addr2_16(&netif->ip_addr),
	   ip4_addr3_16(&netif->ip_addr),
	   ip4_addr4_16(&netif->ip_addr) );
  }
}
#endif

extern uint8_t mac_addr[];

err_t enc28j60_init(struct netif *netif)
{
  netif->state = NULL;
  netif->hwaddr_len = 6;
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->output = etharp_output;
  netif->linkoutput = enc_low_level_output;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_ETHARP;
#ifdef LWIP_NETIF_STATUS_CALLBACK
  netif->status_callback = enc28j60_status_callback;
#endif

  memcpy(netif->hwaddr, mac_addr, 6);

  return ERR_OK;
}

/**
 * Initialize the ENC28J60 with the given MAC-address
 */
void enc_init(const uint8_t *mac) {
	enc_next_packet = 0x000;

	//MAP_GPIOPinWrite(ENC_RESET_PORT, ENC_RESET, ENC_RESET);

	enc_reset();

	uint8_t reg;
	do {
		reg = READ_REG(ENC_ESTAT);
		delayMs(200);
		printf("ENC_ESTAT: %x\n", reg);
	} while ((reg & ENC_ESTAT_CLKRDY) == 0);


	enc_switch_bank(0);

	printf("Econ: %x\n", READ_REG(ENC_ECON1));

#if 1
	printf("Silicon Revision: %d\n", READ_REG(ENC_EREVID));
#endif

	//SET_REG_BITS(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
	SET_REG_BITS(ENC_ECON1, ENC_ECON1_TXRST);
	CLEAR_REG_BITS(ENC_ECON1, ENC_ECON1_RXEN);

	SET_REG_BITS(ENC_ECON2, ENC_ECON2_AUTOINC);

	enc_set_rx_area(0x000, RX_END);

	uint16_t phyreg = enc_phy_read(ENC_PHSTAT2);
	phyreg &= ~ENC_PHSTAT2_DPXSTAT;
	enc_phy_write(ENC_PHSTAT2, phyreg);

	phyreg = enc_phy_read(ENC_PHCON1);
	phyreg &= ~ENC_PHCON_PDPXMD;
	enc_phy_write(ENC_PHCON1, phyreg);

	/* Setup receive filter to receive
	 * broadcast, multicast and unicast to the given MAC */
#if 0
	printf("Setting MAC: %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2],
	       mac[3], mac[4], mac[5]);
#endif
	enc_set_mac_addr(mac);
	WRITE_REG(
		  ENC_ERXFCON,
		  ENC_ERXFCON_UCEN | ENC_ERXFCON_CRCEN | ENC_ERXFCON_BCEN |
		  ENC_ERXFCON_MCEN);

	/* Initialize MAC */
	WRITE_REG(ENC_MACON1,
		  ENC_MACON1_TXPAUS | ENC_MACON1_RXPAUS | ENC_MACON1_MARXEN);

	WRITE_REG(
		  ENC_MACON3,
		  (0x1 << ENC_MACON3_PADCFG_SHIFT) | ENC_MACON3_TXRCEN |
		  /*ENC_MACON3_FULDPX |*/ENC_MACON3_FRMLNEN);

	WRITE_REG(ENC_MAMXFLL, 1518 & 0xFF);
	WRITE_REG(ENC_MAMXFLH, (1518 >> 8) & 0xFF);

	WRITE_REG(ENC_MABBIPG, 0x12);
	WRITE_REG(ENC_MAIPGL, 0x12);
	WRITE_REG(ENC_MAIPGH, 0x0C);

	SET_REG_BITS(ENC_EIE, ENC_EIE_INTIE | ENC_EIE_PKTIE);

	CLEAR_REG_BITS(ENC_ECON1, ENC_ECON1_TXRST | ENC_ECON1_RXRST);
	SET_REG_BITS(ENC_ECON1, ENC_ECON1_RXEN);

#if 0
	uint8_t mc[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	enc_get_mac_addr(mc);
	printf("Mac addr set to: %x:%x:%x:%x:%x:%x\n", mc[0], mc[1], mc[2],
	       mc[3], mc[4], mc[5]);
#endif
}

/**
 * Receive a single packet.
 * The contents will be placed in uip_buf, and uIP is called
 * as appropriate.
 */
void enc_receive_packet(void) {
#if 0
	/* Receive a single packet */
	uint8_t header[6];
	uint8_t *status = header + 2;

	WRITE_REG(ENC_ERDPTL, enc_next_packet & 0xFF);
	WRITE_REG(ENC_ERDPTH, (enc_next_packet >> 8) & 0xFF);
	enc_rbm(header, 6);

	/* Update next packet pointer */
	enc_next_packet = header[0] | (header[1] << 8);

	uint16_t data_count = status[0] | (status[1] << 8);
	if (status[2] & (1 << 7)) {
	  uip_len = data_count;
	  enc_rbm(uip_buf, data_count);

	  if( BUF->type == htons(UIP_ETHTYPE_IP) ) {
	    uip_arp_ipin();
	    uip_input();

	    if( uip_len > 0 ) {
	      uip_arp_out();
	      enc_send_packet(uip_buf, uip_len);
	      uip_len = 0;
	    }
	  } else if( BUF->type == htons(UIP_ETHTYPE_ARP) ) {
	    uip_arp_arpin();
	    if( uip_len > 0 ) {
	      //uip_arp_out();
	      enc_send_packet(uip_buf, uip_len);
	      uip_len = 0;
	    }
	  }
	}

	uint16_t erxst = READ_REG(ENC_ERXSTL) | (READ_REG(ENC_ERXSTH) << 8);

	/* Mark packet as read */
	if (enc_next_packet == erxst) {
		WRITE_REG(ENC_ERXRDPTL, READ_REG(ENC_ERXNDL));
		WRITE_REG(ENC_ERXRDPTH, READ_REG(ENC_ERXNDH));
	} else {
		WRITE_REG(ENC_ERXRDPTL, (enc_next_packet-1) & 0xFF);
		WRITE_REG(ENC_ERXRDPTH, ((enc_next_packet-1) >> 8) & 0xFF);
	}
	SET_REG_BITS(ENC_ECON2, ENC_ECON2_PKTDEC);
#endif
}

/**
 * Handle events from the ENC28J60.
 */
void enc_action(struct netif *netif) {
	uint8_t reg = READ_REG(ENC_EIR);

	if (reg & ENC_EIR_PKTIF) {
		while (READ_REG(ENC_EPKTCNT) > 0) {
		  //enc_receive_packet();
		  struct pbuf *p = enc_low_level_input(netif);
		  if( p != NULL ) {
		    netif->input(p, netif);
		  }
		}
	}

}

/**
 * Send an ethernet packet. Function will block until
 * transmission has completed.
 * TODO: Return if the transmission was successful or not
 */
void enc_send_packet(const uint8_t *buf, uint16_t count) {
  WRITE_REG(ENC_ETXSTL, TX_START & 0xFF);
  WRITE_REG(ENC_ETXSTH, TX_START >> 8);

  WRITE_REG(ENC_EWRPTL, TX_START & 0xFF);
  WRITE_REG(ENC_EWRPTH, TX_START >> 8);

#if 0
  printf("dest: %X:%X:%X:%X:%X:%X\n", BUF->dest.addr[0], BUF->dest.addr[1],
	 BUF->dest.addr[2],  BUF->dest.addr[3], BUF->dest.addr[4],
	 BUF->dest.addr[5]);
  printf("src : %X:%X:%X:%X:%X:%X\n", BUF->src.addr[0], BUF->src.addr[1],
	 BUF->src.addr[2], BUF->src.addr[3], BUF->src.addr[4],
	 BUF->src.addr[5]);

  printf("Type: %X\n", htons(BUF->type));
#endif

  uint8_t control = 0x00;
  enc_wbm(&control, 1);

  enc_wbm(buf, count);

  uint16_t tx_end = TX_START + count;
  WRITE_REG(ENC_ETXNDL, tx_end & 0xFF);
  WRITE_REG(ENC_ETXNDH, tx_end >> 8);

  /* Eratta 12 */
  SET_REG_BITS(ENC_ECON1, ENC_ECON1_TXRST);
  CLEAR_REG_BITS(ENC_ECON1, ENC_ECON1_TXRST);

  CLEAR_REG_BITS(ENC_EIR, ENC_EIR_TXIF);
  SET_REG_BITS(ENC_ECON1, ENC_ECON1_TXRTS);

  /* Busy wait for the transmission to complete */
  while (true) {
    uint8_t r = READ_REG(ENC_ECON1);
    if ((r & ENC_ECON1_TXRTS) == 0)
      break;
  }

  /* Read status bits */
  uint8_t status[7];
  tx_end++;
  WRITE_REG(ENC_ERDPTL, tx_end & 0xFF);
  WRITE_REG(ENC_ERDPTH, tx_end >> 8);
  enc_rbm(status, 7);

  uint16_t transmit_count = status[0] | (status[1] << 8);

  if (status[2] & 0x80) {
    /* Transmit OK*/
    //    printf("Transmit OK\n");
  }
}
