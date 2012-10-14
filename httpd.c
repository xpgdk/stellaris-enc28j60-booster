#include "httpd.h"
#include "uip.h"
#include "common.h"

#include <stdbool.h>
#include <string.h>

static const char http_response_header[] =
  "HTTP/1.1 200 OK\r\n"
  "Server: net430\r\n"
  "Content-Type: text/html\r\n\r\n";

static const char http_json_header[] =
  "HTTP/1.1 200 OK\r\n"
  "Server: net430\r\n"
  "Content-Type: application/json\r\n\r\n";

static const char http_404_header[] =
  "HTTP/1.1 404 OK\r\n"
  "Server: net430\r\n"
  "Content-Type: text/html\r\n\r\n";

static const char unknown_request[] = "Unknown request";

static const char read_result[] = "Read:";

/*
  Read response is JSON:
  {
	"J1" = ["x", 1.02, 1.03, 1.04, 1.05, 1.06, 1.07, 1.08, 1.09, 1.10],
	"J2" = ["x", 2.02, 2.03, 2.04,  "x", 2.06, 2.07, 2.08, 2.09, 2.10]
  }

  Where
  1.02 = PB5
  1.03 = PB0
  1.04 = PB1
  1.05 = PE4
  1.06 = PE5
  1.07 = PB4
  1.08 = PA5
  1.09 = PA6
  1.10 = PA7

  2.02 = PB2
  2.03 = PE0
  2.04 = PF0
  X
  2.06 = PB7
  2.07 = PB6
  2.08 = PA4
  2.09 = PA3
  2.10 = PA2
 */

#define CONFIG_NOT_USED		0
#define CONFIG_INPUT		1
#define CONFIG_OUTPUT		2

struct header_pin {
  uint32_t	base;
  uint8_t	pin;
  uint8_t	config;
};

static struct header_pin j1[10];
static uint16_t j1_length = 10;

static struct header_pin j2[10];
static uint16_t j2_length = 10;

static struct header_pin j3[10];
static uint16_t j3_length = 10;

static void *headers[3] = {&j1, &j2, &j3};

#define DATA_BUF ((uint8_t*)(uip_appdata))

#define PIN_UNUSED(pin) pin.config = CONFIG_NOT_USED
#define SETUP_PIN(S, BASE, PIN, CONFIG)		\
  S.config = CONFIG;				\
  S.base = BASE;				\
  S.pin = PIN

static void configure_pins(struct header_pin pins[], uint16_t length);
static int read_pins(struct header_pin pins[], uint16_t length, uint8_t *buf);

void
httpd_init(void) {
  uip_listen(HTONS(80));

  PIN_UNUSED(j1[0]);
  PIN_UNUSED(j1[1]); // PB5
  SETUP_PIN(j1[2], GPIO_PORTB_BASE, GPIO_PIN_0, CONFIG_INPUT);
  SETUP_PIN(j1[3], GPIO_PORTB_BASE, GPIO_PIN_1, CONFIG_INPUT);
  SETUP_PIN(j1[4], GPIO_PORTE_BASE, GPIO_PIN_4, CONFIG_INPUT);
  PIN_UNUSED(j1[5]); // PE5
  PIN_UNUSED(j1[6]); // PB4
  PIN_UNUSED(j1[7]); // PA5
  SETUP_PIN(j1[8], GPIO_PORTA_BASE, GPIO_PIN_6, CONFIG_INPUT);
  SETUP_PIN(j1[9], GPIO_PORTA_BASE, GPIO_PIN_7, CONFIG_INPUT);

  PIN_UNUSED(j2[0]); // GND
  SETUP_PIN(j2[1], GPIO_PORTB_BASE, GPIO_PIN_2, CONFIG_INPUT);
  SETUP_PIN(j2[2], GPIO_PORTE_BASE, GPIO_PIN_0, CONFIG_INPUT);
  PIN_UNUSED(j2[3]); // PF0 -- not used
  PIN_UNUSED(j2[4]); // RESET
  PIN_UNUSED(j2[5]); // PB7 -- used by SSI2
  PIN_UNUSED(j2[6]); // PB6 -- used by SSI2
  SETUP_PIN(j2[7], GPIO_PORTA_BASE, GPIO_PIN_4, CONFIG_INPUT);
  SETUP_PIN(j2[8], GPIO_PORTA_BASE, GPIO_PIN_3, CONFIG_INPUT);
  SETUP_PIN(j2[9], GPIO_PORTA_BASE, GPIO_PIN_2, CONFIG_INPUT);

  PIN_UNUSED(j3[0]); // 5.0V
  PIN_UNUSED(j3[1]); // GND
  SETUP_PIN(j3[2], GPIO_PORTD_BASE, GPIO_PIN_0, CONFIG_INPUT);
  SETUP_PIN(j3[3], GPIO_PORTD_BASE, GPIO_PIN_1, CONFIG_INPUT);
  SETUP_PIN(j3[4], GPIO_PORTD_BASE, GPIO_PIN_2, CONFIG_INPUT);
  SETUP_PIN(j3[5], GPIO_PORTD_BASE, GPIO_PIN_3, CONFIG_INPUT);
  SETUP_PIN(j3[6], GPIO_PORTE_BASE, GPIO_PIN_1, CONFIG_INPUT);
  SETUP_PIN(j3[7], GPIO_PORTE_BASE, GPIO_PIN_2, CONFIG_INPUT);
  SETUP_PIN(j3[8], GPIO_PORTE_BASE, GPIO_PIN_3, CONFIG_INPUT);
  SETUP_PIN(j3[9], GPIO_PORTF_BASE, GPIO_PIN_1, CONFIG_OUTPUT);

  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

  configure_pins(j1, j1_length);
  configure_pins(j2, j2_length);
  configure_pins(j3, j3_length);
}

void httpd_appcall(void) {
  if(uip_conn->lport != HTONS(80)) {
    uip_abort();
    return;
  }

  struct httpd_state *hs = (struct httpd_state *)&(uip_conn->appstate);
  bool send_new_data = false;

  if(uip_connected()) {
    printf("Connected\n");
    hs->data_count = 0;
    hs->idle_count = 0;
    hs->state = 0;
    hs->done = false;
  } else if(uip_newdata()) {
    printf("New data\n");
    if(strncmp(DATA_BUF, "GET ", 4) != 0) {
      uip_abort();
      return;
    }
    printf("Got get request\n");

#define PATH_START 4
    /* Get path */
    int i;
    for(i = 4; i<uip_datalen() && DATA_BUF[i] != ' '; i++);

    char path[21];
    if( i-PATH_START > 20 ) {
      i = 20;
    }

    memcpy(path, DATA_BUF + PATH_START, i-PATH_START);
    path[i-PATH_START] = '\0';
    printf("Path: '%s'\n", path);

    if(strncmp(path, "/read", 5) == 0) {
      hs->request_type = REQUEST_READ;
    } else if(strncmp(path, "/write/", 7) == 0) {
      hs->request_type = REQUEST_WRITE;
      char buf[20];
      struct header_pin *connector;
      int i,l;
      for(i=7; path[i] != '\0' && path[i] != '.'; i++);

      memcpy(buf, path+7, i-7);
      buf[i-7] = '\0';

      printf("Port: %s\n", buf);
      l = i+1;
      if( l == '\0' ) {
	goto config_done;
      }
      printf("c: %d\n", (buf[0]-'0')-1);
      connector = (struct header_pin*)headers[(buf[0]-'0')-1];
      printf("j1: %p\n", &j1);
      printf("j2: %p\n", &j2);
      printf("j3: %p\n", &j3);
      printf("connector: %p\n", connector);
      for(; path[i] != '\0' && path[i] != '/'; i++);

      memcpy(buf, path+l, i-l);
      buf[i-l] = '\0';
      printf("Pin: %s\n", buf);

      l = i+1;
      uint8_t pin = atoi(buf)-1; //(buf[0] - '0')-1;
      if( l == '\0' ) {
	goto config_done;
      }
      for(; path[i] != '\0'; i++);

      memcpy(buf, path+l, i-l);
      buf[i-l] = '\0';
      printf("pin: %d\n", pin);
      printf("Value: '%s'\n", buf);

      printf("C: %p\n", connector[pin].base);
      printf("C: %p\n", (*(&j3))[pin].base);
      printf("P: %p\n", GPIO_PORTF_BASE);

      if( connector[pin].config != CONFIG_OUTPUT )
	goto config_done;

      if(buf[0] == '1') {
	MAP_GPIOPinWrite(connector[pin].base, connector[pin].pin, connector[pin].pin);
      } else {
	MAP_GPIOPinWrite(connector[pin].base, connector[pin].pin, 0);
      }
    } else if(strncmp(path, "/config/",8) == 0) {
      hs->request_type = REQUEST_CONFIG;
      char buf[20];
      int i,l;
      for(i=8; path[i] != '\0' && path[i] != '.'; i++);

      memcpy(buf, path+8, i-8);
      buf[i-8] = '\0';

      printf("Port: %s\n", buf);
      l = i+1;
      if( l == '\0' ) {
	goto config_done;
      }
      for(; path[i] != '\0' && path[i] != '/'; i++);

      memcpy(buf, path+l, i-l);
      buf[i-l] = '\0';
      printf("Pin: %s\n", buf);

      l = i+1;
      if( l == '\0' ) {
	goto config_done;
      }
      for(; path[i] != '\0'; i++);

      memcpy(buf, path+l, i-l);
      buf[i-l] = '\0';
      printf("Dir: '%s'\n", buf);
    } else {
      hs->request_type = 0;
    }
  config_done:
    send_new_data = true;
  } else if( uip_acked() ) {
    hs->data_count++;
    if( hs->done ) {
      uip_close();
    } else {
      send_new_data = true;
    }
  } else if( uip_poll() ) {
    printf("Poll\n");
    hs->idle_count++;
    if( hs->idle_count > 5 ) {
      uip_close();
    }
  }

  if( uip_rexmit() || send_new_data ) {
    printf("%p: Request type: %d\n", hs, hs->request_type);
    printf("%p: Sending data (%d)\n", hs, hs->data_count);
    switch(hs->request_type) {
    case REQUEST_READ:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_json_header;
	hs->xmit_buf_size = sizeof(http_json_header)-1;
      } else if(hs->data_count == 1) {
	hs->xmit_buf = NULL;

	uint8_t buf[500];
	uint16_t i = 0;

	static char b1[] = "{\n\t\"J1\" = [";
	static char b2[] = "\t\"J2\" = [";
	static char b3[] = "\t\"J3\" = [";
	static char bnone[] = "\"x\"";
	static char bx[] = "\n}";

	memcpy(buf+i, b1, sizeof(b1)-1);
	i+= sizeof(b1)-1;
	i += read_pins(j1, j1_length, buf+i);
	buf[i++] = ']';
	buf[i++] = ',';
	buf[i++] = '\n';

	memcpy(buf+i, b2, sizeof(b2)-1);
	i+= sizeof(b2)-1;
	i += read_pins(j2, j2_length, buf+i);
	buf[i++] = ']';
	buf[i++] = ',';
	buf[i++] = '\n';

	memcpy(buf+i, b3, sizeof(b3)-1);
	i+= sizeof(b3)-1;
	i += read_pins(j3, j3_length, buf+i);
	buf[i++] = ']';

	memcpy(buf+i, bx, sizeof(bx)-1);
	i+= sizeof(bx)-1;

	uip_send(buf, i);
	hs->done = true;
      } else {
	hs->xmit_buf = NULL;
	uip_close();
      }
      break;
    case REQUEST_CONFIG:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_json_header;
	hs->xmit_buf_size = sizeof(http_json_header)-1;
	hs->done = true;
      } /*else if(hs->data_count == 1) {
	}*/ else {
	hs->xmit_buf = NULL;
	uip_close();
      }
      break;
    case REQUEST_WRITE:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_json_header;
	hs->xmit_buf_size = sizeof(http_json_header)-1;
	hs->done = true;
      } /*else if(hs->data_count == 1) {
	}*/ else {
	hs->xmit_buf = NULL;
	uip_close();
      }
      break;
    default:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_404_header;
	hs->xmit_buf_size = sizeof(http_404_header)-1;
      } else if(hs->data_count == 1) {
	printf("Unknown request\n");
	hs->xmit_buf = unknown_request;
	hs->xmit_buf_size = sizeof(unknown_request)-1;
	hs->done = true;
      } else {
	hs->xmit_buf = NULL;
	uip_close();
      }
      break;
    }

    if (hs->xmit_buf != NULL ) {
      uip_send(hs->xmit_buf, hs->xmit_buf_size);
    }
  }
}

void
configure_pins(struct header_pin pins[], uint16_t length) {
  for(int i=0; i<length; i++) {
    if(pins[i].config == CONFIG_INPUT) {
      printf("Setting input %d\n", i);
      MAP_GPIOPinTypeGPIOInput(pins[i].base, pins[i].pin);
    } else if( pins[i].config == CONFIG_OUTPUT) {
      MAP_GPIOPinTypeGPIOOutput(pins[i].base, pins[i].pin);
    } else {
      printf("NOP           %d\n", i);
    }
    UARTFlushTx(false);
  }
}

int
read_pins(struct header_pin pins[], uint16_t length, uint8_t *buf) {
  static char bnone[] = "\"x\"";
  uint16_t i = 0;

  for(int l=0;l<length;l++) {
    if( l > 0)
      buf[i++] = ',';
    printf("pin %d: %d\n", l, pins[l].config);
    if( pins[l].config == CONFIG_NOT_USED ||
	pins[l].config == CONFIG_OUTPUT ) {
      memcpy(buf+i, bnone, sizeof(bnone)-1);
      i+= sizeof(bnone)-1;
    } else {
      if( MAP_GPIOPinRead(pins[l].base, pins[l].pin) == 0x00 ) {
	buf[i++] = '0';
      } else {
	buf[i++] = '1';
      }
    }
  }
  return i;
}
