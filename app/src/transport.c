/**
 * @file transport.c
 * @brief Cài đặt tầng vận chuyển bằng BLE Broadcaster + Observer role.
 *        KHÔNG ĐỔI GÌ về logic so với M3 — toàn bộ ACK/retry/state machine
 *        của M4 nằm ở main.c, transport.c vẫn chỉ có nhiệm vụ gửi/nhận
 *        byte thô qua BLE, đúng như đã chốt trong bảng Decision/Rationale
 *        (mục "File placement").
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include "transport.h"
#include "packet.h"

#define ADV_INTERVAL_GUARD_MS 1300

/** M4: tăng số slot từ 8 (M3) lên 10 — RX thread giờ có thể bận tới
 *  ~9.6s cho 1 giao dịch ACK/retry, trong lúc đó các gói TX định kỳ
 *  (1s/lần) khác vẫn có thể tới và cần chỗ chứa tạm. CHÚ Ý: đây chỉ là
 *  giảm nguy cơ tràn hàng đợi trong phạm vi demo 3-node, không phải giải
 *  pháp triệt để — nếu RX thread bận đủ lâu, số gói đến trong lúc đó vẫn
 *  có thể vượt quá 10 slot. */
K_MSGQ_DEFINE(rx_msgq, APP_MSG_LEN, 10, 4);

static struct k_mutex send_lock;

static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    BT_LE_ADV_OPT_NONE,
    BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX,
    NULL);

static const struct bt_le_scan_param scan_param = {
    .type     = BT_LE_SCAN_TYPE_PASSIVE,
    .options  = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL,
    .window   = BT_GAP_SCAN_FAST_WINDOW,
};

static bool adv_started;

static bool data_cb(struct bt_data *data, void *user_data)
{
    ARG_UNUSED(user_data);

    if (data->type == BT_DATA_MANUFACTURER_DATA && data->data_len == APP_MSG_LEN) {
        const struct packet *pkt = (const struct packet *)data->data;

        if (pkt->src_id != (uint8_t)CONFIG_APP_NODE_ID) {
            k_msgq_put(&rx_msgq, data->data, K_NO_WAIT);
        }
    }
    return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                     uint8_t adv_type, struct net_buf_simple *buf)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(rssi);
    ARG_UNUSED(adv_type);

    bt_data_parse(buf, data_cb, NULL);
}

int transport_init(void)
{
    k_mutex_init(&send_lock);

    int err = bt_enable(NULL);
    if (err) {
        return err;
    }

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        return err;
    }

    adv_started = false;
    return 0;
}

int transport_send(const uint8_t *data, size_t len)
{
    struct bt_data ad[] = {
        BT_DATA(BT_DATA_MANUFACTURER_DATA, data, len),
    };
    int ret;

    k_mutex_lock(&send_lock, K_FOREVER);

    if (!adv_started) {
        ret = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
        if (ret == 0) {
            adv_started = true;
        }
    } else {
        ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
    }

    if (ret == 0) {
        k_sleep(K_MSEC(ADV_INTERVAL_GUARD_MS));
    }

    k_mutex_unlock(&send_lock);
    return ret;
}

int transport_recv(uint8_t *buf, size_t buf_len, k_timeout_t timeout)
{
    if (buf_len < APP_MSG_LEN) {
        return -EINVAL;
    }
    return k_msgq_get(&rx_msgq, buf, timeout);
}