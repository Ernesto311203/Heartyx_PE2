#include "Funciones.h"

static struct bt_uuid_128 svc_uuid =
    BT_UUID_INIT_128(0x6E,0x40,0x00,0x01,0xB5,0xA3,0xF3,0x93,
                     0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

static struct bt_uuid_128 chr_tx_uuid =
    BT_UUID_INIT_128(0x6E,0x40,0x00,0x03,0xB5,0xA3,0xF3,0x93,
                     0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

static struct bt_uuid_128 chr_rx_uuid =
    BT_UUID_INIT_128(0x6E,0x40,0x00,0x09,0xB5,0xA3,0xF3,0x93,
                     0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

struct bt_conn *current_conn;
static bool notify_enabled = false;

#define MAX_PENDING_NOTIFS 8
static atomic_t outstanding_notifications = ATOMIC_INIT(0);
struct k_sem tx_sem;

struct ble_cmd_msg {
    uint8_t  cmd;
    uint8_t  _pad[3];
    uint32_t num_sequences;
};

K_MSGQ_DEFINE(cmd_msgq, sizeof(struct ble_cmd_msg), 4, 4);

static void notif_done_cb(struct bt_conn *conn, void *user_data)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(user_data);

    atomic_dec(&outstanding_notifications);
    k_sem_give(&tx_sem);
}

static void cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static int ble_notify_fixed(uint8_t kind,
                            uint16_t seq,
                            uint16_t chunk_idx,
                            uint16_t chunk_max,
                            const uint8_t *payload,
                            size_t payload_len)
{
    if (!current_conn || !notify_enabled) {
        return -ENOTCONN;
    }

    if (k_sem_take(&tx_sem, K_MSEC(100)) != 0) {
        return -EBUSY;
    }

    uint8_t buf[HEADER_SIZE + CHUNK_SIZE_BYTES];
    memset(buf, 0, sizeof(buf));

    buf[0] = kind;
    buf[1] = 0;
    sys_put_le16(seq,       &buf[2]);
    sys_put_le16(chunk_idx, &buf[4]);
    sys_put_le16(chunk_max, &buf[6]);

    if (payload && payload_len > 0) {
        memcpy(&buf[HEADER_SIZE], payload, payload_len);
    }

    struct bt_gatt_notify_params params = {
        .uuid = &chr_tx_uuid.uuid,
        .data = buf,
        .len  = HEADER_SIZE + payload_len,
        .func = notif_done_cb,
    };

    int err = bt_gatt_notify_cb(current_conn, &params);
    if (err) {
        k_sem_give(&tx_sem);
        return err;
    }

    atomic_inc(&outstanding_notifications);
    return 0;
}

void ble_start_adv(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, NULL, 0, NULL, 0);
}

int ble_init_stack(void)
{
    k_sem_init(&tx_sem, MAX_PENDING_NOTIFS, MAX_PENDING_NOTIFS);
    int err = bt_enable(NULL);
    return 0;
}

void ble_pause_for_measurement(void)
{
    int err;

    if (current_conn) {
        err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }

    err = bt_le_adv_stop();
}

void ble_resume_after_measurement(void)
{
    ble_start_adv();
}

static void send_stream_from_flash(uint32_t base_addr,
                                   uint8_t kind,
                                   uint16_t seq)
{
    if (!current_conn || !notify_enabled) {
        return;
    }

    uint32_t total_bytes = TOTAL_BYTES_PER_VEC;
    uint16_t chunk_max   = TOTAL_CHUNK_MAX_IDX;

    uint8_t buf[CHUNK_SIZE_BYTES];

    for (uint16_t chunk = 0; chunk <= chunk_max; chunk++) {
        uint32_t offset = (uint32_t)chunk * CHUNK_SIZE_BYTES;
        uint32_t addr   = base_addr + offset;

        size_t n = CHUNK_SIZE_BYTES;
        if (offset + n > total_bytes) {
            n = total_bytes - offset;
        }

        flash_read_bytes(addr, buf, n);

        int err;
        do {
            err = ble_notify_fixed(kind, seq, chunk, chunk_max, buf, n);

            if (err == -ENOTCONN) {
                return;
            }
            if (err && err != -EBUSY) {
                return;
            }
            if (err == -EBUSY) {
                k_msleep(2);
            }
        } while (err == -EBUSY);

        if ((chunk % 40u) == 0u) {
            k_msleep(1);
        }
    }
}

static void send_sequence_from_flash(uint16_t seq)
{
    if (!current_conn || !notify_enabled) {

        return;
    }

    uint32_t base      = FLASH_SEQ_BASE + (uint32_t)seq * SEQ_SLOT_SIZE;
    uint32_t addr_ppg1 = base;
    uint32_t addr_ppg2 = base + TOTAL_BYTES_PER_VEC;
    uint32_t addr_temp = base + TOTAL_BYTES_PER_VEC * 2u;

    send_stream_from_flash(addr_ppg1, 1, seq);
    k_msleep(3);

    send_stream_from_flash(addr_ppg2, 2, seq);
    k_msleep(3);

    uint8_t tbuf[4];
    flash_read_bytes(addr_temp, tbuf, 4);
    (void)ble_notify_fixed(3, seq, 0, 0, tbuf, 4);

    (void)ble_notify_fixed(0, seq, 0, 0, NULL, 0);
}

#define HOLTER_REAL_MEASURES      3u
#define HOLTER_TEST_INTERVAL_MS   (60u * 1000u)

static void handle_cmd_store(uint32_t N)
{
    if (N == 0) {
        return;
    }
    if (N > MAX_MEASUREMENTS) {
        N = MAX_MEASUREMENTS;
    }

    if (atomic_get(&holter_active_flag)) {
        return;
    }

    uint64_t needed = (uint64_t)FLASH_SEQ_BASE +
                      (uint64_t)N * (uint64_t)SEQ_SLOT_SIZE;

    if (needed > FLASH_TOTAL_BYTES) {
        return;
    }

    uint32_t last_addr = (uint32_t)needed;

    atomic_set(&holter_done_flag, 0);
    atomic_set(&holter_active_flag, 1);

    for (uint32_t addr = 0; addr < last_addr; addr += FLASH_SECTOR_SIZE) {
        flash_sector_erase(addr);
        k_msleep(1);
    }

    flash_write_config(N);

    uint32_t real_count = (N < HOLTER_REAL_MEASURES) ? N : HOLTER_REAL_MEASURES;

    int ret;

    for (uint32_t seq = 0; seq < N; seq++) {

        if (seq < real_count) {

            ble_pause_for_measurement();

            ret = measure_ppg_template();

            ble_resume_after_measurement();
        } 
        ret = flash_store_measurement((uint16_t)seq);
        if (ret) {
            break;
        }

        if ((seq + 1u) < real_count) {
            uint32_t remaining = HOLTER_TEST_INTERVAL_MS;

            while (remaining > 0u) {
                uint32_t step = (remaining > 1000u) ? 1000u : remaining;
                k_msleep(step);
                remaining -= step;
            }
        }
    }

    atomic_set(&holter_active_flag, 0);
    atomic_set(&holter_done_flag, 1);
}

static void handle_cmd_tx_all(void)
{
    uint32_t N = flash_read_config();

    if (N == 0 || N > MAX_MEASUREMENTS) {
        return;
    }

    atomic_set(&tx_in_progress_flag, 1);

    for (uint32_t seq = 0; seq < N; seq++) {
        send_sequence_from_flash((uint16_t)seq);
        k_msleep(5);
    }

    atomic_set(&tx_in_progress_flag, 0);
    atomic_set(&holter_done_flag, 1);
}

static void cmd_worker(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct ble_cmd_msg msg;

    while (1) {
        k_msgq_get(&cmd_msgq, &msg, K_FOREVER);

        switch (msg.cmd) {
        case 0x01:
            handle_cmd_store(msg.num_sequences);
            break;
        case 0x02:
            handle_cmd_tx_all();
            break;
        default:
            break;
        }
    }
}

K_THREAD_DEFINE(cmd_worker_id, 4096, cmd_worker, NULL, NULL, NULL,
                5, 0, 0);

static ssize_t ble_rx(struct bt_conn *conn,
                      const struct bt_gatt_attr *attr,
                      const void *buf, uint16_t len,
                      uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len == 0) {
        return 0;
    }

    const uint8_t *data = buf;

    struct ble_cmd_msg msg = {0};

    switch (data[0]) {
    case 0x01:
        if (len < 5) {
            break;
        }
        msg.cmd = 0x01;
        msg.num_sequences = sys_get_le32(&data[1]);
        break;

    case 0x02:
        msg.cmd = 0x02;
        msg.num_sequences = 0;
        break;

    default:
        break;
    }

    return len;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        return;
    }

    current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{


    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    notify_enabled = false;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

BT_GATT_SERVICE_DEFINE(wearable_svc,
    BT_GATT_PRIMARY_SERVICE(&svc_uuid.uuid),

    BT_GATT_CHARACTERISTIC(&chr_tx_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL
    ),

    BT_GATT_CCC(cccd_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
    ),

    BT_GATT_CHARACTERISTIC(&chr_rx_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, ble_rx, NULL
    )
);