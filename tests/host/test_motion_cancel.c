/* GdoLib - A library for controlling garage door openers.
 * Copyright (C) 2026 CircuitSetup
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "harness.h"

static void partial_open(void) {
    start_v2();
    CHECK_EQ(gdo_set_open_duration(14000), ESP_OK);
    CHECK_EQ(gdo_set_close_duration(14000), ESP_OK);
    opener_sends(0, GDO_DOOR_STATE_CLOSED);
    run();
    CHECK_EQ(gdo_door_move_to_target(5000), ESP_OK);
}

static void stop_door(void) { CHECK_EQ(gdo_door_stop(), ESP_OK); }
static void open_door(void) { CHECK_EQ(gdo_door_open(), ESP_OK); }
static void close_door(void) { CHECK_EQ(gdo_door_close(), ESP_OK); }
static void toggle_door(void) { CHECK_EQ(gdo_door_toggle(), ESP_OK); }

static int queued_action(gdo_door_action_t action) {
    int count = 0;
    for (int i = 0; i < s_queued_count; ++i) {
        count += s_queued[i].cmd == GDO_CMD_DOOR_ACTION && s_queued[i].nibble == action;
    }
    return count;
}

static int transmitted_action(gdo_door_action_t action) {
    int count = 0;
    for (int i = 0; i < fake_tx_count; ++i) {
        if (fake_tx_log[i].len != FRAME_SIZE) continue;
        uint32_t rolling, data;
        uint64_t fixed;
        CHECK_EQ(decode_wireline(fake_tx_log[i].bytes, &rolling, &fixed, &data), 0);
        const uint32_t cmd = ((fixed >> 24) & 0xf00) | (data & 0xff);
        count += cmd == GDO_CMD_DOOR_ACTION && ((data >> 8) & 0xf) == action;
    }
    return count;
}

static void test_stop_then_open_cancels_old_position_stop(void) {
    partial_open();
    opener_sends(1000, GDO_DOOR_STATE_OPENING);
    add_step(2000)->before = stop_door;
    opener_sends(2200, GDO_DOOR_STATE_STOPPED);
    add_step(3000)->before = open_door;
    opener_sends(3200, GDO_DOOR_STATE_OPENING);
    wait_until(9000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_STOP), 2); // only the explicit Stop
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 2);
}

static void test_full_open_cancels_position_stop_while_already_opening(void) {
    partial_open();
    opener_sends(1000, GDO_DOOR_STATE_OPENING);
    add_step(2000)->before = open_door;
    wait_until(9000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_STOP), 0);
}

static void start_toggle_dance(void) {
    start_v2();
    gdo_set_toggle_only(true);
    g_status.door = GDO_DOOR_STATE_STOPPED;
    g_status.door_position = 7000;
    g_status.last_move_direction = GDO_DOOR_STATE_OPENING;
    CHECK_EQ(gdo_door_open(), ESP_OK);
}

static void test_stop_cancels_pending_toggle_dance_while_stopped(void) {
    start_toggle_dance();
    add_step(100)->before = stop_door;
    wait_until(2000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_TOGGLE), 2);
}

static void test_close_supersedes_pending_toggle_dance(void) {
    start_toggle_dance();
    add_step(100)->before = close_door;
    wait_until(2000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_TOGGLE), 4);
}

static void test_toggle_supersedes_pending_toggle_dance(void) {
    start_toggle_dance();
    add_step(100)->before = toggle_door;
    wait_until(2000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_TOGGLE), 4);
}

static void test_partial_move_keeps_its_own_toggle_dance_and_stop(void) {
    start_v2();
    gdo_set_toggle_only(true);
    g_status.door = GDO_DOOR_STATE_STOPPED;
    g_status.door_position = 7000;
    g_status.last_move_direction = GDO_DOOR_STATE_OPENING;
    CHECK_EQ(gdo_set_open_duration(10000), ESP_OK);
    CHECK_EQ(gdo_set_close_duration(10000), ESP_OK);
    CHECK_EQ(gdo_door_move_to_target(3000), ESP_OK);
    wait_until(6000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_TOGGLE), 6);
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_STOP), 2);
}

static void test_door_stop_preserves_light_release(void) {
    start_v1();
    CHECK_EQ(gdo_light_on(), ESP_OK);
    CHECK_EQ(gdo_door_stop(), ESP_OK);
    wait_until(1000);
    run();
    CHECK_EQ(queued(V1_CMD_TOGGLE_LIGHT_PRESS), 1);
    CHECK_EQ(queued(V1_CMD_TOGGLE_LIGHT_RELEASE), 1);
}

static void test_cancel_preserves_release_of_transmitted_v2_press(void) {
    start_toggle_dance();
    add_step(525)->before = stop_door; // first delayed press at 500, release at 550
    wait_until(2000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_TOGGLE), 4);
}

static void test_cancel_preserves_release_of_transmitted_v1_press(void) {
    start_v1();
    gdo_set_toggle_only(true);
    g_status.door = GDO_DOOR_STATE_STOPPED;
    g_status.last_move_direction = GDO_DOOR_STATE_OPENING;
    CHECK_EQ(gdo_door_open(), ESP_OK);
    add_step(525)->before = stop_door;
    wait_until(2000);
    run();
    CHECK_EQ(queued(V1_CMD_TOGGLE_DOOR_PRESS), 2);
    CHECK_EQ(queued(V1_CMD_TOGGLE_DOOR_RELEASE), 2);
}

static void test_failed_dance_setup_cancels_first_timer(void) {
    start_v2();
    gdo_set_toggle_only(true);
    g_status.door = GDO_DOOR_STATE_STOPPED;
    g_status.last_move_direction = GDO_DOOR_STATE_OPENING;
    fake_timer_create_fail_after = 1;
    CHECK_EQ(gdo_door_open(), ESP_ERR_NO_MEM);
    fake_timer_create_fail_after = -1;
    wait_until(2000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_TOGGLE), 0);
}

static void test_sync_timeout_keeps_previous_position_stop(void) {
    partial_open();
    gdo_sync_task(NULL); // execute the real failed diagnostic-sync completion
    CHECK(!g_status.synced);
    g_status.synced = true;
    wait_until(9000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_STOP), 2);
}

static void test_stop_before_first_motion_feedback_cancels_queued_start(void) {
    partial_open();
    CHECK_EQ(g_status.door, GDO_DOOR_STATE_CLOSED);
    CHECK_EQ(gdo_door_stop(), ESP_OK);
    wait_until(9000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 0);
}

static void test_stop_after_start_transmits_before_motion_feedback(void) {
    partial_open();
    wait_until(100);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 2);
    CHECK_EQ(g_status.door, GDO_DOOR_STATE_CLOSED);
    CHECK_EQ(gdo_door_stop(), ESP_OK);
    wait_until(9000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 2);
}

static void test_failed_close_keeps_previous_position_stop(void) {
    partial_open();
    opener_sends(1000, GDO_DOOR_STATE_OPENING);
    run();
    while (gdo_light_toggle() == ESP_OK) {}
    CHECK_EQ(gdo_door_close(), ESP_ERR_NO_MEM);
    xQueueReset(gdo_tx_queue);
    xQueueReset(gdo_event_queue);
    wait_until(9000);
    run();
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_STOP), 2);
}

static void test_near_noop_cancels_untransmitted_v1_move_and_its_stop(void) {
    start_v1();
    gdo_set_toggle_only(true);
    CHECK_EQ(gdo_set_open_duration(14000), ESP_OK);
    CHECK_EQ(gdo_set_close_duration(14000), ESP_OK);
    opener_v1(0, V1_CMD_QUERY_DOOR_STATUS, 0x05);
    run();
    CHECK_EQ(gdo_door_move_to_target(5000), ESP_OK);
    CHECK_EQ(gdo_door_move_to_target(9900), ESP_OK);
    wait_until(9000);
    run();
    int presses = 0;
    for (int i = 0; i < fake_tx_count; ++i) {
        presses += fake_tx_log[i].len == 1 && fake_tx_log[i].bytes[0] == V1_CMD_TOGGLE_DOOR_PRESS;
    }
    CHECK_EQ(presses, 0);
}

static void test_close_replaces_transmitted_open_before_feedback(void) {
    partial_open();
    wait_until(100);
    run();
    CHECK_EQ(g_status.door, GDO_DOOR_STATE_CLOSED);
    CHECK_EQ(gdo_door_close(), ESP_OK);
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_CLOSE), 2);
}

static void test_open_replaces_transmitted_close_before_feedback(void) {
    start_v2();
    CHECK_EQ(gdo_set_open_duration(14000), ESP_OK);
    CHECK_EQ(gdo_set_close_duration(14000), ESP_OK);
    opener_sends(0, GDO_DOOR_STATE_OPEN);
    run();
    CHECK_EQ(gdo_door_move_to_target(5000), ESP_OK);
    wait_until(100);
    run();
    CHECK_EQ(g_status.door, GDO_DOOR_STATE_OPEN);
    CHECK_EQ(gdo_door_open(), ESP_OK);
    CHECK_EQ(queued_action(GDO_DOOR_ACTION_OPEN), 2);
}

static bool interrupt_scheduled_stop;
static void supersede_during_enqueue(QueueHandle_t queue, const void *item) {
    on_queue_send(queue, item);
    if (queue == gdo_tx_queue && interrupt_scheduled_stop &&
        last_queued(GDO_CMD_DOOR_ACTION)->nibble == GDO_DOOR_ACTION_STOP) {
        interrupt_scheduled_stop = false;
        CHECK_EQ(gdo_door_open(), ESP_OK);
    }
}

static void test_superseded_timer_command_is_discarded_after_enqueue(void) {
    partial_open();
    fake_on_queue_send = supersede_during_enqueue;
    interrupt_scheduled_stop = true;
    opener_sends(1000, GDO_DOOR_STATE_OPENING);
    wait_until(9000);
    run();
    CHECK(!interrupt_scheduled_stop);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 0);
}

static void partial_close(void) {
    start_v2();
    CHECK_EQ(gdo_set_open_duration(10000), ESP_OK);
    CHECK_EQ(gdo_set_close_duration(10000), ESP_OK);
    opener_sends(0, GDO_DOOR_STATE_OPEN);
    run();
    CHECK_EQ(gdo_door_move_to_target(5000), ESP_OK);
    opener_sends(1000, GDO_DOOR_STATE_CLOSING);
}

static void test_stop_cancels_pending_close_verification(void) {
    partial_close();
    add_step(6400)->before = stop_door;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    run();
    CHECK_EQ(queued(GDO_CMD_GET_STATUS), 0);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 0);
}

static void test_stop_cancels_pending_close_verify_reply(void) {
    partial_close();
    add_step(6600)->before = stop_door;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    run();
    CHECK_EQ(queued(GDO_CMD_GET_STATUS), 1);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 0);
}

static void test_open_cancels_recovery_stop(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    add_step(8400)->before = open_door;
    wait_until(11000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 2);
    CHECK_EQ(g_status.door_target, 0);
}

static void supersede_recovery_reverse(QueueHandle_t queue, const void *item) {
    on_queue_send(queue, item);
    if (queue == gdo_tx_queue && last_queued(GDO_CMD_DOOR_ACTION)->nibble == GDO_DOOR_ACTION_OPEN) {
        fake_on_queue_send = on_queue_send;
        CHECK_EQ(gdo_door_stop(), ESP_OK);
    }
}

static void test_stop_cancels_queued_recovery_reverse(void) {
    partial_close();
    fake_on_queue_send = supersede_recovery_reverse;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    wait_until(8000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 0);
}

static void fail_replacement_open(void) {
    while (gdo_light_toggle() == ESP_OK) {}
    CHECK_EQ(gdo_door_open(), ESP_ERR_NO_MEM);
    xQueueReset(gdo_tx_queue);
    xQueueReset(gdo_event_queue);
}

static void test_failed_replacement_keeps_close_recovery_and_target(void) {
    partial_close();
    add_step(6600)->before = fail_replacement_open;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    wait_until(11000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 2);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
    CHECK_EQ(g_status.door_target, 5000);
}

static void fill_queue_after_recovery_press(QueueHandle_t queue, const void *item) {
    on_queue_send(queue, item);
    if (queue == gdo_tx_queue && last_queued(GDO_CMD_DOOR_ACTION)->nibble == GDO_DOOR_ACTION_OPEN) {
        fake_on_queue_send = on_queue_send;
        while (gdo_light_toggle() == ESP_OK) {}
    }
}

static void test_recovery_release_failure_keeps_recovery_stop(void) {
    partial_close();
    fake_on_queue_send = fill_queue_after_recovery_press;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    wait_until(11000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 1);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
}

static void hold_bus(void) { fake_gpio_level = 1; }
static void release_bus(void) { fake_gpio_level = 0; }
static void poll_status(void) { CHECK_EQ(get_status(), ESP_OK); }
static void exhaust_timer_allocations(void) { fake_timer_create_fail_after = 0; }

static void test_recovery_stop_transmits_when_pacing_timer_allocation_fails(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    add_step(8230)->before = poll_status;
    add_step(8240)->before = exhaust_timer_allocations;
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    wait_until(8500);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
}

static void test_recovery_stop_transmits_after_collision_without_timer_allocations(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    add_step(8230)->before = hold_bus;
    add_step(8240)->before = exhaust_timer_allocations;
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    add_step(8400)->before = release_bus;
    wait_until(8700);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
}

static void terminal_status_cancels_queued_reverse(gdo_door_state_t state) {
    partial_close();
    add_step(6600)->before = hold_bus;
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(6800, state);
    add_step(7000)->before = release_bus;
    wait_until(9000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 0);
    CHECK_EQ(g_status.door, state);
}

static void test_stopped_cancels_queued_recovery_reverse(void) {
    terminal_status_cancels_queued_reverse(GDO_DOOR_STATE_STOPPED);
}

static void test_closed_cancels_queued_recovery_reverse(void) {
    terminal_status_cancels_queued_reverse(GDO_DOOR_STATE_CLOSED);
}

static void test_late_stopped_preserves_transmitted_recovery_stop(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(6720, GDO_DOOR_STATE_STOPPED);
    opener_sends(8250, GDO_DOOR_STATE_OPENING);
    wait_until(11000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 2);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
}

static void test_stop_after_late_stopped_brakes_transmitted_recovery(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(6720, GDO_DOOR_STATE_STOPPED);
    add_step(7000)->before = stop_door;
    wait_until(7400);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_OPEN), 2);
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
}

static void test_full_open_finishes_recovery_without_opening_report(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(10000, GDO_DOOR_STATE_OPEN);
    opener_sends(12000, GDO_DOOR_STATE_CLOSING);
    opener_sends(16000, GDO_DOOR_STATE_OPENING);
    wait_until(18000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 2);
    CHECK_EQ(g_status.door_target, 0);
}

static void test_unconfirmed_reverse_expires_before_later_wall_motion(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    opener_sends(6720, GDO_DOOR_STATE_STOPPED);
    wait_until(12000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);

    opener_sends(20000, GDO_DOOR_STATE_OPENING);
    wait_until(23000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 4);
    CHECK_EQ(g_status.door_target, 0);
}

static void test_open_cancels_unconfirmed_reverse_timeout(void) {
    partial_close();
    opener_sends(6700, GDO_DOOR_STATE_CLOSING);
    add_step(7000)->before = open_door;
    wait_until(13000);
    run();
    CHECK_EQ(transmitted_action(GDO_DOOR_ACTION_STOP), 2);
}

RUN_TESTS(
    TEST(test_stop_then_open_cancels_old_position_stop),
    TEST(test_full_open_cancels_position_stop_while_already_opening),
    TEST(test_stop_cancels_pending_toggle_dance_while_stopped),
    TEST(test_close_supersedes_pending_toggle_dance),
    TEST(test_toggle_supersedes_pending_toggle_dance),
    TEST(test_partial_move_keeps_its_own_toggle_dance_and_stop),
    TEST(test_door_stop_preserves_light_release),
    TEST(test_cancel_preserves_release_of_transmitted_v2_press),
    TEST(test_cancel_preserves_release_of_transmitted_v1_press),
    TEST(test_failed_dance_setup_cancels_first_timer),
    TEST(test_sync_timeout_keeps_previous_position_stop),
    TEST(test_stop_before_first_motion_feedback_cancels_queued_start),
    TEST(test_stop_after_start_transmits_before_motion_feedback),
    TEST(test_failed_close_keeps_previous_position_stop),
    TEST(test_near_noop_cancels_untransmitted_v1_move_and_its_stop),
    TEST(test_close_replaces_transmitted_open_before_feedback),
    TEST(test_open_replaces_transmitted_close_before_feedback),
    TEST(test_superseded_timer_command_is_discarded_after_enqueue),
    TEST(test_stop_cancels_pending_close_verification),
    TEST(test_stop_cancels_pending_close_verify_reply),
    TEST(test_open_cancels_recovery_stop),
    TEST(test_stop_cancels_queued_recovery_reverse),
    TEST(test_failed_replacement_keeps_close_recovery_and_target),
    TEST(test_recovery_release_failure_keeps_recovery_stop),
    TEST(test_stopped_cancels_queued_recovery_reverse),
    TEST(test_closed_cancels_queued_recovery_reverse),
    TEST(test_late_stopped_preserves_transmitted_recovery_stop),
    TEST(test_stop_after_late_stopped_brakes_transmitted_recovery),
    TEST(test_full_open_finishes_recovery_without_opening_report),
    TEST(test_unconfirmed_reverse_expires_before_later_wall_motion),
    TEST(test_open_cancels_unconfirmed_reverse_timeout),
    TEST(test_recovery_stop_transmits_when_pacing_timer_allocation_fails),
    TEST(test_recovery_stop_transmits_after_collision_without_timer_allocations)
)
