#ifndef BLE_HID_H_
#define BLE_HID_H_

#include <stdint.h>
#include <stdbool.h>

#define REPORT_ID_CONSUMER      1
#define REPORT_ID_MOUSE         2

/* Consumer Control Keys */
#define HID_KEY_NONE            0x00
#define HID_KEY_VOL_UP          (1 << 0) /* Bit 0: Vol Up */
#define HID_KEY_VOL_DOWN        (1 << 1) /* Bit 1: Vol Down */
#define HID_KEY_MUTE            (1 << 2) /* Bit 2: Mute */
#define HID_KEY_PLAY_PAUSE      (1 << 3) /* Bit 3: Play/Pause */
#define HID_KEY_NEXT_TRACK      (1 << 4) /* Bit 4: Next Track */
#define HID_KEY_PREV_TRACK      (1 << 5) /* Bit 5: Previous Track */
#define HID_KEY_BRIGHTNESS_UP   (1 << 6) /* Bit 6: Brightness Up */
#define HID_KEY_BRIGHTNESS_DOWN (1 << 7) /* Bit 7: Brightness Down */

int ble_hid_init(void);
bool ble_hid_is_connected(void);
int ble_hid_send_consumer_key(uint8_t key_mask);
int ble_hid_send_mouse_scroll(int8_t scroll_steps);

#endif /* BLE_HID_H_ */