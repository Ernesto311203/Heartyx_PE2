#include "Funciones.h"

#define CMD_WRITE_ENABLE   0x06
#define CMD_READ_STATUS1   0x05
#define CMD_PAGE_PROGRAM   0x02
#define CMD_READ_DATA      0x03
#define CMD_SECTOR_ERASE   0x20

#define SPI_BUS_NODE DT_NODELABEL(spi1)
static const struct device *spi_dev = DEVICE_DT_GET(SPI_BUS_NODE);

static struct spi_config spi_cfg = {
    .operation = SPI_OP_MODE_MASTER |
                 SPI_WORD_SET(8) |
                 SPI_TRANSFER_MSB,
    .frequency = 8000000,
    .slave     = 0,
    .cs = {
        .gpio = {
            .port    = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
            .pin     = 30,
            .dt_flags = GPIO_ACTIVE_LOW,
        },
        .delay = 0,
    },
};

static int spi_write_bytes(const uint8_t *tx, size_t len)
{
    struct spi_buf buf = {
        .buf = (void *)tx,
        .len = len
    };
    struct spi_buf_set tx_set = {
        .buffers = &buf,
        .count   = 1
    };
    return spi_write(spi_dev, &spi_cfg, &tx_set);
}

static int spi_txrx(const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_buf txb = {
        .buf = (void *)tx,
        .len = len
    };
    struct spi_buf rxb = {
        .buf = rx,
        .len = len
    };

    struct spi_buf_set txs = {
        .buffers = &txb,
        .count   = 1
    };
    struct spi_buf_set rxs = {
        .buffers = &rxb,
        .count   = 1
    };

    return spi_transceive(spi_dev, &spi_cfg, &txs, &rxs);
}

void init_spi_flash(void)
{
    if (!device_is_ready(spi_dev)) {
        printk("SPI flash dev not ready\n");
    } else {
        printk("SPI flash interface ready\n");
    }
}

void flash_wait_busy(void)
{
    uint8_t tx[2] = { CMD_READ_STATUS1, 0 };
    uint8_t rx[2];

    do {
        spi_txrx(tx, rx, 2);
    } while (rx[1] & 0x01);
}

void flash_write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    spi_write_bytes(&cmd, 1);
}

void flash_page_program(uint32_t addr, const uint8_t *data, size_t len)
{
    flash_write_enable();

    uint8_t header[4] = {
        CMD_PAGE_PROGRAM,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr)
    };

    struct spi_buf tx_bufs[2] = {
        { .buf = header,       .len = 4    },
        { .buf = (void *)data, .len = len  }
    };

    struct spi_buf_set tx_set = {
        .buffers = tx_bufs,
        .count   = 2
    };

    spi_write(spi_dev, &spi_cfg, &tx_set);
    flash_wait_busy();
}

void flash_write_buffer(uint32_t addr, const uint8_t *data, size_t len)
{
    while (len > 0) {
        uint32_t page_off       = addr % FLASH_PAGE_SIZE;
        uint32_t space_in_page  = FLASH_PAGE_SIZE - page_off;
        size_t   chunk          = (len < space_in_page) ? len : space_in_page;

        flash_page_program(addr, data, chunk);

        addr += chunk;
        data += chunk;
        len  -= chunk;
    }
}

void flash_sector_erase(uint32_t addr)
{
    flash_write_enable();

    uint8_t cmd[4] = {
        CMD_SECTOR_ERASE,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr)
    };

    spi_write_bytes(cmd, 4);
    flash_wait_busy();
}

void flash_read_bytes(uint32_t addr, uint8_t *dst, size_t len)
{
    uint8_t hdr[4] = {
        CMD_READ_DATA,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr)
    };

    struct spi_buf txb[2] = {
        { .buf = hdr,  .len = 4 },
        { .buf = NULL, .len = 0 }
    };
    struct spi_buf rxb[2] = {
        { .buf = NULL, .len = 4 },
        { .buf = dst,  .len = len }
    };

    struct spi_buf_set txs = { .buffers = txb, .count = 2 };
    struct spi_buf_set rxs = { .buffers = rxb, .count = 2 };

    spi_transceive(spi_dev, &spi_cfg, &txs, &rxs);
}

void flash_write_config(uint32_t num_sequences)
{
    flash_write_buffer(FLASH_CFG_ADDR,
                       (const uint8_t *)&num_sequences,
                       sizeof(num_sequences));
}

uint32_t flash_read_config(void)
{
    uint32_t N = 0;
    flash_read_bytes(FLASH_CFG_ADDR, (uint8_t *)&N, sizeof(N));
    return N;
}