#include "adc_hal.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "ADC_HAL";

//Pin assignments
#define PIN_MOSI     11
#define PIN_SCLK     12
#define PIN_MISO     13
#define PIN_DRDY     10   //ADC A only
#define PIN_MCLK     14
#define PIN_SYNC      4   //shared SYNC/RESET

#define PIN_CS_A      7
#define PIN_CS_B      6
#define PIN_CS_C      5

//SPI/MCLK config
#define ADC_SPI_HOST        SPI2_HOST
#define ADC_SPI_FREQ_HZ     (200000)
#define MCLK_FREQ_HZ        (4096000/2)
#define MCLK_LEDC_TIMER     LEDC_TIMER_0
#define MCLK_LEDC_CHANNEL   LEDC_CHANNEL_0

//Internal state
static spi_device_handle_t s_spi_a;
static spi_device_handle_t s_spi_b;
static spi_device_handle_t s_spi_c;

static SemaphoreHandle_t   s_drdy_sem   = NULL;
static volatile uint32_t   s_drdy_count = 0;

//DRDY ISR (ADC A only)
static void IRAM_ATTR drdy_isr(void *arg)
{
    s_drdy_count++;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_drdy_sem, &hp);
    portYIELD_FROM_ISR(hp);
}

//Per-device SPI transfer functions
static uint8_t dummy_rx[32];

//Forward declarations
static uint8_t hal_spi_transfer_a(uint8_t *tx, uint8_t *rx, uint32_t len);
static uint8_t hal_spi_transfer_b(uint8_t *tx, uint8_t *rx, uint32_t len);
static uint8_t hal_spi_transfer_c(uint8_t *tx, uint8_t *rx, uint32_t len);
static void hal_cs_pin_a(uint8_t state);
static void hal_cs_pin_b(uint8_t state);
static void hal_cs_pin_c(uint8_t state);

#define MAKE_SPI_TRANSFER(LETTER, SPI_HANDLE)                                \
static uint8_t hal_spi_transfer_##LETTER(uint8_t *tx, uint8_t *rx,           \
                                         uint32_t len)                       \
{                                                                            \
    uint8_t *rx_buf = (rx != NULL) ? rx : dummy_rx;                          \
    spi_transaction_t t = {                                                  \
        .length    = len * 8,                                                \
        .tx_buffer = tx,                                                     \
        .rx_buffer = rx_buf,                                                 \
    };                                                                       \
    esp_err_t err = spi_device_transmit(SPI_HANDLE, &t);                     \
    return (err == ESP_OK) ? ADS131_OK : ADS131_FAILED;                      \
}

MAKE_SPI_TRANSFER(a, s_spi_a)
MAKE_SPI_TRANSFER(b, s_spi_b)
MAKE_SPI_TRANSFER(c, s_spi_c)

//CS functions with setup delay
static void hal_cs_pin_a(uint8_t state)
{
    gpio_set_level(PIN_CS_A, state);
    if (state == 0) esp_rom_delay_us(5);
}
static void hal_cs_pin_b(uint8_t state)
{
    gpio_set_level(PIN_CS_B, state);
    if (state == 0) esp_rom_delay_us(5);
}
static void hal_cs_pin_c(uint8_t state)
{
    gpio_set_level(PIN_CS_C, state);
    if (state == 0) esp_rom_delay_us(5);
}

//Shared SYNC and delay
static void hal_sync_pin(uint8_t state) { gpio_set_level(PIN_SYNC, state); }
static void hal_delay_ms(uint32_t ms)   { vTaskDelay(pdMS_TO_TICKS(ms)); }

//MCLK via LEDC
static esp_err_t mclk_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num       = MCLK_LEDC_TIMER,
        .freq_hz         = MCLK_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_MCLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = MCLK_LEDC_CHANNEL,
        .timer_sel  = MCLK_LEDC_TIMER,
        .duty       = 1,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch);
}

//SPI bus
static esp_err_t spi_init(void)
{
    //CS pins already configured high in gpio_init_local
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = PIN_MISO,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };
    esp_err_t err = spi_bus_initialize(ADC_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = ADC_SPI_FREQ_HZ,
        .mode           = 1,   //CPOL=0, CPHA=1 required by ADS131M02
        .spics_io_num   = -1,  //CS
        .queue_size     = 1,
    };

    err = spi_bus_add_device(ADC_SPI_HOST, &dev, &s_spi_a);
    if (err != ESP_OK) return err;
    err = spi_bus_add_device(ADC_SPI_HOST, &dev, &s_spi_b);
    if (err != ESP_OK) return err;
    err = spi_bus_add_device(ADC_SPI_HOST, &dev, &s_spi_c);
    return err;
}

//GPIO
static esp_err_t gpio_init_local(void)
{
    //SYNC/RESET active low, start HIGH
    gpio_reset_pin(PIN_SYNC);
    gpio_set_direction(PIN_SYNC, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_SYNC, 1);  //HIGH for normal operation

    //All CS pins are high before SPI init
    gpio_reset_pin(PIN_CS_A);
    gpio_set_direction(PIN_CS_A, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS_A, 1);
    gpio_reset_pin(PIN_CS_B);
    gpio_set_direction(PIN_CS_B, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS_B, 1);
    gpio_reset_pin(PIN_CS_C);
    gpio_set_direction(PIN_CS_C, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS_C, 1);

    s_drdy_sem = xSemaphoreCreateBinary();
    if (!s_drdy_sem) return ESP_ERR_NO_MEM;

    gpio_reset_pin(PIN_DRDY);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DRDY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    gpio_sleep_sel_dis(PIN_DRDY);
    gpio_set_intr_type(PIN_DRDY, GPIO_INTR_NEGEDGE);

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = gpio_isr_handler_add(PIN_DRDY, drdy_isr, NULL);
    if (err != ESP_OK) return err;

    gpio_intr_enable(PIN_DRDY);
    return ESP_OK;
}

//Drain stale DRDY semaphores
static void drain_drdy_semaphore(void)
{
    if (s_drdy_sem == NULL) return;
    while (xSemaphoreTake(s_drdy_sem, 0) == pdTRUE) {
    }
}

//init one ADC object
static esp_err_t init_one_adc(ads131_t *adc,
                              SPITransfer_f spi_fn,
                              CSPin_f cs_fn,
                              const char *name,
                              uint8_t expected_channels)
{
    memset(adc, 0, sizeof(ads131_t));
    adc->fxn.SPITransfer = spi_fn;
    adc->fxn.CSPin       = cs_fn;
    adc->fxn.SYNCPin     = hal_sync_pin;
    adc->fxn.DelayMs     = hal_delay_ms;

    ads131_err_e err = ads131_init(adc);
    if (err != ADS131_OK) {
        ESP_LOGE(TAG, "%s ads131_init failed: %d", name, err);
        return ESP_FAIL;
    }

    //If expected_channels is set and nChannels is wrong, force it
    if (expected_channels > 0 && adc->nChannels != expected_channels) {
        ESP_LOGW(TAG, "%s: nChannels=%d (expected %d), forcing to %d",
                 name, adc->nChannels, expected_channels, expected_channels);
        adc->nChannels = expected_channels;
        adc->DeviceModel = (ads131_model_e)expected_channels;
    }

    //If nChannels is still 0 after init, ADC is not connected
    if (adc->nChannels == 0) {
        ESP_LOGW(TAG, "%s: not detected (nChannels=0)", name);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%s OK — model=ADS131M0%d ch=%d rate=%lu SPS",
             name,
             (int)adc->DeviceModel,
             adc->nChannels,
             (unsigned long)adc->_intern.kSamples);

    return ESP_OK;
}

//Wait for one DRDY edge from ADC A
static ads131_err_e wait_for_drdy_tick(TickType_t timeout_ticks)
{
    if (xSemaphoreTake(s_drdy_sem, timeout_ticks) != pdTRUE) {
        ESP_LOGE(TAG, "DRDY timeout");
        return ADS131_FAILED;
    }
    return ADS131_OK;
}

//Read all 3 ADCs once
static ads131_err_e read_all_now(adc_hal_all_t *adcs,
                                 ads131_channels_val_t *a,
                                 ads131_channels_val_t *b,
                                 ads131_channels_val_t *c)
{
    ads131_err_e r;

    //ADC A required
    if (adcs->a.nChannels == 0 || adcs->a.nChannels > 2) {
        ESP_LOGW(TAG, "ADC-A nChannels=%d invalid, forcing to 2", adcs->a.nChannels);
        adcs->a.nChannels = 2;
    }
    r = ads131_read_all_channel(&adcs->a, a);
    if (r != ADS131_OK) {
        ESP_LOGE(TAG, "read A failed: %d", r);
        return r;
    }

    //ADC B and C only if initialised correctly
    if (adcs->b.nChannels > 0) {
        r = ads131_read_all_channel(&adcs->b, b);
        if (r != ADS131_OK) ESP_LOGW(TAG, "read B failed: %d", r);
    } else {
        memset(b, 0, sizeof(*b));
    }

    if (adcs->c.nChannels > 0) {
        r = ads131_read_all_channel(&adcs->c, c);
        if (r != ADS131_OK) ESP_LOGW(TAG, "read C failed: %d", r);
    } else {
        memset(c, 0, sizeof(*c));
    }

    return ADS131_OK;
}

//Public init
esp_err_t adc_hal_init(adc_hal_all_t *adcs)
{
    esp_err_t err;

    err = mclk_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCLK init: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "MCLK %d Hz on GPIO%d", MCLK_FREQ_HZ, PIN_MCLK);

    //ADS131M02 needs stable MCLK before SYNC/RESET is released
    vTaskDelay(pdMS_TO_TICKS(100));

    err = gpio_init_local();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI init: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SPI ready — CS_A=GPIO%d CS_B=GPIO%d CS_C=GPIO%d",
             PIN_CS_A, PIN_CS_B, PIN_CS_C);

    //ADC-A required pass expected_channels=2 to force correct value if SPI alignment is off
    err = init_one_adc(&adcs->a, hal_spi_transfer_a, hal_cs_pin_a, "ADC-A", 2);
    if (err != ESP_OK) return err;

    //ADC-B and C optional pass 2 if connected, 0 if not
    err = init_one_adc(&adcs->b, hal_spi_transfer_b, hal_cs_pin_b, "ADC-B", 2);
    if (err != ESP_OK) ESP_LOGW(TAG, "ADC-B not detected — phase 2 will read zero");

    err = init_one_adc(&adcs->c, hal_spi_transfer_c, hal_cs_pin_c, "ADC-C", 2);
    if (err != ESP_OK) ESP_LOGW(TAG, "ADC-C not detected — phase 3 will read zero");

    //Write CLOCK register with known good value
    //ADS131M02, HR mode (POWER=2), OSR=1024 (OSR=3), CH0+CH1 enabled (bits 8+9)
    //CLOCK = 0x030E: [15:10]=0 CH1_EN=1 CH0_EN=1 [7:6]=0 TBM=0 OSR=011 POWER=10
    #define ADC_CLOCK_VAL  0x030E
    if (adcs->a.nChannels > 0) {
        uint16_t status_a = 0, id_a = 0;
        ads131_read_reg(&adcs->a, ADS131_REG_STATUS, &status_a);
        ads131_read_reg(&adcs->a, ADS131_REG_ID, &id_a);
        ESP_LOGI(TAG, "ADC-A pre-write: ID=0x%04X STATUS=0x%04X LOCK=%d",
                 id_a, status_a, (status_a >> 15) & 1);
        //Force unlock regardless of LOCK bit state
        ads131_unlock(&adcs->a);
        vTaskDelay(pdMS_TO_TICKS(5));
        ads131_write_reg(&adcs->a, ADS131_REG_CLOCK, ADC_CLOCK_VAL);
        vTaskDelay(pdMS_TO_TICKS(5));
        uint16_t clk_a = 0;
        ads131_read_reg(&adcs->a, ADS131_REG_CLOCK, &clk_a);
        ESP_LOGI(TAG, "ADC-A CLOCK wrote=0x%04X readback=0x%04X %s",
                 ADC_CLOCK_VAL, clk_a,
                 (clk_a == ADC_CLOCK_VAL) ? "OK" : "MISMATCH");
    }
    if (adcs->b.nChannels > 0) {
        uint16_t status_b = 0, id_b = 0;
        ads131_read_reg(&adcs->b, ADS131_REG_STATUS, &status_b);
        ads131_read_reg(&adcs->b, ADS131_REG_ID, &id_b);
        ESP_LOGI(TAG, "ADC-B pre-write: ID=0x%04X STATUS=0x%04X LOCK=%d",
                 id_b, status_b, (status_b >> 15) & 1);
        ads131_unlock(&adcs->b);
        vTaskDelay(pdMS_TO_TICKS(5));
        ads131_write_reg(&adcs->b, ADS131_REG_CLOCK, ADC_CLOCK_VAL);
        vTaskDelay(pdMS_TO_TICKS(5));
        uint16_t clk_b = 0;
        ads131_read_reg(&adcs->b, ADS131_REG_CLOCK, &clk_b);
        ESP_LOGI(TAG, "ADC-B CLOCK wrote=0x%04X readback=0x%04X %s",
                 ADC_CLOCK_VAL, clk_b,
                 (clk_b == ADC_CLOCK_VAL) ? "OK" : "MISMATCH");
    }
    if (adcs->c.nChannels > 0) {
        uint16_t status_c = 0, id_c = 0;
        ads131_read_reg(&adcs->c, ADS131_REG_STATUS, &status_c);
        ads131_read_reg(&adcs->c, ADS131_REG_ID, &id_c);
        ESP_LOGI(TAG, "ADC-C pre-write: ID=0x%04X STATUS=0x%04X LOCK=%d",
                 id_c, status_c, (status_c >> 15) & 1);
        ads131_unlock(&adcs->c);
        vTaskDelay(pdMS_TO_TICKS(5));
        ads131_write_reg(&adcs->c, ADS131_REG_CLOCK, ADC_CLOCK_VAL);
        vTaskDelay(pdMS_TO_TICKS(5));
        uint16_t clk_c = 0;
        ads131_read_reg(&adcs->c, ADS131_REG_CLOCK, &clk_c);
        ESP_LOGI(TAG, "ADC-C CLOCK wrote=0x%04X readback=0x%04X %s",
                 ADC_CLOCK_VAL, clk_c,
                 (clk_c == ADC_CLOCK_VAL) ? "OK" : "MISMATCH");
    }

    drain_drdy_semaphore();

    ESP_LOGI(TAG, "Waiting for first DRDY pulse...");
    if (xSemaphoreTake(s_drdy_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "DRDY never pulsed — count=%lu level=%d",
                 (unsigned long)s_drdy_count,
                 gpio_get_level(PIN_DRDY));
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "DRDY pulsing (ISR count: %lu)", (unsigned long)s_drdy_count);
    return ESP_OK;
}

//Public read all
ads131_err_e adc_hal_read_all(adc_hal_all_t *adcs,
                              ads131_channels_val_t *a,
                              ads131_channels_val_t *b,
                              ads131_channels_val_t *c)
{
    ads131_err_e r;

    //Wait for ADC A DRDY
    r = wait_for_drdy_tick(pdMS_TO_TICKS(20));
    if (r != ADS131_OK) {
        ESP_LOGE(TAG, "adc_hal_read_all: DRDY timeout");
        return r;
    }

    //Read A, B, C right after the same DRDY event
    r = read_all_now(adcs, a, b, c);
    if (r != ADS131_OK) return r;

    return ADS131_OK;
}