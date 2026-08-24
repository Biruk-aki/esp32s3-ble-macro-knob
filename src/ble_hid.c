#include "ble_hid.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>

static struct bt_conn *current_conn = NULL;

/* Composite HID Report Map: Consumer Control (ID 1) + Mouse Wheel (ID 2) */
static const uint8_t hid_report_map[] = {
    /* --- REPORT ID 1: Consumer Control (Media & Brightness) --- */
    0x05, 0x0C,       /* Usage Page (Consumer Devices) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1 bit) */
    0x95, 0x08,       /*   Report Count (8 functional bits) */
    0x09, 0xE9,       /*   Bit 0: Volume Increment */
    0x09, 0xEA,       /*   Bit 1: Volume Decrement */
    0x09, 0xE2,       /*   Bit 2: Mute */
    0x09, 0xCD,       /*   Bit 3: Play/Pause */
    0x09, 0xB5,       /*   Bit 4: Scan Next Track */
    0x09, 0xB6,       /*   Bit 5: Scan Prev Track */
    0x09, 0x6F,       /*   Bit 6: Brightness Increment */
    0x09, 0x70,       /*   Bit 7: Brightness Decrement */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0xC0,             /* End Collection */

    /* --- REPORT ID 2: Mouse (Wheel Scroll) --- */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x02,       /* Usage (Mouse) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x02,       /*   Report ID (2) */
    0x09, 0x01,       /*   Usage (Pointer) */
    0xA1, 0x00,       /*   Collection (Physical) */
    0x05, 0x09,       /*     Usage Page (Buttons) */
    0x19, 0x01,       /*     Usage Minimum (1) */
    0x29, 0x03,       /*     Usage Maximum (3) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x95, 0x03,       /*     Report Count (3 buttons) */
    0x75, 0x01,       /*     Report Size (1 bit) */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute) */
    0x95, 0x01,       /*     Report Count (1) */
    0x75, 0x05,       /*     Report Size (5 bits padding) */
    0x81, 0x01,       /*     Input (Constant) */
    0x05, 0x01,       /*     Usage Page (Generic Desktop) */
    0x09, 0x30,       /*     Usage (X) */
    0x09, 0x31,       /*     Usage (Y) */
    0x09, 0x38,       /*     Usage (Wheel) */
    0x15, 0x81,       /*     Logical Minimum (-127) */
    0x25, 0x7F,       /*     Logical Maximum (127) */
    0x75, 0x08,       /*     Report Size (8 bits) */
    0x95, 0x03,       /*     Report Count (3 axes: X, Y, Wheel) */
    0x81, 0x06,       /*     Input (Data, Variable, Relative) */
    0xC0,             /*   End Collection */
    0xC0              /* End Collection */
};

static const uint8_t hid_info[] = { 0x01, 0x01, 0x00, 0x02 };
static uint8_t hid_protocol_mode = 0x01;
static uint8_t hid_ctrl_point;
static uint8_t hid_consumer_input;
static uint8_t hid_mouse_input[4];

static const uint8_t report_ref_consumer[] = { REPORT_ID_CONSUMER, 0x01 };
static const uint8_t report_ref_mouse[]    = { REPORT_ID_MOUSE, 0x01 };

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

static ssize_t read_report_ref_consumer(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                        void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_ref_consumer, sizeof(report_ref_consumer));
}

static ssize_t read_report_ref_mouse(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                     void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_ref_mouse, sizeof(report_ref_mouse));
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
    bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed: 0x%02x\n", err);
        return;
    }
    current_conn = bt_conn_ref(conn);
    printk(">>> CONNECTED TO PC <<<\n");
    bt_conn_set_security(conn, BT_SECURITY_L2);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected: 0x%02x\n", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    k_work_reschedule(&adv_restart_work, K_MSEC(200));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(conn);
    if (!err && level >= BT_SECURITY_L2) {
        printk("Security established (Level %d)\n", level);
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
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_protocol_mode, NULL, &hid_protocol_mode),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_hid_info, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, write_ctrl_point, &hid_ctrl_point),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_report_map, NULL, NULL),

    /* Characteristic Index 9: Consumer Report (ID 1) */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           NULL, NULL, &hid_consumer_input),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ,
                       read_report_ref_consumer, NULL, NULL),

    /* Characteristic Index 13: Mouse Report (ID 2) */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           NULL, NULL, hid_mouse_input),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF,
                       BT_GATT_PERM_READ,
                       read_report_ref_mouse, NULL, NULL),
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
    printk("Pairing bonded: %s\n", bonded ? "true" : "false");
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
};

bool ble_hid_is_connected(void)
{
    return current_conn != NULL;
}

int ble_hid_send_consumer_key(uint8_t key_mask)
{
    if (!current_conn) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[9];
    uint8_t report = key_mask;
    int err;

    /* Press */
    err = bt_gatt_notify(current_conn, report_attr, &report, sizeof(report));
    if (err) {
        return err;
    }

    k_msleep(20);

    /* Release */
    report = HID_KEY_NONE;
    err = bt_gatt_notify(current_conn, report_attr, &report, sizeof(report));
    return err;
}

int ble_hid_send_mouse_scroll(int8_t scroll_steps)
{
    if (!current_conn) {
        return -ENOTCONN;
    }

    const struct bt_gatt_attr *report_attr = &hids_svc.attrs[13];
    /* Mouse Report Format: [Buttons, delta_X, delta_Y, delta_Wheel] */
    uint8_t mouse_report[4] = { 0x00, 0x00, 0x00, (uint8_t)scroll_steps };

    return bt_gatt_notify(current_conn, report_attr, mouse_report, sizeof(mouse_report));
}

int ble_hid_init(void)
{
    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_info_cb);

    int err = bt_enable(NULL);
    if (err) {
        printk("BT init fail: %d\n", err);
        return err;
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_subsys_init();
        settings_load();
    }

    size_t id_count = 0;
    bt_id_get(NULL, &id_count);
    if (id_count == 0) {
        bt_id_create(NULL, NULL);
    }

    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}