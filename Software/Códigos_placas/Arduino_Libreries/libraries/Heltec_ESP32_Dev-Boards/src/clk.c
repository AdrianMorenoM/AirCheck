/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(MCU_ESP32_S3) && (SLOW_CLK_TPYE == 1)
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/time.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
//#include "esp32s3/clk.h"
#include "esp_clk_internal.h"
#include "esp_rom_uart.h"
#include "esp_rom_sys.h"
#include "soc/system_reg.h"
#include "soc/dport_access.h"
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/rtc_periph.h"
#include "soc/i2s_reg.h"
#include "hal/cpu_hal.h"
#include "hal/wdt_hal.h"
#include "driver/periph_ctrl.h"
#include "bootloader_clock.h"
#include "soc/syscon_reg.h"
#include "driver/debug.h"

#define SYSTEM_WIFI_CLK_I2C_CLK_EN  BIT(5)
#define ESP_EARLY_LOGW(tag, format, ...)  lora_printf(format, ##__VA_ARGS__)
#define ESP_EARLY_LOGD(tag, format, ...)  lora_printf(format, ##__VA_ARGS__)

static const char *TAG = "clk";
extern void esp_clk_slowclk_cal_set(uint32_t value);
/* Number of cycles to wait from the 32k XTAL oscillator to consider it running.
 * Larger values increase startup delay. Smaller values may cause false positive
 * detection (i.e. oscillator runs for a few cycles and then stops).
 */
#define SLOW_CLK_CAL_CYCLES     CONFIG_ESP32S3_RTC_CLK_CAL_CYCLES

#ifdef CONFIG_ESP32S3_RTC_XTAL_CAL_RETRY
#define RTC_XTAL_CAL_RETRY CONFIG_ESP32S3_RTC_XTAL_CAL_RETRY
#else
#define RTC_XTAL_CAL_RETRY 1
#endif

/* Lower threshold for a reasonably-looking calibration value for a 32k XTAL.
 * The ideal value (assuming 32768 Hz frequency) is 1000000/32768*(2**19) = 16*10^6.
 */
#define MIN_32K_XTAL_CAL_VAL  15000000L

/* Indicates that this 32k oscillator gets input from external oscillator, rather
 * than a crystal.
 */
#define EXT_OSC_FLAG    BIT(3)

RTC_DATA_ATTR static bool rtcstarted=false;
/* This is almost the same as rtc_slow_freq_t, except that we define
 * an extra enum member for the external 32k oscillator.
 * For convenience, lower 2 bits should correspond to rtc_slow_freq_t values.
 */
typedef enum {
    SLOW_CLK_RTC = RTC_SLOW_FREQ_RTC,           //!< Internal 150 kHz RC oscillator
    SLOW_CLK_32K_XTAL = RTC_SLOW_FREQ_32K_XTAL, //!< External 32 kHz XTAL
    SLOW_CLK_8MD256 = RTC_SLOW_FREQ_8MD256,     //!< Internal 8 MHz RC oscillator, divided by 256
    SLOW_CLK_32K_EXT_OSC = RTC_SLOW_FREQ_32K_XTAL | EXT_OSC_FLAG //!< External 32k oscillator connected to 32K_XP pin
} slow_clk_sel_t;

RTC_DATA_ATTR uint8_t ex_32k_start_cnt=0;
static void select_rtc_slow_clk(slow_clk_sel_t slow_clk);
extern uint64_t rtccnt_cal;
extern bool webconfStart;
 __attribute__((weak)) void esp_clk_init(void)
{
    rtc_config_t cfg = RTC_CONFIG_DEFAULT();
    soc_reset_reason_t rst_reas;
    rst_reas = esp_rom_get_reset_reason(0);
    //When power on, we need to set `cali_ocode` to 1, to do a OCode calibration, which will calibrate the rtc reference voltage to a tested value
    if (rst_reas == RESET_REASON_CHIP_POWER_ON) {
        cfg.cali_ocode = 1;
    }

    
    rtc_init(cfg);

    assert(rtc_clk_xtal_freq_get() == RTC_XTAL_FREQ_40M);

    rtc_clk_fast_freq_set(RTC_FAST_FREQ_8M);

#ifdef CONFIG_BOOTLOADER_WDT_ENABLE
    // WDT uses a SLOW_CLK clock source. After a function select_rtc_slow_clk a frequency of this source can changed.
    // If the frequency changes from 150kHz to 32kHz, then the timeout set for the WDT will increase 4.6 times.
    // Therefore, for the time of frequency change, set a new lower timeout value (1.6 sec).
    // This prevents excessive delay before resetting in case the supply voltage is drawdown.
    // (If frequency is changed from 150kHz to 32kHz then WDT timeout will increased to 1.6sec * 150/32 = 7.5 sec).
    wdt_hal_context_t rtc_wdt_ctx = {.inst = WDT_RWDT, .rwdt_dev = &RTCCNTL};
    uint32_t stage_timeout_ticks = (uint32_t)(1600ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_feed(&rtc_wdt_ctx);
    //Bootloader has enabled RTC WDT until now. We're only modifying timeout, so keep the stage and timeout action the same
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);
#endif

    if(rtccnt_cal==0 || (rtccnt_cal!=0 && rtccnt_cal>400))
    {
      ex_32k_start_cnt++;
      ESP_EARLY_LOGD(TAG, "select external 32K RTC\r\n");
      select_rtc_slow_clk(SLOW_CLK_32K_XTAL); //extern 32K
//      select_rtc_slow_clk(SLOW_CLK_RTC);
    }
    

#ifdef CONFIG_BOOTLOADER_WDT_ENABLE
    // After changing a frequency WDT timeout needs to be set for new frequency.
    stage_timeout_ticks = (uint32_t)((uint64_t)CONFIG_BOOTLOADER_WDT_TIME_MS * rtc_clk_slow_freq_get_hz() / 1000ULL);
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_feed(&rtc_wdt_ctx);
    wdt_hal_config_stage(&rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);
#endif

    rtc_cpu_freq_config_t old_config, new_config;
    rtc_clk_cpu_freq_get_config(&old_config);
    const uint32_t old_freq_mhz = old_config.freq_mhz;
    const uint32_t new_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ;

    bool res = rtc_clk_cpu_freq_mhz_to_config(new_freq_mhz, &new_config);
    assert(res);

    // Wait for UART TX to finish, otherwise some UART output will be lost
    // when switching APB frequency
    if (CONFIG_ESP_CONSOLE_UART_NUM >= 0) {
        esp_rom_uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
    }

    if (res) {
        rtc_clk_cpu_freq_set_config(&new_config);
    }

    // Re calculate the ccount to make time calculation correct.
    cpu_hal_set_cycle_count( (uint64_t)cpu_hal_get_cycle_count() * new_freq_mhz / old_freq_mhz );
}

static void select_rtc_slow_clk(slow_clk_sel_t slow_clk)
{
    rtc_slow_freq_t rtc_slow_freq = slow_clk & RTC_CNTL_ANA_CLK_RTC_SEL_V;
    uint32_t cal_val = 0;
    /* number of times to repeat 32k XTAL calibration
     * before giving up and switching to the internal RC
     */
    int retry_32k_xtal = RTC_XTAL_CAL_RETRY;

    do {
        if (rtc_slow_freq == RTC_SLOW_FREQ_32K_XTAL) {
            /* 32k XTAL oscillator needs to be enabled and running before it can
             * be used. Hardware doesn't have a direct way of checking if the
             * oscillator is running. Here we use rtc_clk_cal function to count
             * the number of main XTAL cycles in the given number of 32k XTAL
             * oscillator cycles. If the 32k XTAL has not started up, calibration
             * will time out, returning 0.
             */
            ESP_EARLY_LOGD(TAG, "waiting for 32k oscillator to start up\r\n");
            if (slow_clk == SLOW_CLK_32K_XTAL) {
                rtc_clk_32k_enable(true);
            } else if (slow_clk == SLOW_CLK_32K_EXT_OSC) {
                rtc_clk_32k_enable_external();
            }
            // When SLOW_CLK_CAL_CYCLES is set to 0, clock calibration will not be performed at startup.
            if (SLOW_CLK_CAL_CYCLES > 0) {
                cal_val = rtc_clk_cal(RTC_CAL_32K_XTAL, SLOW_CLK_CAL_CYCLES);
                if (cal_val == 0 || cal_val < MIN_32K_XTAL_CAL_VAL) {
                    if (retry_32k_xtal-- > 0) {
                        continue;
                    }
                    ESP_EARLY_LOGD(TAG, "32 kHz XTAL not found, switching to internal 150 kHz osc\r\n");
                    rtc_slow_freq = RTC_SLOW_FREQ_RTC;
                }
            }
        } else if (rtc_slow_freq == RTC_SLOW_FREQ_8MD256) {
            rtc_clk_8m_enable(true, true);
        }
        rtc_clk_slow_freq_set(rtc_slow_freq);
        //ESP_EARLY_LOGD(TAG, "SLOW_CLK_CAL_CYCLES: %d", SLOW_CLK_CAL_CYCLES);
        if (SLOW_CLK_CAL_CYCLES > 0) {
            /* TODO: 32k XTAL oscillator has some frequency drift at startup.
             * Improve calibration routine to wait until the frequency is stable.
             */
            cal_val = rtc_clk_cal(RTC_CAL_RTC_MUX, SLOW_CLK_CAL_CYCLES);
        } else {
            const uint64_t cal_dividend = (1ULL << RTC_CLK_CAL_FRACT) * 1000000ULL;
            cal_val = (uint32_t) (cal_dividend / rtc_clk_slow_freq_get_hz());
        }
    } while (cal_val == 0);
    //ESP_EARLY_LOGD(TAG, "RTC_SLOW_CLK calibration value: %d\r\n", cal_val);
    esp_clk_slowclk_cal_set(cal_val);
}
 
static void rtc_clk_select_rtc_slow_clk(void)
{
    select_rtc_slow_clk(RTC_SLOW_FREQ_32K_XTAL);
}

/* This function is not exposed as an API at this point.
 * All peripheral clocks are default enabled after chip is powered on.
 * This function disables some peripheral clocks when cpu starts.
 * These peripheral clocks are enabled when the peripherals are initialized
 * and disabled when they are de-initialized.
 */
 
__attribute__((weak)) void esp_perip_clk_init(void)
{
    uint32_t common_perip_clk, hwcrypto_perip_clk, wifi_bt_sdio_clk = 0;
    uint32_t common_perip_clk1 = 0;

#if CONFIG_FREERTOS_UNICORE
    soc_reset_reason_t rst_reas[1];
#else
    soc_reset_reason_t rst_reas[2];
#endif

    rst_reas[0] = esp_rom_get_reset_reason(0);
#if !CONFIG_FREERTOS_UNICORE
    rst_reas[1] = esp_rom_get_reset_reason(1);
#endif


    if ((rst_reas[0] == RESET_REASON_CPU0_MWDT0 || rst_reas[0] == RESET_REASON_CPU0_SW ||
            rst_reas[0] == RESET_REASON_CPU0_RTC_WDT || rst_reas[0] == RESET_REASON_CPU0_MWDT1)
#if !CONFIG_FREERTOS_UNICORE
        || (rst_reas[1] == RESET_REASON_CPU1_MWDT0 || rst_reas[1] == RESET_REASON_CPU1_SW ||
            rst_reas[1] == RESET_REASON_CPU1_RTC_WDT || rst_reas[1] == RESET_REASON_CPU1_MWDT1)
#endif
       ) {
        common_perip_clk = ~READ_PERI_REG(SYSTEM_PERIP_CLK_EN0_REG);
        hwcrypto_perip_clk = ~READ_PERI_REG(SYSTEM_PERIP_CLK_EN1_REG);
        wifi_bt_sdio_clk = ~READ_PERI_REG(SYSTEM_WIFI_CLK_EN_REG);
    } else {
        common_perip_clk = SYSTEM_WDG_CLK_EN |
                           SYSTEM_I2S0_CLK_EN |
#if CONFIG_CONSOLE_UART_NUM != 0
                           SYSTEM_UART_CLK_EN |
#endif
#if CONFIG_CONSOLE_UART_NUM != 1
                           SYSTEM_UART1_CLK_EN |
#endif
#if CONFIG_CONSOLE_UART_NUM != 2
                           SYSTEM_UART2_CLK_EN |
#endif
                           SYSTEM_USB_CLK_EN |
                           SYSTEM_SPI2_CLK_EN |
                           SYSTEM_I2C_EXT0_CLK_EN |
                           SYSTEM_UHCI0_CLK_EN |
                           SYSTEM_RMT_CLK_EN |
                           SYSTEM_PCNT_CLK_EN |
                           SYSTEM_LEDC_CLK_EN |
                           SYSTEM_TIMERGROUP1_CLK_EN |
                           SYSTEM_SPI3_CLK_EN |
                           SYSTEM_SPI4_CLK_EN |
                           SYSTEM_PWM0_CLK_EN |
                           SYSTEM_TWAI_CLK_EN |
                           SYSTEM_PWM1_CLK_EN |
                           SYSTEM_I2S1_CLK_EN |
                           SYSTEM_SPI2_DMA_CLK_EN |
                           SYSTEM_SPI3_DMA_CLK_EN |
                           SYSTEM_PWM2_CLK_EN |
                           SYSTEM_PWM3_CLK_EN;
        common_perip_clk1 = 0;
        hwcrypto_perip_clk = SYSTEM_CRYPTO_AES_CLK_EN |
                             SYSTEM_CRYPTO_SHA_CLK_EN |
                             SYSTEM_CRYPTO_RSA_CLK_EN;
        wifi_bt_sdio_clk = SYSTEM_WIFI_CLK_WIFI_EN |
                           SYSTEM_WIFI_CLK_BT_EN_M |
                           SYSTEM_WIFI_CLK_I2C_CLK_EN |
                           SYSTEM_WIFI_CLK_UNUSED_BIT12 |
                           SYSTEM_WIFI_CLK_SDIO_HOST_EN;
    }

    //Reset the communication peripherals like I2C, SPI, UART, I2S and bring them to known state.
    common_perip_clk |= SYSTEM_I2S0_CLK_EN |
#if CONFIG_CONSOLE_UART_NUM != 0
                        SYSTEM_UART_CLK_EN |
#endif
#if CONFIG_CONSOLE_UART_NUM != 1
                        SYSTEM_UART1_CLK_EN |
#endif
#if CONFIG_CONSOLE_UART_NUM != 2
                        SYSTEM_UART2_CLK_EN |
#endif
                        SYSTEM_USB_CLK_EN |
                        SYSTEM_SPI2_CLK_EN |
                        SYSTEM_I2C_EXT0_CLK_EN |
                        SYSTEM_UHCI0_CLK_EN |
                        SYSTEM_RMT_CLK_EN |
                        SYSTEM_UHCI1_CLK_EN |
                        SYSTEM_SPI3_CLK_EN |
                        SYSTEM_SPI4_CLK_EN |
                        SYSTEM_I2C_EXT1_CLK_EN |
                        SYSTEM_I2S1_CLK_EN |
                        SYSTEM_SPI2_DMA_CLK_EN |
                        SYSTEM_SPI3_DMA_CLK_EN;
    common_perip_clk1 = 0;

    //* Disable some peripheral clocks. 
    CLEAR_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN0_REG, common_perip_clk);
    SET_PERI_REG_MASK(SYSTEM_PERIP_RST_EN0_REG, common_perip_clk);

    CLEAR_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN1_REG, common_perip_clk1);
    SET_PERI_REG_MASK(SYSTEM_PERIP_RST_EN1_REG, common_perip_clk1);

    //* Disable hardware crypto clocks. 
    CLEAR_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN1_REG, hwcrypto_perip_clk);
    SET_PERI_REG_MASK(SYSTEM_PERIP_RST_EN1_REG, hwcrypto_perip_clk);

    //* Disable WiFi/BT/SDIO clocks. 
    CLEAR_PERI_REG_MASK(SYSTEM_WIFI_CLK_EN_REG, wifi_bt_sdio_clk);
    SET_PERI_REG_MASK(SYSTEM_WIFI_CLK_EN_REG, SYSTEM_WIFI_CLK_EN);

    //* Set WiFi light sleep clock source to RTC slow clock 
    REG_SET_FIELD(SYSTEM_BT_LPCK_DIV_INT_REG, SYSTEM_BT_LPCK_DIV_NUM, 0);
    CLEAR_PERI_REG_MASK(SYSTEM_BT_LPCK_DIV_FRAC_REG, SYSTEM_LPCLK_SEL_8M);
    SET_PERI_REG_MASK(SYSTEM_BT_LPCK_DIV_FRAC_REG, SYSTEM_LPCLK_SEL_RTC_SLOW);

    //* Enable RNG clock. 
    periph_module_enable(PERIPH_RNG_MODULE);
}

#endif
