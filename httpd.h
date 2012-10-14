#ifndef HTTPD_H
#define HTTPD_H

#include <stdint.h>

#define REQUEST_READ	1
#define REQUEST_WRITE	2

struct httpd_state {
  uint8_t	data_count;
  uint8_t	state;
  uint8_t	request_type;
  const uint8_t	*xmit_buf;
  uint8_t	xmit_buf_size;
};

void httpd_appcall(void);
void httpd_init(void);

#endif
