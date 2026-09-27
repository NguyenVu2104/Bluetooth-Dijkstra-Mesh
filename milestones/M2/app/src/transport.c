// phần boilerplate BLE cố định:
// #include <zephyr/bluetooth/bluetooth.h>
// #include "transport.h"

// static const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM(
//     BT_LE_ADV_OPT_NONE,   /* không connectable — đúng vai trò Broadcaster */
//     BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

// static const struct bt_le_scan_param scan_param = {
//     .type     = BT_LE_SCAN_TYPE_PASSIVE,
//     .options  = BT_LE_SCAN_OPT_NONE,
//     .interval = BT_GAP_SCAN_FAST_INTERVAL,
//     .window   = BT_GAP_SCAN_FAST_WINDOW,
// };

// static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
//                      uint8_t adv_type, struct net_buf_simple *buf)
// {
//     // TODO (bạn viết): bt_data_parse(buf, ...) tìm AD type BT_DATA_MANUFACTURER_DATA,
//     // copy nội dung + rssi vào 1 struct, k_msgq_put() vào tx_to_rx_q.
//     // KHÔNG printk dài hay xử lý logic nặng ở đây.
// }

// int transport_init(void)
// {
//     int err = bt_enable(NULL);
//     if (err) {
//         return err;
//     }
//     return bt_le_scan_start(&scan_param, scan_cb);
// }

// int transport_send(const uint8_t *data, size_t len)
// {
//     struct bt_data ad[] = {
//         BT_DATA(BT_DATA_MANUFACTURER_DATA, data, len),
//     };
//     // TODO (bạn quyết định): gọi bt_le_adv_start() lần đầu trong transport_init(),
//     // các lần sau dùng bt_le_adv_update_data() thay vì stop()+start() lại —
//     // xem câu hỏi 4 bên dưới.
//     return bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
// }

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include "transport.h"

K_MSGQ_DEFINE(rx_msgq, APP_MSG_LEN, 8, 4);

/* SỬA: adv_param giờ là CON TRỎ, không phải struct */
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
        /* Tu loc: bo qua neu byte dau (NODE_ID nguoi gui) trung voi chinh minh */
        if (data->data[0] != (uint8_t)CONFIG_APP_NODE_ID) {
            k_msgq_put(&rx_msgq, data->data, K_NO_WAIT);
        }
    }
    return true; /* tiep tuc quet cac AD structure khac neu co */
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

    if (!adv_started) {
        int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);  /* bỏ & */
        if (err == 0) {
            adv_started = true;
        }
        return err;
    }

    return bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
}

int transport_recv(uint8_t *buf, size_t buf_len, k_timeout_t timeout)
{
    if (buf_len < APP_MSG_LEN) {
        return -EINVAL;
    }
    return k_msgq_get(&rx_msgq, buf, timeout);
}