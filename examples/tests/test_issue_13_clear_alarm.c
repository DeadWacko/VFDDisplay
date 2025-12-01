/**
 * Minimal manual check for Issue 13 (clear alarm cancellation).
 *
 * Scenario:
 *   - init LL
 *   - start refresh (PWM clear alarms are scheduled)
 *   - wait a bit
 *   - stop refresh
 *   - deinit LL
 *   - keep running so a stray clear alarm would fire if it was not cancelled
 *
 * Expected:
 *   - no GPIO activity after deinit
 *   - ll_clear_cb must not be invoked after deinit
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "display_ll.h"

#define TEST_DATA_PIN   15
#define TEST_CLOCK_PIN  14
#define TEST_LATCH_PIN  13
#define TEST_DIGITS      4
#define TEST_REFRESH_HZ 120

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    printf("=== LL Issue 13 clear-alarm check ===\n");

    display_ll_config_t cfg = {
        .data_pin = TEST_DATA_PIN,
        .clock_pin = TEST_CLOCK_PIN,
        .latch_pin = TEST_LATCH_PIN,
        .digit_count = TEST_DIGITS,
        .refresh_rate_hz = TEST_REFRESH_HZ,
    };

    if (!display_ll_init(&cfg)) {
        printf("LL init failed\n");
        while (true) tight_loop_contents();
    }

    if (!display_ll_start_refresh()) {
        printf("LL refresh start failed\n");
        while (true) tight_loop_contents();
    }

    // Light up a digit so refresh loop schedules PWM clears.
    for (int i = 0; i < TEST_DIGITS; i++) {
        display_ll_set_digit_raw((uint8_t)i, 0x7F); // pwm ~ 50%
    }

    sleep_ms(500);

    printf("Stopping refresh...\n");
    display_ll_stop_refresh();

    printf("Deinit LL...\n");
    display_ll_deinit();

    // Stay alive: if a stray clear alarm fires after deinit, GPIO will flicker.
    while (true) {
        tight_loop_contents();
    }

    return 0;
}
