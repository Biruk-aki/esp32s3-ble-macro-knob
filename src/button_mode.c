#include "button_mode.h"
#include "ble_hid.h"
#include "status_led.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#define BTN_MODE_PIN        6       /* GPIO 6: Mode Cycle */
#define BTN_PLAY_PIN        5       /* GPIO 5: Play/Pause */
#define BTN_TRACK_PIN       4       /* GPIO 4: Secondary Action */
#define HOLD_THRESHOLD_MS   800

static const struct device *gpio_dev;
static controller_mode_t current_mode = MODE_MEDIA;

static int btn_mode_last = 1;
static int btn_play_last = 1;
static int btn_track_last = 1;
static int64_t track_press_time = 0;
static bool track_long_handled = false;

int button_mode_init(void)
{
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        printk("GPIO0 device not ready\n");
        return -ENODEV;
    }

    gpio_pin_configure(gpio_dev, BTN_MODE_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_PLAY_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_TRACK_PIN, GPIO_INPUT | GPIO_PULL_UP);

    return 0;
}

controller_mode_t button_mode_get_current(void)
{
    return current_mode;
}

void button_mode_process(void)
{
    int64_t now = k_uptime_get();

    int mode_val = gpio_pin_get(gpio_dev, BTN_MODE_PIN);
    int play_val = gpio_pin_get(gpio_dev, BTN_PLAY_PIN);
    int track_val = gpio_pin_get(gpio_dev, BTN_TRACK_PIN);

    /* Button 1 (GPIO 6): Cycle Modes & Update LED */
    if (mode_val == 0 && btn_mode_last == 1) {
        current_mode = (current_mode + 1) % MODE_COUNT;
        status_led_set_mode(current_mode);

        switch (current_mode) {
        case MODE_MEDIA:
            printk("\n>>> [MODE 1: MEDIA (LED: BLUE)] <<<\n");
            break;
        case MODE_NAVIGATION:
            printk("\n>>> [MODE 2: NAVIGATION (LED: GREEN)] <<<\n");
            break;
        case MODE_SYSTEM:
            printk("\n>>> [MODE 3: SYSTEM (LED: MAGENTA)] <<<\n");
            break;
        default:
            break;
        }
        k_msleep(150);
    }

    /* Button 2 (GPIO 5): Play / Pause */
    if (play_val == 0 && btn_play_last == 1) {
        printk("[BUTTON 2] PLAY / PAUSE\n");
        ble_hid_send_consumer_key(HID_KEY_PLAY_PAUSE);
        k_msleep(100);
    }

    /* Button 3 (GPIO 4): Mode-dependent action */
    if (current_mode == MODE_MEDIA) {
        if (track_val == 0 && btn_track_last == 1) {
            track_press_time = now;
            track_long_handled = false;
        } else if (track_val == 0 && btn_track_last == 0) {
            if (!track_long_handled && (now - track_press_time) >= HOLD_THRESHOLD_MS) {
                printk("[BUTTON 3] PREVIOUS TRACK\n");
                ble_hid_send_consumer_key(HID_KEY_PREV_TRACK);
                track_long_handled = true;
            }
        } else if (track_val == 1 && btn_track_last == 0) {
            if (!track_long_handled) {
                printk("[BUTTON 3] NEXT TRACK\n");
                ble_hid_send_consumer_key(HID_KEY_NEXT_TRACK);
            }
        }
    } else {
        if (track_val == 0 && btn_track_last == 1) {
            printk("[BUTTON 3] MUTE\n");
            ble_hid_send_consumer_key(HID_KEY_MUTE);
            k_msleep(100);
        }
    }

    btn_mode_last = mode_val;
    btn_play_last = play_val;
    btn_track_last = track_val;
}