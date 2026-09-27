#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include "transport.h"

#define TX_PERIOD_MS 1000
#define MAX_NODES    3

static void encode_msg(uint8_t *buf, uint8_t node_id, uint32_t counter)
{
    buf[0] = node_id;
    sys_put_le32(counter, &buf[1]);
}

static void decode_msg(const uint8_t *buf, uint8_t *node_id, uint32_t *counter)
{
    *node_id = buf[0];
    *counter = sys_get_le32(&buf[1]);
}

void tx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint8_t msg[APP_MSG_LEN];
    uint32_t counter = 0;

    while (1) {
        counter++;
        encode_msg(msg, (uint8_t)CONFIG_APP_NODE_ID, counter);

        int ret = transport_send(msg, sizeof(msg));
        if (ret == 0) {
            printk("[TX] Da gui, counter = %u\n", counter);
        } else {
            printk("[TX] LOI gui, counter = %u (ret=%d)\n", counter, ret);
        }

        k_sleep(K_MSEC(TX_PERIOD_MS));
    }
}

void rx_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint8_t msg[APP_MSG_LEN];
    uint32_t last_seen[MAX_NODES] = {0};

    while (1) {
        int ret = transport_recv(msg, sizeof(msg), K_FOREVER);
        if (ret != 0) {
            continue;
        }

        uint8_t sender_id;
        uint32_t counter;
        decode_msg(msg, &sender_id, &counter);

        if (sender_id >= MAX_NODES) {
            continue; /* du lieu la, bo qua */
        }
        if (counter == last_seen[sender_id]) {
            continue; /* trung lap do BLE tu phat lai, khong phai gia tri moi */
        }
        last_seen[sender_id] = counter;

        printk("[RX] Nhan tu Node %u, counter = %u\n", sender_id, counter);
    }
}

/* Tao thread o trang thai suspended (SYS_FOREVER_MS) - danh thuc sau khi BLE san sang */
K_THREAD_DEFINE(tx_tid, 1024, tx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(rx_tid, 1024, rx_thread_entry, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

int main(void)
{
    printk("Node ID = %d\n", CONFIG_APP_NODE_ID);

    int err = transport_init();
    if (err) {
        printk("Loi khoi tao transport: %d\n", err);
        return 0;
    }

    k_thread_start(tx_tid);
    k_thread_start(rx_tid);

    return 0;
}