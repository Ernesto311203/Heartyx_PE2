#include "Funciones.h"

#include "adi_adpd6000.h"
#include "adi_adpd6000_device.h"
#include "adi_adpd6000_ppg.h"
#include "adi_adpd6000_gpio.h"
#include "adi_adpd6000_ecg.h"
#include "adi_adpd6000_hal.h"

#define ADPD_SPI_NODE       DT_NODELABEL(spi1)
#define ADPD_CS_GPIO_NODE   DT_NODELABEL(gpio0)
#define ADPD_CS_PIN         17

#define I2C_NODE DT_NODELABEL(i2c0)
#define TMP117_ADDR 0x48

static const struct device *adpd_spi_dev = DEVICE_DT_GET(ADPD_SPI_NODE);
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

static struct spi_config adpd_spi_cfg = {
    .operation = SPI_OP_MODE_MASTER |
                 SPI_WORD_SET(8) |
                 SPI_TRANSFER_MSB |
                 SPI_MODE_CPOL |
                 SPI_MODE_CPHA,
    .frequency = 1000000U,
    .slave     = 0,
    .cs = {
        .gpio = {
            .port    = DEVICE_DT_GET(ADPD_CS_GPIO_NODE),
            .pin     = ADPD_CS_PIN,
            .dt_flags = GPIO_ACTIVE_LOW,
        },
        .delay = 0,
    },
};

static adi_adpd6000_device_t adpd6000_dev;
static adi_adpd6000_fifo_config_t adpd_fifo_cfg;

static int32_t ppg1_buf[VEC_LEN];
static int32_t ppg2_buf[VEC_LEN];
static float   template_temp_val = 0.0f;

static int32_t adpd6000_spi_write(void *user_data, uint8_t *wr_buf, uint32_t len)
{
    ARG_UNUSED(user_data);

    struct spi_buf tx_buf = {
        .buf = wr_buf,
        .len = len
    };
    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count   = 1
    };

    int ret = spi_write(adpd_spi_dev, &adpd_spi_cfg, &tx_set);
    return ret;
}

static int32_t adpd6000_spi_read(void *user_data,
                                 uint8_t *rd_buf, uint32_t rd_len,
                                 uint8_t *wr_buf, uint32_t wr_len)
{
    ARG_UNUSED(user_data);

    struct spi_buf tx_bufs[2] = {
        { .buf = wr_buf, .len = wr_len },
        { .buf = NULL,  .len = 0 }
    };
    struct spi_buf rx_bufs[2] = {
        { .buf = NULL,    .len = wr_len },
        { .buf = rd_buf,  .len = rd_len }
    };

    struct spi_buf_set tx_set = {
        .buffers = tx_bufs,
        .count   = 2
    };
    struct spi_buf_set rx_set = {
        .buffers = rx_bufs,
        .count   = 2
    };

    int ret = spi_transceive(adpd_spi_dev, &adpd_spi_cfg, &tx_set, &rx_set);
    if (ret) {
    }
    return ret;
}

static int32_t adpd6000_log_write(void *user_data, char *string)
{
    ARG_UNUSED(user_data);
    return 0;
}

static int adpd_check_error(int32_t err, const char *fn)
{
    if (err != API_ADPD6000_ERROR_OK) {
        atomic_set(&adpd_error_flag, 1);
        gpio_pin_set_dt(&led, 1);
        return -EIO;
    }
    return 0;
}

static bool adpd6000_verify_connected(void)
{
    uint8_t chip_id = 0, chip_rev = 0;
    int32_t err = adi_adpd6000_device_get_id(&adpd6000_dev, &chip_id, &chip_rev);

    if (err != API_ADPD6000_ERROR_OK) {
        atomic_set(&adpd_error_flag, 1);
        gpio_pin_set_dt(&led, 1);
        return false;
    }

    if (chip_id == 0xC4) {
        return true;
    } else {
               chip_id, chip_rev);
        atomic_set(&adpd_error_flag, 1);
        gpio_pin_set_dt(&led, 1);
        return false;
    }
}

int adpd6000_init_config(void)
{
    int32_t err;

    if (!device_is_ready(adpd_spi_dev)) {
        return -ENODEV;
    }

    memset(&adpd6000_dev, 0, sizeof(adpd6000_dev));
    adpd6000_dev.user_data = NULL;
    adpd6000_dev.write     = adpd6000_spi_write;
    adpd6000_dev.read      = adpd6000_spi_read;
    adpd6000_dev.log_write = adpd6000_log_write;

    err = adi_adpd6000_device_sw_reset(&adpd6000_dev);
    if (adpd_check_error(err, "device_sw_reset")) return err;
    k_msleep(100);

    if (!adpd6000_verify_connected()) {
        return -ENODEV;
    }

    err = adi_adpd6000_device_init(&adpd6000_dev);
    if (adpd_check_error(err, "device_init")) return err;
    k_msleep(100);


    err = adi_adpd6000_device_enable_internal_osc_960k(&adpd6000_dev);
    if (adpd_check_error(err, "enable_internal_osc_960k")) return err;
    k_msleep(100);


    {
        uint32_t sys_clk = 960000;
        uint32_t freq    = 125;
        err = adi_adpd6000_device_set_slot_freq(&adpd6000_dev, sys_clk, freq);
        if (adpd_check_error(err, "device_set_slot_freq")) return err;
        k_msleep(100);
    }

    err = adi_adpd6000_ppg_set_slot_mode(&adpd6000_dev, API_ADPD6000_PPG_SLOT_AB);
    if (adpd_check_error(err, "ppg_set_slot_mode")) return err;
    k_msleep(50);

    uint8_t slot_A   = 0;
    uint8_t slot_B   = 1;
    uint8_t channel_1 = 0;
    uint8_t channel_2 = 1;
    uint8_t pair_12  = 0;
    uint8_t pair_34  = 1;
    uint8_t vc_index = 0;

    err = adi_adpd6000_ppg_tia_set_input_res(&adpd6000_dev, slot_A, API_ADPD6000_PPG_TIA_INPUT_RES_6K5);
    if (adpd_check_error(err, "ppg_tia_set_input_res A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_input_res(&adpd6000_dev, slot_B, API_ADPD6000_PPG_TIA_INPUT_RES_6K5);
    if (adpd_check_error(err, "ppg_tia_set_input_res B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_gain_res(&adpd6000_dev, slot_A, channel_1, API_ADPD6000_PPG_TIA_GAIN_RES_25K);
    if (adpd_check_error(err, "ppg_tia_set_gain_res A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_gain_res(&adpd6000_dev, slot_B, channel_1, API_ADPD6000_PPG_TIA_GAIN_RES_25K);
    if (adpd_check_error(err, "ppg_tia_set_gain_res B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_vref_value(&adpd6000_dev, slot_A, API_ADPD6000_PPG_TIA_VREF_1P265);
    if (adpd_check_error(err, "ppg_tia_set_vref_value A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_vref_value(&adpd6000_dev, slot_B, API_ADPD6000_PPG_TIA_VREF_1P265);
    if (adpd_check_error(err, "ppg_tia_set_vref_value B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_vref_pulse_alt_value(&adpd6000_dev, slot_A, API_ADPD6000_PPG_TIA_VREF_0P8855);
    if (adpd_check_error(err, "ppg_tia_set_vref_pulse_alt_value A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_set_vref_pulse_alt_value(&adpd6000_dev, slot_B, API_ADPD6000_PPG_TIA_VREF_0P8855);
    if (adpd_check_error(err, "ppg_tia_set_vref_pulse_alt_value B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_enable_vref_pulse(&adpd6000_dev, slot_A, true);
    if (adpd_check_error(err, "ppg_tia_enable_vref_pulse A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_tia_enable_vref_pulse(&adpd6000_dev, slot_B, true);
    if (adpd_check_error(err, "ppg_tia_enable_vref_pulse B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_enable_input_diff_mode(&adpd6000_dev, pair_12, false);
    if (adpd_check_error(err, "ppg_enable_input_diff_mode pair_12")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_enable_input_diff_mode(&adpd6000_dev, pair_34, false);
    if (adpd_check_error(err, "ppg_enable_input_diff_mode pair_34")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_input_mux(&adpd6000_dev, slot_A, pair_12, API_ADPD6000_PPG_INPUT_B1);
    if (adpd_check_error(err, "ppg_set_input_mux A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_input_mux(&adpd6000_dev, slot_B, pair_34, API_ADPD6000_PPG_INPUT_B1);
    if (adpd_check_error(err, "ppg_set_input_mux B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_sleep_input_mux(&adpd6000_dev, pair_12, API_ADPD6000_PPG_INPUT_SLEEP_BOTH_CATH1);
    if (adpd_check_error(err, "ppg_set_sleep_input_mux pair_12")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_sleep_input_mux(&adpd6000_dev, pair_34, API_ADPD6000_PPG_INPUT_SLEEP_BOTH_CATH1);
    if (adpd_check_error(err, "ppg_set_sleep_input_mux pair_34")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_cathode(&adpd6000_dev,
                                       API_ADPD6000_PPG_CATH_VDD,
                                       API_ADPD6000_PPG_CATH_VDD);
    if (adpd_check_error(err, "ppg_set_cathode")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_enable_amp(&adpd6000_dev, slot_A, channel_1, false);
    if (adpd_check_error(err, "ppg_enable_amp A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_enable_amp(&adpd6000_dev, slot_B, channel_1, false);
    if (adpd_check_error(err, "ppg_enable_amp B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_set_gain(&adpd6000_dev, slot_A, channel_1, API_ADPD6000_PPG_INTEG_50K_GAIN_2);
    if (adpd_check_error(err, "ppg_integ_set_gain A ch1")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_set_gain(&adpd6000_dev, slot_B, channel_1, API_ADPD6000_PPG_INTEG_50K_GAIN_2);
    if (adpd_check_error(err, "ppg_integ_set_gain B ch1")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_select_cap(&adpd6000_dev, slot_A, channel_1, API_ADPD6000_PPG_INTEG_CAP_12P6);
    if (adpd_check_error(err, "ppg_integ_select_cap A ch1")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_select_cap(&adpd6000_dev, slot_B, channel_1, API_ADPD6000_PPG_INTEG_CAP_12P6);
    if (adpd_check_error(err, "ppg_integ_select_cap B ch1")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_select_cap(&adpd6000_dev, slot_A, channel_2, API_ADPD6000_PPG_INTEG_CAP_12P6);
    if (adpd_check_error(err, "ppg_integ_select_cap A ch2")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_select_cap(&adpd6000_dev, slot_B, channel_2, API_ADPD6000_PPG_INTEG_CAP_12P6);
    if (adpd_check_error(err, "ppg_integ_select_cap B ch2")) return err;
    k_msleep(50);

    {
        uint8_t width_integ = 3;
        err = adi_adpd6000_ppg_integ_set_width(&adpd6000_dev, slot_A, width_integ);
        if (adpd_check_error(err, "ppg_integ_set_width A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_integ_set_width(&adpd6000_dev, slot_B, width_integ);
        if (adpd_check_error(err, "ppg_integ_set_width B")) return err;
        k_msleep(50);
    }

    err = adi_adpd6000_ppg_integ_enable_single_clk(&adpd6000_dev, slot_A, true);
    if (adpd_check_error(err, "ppg_integ_enable_single_clk A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_integ_enable_single_clk(&adpd6000_dev, slot_B, true);
    if (adpd_check_error(err, "ppg_integ_enable_single_clk B")) return err;
    k_msleep(50);

    {
        uint8_t  led_idx_1A     = 0;
        uint8_t  current        = 50;
        uint8_t  led_width      = 24;
        uint8_t  led_offset     = 59;
        uint8_t  sec_led_offset = 0x13;
        uint16_t num_int        = 9;
        uint16_t num_repeat     = 26;
        uint16_t min_period     = 60;

        err = adi_adpd6000_ppg_led_set_current(&adpd6000_dev, slot_A, led_idx_1A, current);
        if (adpd_check_error(err, "ppg_led_set_current A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_channel(&adpd6000_dev, slot_A, led_idx_1A, API_ADPD6000_PPG_LED_A);
        if (adpd_check_error(err, "ppg_led_set_channel A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_mode(&adpd6000_dev, slot_A, led_idx_1A, API_ADPD6000_PPG_LED_HIGH_SNR);
        if (adpd_check_error(err, "ppg_led_set_mode A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_width(&adpd6000_dev, slot_A, led_width);
        if (adpd_check_error(err, "ppg_led_set_width A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_offset(&adpd6000_dev, slot_A, led_offset, sec_led_offset);
        if (adpd_check_error(err, "ppg_led_set_offset A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_count(&adpd6000_dev, slot_A, num_int, num_repeat);
        if (adpd_check_error(err, "ppg_led_set_count A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_set_minperiod(&adpd6000_dev, slot_A, min_period);
        if (adpd_check_error(err, "ppg_set_minperiod A")) return err;
        k_msleep(50);
    }

    {
        uint8_t  led_idx_1x       = 0;
        uint8_t  current_B        = 53;
        uint8_t  led_width_B      = 36;
        uint8_t  led_offset_B     = 63;
        uint8_t  sec_led_offset_B = 0x13;
        uint16_t num_int_B        = 13;
        uint16_t num_repeat_B     = 20;
        uint16_t min_period_B     = 138;

        err = adi_adpd6000_ppg_led_set_current(&adpd6000_dev, slot_B, led_idx_1x, current_B);
        if (adpd_check_error(err, "ppg_led_set_current B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_channel(&adpd6000_dev, slot_B, led_idx_1x, API_ADPD6000_PPG_LED_B);
        if (adpd_check_error(err, "ppg_led_set_channel B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_mode(&adpd6000_dev, slot_B, led_idx_1x, API_ADPD6000_PPG_LED_HIGH_SNR);
        if (adpd_check_error(err, "ppg_led_set_mode B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_width(&adpd6000_dev, slot_B, led_width_B);
        if (adpd_check_error(err, "ppg_led_set_width B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_offset(&adpd6000_dev, slot_B, led_offset_B, sec_led_offset_B);
        if (adpd_check_error(err, "ppg_set_led_offset B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_led_set_count(&adpd6000_dev, slot_B, num_int_B, num_repeat_B);
        if (adpd_check_error(err, "ppg_led_set_count B")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_set_minperiod(&adpd6000_dev, slot_B, min_period_B);
        if (adpd_check_error(err, "ppg_set_minperiod B")) return err;
        k_msleep(50);
    }

    err = adi_adpd6000_ppg_sel_precon(&adpd6000_dev, slot_A, API_ADPD6000_PPG_PRECON_AFE_VREF);
    if (adpd_check_error(err, "ppg_sel_precon A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_sel_precon(&adpd6000_dev, slot_B, API_ADPD6000_PPG_PRECON_AFE_VREF);
    if (adpd_check_error(err, "ppg_sel_precon B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_sel_afe_path(&adpd6000_dev, slot_A, API_ADPD6000_PPG_AFE_PATH_TIA_BUF_ADC_1X);
    if (adpd_check_error(err, "ppg_sel_afe_path A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_sel_afe_path(&adpd6000_dev, slot_B, API_ADPD6000_PPG_AFE_PATH_TIA_BUF_ADC_1X);
    if (adpd_check_error(err, "ppg_sel_afe_path B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_config_vc(&adpd6000_dev, slot_A, vc_index,
                                     API_ADPD6000_PPG_VC_DELTA,
                                     API_ADPD6000_PPG_VC_VDD,
                                     API_ADPD6000_PPG_VC_PULSE_NO);
    if (adpd_check_error(err, "ppg_config_vc A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_config_vc(&adpd6000_dev, slot_B, vc_index,
                                     API_ADPD6000_PPG_VC_DELTA,
                                     API_ADPD6000_PPG_VC_VDD,
                                     API_ADPD6000_PPG_VC_PULSE_NO);
    if (adpd_check_error(err, "ppg_config_vc B")) return err;
    k_msleep(50);

    {
        uint8_t dc_current   = 0;
        uint8_t dc_current_B = 15;

        err = adi_adpd6000_ppg_set_dcdac(&adpd6000_dev, slot_A, channel_1, dc_current);
        if (adpd_check_error(err, "ppg_set_dcdac A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_set_dcdac(&adpd6000_dev, slot_B, channel_1, dc_current_B);
        if (adpd_check_error(err, "ppg_set_dcdac B")) return err;
        k_msleep(50);
    }

    {
        uint8_t signal_size = 4;
        uint8_t lit_size    = 4;
        uint8_t dark_size   = 4;

        err = adi_adpd6000_ppg_set_data_size(&adpd6000_dev, slot_A, signal_size, lit_size, dark_size);
        if (adpd_check_error(err, "ppg_set_data_size A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_set_data_size(&adpd6000_dev, slot_B, signal_size, lit_size, dark_size);
        if (adpd_check_error(err, "ppg_set_data_size B")) return err;
        k_msleep(50);
    }

    {
        uint8_t signal_offset = 0;
        uint8_t lit_offset    = 0;
        uint8_t dark_offset   = 0;

        err = adi_adpd6000_ppg_set_window_offset(&adpd6000_dev, slot_A, signal_offset, lit_offset, dark_offset);
        if (adpd_check_error(err, "ppg_set_window_offset A")) return err;
        k_msleep(50);

        err = adi_adpd6000_ppg_set_window_offset(&adpd6000_dev, slot_B, signal_offset, lit_offset, dark_offset);
        if (adpd_check_error(err, "ppg_set_window_offset B")) return err;
        k_msleep(50);
    }

    err = adi_adpd6000_ppg_set_alctype(&adpd6000_dev, slot_A, API_ADPD6000_PPG_ALC_COARSE_FINE);
    if (adpd_check_error(err, "ppg_set_alctype A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_alctype(&adpd6000_dev, slot_B, API_ADPD6000_PPG_ALC_COARSE_FINE);
    if (adpd_check_error(err, "ppg_set_alctype B")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_sample_type(&adpd6000_dev, slot_A, API_ADPD6000_PPG_SAMPLE_TYPE_TWO_REGION);
    if (adpd_check_error(err, "ppg_set_sample_type A")) return err;
    k_msleep(50);

    err = adi_adpd6000_ppg_set_sample_type(&adpd6000_dev, slot_B, API_ADPD6000_PPG_SAMPLE_TYPE_TWO_REGION);
    if (adpd_check_error(err, "ppg_set_sample_type B")) return err;
    k_msleep(50);

    {
        uint16_t threshold = 4;

        err = adi_adpd6000_device_set_fifo_threshold(&adpd6000_dev, threshold);
        if (adpd_check_error(err, "device_set_fifo_threshold")) return err;
        k_msleep(50);

        err = adi_adpd6000_device_get_sequence_fifo_config(&adpd6000_dev, &adpd_fifo_cfg);
        if (adpd_check_error(err, "device_get_sequence_fifo_config")) return err;
        k_msleep(50);

        err = adi_adpd6000_device_enable_fifo_thres_interrupt(&adpd6000_dev,
                                                              API_ADPD6000_INTERRUPT_X, true);
        if (adpd_check_error(err, "device_enable_fifo_thres_interrupt")) return err;
        k_msleep(50);
    }

    err = adi_adpd6000_device_enable_slot_operation_mode_go(&adpd6000_dev, false);
    if (adpd_check_error(err, "device_enable_slot_operation_mode_go(false)")) return err;
    k_msleep(20);

    return 0;
}

static int adpd6000_read_ppg_pair(uint32_t *ppg_red, uint32_t *ppg_ir)
{
    uint32_t signal_data[2] = {0};
    uint32_t dark_data[2]   = {0};
    uint32_t lit_data[2]    = {0};
    uint8_t  ppg_num        = 0;

    int32_t err = adi_adpd6000_ppg_read_fifo(&adpd6000_dev, &adpd_fifo_cfg,
                                             signal_data, dark_data, lit_data, &ppg_num);
    if (err != API_ADPD6000_ERROR_OK) {
        adpd_check_error(err, "ppg_read_fifo");
        return -EIO;
    }

    if (ppg_num < 1) {
        return -EAGAIN;
    }

    *ppg_red = signal_data[0];
    *ppg_ir  = signal_data[1];
    return 0;
}

void init_i2c(void)
{
    if (!device_is_ready(i2c_dev)) {
        printk("I2C0 NOT READY\n");
    } else {
        printk("I2C0 OK\n");
    }
}

static int tmp117_read_raw(int16_t *raw)
{
    uint8_t reg = 0x00;
    uint8_t data[2];

    if (!device_is_ready(i2c_dev)) {
        return -ENODEV;
    }

    int ret = i2c_write_read(i2c_dev, TMP117_ADDR, &reg, 1, data, 2);
    if (ret < 0) {
        return ret;
    }

    *raw = ((int16_t)data[0] << 8) | data[1];
    return 0;
}

static float tmp117_read_celsius(void)
{
    int16_t raw;
    if (tmp117_read_raw(&raw) < 0) {
        return 25.0f;
    }
    return raw * 0.0078125f;
}

static int adpd6000_afe_set_go(bool enable)
{
    int32_t err = adi_adpd6000_device_enable_slot_operation_mode_go(&adpd6000_dev, enable);
    if (adpd_check_error(err, enable ? "AFE GO ON" : "AFE GO OFF")) {
        return -EIO;
    }
    return 0;
}

int measure_ppg_template(void)
{
    uint32_t ppg_red = 0, ppg_ir = 0;
    int ret = 0;

    ret = adpd6000_afe_set_go(true);
    if (ret) {
        return ret;
    }

    uint32_t elapsed_ms = 0;
    while (elapsed_ms < 3800) {
        k_msleep(8);
        elapsed_ms += 8;
        (void)adpd6000_read_ppg_pair(&ppg_red, &ppg_ir);
    }

    for (uint32_t i = 0; i < VEC_LEN; i++) {
        int r;
        do {
            r = adpd6000_read_ppg_pair(&ppg_red, &ppg_ir);
            if (r == -EAGAIN) {
                k_msleep(1);
            } else if (r != 0) {
                ret = r;
                goto out_poweroff;
            }
        } while (r == -EAGAIN);

        ppg1_buf[i] = (int32_t)ppg_red;
        ppg2_buf[i] = (int32_t)ppg_ir;

        k_msleep(8);
    }

    template_temp_val = tmp117_read_celsius();


out_poweroff:
    (void)adpd6000_afe_set_go(false);
    return ret;
}

int flash_store_measurement(uint16_t seq)
{
    uint32_t base = FLASH_SEQ_BASE + (uint32_t)seq * SEQ_SLOT_SIZE;


           (unsigned int)seq, (unsigned int)base);

    flash_write_buffer(base,
                       (const uint8_t *)ppg1_buf,
                       TOTAL_BYTES_PER_VEC);

    flash_write_buffer(base + TOTAL_BYTES_PER_VEC,
                       (const uint8_t *)ppg2_buf,
                       TOTAL_BYTES_PER_VEC);

    flash_write_buffer(base + TOTAL_BYTES_PER_VEC * 2u,
                       (const uint8_t *)&template_temp_val,
                       sizeof(template_temp_val));

    return 0;
}