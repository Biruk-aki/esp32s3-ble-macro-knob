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

/* Pin Definitions */
#define BTN_MODE_PIN          4   /* GPIO 4: Mode Toggle */
#define BTN_PLAY_PIN          5   /* GPIO 5: Play/Pause */
#define BTN_MUTE_PIN          6   /* GPIO 6: Mute */
#define POT_ADC_CHANNEL       0   /* ADC1_CH0 -> GPIO 1 */

/* Operating Modes */
enum control_mode {
    MODE_VOLUME = 0,
    MODE_SCROLL,
    MODE_TRACK
};

static enum control_mode current_mode = MODE_VOLUME;
static bool is_connected = false;

/* Combined HID Report Descriptor: Media Keys (ID 1) + Mouse Wheel (ID 2) */
static const uint8_t hid_report_map[] = {
    /* --- Report ID 1: Consumer Control (Media Keys) --- */
    0x05, 0x0C,       /* Usage Page (Consumer Devices) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0 = released) */
    0x25, 0x01,       /*   Logical Maximum (1 = pressed) */
    0x75, 0x01,       /*   Report Size (1 bit per button) */
    0x95, 0x07,       /*   Report Count (7 functional buttons) */
    0x09, 0xE9,       /*   Usage (Volume Increment) -> Bit 0 */
    0x09, 0xEA,       /*   Usage (Volume Decrement) -> Bit 1 */
    0x09, 0xE2,       /*   Usage (Mute)             -> Bit 2 */
    0x09, 0xCD,       /*   Usage (Play/Pause)        -> Bit 3 */
    0x09, 0xB5,       /*   Usage (Scan Next Track)  -> Bit 4 */
    0x09, 0xB6,       /*   Usage (Scan Prev Track)  -> Bit 5 */
    0x09, 0xB7,       /*   Usage (Stop)             -> Bit 6 */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x01,       /*   Report Count (1 bit padding) */
    0x81, 0x01,       /*   Input (Constant) -> Bit 7 padding */
    0xC0,             /* End Collection */

    /* --- Report ID 2: Mouse Wheel (Vertical Scroll) --- */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x02,       /* Usage (Mouse) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x02,       /*   Report ID (2) */
    0x09, 0x01,       /*   Usage (Pointer) */
    0xA1, 0x00,       /*   Collection (Physical) */
    0x05, 0x01,       /*     Usage Page (Generic Desktop) */
    0x09, 0x38,       /*     Usage (Wheel / Vertical Scroll) */
    0x15, 0x81,       /*     Logical Minimum (-127) */
    0x25, 0x7F,       /*     Logical Maximum (127) */
    0x75, 0x08,       /*     Report Size (8 bits) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative) */
    0xC0,             /*   End Collection (Physical) */
    0xC0              /* End Collection (Application) */
};

/* HID Metadata */
static const uint8_t hid_info[] = { 0x01, 0x01, 0x00, 0x02 };
static uint8_t hid_protocol_mode = 0x01;
static uint8_t hid_ctrl_point;
static uint8_t hid_input_value[2];
static const uint8_t hid_report_ref[] = { 0x01, 0x01 };

/* Read Callbacks */
static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             hid_report_map, sizeof(hid_report_map));
}

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             hid_info, sizeof(hid_info));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             hid_report_ref, sizeof(hid_report_ref));
}

static ssize_t read_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &hid_protocol_mode, sizeof(hid_protocol_mode));
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

/* Advertising Data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Advertising Work Handler */
static void adv_restart_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(adv_restart_work, adv_restart_handler);

static void adv_restart_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                              ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Failed to restart advertising (err %d)\n", err);
    } else {
        printk("Advertising restarted\n");
    }
}

/* Connection Callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
        return;
    }
    is_connected = true;
    printk("Connected to host!\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    is_connected = false;
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

/* GATT Service Declaration */
BT_GATT_SERVICE_DEFINE(hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    /* 1. Protocol Mode */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           read_protocol_mode, NULL, &hid_protocol_mode),

    /* 2. HID Info */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_hid_info, NULL, NULL),

    /* 3. HID Control Point */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, write_ctrl_point, &hid_ctrl_point),

    /* 4. Report Map */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_report_map, NULL, NULL),

    /* 5. Input Report */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           NULL, NULL, hid_input_value),

    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ_ENCRYPT,
                       read_report_ref, NULL, NULL),
);

/* Pairing Callbacks */
static void pairing_confirm(struct bt_conn *conn)
{
    int err = bt_conn_auth_pairing_confirm(conn);
    if (err) {
        printk("Pairing confirm failed (err %d)\n", err);
    } else {
        printk("Pairing confirmed\n");
    }
}

static void auth_cancel(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    printk("Pairing cancelled\n");
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
    if (!is_connected) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[10];
    uint8_t report[2] = { 0x01, key_mask }; /* [Report ID, Bitmask] */
    int err;

    /* 1. Key Press */
    err = bt_gatt_notify(NULL, report_attr, report, sizeof(report));
    if (err) {
        return err;
    }

    k_msleep(20);

    /* 2. Key Release */
    report[1] = HID_KEY_NONE;
    err = bt_gatt_notify(NULL, report_attr, report, sizeof(report));
    return err;
}

/* Send HID Vertical Scroll Step (Report ID 2) */
static int send_hid_scroll(int8_t wheel_delta)
{
    if (!is_connected) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[10];
    uint8_t report[2] = { 0x02, (uint8_t)wheel_delta }; /* [Report ID, Delta] */

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
        printk("GPIO device not ready\n");
        return -ENODEV;
    }

    /* Configure Buttons with Internal Pull-Ups */
    gpio_pin_configure(gpio_dev, BTN_MODE_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_PLAY_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio_dev, BTN_MUTE_PIN, GPIO_INPUT | GPIO_PULL_UP);

    /* Configure ADC */
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
    if (!device_is_ready(adc_dev)) {
        printk("ADC device not ready\n");
        return -ENODEV;
    }

    adc_channel_setup(adc_dev, &channel_cfg);
    return 0;
}

int main(void)
{
    int err;

    printk("Starting BLE Desk Knob Firmware...\n");

    init_hardware();

    /* Register security callbacks */
    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_info_cb);

    /* Enable Bluetooth */
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

    /* Start Advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                          ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return 0;
    }
    printk("Advertising started as %s\n", CONFIG_BT_DEVICE_NAME);

    /* Polling Loop Variables */
    int prev_pot_val = -1;
    int btn_mode_last = 1, btn_play_last = 1, btn_mute_last = 1;

    while (1) {
        k_msleep(30);

        if (!is_connected) {
            continue;
        }

        /* 1. Read Button 1: Mode Toggle */
        int btn_mode_val = gpio_pin_get(gpio_dev, BTN_MODE_PIN);
        if (btn_mode_val == 0 && btn_mode_last == 1) {
            if (current_mode == MODE_VOLUME) {
                current_mode = MODE_SCROLL;
                printk("Switched Mode to: SCROLL\n");
            } else if (current_mode == MODE_SCROLL) {
                current_mode = MODE_TRACK;
                printk("Switched Mode to: TRACK\n");
            } else {
                current_mode = MODE_VOLUME;
                printk("Switched Mode to: VOLUME\n");
            }
        }
        btn_mode_last = btn_mode_val;

        /* 2. Read Button 2: Play/Pause */
        int btn_play_val = gpio_pin_get(gpio_dev, BTN_PLAY_PIN);
        if (btn_play_val == 0 && btn_play_last == 1) {
            printk("Sending Play/Pause\n");
            send_hid_key(HID_KEY_PLAY_PAUSE);
        }
        btn_play_last = btn_play_val;

        /* 3. Read Button 3: Mute */
        int btn_mute_val = gpio_pin_get(gpio_dev, BTN_MUTE_PIN);
        if (btn_mute_val == 0 && btn_mute_last == 1) {
            printk("Sending Mute\n");
            send_hid_key(HID_KEY_MUTE);
        }
        btn_mute_last = btn_mute_val;

        /* 4. Read ADC (Potentiometer) */
        err = adc_read(adc_dev, &sequence);
        if (err == 0) {
            if (prev_pot_val == -1) {
                prev_pot_val = adc_sample_buffer;
            }

            int diff = adc_sample_buffer - prev_pot_val;
            int threshold = 120; /* Noise filter deadzone */

            if (diff > threshold) {
                if (current_mode == MODE_VOLUME) {
                    printk("Pot CW -> Volume Up\n");
                    send_hid_key(HID_KEY_VOL_UP);
                } else if (current_mode == MODE_SCROLL) {
                    printk("Pot CW -> Scroll Up\n");
                    send_hid_scroll(1);
                } else {
                    printk("Pot CW -> Next Track\n");
                    send_hid_key(HID_KEY_NEXT_TRACK);
                }
                prev_pot_val = adc_sample_buffer;
            } else if (diff < -threshold) {
                if (current_mode == MODE_VOLUME) {
                    printk("Pot CCW -> Volume Down\n");
                    send_hid_key(HID_KEY_VOL_DOWN);
                } else if (current_mode == MODE_SCROLL) {
                    printk("Pot CCW -> Scroll Down\n");
                    send_hid_scroll(-1);
                } else {
                    printk("Pot CCW -> Prev Track\n");
                    send_hid_key(HID_KEY_PREV_TRACK);
                }
                prev_pot_val = adc_sample_buffer;
            }
        }
    }

    return 0;
}