#ifndef FUNCIONES_H
#define FUNCIONES_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

#define VEC_LEN           1024
#define BYTES_PER_SAMPLE  4
#define CHUNK_SIZE_BYTES  12
#define HEADER_SIZE       8
#define TOTAL_BYTES_PER_VEC (VEC_LEN * BYTES_PER_SAMPLE)
#define TOTAL_CHUNKS        ((TOTAL_BYTES_PER_VEC + CHUNK_SIZE_BYTES - 1)/CHUNK_SIZE_BYTES)
#define TOTAL_CHUNK_MAX_IDX (TOTAL_CHUNKS - 1)

#define FLASH_TOTAL_BYTES   (16u * 1024u * 1024u)
#define FLASH_SECTOR_SIZE   4096u
#define FLASH_PAGE_SIZE     256u
#define FLASH_CFG_ADDR      0u
#define FLASH_SEQ_BASE      FLASH_SECTOR_SIZE
#define SEQ_RAW_BYTES       (TOTAL_BYTES_PER_VEC*2u + 4u)
#define SEQ_SLOT_SIZE       (((SEQ_RAW_BYTES + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE)
#define MAX_MEASUREMENTS    96u

extern atomic_t adpd_error_flag;
extern atomic_t holter_done_flag;
extern atomic_t holter_active_flag;
extern atomic_t tx_in_progress_flag;

extern const struct gpio_dt_spec led;
extern struct k_sem tx_sem;

void power_off_system(void);

void init_spi_flash(void);
void flash_wait_busy(void);
void flash_write_enable(void);
void flash_page_program(uint32_t addr, const uint8_t *data, size_t len);
void flash_write_buffer(uint32_t addr, const uint8_t *data, size_t len);
void flash_sector_erase(uint32_t addr);
void flash_read_bytes(uint32_t addr, uint8_t *dst, size_t len);
void flash_write_config(uint32_t num_sequences);
uint32_t flash_read_config(void);

void init_i2c(void);
int adpd6000_init_config(void);
int measure_ppg_template(void);
int flash_store_measurement(uint16_t seq);

int ble_init_stack(void);
void ble_start_adv(void);
void ble_pause_for_measurement(void);
void ble_resume_after_measurement(void);

#endif