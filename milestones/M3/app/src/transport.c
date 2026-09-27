/**
 * @file transport.c
 * @brief Cài đặt tầng vận chuyển bằng BLE Broadcaster + Observer role
 *        (không dùng Central/Peripheral connection-based), khớp bản chất
 *        flooding/broadcast của mesh. Toàn bộ logic BLE chỉ tồn tại ở file
 *        này — main.c không bao giờ đụng trực tiếp vào BLE API.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include "transport.h"
#include "packet.h"

/* Sau khi ghi payload mới (start hoặc update), giữ mutex CHO TỚI KHI
 * chắc chắn đã có ít nhất 1 chu kỳ advertising thực sự phát sóng ra
 * (khoảng BT_GAP_ADV_SLOW_INT_MIN..MAX = ~1.0-1.2s). Nếu không có
 * bước chờ này, TX thread định kỳ và RX thread (khi forward) sẽ ghi
 * đè lên nhau liên tục trước khi payload cũ kịp phát sóng thật —
 * đây chính là nguyên nhân khiến gói forward 2-hop (A->C, C->A qua B)
 * mất 100% có hệ thống dù transport_send() luôn trả về thành công. */
#define ADV_INTERVAL_GUARD_MS 1300

/** Hàng đợi trung gian giữa BLE scan callback (chạy trong BT thread
 *  context) và RX thread của ứng dụng — mục đích là không xử lý nặng
 *  bên trong callback/ISR context. */
K_MSGQ_DEFINE(rx_msgq, APP_MSG_LEN, 8, 4);

/**
 * Bảo vệ mọi lời gọi transport_send() bằng mutex. Cần thiết từ M3 vì
 * cả TX thread (định kỳ) và RX thread (khi forward hộ gói) đều có thể
 * gọi hàm này — không có mutex sẽ có race điều kiện trên adv_started
 * flag và trên API adv của BT stack.
 */
static struct k_mutex send_lock;

/** Tham số advertising — con trỏ (BT_LE_ADV_PARAM trả về địa chỉ), khớp
 *  đúng API của Zephyr 3.7 cho bt_le_adv_start(). */
static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    BT_LE_ADV_OPT_NONE,
    BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX,
    NULL);

/** Passive scan là đủ — chỉ cần nghe adv packet, không cần scan request
 *  chủ động (active scan) vì không dùng scan response data. */
static const struct bt_le_scan_param scan_param = {
    .type     = BT_LE_SCAN_TYPE_PASSIVE,
    .options  = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL,
    .window   = BT_GAP_SCAN_FAST_WINDOW,
};

/** true sau lần gọi bt_le_adv_start() đầu tiên — quyết định lần gọi
 *  transport_send() kế tiếp dùng start hay update_data(). */
static bool adv_started;

/**
 * @brief Callback duyệt từng AD structure trong 1 gói adv nhận được.
 *        Chỉ quan tâm AD type Manufacturer Data đúng kích thước packet.
 */
static bool data_cb(struct bt_data *data, void *user_data)
{
    ARG_UNUSED(user_data);

    if (data->type == BT_DATA_MANUFACTURER_DATA && data->data_len == APP_MSG_LEN) {
        const struct packet *pkt = (const struct packet *)data->data;

        /* Tự lọc self-reception: BLE adv là broadcast vật lý, node sẽ
         * tình cờ nghe lại chính sóng mình phát ra. So src_id (nằm
         * trong struct packet) thay vì địa chỉ BLE/MAC để tránh rủi ro
         * Random Static Address thay đổi trong môi trường giả lập. */
        if (pkt->src_id != (uint8_t)CONFIG_APP_NODE_ID) {
            k_msgq_put(&rx_msgq, data->data, K_NO_WAIT);
        }
    }
    return true; /* tiếp tục duyệt các AD structure khác nếu có */
}

/** Callback gốc của bt_le_scan_start() — chỉ làm nhiệm vụ parse AD data,
 *  không xử lý logic nghiệp vụ (để ở RX thread, tránh chặn BT thread). */
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

    /* Khoá toàn bộ thao tác adv (kiểm tra flag + gọi API) thành 1 khối
     * atomic — nếu không, 2 thread có thể cùng thấy adv_started=false
     * và cùng gọi bt_le_adv_start(), gây lỗi trên BT stack. */
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
        /* Bat buoc cho du 1 chu ky advertising thuc su phat song
         * truoc khi tra lai mutex — day la diem khac biet quan trong
         * so voi ban truoc, sua dung nguyen nhan mat goi he thong. */
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