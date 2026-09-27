/**
 * @file routing.c
 * @brief Cài đặt Dijkstra generic O(N^2) — vòng lặp thuật toán không đổi
 *        gì so với M3. Điểm mới của M4 là graph có thêm 1 bản sao "cost
 *        gốc" để nhớ hồi phục về đâu, cùng 2 hàm chuyên biệt để tăng và
 *        hồi phục cost thay vì để main.c tự gán 1 con số tuỳ ý.
 */

#include "routing.h"
#include <stdbool.h>
#include <string.h>

/** Ma trận kề hiện tại — cái Dijkstra thực sự đọc mỗi lần được gọi. Cost
 *  có thể thay đổi theo thời gian từ M4 (miss làm tăng, success làm hồi
 *  phục). */
static uint8_t graph[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT];

/** Ma trận kề "gốc" — chụp lại đúng lúc routing_init(), không bao giờ
 *  đổi sau đó. Cần lưu riêng vì một khi graph[] đã bị miss ghi đè nhiều
 *  lần (vd 1 -> 2 -> 4 -> 8), không có cách nào tự suy ngược lại được
 *  "giá trị ban đầu là bao nhiêu" nếu không có bản sao này. */
static uint8_t base_graph[ROUTING_NODE_COUNT][ROUTING_NODE_COUNT];

void routing_set_cost(uint8_t node_a, uint8_t node_b, uint8_t cost)
{
    /* Graph vô hướng — cost phải đối xứng cả 2 chiều */
    graph[node_a][node_b] = cost;
    graph[node_b][node_a] = cost;
}

/** Ghi cost gốc cho 1 cạnh vào base_graph — chỉ dùng nội bộ lúc
 *  routing_init(). Không cần expose ra ngoài vì main.c không bao giờ
 *  cần biết "cost gốc là bao nhiêu", chỉ cần gọi record_miss/
 *  record_success và để routing.c tự lo phần còn lại. */
static void set_base_cost(uint8_t node_a, uint8_t node_b, uint8_t cost)
{
    base_graph[node_a][node_b] = cost;
    base_graph[node_b][node_a] = cost;
}

void routing_init(void)
{
    for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
        for (int j = 0; j < ROUTING_NODE_COUNT; j++) {
            graph[i][j] = (i == j) ? 0 : ROUTING_COST_INF;
            base_graph[i][j] = graph[i][j];
        }
    }

    /* Topology M4: A=0, B=1, C=2. So với M3, thêm cạnh A-C cost=5 để khi
     * A-B xuống cấp, Dijkstra có 1 đường thay thế thật sự thay vì chỉ
     * mất route hoàn toàn — đây chính là điều kiện để chứng minh
     * "rerouting" thay vì chỉ "phát hiện mất route". */
    routing_set_cost(0, 1, 1);
    set_base_cost(0, 1, 1);

    routing_set_cost(1, 2, 1);
    set_base_cost(1, 2, 1);

    routing_set_cost(0, 2, 5);
    set_base_cost(0, 2, 5);
}

void routing_record_miss(uint8_t node_a, uint8_t node_b)
{
    /* Dùng uint16_t để nhân tạm thời, tránh tràn số uint8_t trước khi
     * kịp so sánh với trần ROUTING_COST_INF. */
    uint16_t doubled = (uint16_t)graph[node_a][node_b] * 2;

    if (doubled >= ROUTING_COST_INF) {
        doubled = ROUTING_COST_INF;
    }
    routing_set_cost(node_a, node_b, (uint8_t)doubled);
}

void routing_record_success(uint8_t node_a, uint8_t node_b)
{
    /* Hồi phục thẳng về cost gốc — không giảm dần từng bước, đủ đơn
     * giản để minh hoạ ý tưởng "link tốt trở lại" trong phạm vi M4. */
    routing_set_cost(node_a, node_b, base_graph[node_a][node_b]);
}

uint8_t routing_get_cost(uint8_t node_a, uint8_t node_b)
{
    return graph[node_a][node_b];
}

uint8_t routing_next_hop(uint8_t self_id, uint8_t dst_id)
{
    uint8_t dist[ROUTING_NODE_COUNT];
    uint8_t prev[ROUTING_NODE_COUNT];
    bool visited[ROUTING_NODE_COUNT];

    for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
        dist[i] = ROUTING_COST_INF;
        prev[i] = ROUTING_INVALID_NODE;
        visited[i] = false;
    }
    dist[self_id] = 0;

    /* Vòng lặp Dijkstra chuẩn — không đổi gì so với M3. Nó luôn đọc
     * graph[] mới nhất tại thời điểm được gọi, nên tự động "thấy" mọi
     * thay đổi cost do routing_record_miss()/routing_record_success()
     * gây ra mà không cần biết gì về sự tồn tại của 2 hàm đó. */
    for (int iter = 0; iter < ROUTING_NODE_COUNT; iter++) {
        uint8_t u = ROUTING_INVALID_NODE;
        uint8_t best = ROUTING_COST_INF;

        for (int i = 0; i < ROUTING_NODE_COUNT; i++) {
            if (!visited[i] && dist[i] < best) {
                best = dist[i];
                u = i;
            }
        }
        if (u == ROUTING_INVALID_NODE) {
            break; /* Phần graph còn lại không tới được từ self_id */
        }
        visited[u] = true;

        for (int v = 0; v < ROUTING_NODE_COUNT; v++) {
            if (graph[u][v] == ROUTING_COST_INF) {
                continue;
            }
            uint16_t alt = (uint16_t)dist[u] + graph[u][v];
            if (alt < dist[v]) {
                dist[v] = (uint8_t)alt;
                prev[v] = u;
            }
        }
    }

    if (dst_id == self_id || dist[dst_id] == ROUTING_COST_INF) {
        return ROUTING_INVALID_NODE;
    }

    uint8_t path[ROUTING_NODE_COUNT];
    int path_len = 0;
    uint8_t cur = dst_id;

    while (cur != self_id) {
        path[path_len++] = cur;
        cur = prev[cur];
    }

    return path[path_len - 1];
}