/*
 * enc28j60.h
 *
 */

#ifndef ENC28J60_H_
#define ENC28J60_H_

#include <common.h>
#include <stdint.h>
#include <stdbool.h>

#include <lwip/netif.h>

/* Pins */
#define ENC_CS_PORT		GPIO_PORTB_BASE
#define ENC_INT_PORT		GPIO_PORTE_BASE
//#define ENC_RESET_PORT		GPIO_PORTA_BASE
#define ENC_CS			GPIO_PIN_5
#define ENC_INT			GPIO_PIN_4
//#define ENC_RESET		GPIO_PIN_2


err_t enc28j60_init(struct netif *netif);

/**** API ****/
void enc_init(const uint8_t *mac);

/**
 * Function which does all the heavy work
 * It should be called when the ENC28J60 has signaled an interrupt
 */
void enc_action(struct netif *netif);

/**
 * Send an ethernet packet. Function will block until
 * transmission has completed.
 * TODO: Return if the transmission was successful or not
 */
void enc_send_packet(const uint8_t *buf, uint16_t count);

#endif /* ENC28J60_H_ */
