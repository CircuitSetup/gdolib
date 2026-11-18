/* GdoLib - A library for controlling garage door openers.
 * Copyright (C) 2024  Konnected Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * TX queue packets are stored inline, so saturating or draining the queue must
 * not allocate packet buffers.
 */

#include <stdlib.h>

static int s_live_allocs;

static void *counted_malloc(size_t n) {
    void *p = malloc(n);
    if (p) {
        s_live_allocs++;
    }
    return p;
}

static void counted_free(void *p) {
    if (p) {
        s_live_allocs--;
    }
    free(p);
}

#define malloc(n) counted_malloc(n)
#define free(p) counted_free(p)

#include "harness.h"

#define TX_QUEUE_LEN 16

static void test_v2_queue_full_does_not_allocate(void) {
    start_v2();
    int base = s_live_allocs;

    for (int i = 0; i < TX_QUEUE_LEN; i++) {
        CHECK_EQ(get_status(), ESP_OK);
    }
    CHECK_EQ(tx_queue_depth(), TX_QUEUE_LEN);
    CHECK_EQ(s_live_allocs - base, 0);

    CHECK_EQ(get_status(), ESP_ERR_NO_MEM);
    CHECK_EQ(get_status(), ESP_ERR_NO_MEM);
    CHECK_EQ(tx_queue_depth(), TX_QUEUE_LEN);
    CHECK_EQ(s_live_allocs - base, 0);
}

static void test_v1_queue_full_does_not_allocate(void) {
    start_v1();
    int base = s_live_allocs;

    for (int i = 0; i < TX_QUEUE_LEN; i++) {
        CHECK_EQ(queue_v1_command(V1_CMD_QUERY_DOOR_STATUS), ESP_OK);
    }
    CHECK_EQ(tx_queue_depth(), TX_QUEUE_LEN);
    CHECK_EQ(s_live_allocs - base, 0);

    CHECK_EQ(queue_v1_command(V1_CMD_QUERY_DOOR_STATUS), ESP_ERR_NO_MEM);
    CHECK_EQ(tx_queue_depth(), TX_QUEUE_LEN);
    CHECK_EQ(s_live_allocs - base, 0);
}

static void test_queued_packets_transmit_without_allocating(void) {
    start_v2();
    int base = s_live_allocs;

    CHECK_EQ(get_status(), ESP_OK);
    CHECK_EQ(get_openings(), ESP_OK);
    wait_until(500);
    run();

    CHECK_EQ(transmitted_count(), 2);
    CHECK_EQ(s_live_allocs - base, 0);
}

/* A pending V1 poll must be dropped when auto-detection switches to V2. */
static void test_v1_poll_is_discarded_after_switch_to_v2(void) {
    start_v1();
    CHECK_EQ(queue_v1_command(V1_CMD_QUERY_DOOR_STATUS), ESP_OK);

    g_status.protocol = GDO_PROTOCOL_SEC_PLUS_V2;
    CHECK_EQ(get_status(), ESP_OK);
    wait_until(500);
    run();

    CHECK_EQ(transmitted_count(), 1);
    CHECK_EQ(fake_uart_frames_written, 1);
    CHECK_EQ(tx_queue_depth(), 0);
}

static int s_busy_base;
static int s_sent_after_recovery;

static void clear_busy_bus(void) {
    CHECK_EQ(transmitted_count(), 0);
    CHECK_EQ(tx_queue_depth(), TX_QUEUE_LEN);
    CHECK_EQ(s_live_allocs - s_busy_base, 0);
    fake_gpio_level = 0;
}

static void check_bus_recovered(void) {
    s_sent_after_recovery = transmitted_count();
    CHECK(s_sent_after_recovery > 0);
    CHECK(tx_queue_depth() <= 1);
    CHECK_EQ(s_live_allocs - s_busy_base, 0);
}

/* #58: a busy Sec+ 1.0 bus fills the queue while the 250 ms timer polls.
 * Keep one main loop running so its collision retry can drain the queue. */
static void test_v1_busy_bus_stays_bounded_and_recovers(void) {
    start_v1();
    s_busy_base = s_live_allocs;

    esp_timer_create_args_t args = { .callback = v1_status_timer_cb, .name = "v1_status_timer" };
    esp_timer_handle_t poll;
    CHECK_EQ(esp_timer_create(&args, &poll), ESP_OK);
    CHECK_EQ(esp_timer_start_periodic(poll, 250 * 1000), ESP_OK);

    fake_gpio_level = 1;
    add_step(60000)->before = clear_busy_bus;
    add_step(70000)->before = check_bus_recovered;
    wait_until(75000);
    run();

    // Back to one poll per 250 ms tick.
    CHECK(transmitted_count() - s_sent_after_recovery >= 18);
}

RUN_TESTS(
    TEST(test_v2_queue_full_does_not_allocate),
    TEST(test_v1_queue_full_does_not_allocate),
    TEST(test_queued_packets_transmit_without_allocating),
    TEST(test_v1_poll_is_discarded_after_switch_to_v2),
    TEST(test_v1_busy_bus_stays_bounded_and_recovers)
)
