/**
 * @file transport.h
 * @brief API tầng vận chuyển — đóng gói BLE advertising/scanning thành
 *        giao diện byte-buffer đơn giản cho lớp trên (main.c) sử dụng,
 *        không cần biết chi tiết BLE.
 */

#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <zephyr/kernel.h>
#include "packet.h"

/**
 * Kích thước buffer chuẩn cho mỗi lần gửi/nhận — bằng đúng sizeof(struct
 * packet) từ M3 trở đi (M2 dùng hằng số 5 riêng, đã bỏ).
 */
#define APP_MSG_LEN sizeof(struct packet)

/**
 * @brief Bật BLE, khởi động scanning nền. Phải gọi 1 lần trong main()
 *        trước khi bất cứ thread nào gọi transport_send()/transport_recv().
 * @return 0 nếu thành công, mã lỗi Zephyr (âm) nếu thất bại.
 */
int transport_init(void);

/**
 * @brief Gửi dữ liệu qua BLE advertising (lần đầu gọi bt_le_adv_start(),
 *        các lần sau gọi bt_le_adv_update_data() để không phải stop/start
 *        lại, tránh gián đoạn phát sóng). An toàn khi gọi đồng thời từ
 *        nhiều thread (bảo vệ bởi mutex nội bộ).
 * @param data Con trỏ tới dữ liệu cần gửi.
 * @param len  Số byte, phải <= APP_MSG_LEN.
 * @return 0 nếu thành công, mã lỗi Zephyr (âm) nếu thất bại.
 */
int transport_send(const uint8_t *data, size_t len);

/**
 * @brief Lấy 1 gói đã nhận từ message queue nội bộ (gói đã được BLE
 *        scan callback đẩy vào từ trước, sau khi tự lọc self-reception).
 * @param buf     Buffer đích, tối thiểu APP_MSG_LEN byte.
 * @param buf_len Kích thước buffer, dùng để kiểm tra an toàn.
 * @param timeout Thời gian chờ tối đa (vd K_FOREVER để block vô hạn).
 * @return 0 nếu nhận được, -EINVAL nếu buffer quá nhỏ, mã lỗi khác nếu timeout.
 */
int transport_recv(uint8_t *buf, size_t buf_len, k_timeout_t timeout);

#endif