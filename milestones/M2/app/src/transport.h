#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <zephyr/kernel.h>

/* Quy uoc payload M2: byte 0 = NODE_ID nguoi gui, byte 1-4 = counter (little-endian).
 * Se duoc thay the bang struct packet chinh thuc o M3. */
#define APP_MSG_LEN 5

int transport_init(void);
int transport_send(const uint8_t *data, size_t len);
int transport_recv(uint8_t *buf, size_t buf_len, k_timeout_t timeout);

#endif