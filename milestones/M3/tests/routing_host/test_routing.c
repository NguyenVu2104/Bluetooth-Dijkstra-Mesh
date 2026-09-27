/**
 * @file test_routing.c
 * @brief Unit test độc lập cho routing_next_hop(), chạy trên host bằng
 *        gcc thường — không cần Zephyr/Renode. Mục đích: nếu demo M3 trên
 *        Renode không ra kết quả đúng, chạy test này trước để xác định
 *        lỗi nằm ở thuật toán Dijkstra hay ở tầng BLE/transport.
 */

#include <stdio.h>
#include <assert.h>
#include "routing.h"

/* Macro nhỏ tự chế thay vì dùng framework test ngoài, giữ đúng tinh
 * thần "simple + working" của dự án cho một việc chỉ chạy vài lần. */
#define CHECK(mo_ta, thuc_te, mong_doi) do { \
    printf("[%s] mong doi=%u, thuc te=%u -> %s\n", mo_ta, \
           (unsigned)(mong_doi), (unsigned)(thuc_te), \
           (thuc_te) == (mong_doi) ? "PASS" : "FAIL"); \
    assert((thuc_te) == (mong_doi)); \
} while (0)

int main(void)
{
    routing_init();

    /* A(0)->C(2) bắt buộc qua B(1) vì A-C không có cạnh trực tiếp */
    CHECK("A->C next-hop", routing_next_hop(0, 2), 1);

    /* B(1)->C(2) là láng giềng trực tiếp */
    CHECK("B->C next-hop", routing_next_hop(1, 2), 2);

    /* C(2)->A(0) đối xứng với A->C, cũng phải qua B */
    CHECK("C->A next-hop", routing_next_hop(2, 0), 1);

    /* A(0)->B(1) là láng giềng trực tiếp */
    CHECK("A->B next-hop", routing_next_hop(0, 1), 1);

    /* Gọi dst_id == self_id là ngữ cảnh sai, phải trả về INVALID */
    CHECK("self->self tra ve invalid", routing_next_hop(0, 0), ROUTING_INVALID_NODE);

    /* Mô phỏng trước cho M4: ngắt cạnh A-B, A(0)->C(2) lúc này phải
     * KHÔNG còn route vì B là đường duy nhất tới C. */
    routing_set_cost(0, 1, ROUTING_COST_INF);
    CHECK("A->C sau khi ngat A-B", routing_next_hop(0, 2), ROUTING_INVALID_NODE);

    printf("\nTat ca test case PASS.\n");
    return 0;
}