#pragma once

#include "esp_err.h"
#include "ADS131M0x.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ads131_t a;   //Phase A DRDY wired
    ads131_t b;   // Phase B
    ads131_t c;   // Phase C
} adc_hal_all_t;

//Initialise MCLK, SPI bus, GPIO, and all three ADS131M0x devices
//Call once from app_main before WiFi starts
esp_err_t adc_hal_init(adc_hal_all_t *adcs);

//Block on ADC A DRDY, then read all three ADCs
ads131_err_e adc_hal_read_all(adc_hal_all_t *adcs,
                              ads131_channels_val_t *a,
                              ads131_channels_val_t *b,
                              ads131_channels_val_t *c);

#ifdef __cplusplus
}
#endif