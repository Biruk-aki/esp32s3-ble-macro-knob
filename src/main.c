#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>

/* Consumer Control Key Bitmasks (Report ID 1) */
#define HID_KEY_NONE          0x00
#define HID_KEY_VOL_UP        (1 << 0)  /* 0x01 */
#define HID_KEY_VOL_DOWN      (1 << 1)  /* 0x02 */
#define HID_KEY_MUTE          (1 << 2)  /* 0x04 */
#define HID_KEY_PLAY_PAUSE    (1 << 3)  /* 0x08 */
#define HID_KEY_NEXT_TRACK    (1 << 4)  /* 0x10 */
#define HID_KEY_PREV_TRACK    (1 << 5)  /* 0x20 */
#define HID_KEY_STOP          (1 << 6)  /* 0x40 */

#define BTN_MODE_PIN          4   /* GPIO 4: Mode Switch (Volume <-> Track) */
#define BTN_UP_PIN            5   /* GPIO 5: Vol Up / Next Track */
#define BTN_DOWN_PIN          6   /* GPIO 6: Vol Down / Prev Track / Long Press Mute */
#define POT_ADC_CHANNEL       0   /* ADC1_CH0 -> GPIO 1: Dedicated Scroll */

#define LONG_PRESS_TIME_MS    3000 /* 3 seconds hold for Mute */
#define SCROLL_THRESHOLD      200

enum btn_mode {
    MODE_VOLUME = 0,
    MODE_TRACK
};

static enum btn_mode current_mode = MODE_VOLUME;
static bool is_connected = false;
static bool notify_enabled = false;

/* HID Descriptor: Media Keys (ID 1) + Mouse Wheel (ID 2) */
static const uint8_t hid_report_map[] = {
    /* --- Report ID 1: Consumer Control --- */
    0x05, 0x0C,       /* Usage Page (Consumer Devices) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x07,       /*   Report Count (7 bits) */
    0x09, 0xE9,       /*   Volume Up */
    0x09, 0xEA,       /*   Volume Down */
    0x09, 0xE2,       /*   Mute */
    0x09, 0xCD,       /*   Play/Pause */
    0x09, 0xB5,       /*   Scan Next Track */
    0x09, 0xB6,       /*   Scan Prev Track */
    0x09, 0xB7,       /*   Stop */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x01,       /*   Report Count (1 bit padding) */
    0x81, 0x01,       /*   Input (Constant) */
    0xC0,             /* End Collection */

    /* --- Report ID 2: Mouse Wheel (Scroll) --- */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x02,       /* Usage (Mouse) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x02,       /*   Report ID (2) */
    0x09, 0x01,       /*   Usage (Pointer) */
    0xA1, 0x00,       /*   Collection (Physical) */
    0x05, 0x01,       /*     Usage Page (Generic Desktop) */
    0x09, 0x38,       /*     Usage (Wheel) */
    0x15, 0x81,       /*     Logical Minimum (-127) */
    0x25, 0x7F,       /*     Logical Maximum (127) */
    0x75, 0x08,       /*     Report Size (8 bits) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative) */
    0xC0,             /*   End Collection (Physical) */
    0xC0              /* End Collection (Application) */
};

static const uint8_t hid_info[] = { 0x01, 0x01, 0x00, 0x02 };
static uint8_t hid_protocol_mode = 0x01;
static uint8_t hid_ctrl_point;
static uint8_t hid_input_value[2];
static const uint8_t hid_report_ref[] = { 0x00, 0x01 };

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_map, sizeof(hid_report_map));
}

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_info, sizeof(hid_info));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_ref, sizeof(hid_report_ref));
}

static ssize_t read_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &hid_protocol_mode, sizeof(hid_protocol_mode));
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(buf);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);
    return len;
}

static void hids_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("HID Notifications %s\n", notify_enabled ? "ENABLED by PC" : "DISABLED");
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_restart_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(adv_restart_work, adv_restart_handler);

static void adv_restart_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Failed to restart advertising (err %d)\n", err);
    } else {
        printk("Advertising restarted\n");
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
        return;
    }
    is_connected = true;
    printk(">>> CONNECTED TO PC! <<<\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    is_connected = false;
    notify_enabled = false;
    printk("Disconnected (reason 0x%02x)\n", reason);
    k_work_reschedule(&adv_restart_work, K_MSEC(200));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(conn);
    if (!err) {
        printk("Security established: Level %d\n", level);
    } else {
        printk("Security failed: err %d\n", err);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

BT_GATT_SERVICE_DEFINE(hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           read_protocol_mode, NULL, &hid_protocol_mode),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_hid_info, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, write_ctrl_point, &hid_ctrl_point),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           NULL, NULL, hid_input_value),

    BT_GATT_CCC(hids_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ_ENCRYPT,
                       read_report_ref, NULL, NULL),
);

static void pairing_confirm(struct bt_conn *conn)
{
    bt_conn_auth_pairing_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .pairing_confirm = pairing_confirm,
    .cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    printk("Pairing complete! Bonded: %s\n", bonded ? "true" : "false");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    printk("Pairing failed (reason %d)\n", reason);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

/* Send HID Media Key (Report ID 1) */
static int send_hid_key(uint8_t key_mask)
{
    if (!is_connected || !notify_enabled) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[10];
    uint8_t report[2] = { 0x01, key_mask };
    int err;

    err = bt_gatt_notify(NULL, report_attr, report, sizeof(report));
    if (err) {
        return err;
    }

    k_msleep(25);

    report[1] = HID_KEY_NONE;
    return bt_gatt_notify(NULL, report_attr, report, sizeof(report));
}

/* Send HID Vertical Scroll (Report ID 2) */
static int send_hid_scroll(int8_t scroll_step)
{
    if (!is_connected || !notify_enabled) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[10];
    uint8_t report[2] = { 0x02, (uint8_t)scroll_step };

    return bt_gatt_notify(NULL, report_attr, report, sizeof(report));
}

/* Hardware Configuration */
static const struct device *gpio_dev;
static const struct device *adc_dev;

static int16_t adc_sample_buffer;
static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = POT_ADC_CHANNEL,
};

static struct adc_sequence sequence = {
    .channels = BIT(POT_ADC_CHANNEL),
    .buffer = &adc_sample_buffer,
    .buffer_size = sizeof(adc_sample_buffer),
    .resolution = 12,
};

static int init_hardware(void)
{
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        return -ENODEV;
    }

    gpio_pin_configure(gpio_dev, BTN_MODE_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_UP_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_DOWN_PIN, GPIO_INPUT | GPIO_PULL_UP);

    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (!device_is_ready(adc_dev)) {
        return -ENODEV;
    }

    adc_channel_setup(adc_dev, &channel_cfg);
    return 0;
}

int main(void)
{
    int err;

    k_msleep(500);
    printk("\n=== Booting ESP32-S3 Scroll & Media Knob ===\n");

    init_hardware();

    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_info_cb);

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }
    printk("Bluetooth initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_subsys_init();
        settings_load();
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed (err %d)\n", err);
        return 0;
    }
    printk("Advertising started as %s\n", CONFIG_BT_DEVICE_NAME);

    int filtered_pot = -1;
    int anchor_pot = -1;
    int64_t last_scroll_time = 0;

    int btn_mode_last = 1, btn_up_last = 1, btn_down_last = 1;
    int64_t btn_down_press_start = 0;
    bool mute_triggered = false;

    while (1) {
        k_msleep(15);

        if (!is_connected || !notify_enabled) {
            continue;
        }

        /* --- 1. Mode Button (GPIO 4) --- */
        int btn_mode_val = gpio_pin_get(gpio_dev, BTN_MODE_PIN);
        if (btn_mode_val == 0 && btn_mode_last == 1) {
            current_mode = (current_mode == MODE_VOLUME) ? MODE_TRACK : MODE_VOLUME;
            printk("\n>>> BUTTON MODE: %s <<<\n", current_mode == MODE_VOLUME ? "VOLUME [Up/Down]" : "TRACK [Next/Prev]");
            k_msleep(50);
        }
        btn_mode_last = btn_mode_val;

        /* --- 2. Button UP (GPIO 5) -> Volume Up / Next Track --- */
        int btn_up_val = gpio_pin_get(gpio_dev, BTN_UP_PIN);
        if (btn_up_val == 0 && btn_up_last == 1) {
            if (current_mode == MODE_VOLUME) {
                printk("[ACTION] Volume Up\n");
                send_hid_key(HID_KEY_VOL_UP);
            } else {
                printk("[ACTION] Next Track\n");
                send_hid_key(HID_KEY_NEXT_TRACK);
            }
            k_msleep(50);
        }
        btn_up_last = btn_up_val;

        /* --- 3. Button DOWN (GPIO 6) -> Short: Vol Down/Prev Track | Long: Mute --- */
        int btn_down_val = gpio_pin_get(gpio_dev, BTN_DOWN_PIN);
        int64_t now = k_uptime_get();

        if (btn_down_val == 0 && btn_down_last == 1) {
            /* Button just pressed down */
            btn_down_press_start = now;
            mute_triggered = false;
        } else if (btn_down_val == 0 && btn_down_last == 0) {
            /* Button being held down */
            if (!mute_triggered && (now - btn_down_press_start) >= LONG_PRESS_TIME_MS) {
                printk("\n>>> [ACTION] LONG PRESS (3s) -> TOGGLE MUTE <<<\n");
                send_hid_key(HID_KEY_MUTE);
                mute_triggered = true;
            }
        } else if (btn_down_val == 1 && btn_down_last == 0) {
            /* Button released */
            if (!mute_triggered) {
                if (current_mode == MODE_VOLUME) {
                    printk("[ACTION] Volume Down\n");
                    send_hid_key(HID_KEY_VOL_DOWN);
                } else {
                    printk("[ACTION] Prev Track\n");
                    send_hid_key(HID_KEY_PREV_TRACK);
                }
            }
            k_msleep(50);
        }
        btn_down_last = btn_down_val;

        /* --- 4. Potentiometer (GPIO 1) -> Continuous Vertical Scrolling --- */
        if (adc_dev && device_is_ready(adc_dev)) {
            err = adc_read(adc_dev, &sequence);
            if (err == 0) {
                if (filtered_pot == -1) {
                    filtered_pot = adc_sample_buffer;
                    anchor_pot = adc_sample_buffer;
                } else {
                    filtered_pot = (filtered_pot * 8 + adc_sample_buffer * 2) / 10;
                }

                int diff = filtered_pot - anchor_pot;

                if ((now - last_scroll_time) > 80) {
                    if (diff > SCROLL_THRESHOLD) {
                        printk("[SCROLL] Up\n");
                        send_hid_scroll(1);
                        anchor_pot = filtered_pot;
                        last_scroll_time = now;
                    } else if (diff < -SCROLL_THRESHOLD) {
                        printk("[SCROLL] Down\n");
                        send_hid_scroll(-1);
                        anchor_pot = filtered_pot;
                        last_scroll_time = now;
                    }
                }
            }
        }
    }

    return 0;
}