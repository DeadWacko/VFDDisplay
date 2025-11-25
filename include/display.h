#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

// Display constants
#define DISPLAY_MAX_DIGITS         10  // Maximum number of digits supported
#define DISPLAY_BRIGHTNESS_MIN     0   // Minimum brightness level (0%)
#define DISPLAY_BRIGHTNESS_MAX     100 // Maximum brightness level (100%)
#define DISPLAY_DIGIT_COUNT_MIN    4   // Minimum number of digits in display
#define DISPLAY_DIGIT_COUNT_MAX    10  // Maximum number of digits in display

// Segment codes (accessible externally)
#define SEGMENT_CODE_CLEAR      10     // Code for blank digit
#define SEGMENT_CODE_DOT        11     // Code for decimal point
#define SEGMENT_CODE_E          12     // Code for 'E' character (error indication)
#define SEGMENT_CODE_UNDERSCORE 13     // Code for '_' character (underscore)
#define SEGMENT_CODE_DASH       14     // Code for '-' character (dash for negative numbers)

// Display operating modes
typedef enum {
    DISPLAY_MODE_TIME = 0,  // Display shows real-time clock
    DISPLAY_MODE_EFF        // Display shows an effect or static content
} display_mode_t;

extern volatile display_mode_t g_display_mode; // Current display mode (global)

// Configuration structure for display initialization
typedef struct {
    uint8_t data_pin;           // GPIO pin for data line
    uint8_t clock_pin;          // GPIO pin for clock line
    uint8_t latch_pin;          // GPIO pin for latch line
    uint8_t light_sensor_adc;   // ADC pin for light sensor (26-29)
    uint8_t digit_count;        // Number of digits in the display (4-10)
    uint16_t fast_refresh_rate; // Refresh rate in Hz for multiplexing
    uint8_t night_start_hour;   // Start hour of night mode (0-23)
    uint8_t night_end_hour;     // End hour of night mode (0-23)
    void (*on_effect_complete)(void); // Callback function called when an effect finishes
} display_config_t;

// Initializes the display with the provided configuration
// Returns true on success, false on invalid config or failure
bool display_init(const display_config_t *config);

// Returns true if display is initialized and active
bool display_is_initialized(void);

// Starts periodic time updates on the display (RTC-based)
// Returns true on success, false if already active or timer fails
bool display_start_time_updates(void);

// Enables/disables night mode (low brightness based on night_start_hour and night_end_hour)
void display_enable_night_mode(bool enable);

// Enables/disables adaptive brightness based on light sensor
void display_enable_adaptive_brightness(bool enable);

// Enables/disables manual brightness control (overrides adaptive/night mode)
void display_enable_manual_brightness(bool enable);

// Sets global brightness level (0-100), applied if manual mode is enabled
void display_set_brightness(uint8_t brightness);

// Sets individual brightness for each digit (array of 0-100 values)
// Returns true on success, false if array is NULL or display not initialized
bool display_set_digit_brightness(const uint8_t *brightness_array);

// Returns a pointer to the current digit brightness array (read-only)
const uint8_t* display_get_digit_brightness(void);

// Enables/disables blinking of the decimal point (e.g., for seconds)
void display_set_dot_blinking(bool enable);

// Prints a static string to the display (supports 0-9, '.', 'E', '_', '-')
void display_print(const char *text);

// Prints an integer number to the display (supports negative numbers)
void display_print_digital(int number);

// Displays an error code in format "E_X" where X is the code (0-9)
void display_error(uint8_t code);

// Scrolls a string of digits across the display
void display_scrolling_digits(const char *digits);

// Morphs current display value to a target number over a duration with specified steps
// Returns true on success, false if duration or steps are invalid or timer fails
bool display_morph_to(int target, uint32_t duration_ms, int steps);

// Runs a boot animation effect for the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_boot_animation(uint32_t duration_ms);

// Runs a Wi-Fi connecting animation for the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_connecting_wifi(uint32_t duration_ms);

// Runs an NTP sync animation for the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_ntp_sync(uint32_t duration_ms);

// Runs a pulsing brightness effect for the specified duration and cycles
// Returns true on success, false if duration or cycles is 0 or timer fails
bool display_effect_pulse(uint32_t duration_ms, uint8_t cycles);

// Fades out the display brightness over the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_fade_out(uint32_t duration_ms);

// Fades in the display brightness over the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_fade_in(uint32_t duration_ms);

// Runs a brightness wave effect across digits for the specified duration
// Returns true on success, false if duration is 0 or timer fails
bool display_effect_brightness_wave(uint32_t duration_ms);

// Runs a dynamic flicker effect during time mode with random intervals
// Returns true on success, false if intervals/duration are invalid or timer fails
bool display_effect_dynamic_flicker(uint32_t min_interval_ms, uint32_t max_interval_ms, uint32_t flicker_duration_ms);

// "Matrix rain" brightness effect: random bright digits with smooth fade-out
bool display_effect_matrix_rain(uint32_t duration_ms, uint32_t frame_ms);


// Returns true if flicker effect has been triggered at least once
bool display_flicker_triggered(void);

// Stops the dynamic flicker effect immediately
void display_effect_stop_flicker(void);

// Stops all active effects, resets digit brightness to max, and returns to time mode
void display_stop_all_effects(void);

// Returns a pointer to the current display buffer (read-only, volatile)
const volatile uint8_t* display_get_buffer(void);



#endif // DISPLAY_H