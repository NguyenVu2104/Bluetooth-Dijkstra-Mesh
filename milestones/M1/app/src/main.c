// phần boilerplate BLE cố định:
// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>

// #define QUEUE_MSG_MAX   4          // sức chứa queue — tùy bạn chỉnh
// #define TX_PERIOD_MS    1000       // chu kỳ TX tự sinh dummy data

// struct dummy_msg {
//     uint32_t counter;
// };

// K_MSGQ_DEFINE(tx_to_rx_q, sizeof(struct dummy_msg), QUEUE_MSG_MAX, 4);

// void tx_thread_entry(void *p1, void *p2, void *p3)
// {
//     // TODO: vòng lặp — mỗi TX_PERIOD_MS: tạo dummy_msg, log, k_msgq_put vào tx_to_rx_q
// }

// void rx_thread_entry(void *p1, void *p2, void *p3)
// {
//     // TODO: vòng lặp — k_msgq_get blocking từ tx_to_rx_q, log giá trị nhận được
// }

// K_THREAD_DEFINE(tx_tid, 1024, tx_thread_entry, NULL, NULL, NULL, 7, 0, 0);
// K_THREAD_DEFINE(rx_tid, 1024, rx_thread_entry, NULL, NULL, NULL, 7, 0, 0);

// int main(void)
// {
//     printk("Node ID = %d\n", CONFIG_APP_NODE_ID);
//     return 0;
// }

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define QUEUE_MSG_MAX   4          // sức chứa queue — tùy bạn chỉnh
#define TX_PERIOD_MS    1000       // chu kỳ TX tự sinh dummy data

struct dummy_msg {
    uint32_t counter;
};

K_MSGQ_DEFINE(tx_to_rx_q, sizeof(struct dummy_msg), QUEUE_MSG_MAX, 4);

void tx_thread_entry(void *p1, void *p2, void *p3)
{
    struct dummy_msg msg = {0};

    while (1) {
        msg.counter++;
        int ret = k_msgq_put(&tx_to_rx_q, &msg, K_NO_WAIT);

        if (ret == 0) {
            printk("[TX] Da gui goi tin, counter = %u\n", msg.counter);
        } else {
            printk("[TX] LOI: queue day, khong gui duoc counter = %u (ret=%d)\n",
                   msg.counter, ret);
        }

        k_sleep(K_MSEC(TX_PERIOD_MS));
    }
}

void rx_thread_entry(void *p1, void *p2, void *p3)
{
    struct dummy_msg msg;

    while (1) {
        k_msgq_get(&tx_to_rx_q, &msg, K_FOREVER);
        
        printk("[RX] Da nhan goi tin, counter = %u\n", msg.counter);
    }
}

K_THREAD_DEFINE(tx_tid, 1024, tx_thread_entry, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(rx_tid, 1024, rx_thread_entry, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
    printk("Node ID = %d\n", CONFIG_APP_NODE_ID);
    return 0;
}