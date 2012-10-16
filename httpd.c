#include "httpd.h"
#include "uip.h"
#include "common.h"

#include <stdbool.h>
#include <string.h>

#include "files_data.h"

static const char http_response_header[] =
  "HTTP/1.1 200 OK\r\n"
  "Server: net430\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "Content-Type: text/html\r\n\r\n";

static const char http_json_header[] =
  "HTTP/1.1 200 OK\r\n"
  "Server: net430\r\n"
  "Access-Control-Allow-Origin: *\r\n"
  "Content-Type: application/json\r\n\r\n";

static const char http_404_header[] =
  "HTTP/1.1 404 OK\r\n"
  "Server: net430\r\n"
  "Content-Type: text/html\r\n\r\n";

static const char unknown_request[] = "Unknown request";

static const char read_result[] = "Read:";


#define CONFIG_NOT_USED		0
#define CONFIG_INPUT		1
#define CONFIG_OUTPUT		2

#define FILE_ROOT		1

#define TRANSFER_SIZE		200

struct header_pin {
  uint32_t	base;
  uint8_t	pin;
  uint8_t	config;
};

#define HEADER_SIZE	10

static struct header_pin j1[HEADER_SIZE];
static struct header_pin j2[HEADER_SIZE];
static struct header_pin j3[HEADER_SIZE];
static struct header_pin j4[HEADER_SIZE];

static void *headers[4] = {&j1, &j2, &j3, &j4};

#define DATA_BUF ((uint8_t*)(uip_appdata))

#define PIN_UNUSED(pin) pin.config = CONFIG_NOT_USED
#define SETUP_PIN(S, BASE, PIN, CONFIG)		\
  S.config = CONFIG;				\
  S.base = BASE;				\
  S.pin = PIN

static void configure_pins(struct header_pin pins[], uint16_t length);
static int read_pins(struct header_pin pins[], uint16_t length, uint8_t *buf);
static bool parse_path(char *path, struct header_pin **connector, uint8_t *pin,
		       char *value);
static void configure_pin(struct header_pin *pin);

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

  SETUP_PIN(j4[0], GPIO_PORTF_BASE, GPIO_PIN_2, CONFIG_OUTPUT);
  SETUP_PIN(j4[1], GPIO_PORTF_BASE, GPIO_PIN_3, CONFIG_OUTPUT);
  SETUP_PIN(j4[2], GPIO_PORTB_BASE, GPIO_PIN_3, CONFIG_INPUT);
  SETUP_PIN(j4[3], GPIO_PORTC_BASE, GPIO_PIN_4, CONFIG_INPUT);
  SETUP_PIN(j4[4], GPIO_PORTC_BASE, GPIO_PIN_5, CONFIG_INPUT);
  SETUP_PIN(j4[5], GPIO_PORTC_BASE, GPIO_PIN_6, CONFIG_INPUT);
  SETUP_PIN(j4[6], GPIO_PORTC_BASE, GPIO_PIN_7, CONFIG_INPUT);
  SETUP_PIN(j4[7], GPIO_PORTD_BASE, GPIO_PIN_6, CONFIG_INPUT);
  SETUP_PIN(j4[8], GPIO_PORTD_BASE, GPIO_PIN_7, CONFIG_INPUT);
  SETUP_PIN(j4[9], GPIO_PORTF_BASE, GPIO_PIN_4, CONFIG_INPUT);

  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

  configure_pins(j1, HEADER_SIZE);
  configure_pins(j2, HEADER_SIZE);
  configure_pins(j3, HEADER_SIZE);
  configure_pins(j4, HEADER_SIZE);
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

    if(strcmp(path, "/") == 0) {
      printf("root\n");
      hs->request_type = REQUEST_FILE;
      hs->state = FILE_ROOT;
      hs->offset = 0;
    } else if(strncmp(path, "/read", 5) == 0) {
      hs->request_type = REQUEST_READ;
    } else if(strncmp(path, "/write/", 7) == 0) {
      hs->request_type = REQUEST_WRITE;
      hs->state = REQUEST_WRITE_ERR;
      struct header_pin *connector;
      uint8_t pin;
      char value[10];

      if( !parse_path(path+7, &connector, &pin, value) ) {
	goto config_done;
      }

      if( connector[pin].config != CONFIG_OUTPUT ) {
	goto config_done;
      }

      if(value[0] == '1') {
	MAP_GPIOPinWrite(connector[pin].base, connector[pin].pin, connector[pin].pin);
      } else {
	MAP_GPIOPinWrite(connector[pin].base, connector[pin].pin, 0);
      }
      hs->state = REQUEST_WRITE_OK;
    } else if(strncmp(path, "/config/",8) == 0) {
      hs->request_type = REQUEST_CONFIG;
      struct header_pin *connector;
      uint8_t pin;
      char dir[10];

      if( !parse_path(path+8, &connector, &pin, dir) ) {
	printf("Nope\n");
	goto config_done;
      }

      if( connector[pin].config == CONFIG_NOT_USED ) {
	printf("N/A\n");
	goto config_done;
      }

      printf("Dir: '%c'\n", dir[0]);

      if( dir[0] == 'i' ) {
	printf("Input\n");
	connector[pin].config = CONFIG_INPUT;
	configure_pin(&connector[pin]);
      } else if( dir[0] == 'o' ) {
	printf("Output\n");
	connector[pin].config = CONFIG_OUTPUT;
	configure_pin(&connector[pin]);
      }
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
    if( hs->idle_count > 10 ) {
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

	static char b1[] = "{\n\t\"J1\": [";
	static char b2[] = "\t\"J2\": [";
	static char b3[] = "\t\"J3\": [";
	static char b4[] = "\t\"J4\": [";
	static char bnone[] = "\"x\"";
	static char bx[] = "\n}";

	memcpy(buf+i, b1, sizeof(b1)-1);
	i+= sizeof(b1)-1;
	i += read_pins(j1, HEADER_SIZE, buf+i);
	buf[i++] = ']';
	buf[i++] = ',';
	buf[i++] = '\n';

	memcpy(buf+i, b2, sizeof(b2)-1);
	i+= sizeof(b2)-1;
	i += read_pins(j2, HEADER_SIZE, buf+i);
	buf[i++] = ']';
	buf[i++] = ',';
	buf[i++] = '\n';

	memcpy(buf+i, b3, sizeof(b3)-1);
	i+= sizeof(b3)-1;
	i += read_pins(j3, HEADER_SIZE, buf+i);
	buf[i++] = ']';
	buf[i++] = ',';
	buf[i++] = '\n';

	memcpy(buf+i, b4, sizeof(b4)-1);
	i+= sizeof(b4)-1;
	i += read_pins(j4, HEADER_SIZE, buf+i);
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
      } else if(hs->data_count == 1) {
	if( hs->state == REQUEST_WRITE_OK ) {
	  char buf[] = "ok";
	  uip_send(buf, sizeof(buf)-1);
	} else {
	  char buf[] = "error";
	  uip_send(buf, sizeof(buf)-1);
	}
	hs->done = true;
	hs->xmit_buf = NULL;
      } else {
	hs->xmit_buf = NULL;
	uip_close();
      }
      break;
    case REQUEST_FILE:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_response_header;
	hs->xmit_buf_size = sizeof(http_response_header)-1;
      } else {
	uint32_t offset = (hs->data_count-1) * TRANSFER_SIZE;
	uint32_t remain;

	remain = index_html_len - offset;

	uint16_t count = TRANSFER_SIZE;
	if( remain < TRANSFER_SIZE) {
	  count = remain;
	  hs->done = true;
	}

	printf("remain: %d\n", remain);
	printf("count: %d\n", count);
	if( index_html_len <= offset ) {
	  hs->xmit_buf = NULL;
	  uip_close();
	} else {
	  hs->xmit_buf = index_html + offset;
	  hs->xmit_buf_size = count;
	}
      }
      break;
    default:
      if(hs->data_count == 0) {
	hs->xmit_buf = http_404_header;
	hs->xmit_buf_size = sizeof(http_404_header)-1;
      } else if(hs->data_count == 1) {
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
    configure_pin(&pins[i]);
    UARTFlushTx(false);
  }
}

void configure_pin(struct header_pin *pin) {
  if(pin->config == CONFIG_INPUT) {
    MAP_GPIOPinTypeGPIOInput(pin->base, pin->pin);
  } else if( pin->config == CONFIG_OUTPUT) {
    MAP_GPIOPinTypeGPIOOutput(pin->base, pin->pin);
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
    if( pins[l].config == CONFIG_NOT_USED) {
      memcpy(buf+i, bnone, sizeof(bnone)-1);
      i+= sizeof(bnone)-1;
    } else if( pins[l].config == CONFIG_OUTPUT ) {
      if( MAP_GPIOPinRead(pins[l].base, pins[l].pin) == 0x00 ) {
	buf[i++] = '2';
      } else {
	buf[i++] = '3';
      }
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

bool parse_path(char *path, struct header_pin **connector, uint8_t *pin,
		char *value) {
  char buf[20];
  struct header_pin *con;
  int i,l;
  for(i=0; path[i] != '\0' && path[i] != '.'; i++);

  memcpy(buf, path, i);
  buf[i] = '\0';

  printf("Port: %s\n", buf);
  l = i+1;
  if( l == '\0' ) {
    return false;
  }
  uint8_t index = (buf[0]-'0')-1;
  printf("c: %d\n", (buf[0]-'0')-1);

  UARTFlushTx(false);

  if( index > 3) {
    return false;
  }

  con = (struct header_pin*)headers[(buf[0]-'0')-1];
  printf("j1: %p\n", &j1);
  printf("j2: %p\n", &j2);
  printf("j3: %p\n", &j3);
  printf("connector: %p\n", con);
  for(; path[i] != '\0' && path[i] != '/'; i++);

  memcpy(buf, path+l, i-l);
  buf[i-l] = '\0';
  printf("Pin: %s\n", buf);

  l = i+1;
  *pin = atoi(buf)-1; //(buf[0] - '0')-1;
  if( l == '\0' ) {
    return false;
  }
  for(; path[i] != '\0'; i++);

  memcpy(value, path+l, i-l);
  value[i-l] = '\0';
  printf("pin: %d\n", *pin);
  printf("Value: '%s'\n", value);

  *connector = con;

  return true;
}
