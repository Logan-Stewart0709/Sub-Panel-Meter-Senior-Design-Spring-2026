/*
 * ADS131M0x.h
 * Original author: pablo-jean, March 2024
 * Modified to work with project code
 */

#ifndef DRIVER_ADS131M0X_ADS131M0X_H_
#define DRIVER_ADS131M0X_ADS131M0X_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>


#define __PACKED_STRUCT struct __attribute__((packed))

#define ADS131M0_DRIVER_INITILIZED  0xC4

#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

#ifndef ADS131_MAX_CHANNELS_CHIPSET
#define ADS131_MAX_CHANNELS_CHIPSET 8
#endif
#if defined(ADS131_MAX_CHANNELS_CHIPSET) && (ADS131_MAX_CHANNELS_CHIPSET > 8)
#undef ADS131_MAX_CHANNELS_CHIPSET
#define ADS131_MAX_CHANNELS_CHIPSET 8
#endif

#ifdef ADS131_USE_DOUBLE_PRECISION
typedef double ads131Float_t;
#else
typedef float ads131Float_t;
#endif

#define ADS131_CMD_NULL     0x0000
#define ADS131_CMD_RESET    0x0011
#define ADS131_CMD_STANDBY  0x0022
#define ADS131_CMD_WAKEUP   0x0033
#define ADS131_CMD_LOCK     0x0555
#define ADS131_CMD_UNLOCK   0x0655
#define ADS131_CMD_RREG     0xA000
#define ADS131_CMD_WREG     0x6000

#define ADS131_REG_ID           0x00
#define ADS131_REG_STATUS       0x01
#define ADS131_REG_MODE         0x02
#define ADS131_REG_CLOCK        0x03
#define ADS131_REG_GAIN1        0x04
#define ADS131_REG_GAIN2        0x05
#define ADS131_REG_CFG          0x06
#define ADS131_REG_THRSHLD_MSB  0x07
#define ADS131_REG_THRSHLD_LSB  0x08
#define ADS131_REG_CH0_CFG      0x09
#define ADS131_REG_CH0_OCAL_MSB 0x0A
#define ADS131_REG_CH0_OCAL_LSB 0x0B
#define ADS131_REG_CH0_GCAL_MSB 0x0C
#define ADS131_REG_CH0_GCAL_LSB 0x0D
#define ADS131_REG_CH1_CFG      0x0E
#define ADS131_REG_CH1_OCAL_MSB 0x0F
#define ADS131_REG_CH1_OCAL_LSB 0x10
#define ADS131_REG_CH1_GCAL_MSB 0x11
#define ADS131_REG_CH1_GCAL_LSB 0x12
#define ADS131_REG_REGMAP_CRC   0x3E
#define ADS131_REG_RESERVED     0x3F

typedef enum {
    ADS131_OK,
    ADS131_FAILED,
    ADS131_NOT_INITIALIZED,
    ADS131_CHANNEL_DOENST_EXISTS,
    ADS131_INVALID_PARAMS,
    ADS131_UNKNOWN = 0xFF
} ads131_err_e;

typedef enum {
    ADS131M01 = 0x1, ADS131M02 = 0x2, ADS131M03 = 0x3, ADS131M04 = 0x4,
    ADS131M05 = 0x5, ADS131M06 = 0x6, ADS131M07 = 0x7, ADS131M08 = 0x8,
} ads131_model_e;

typedef enum {
    ADS131_CHANNEL0 = 0, ADS131_CHANNEL1 = 1, ADS131_CHANNEL2 = 2,
    ADS131_CHANNEL3 = 3, ADS131_CHANNEL4 = 4, ADS131_CHANNEL5 = 5,
    ADS131_CHANNEL6 = 6, ADS131_CHANNEL7 = 7,
} ads131_channel_e;

typedef enum {
    ADS131_GAIN_1   = 0x0, ADS131_GAIN_2   = 0x1, ADS131_GAIN_4   = 0x2,
    ADS131_GAIN_8   = 0x3, ADS131_GAIN_16  = 0x4, ADS131_GAIN_32  = 0x5,
    ADS131_GAIN_64  = 0x6, ADS131_GAIN_128 = 0x7,
} ads131_gain_e;

typedef enum { ADS131_DISABLE, ADS131_ENABLE } ads131_enable_e;

typedef enum {
    ADS131_MUX_AINP_AINN  = 0x0, ADS131_MUX_SHORTED    = 0x1,
    ADS131_MUX_POSITIVE_DC = 0x2, ADS131_MUX_NEGATIVE_DC = 0x3
} ads131_mux_e;

typedef enum {
    ADS131_OSR_128   = 0x0, ADS131_OSR_256   = 0x1, ADS131_OSR_512   = 0x2,
    ADS131_OSR_1024  = 0x3, ADS131_OSR_2048  = 0x4, ADS131_OSR_4096  = 0x5,
    ADS131_OSR_8192  = 0x6, ADS131_OSR_16384 = 0x7, ADS131_OSR_64    = 0x8
} ads131_osr_value_e;

typedef enum {
    ADS131_PM_ULTRA_LOW_POWER = 0x0,
    ADS131_PM_LOW_POWER       = 0x1,
    ADS131_PM_HIGH_RESOLUTION = 0x2,
} ads131_power_mode_e;

typedef union {
    __PACKED_STRUCT {
        uint8_t _r1     : 8;
        uint8_t CHANCNT : 4;
        uint8_t _r2     : 4;
    };
    uint16_t _raw;
} ads131_reg_id_t;

typedef union {
    __PACKED_STRUCT {
        uint8_t DRDY0   : 1; uint8_t DRDY1   : 1; uint8_t DRDY2   : 1;
        uint8_t DRDY3   : 1; uint8_t DRDY4   : 1; uint8_t DRDY5   : 1;
        uint8_t DRDY6   : 1; uint8_t DRDY7   : 1;
        uint8_t WLENGTH : 2; uint8_t RESET   : 1; uint8_t CRC_TYPE: 1;
        uint8_t CRC_ERR : 1; uint8_t REG_MAP : 1; uint8_t F_RESYNC: 1;
        uint8_t LOCK    : 1;
    };
    uint16_t _raw;
} ads131_reg_status_t;

typedef union {
    __PACKED_STRUCT {
        uint8_t POWER : 2; uint8_t OSR : 3; uint8_t TBM : 1; uint8_t _r1 : 2;
        uint8_t CH0_EN: 1; uint8_t CH1_EN: 1; uint8_t CH2_EN: 1; uint8_t CH3_EN: 1;
        uint8_t CH4_EN: 1; uint8_t CH5_EN: 1; uint8_t CH6_EN: 1; uint8_t CH7_EN: 1;
    };
    uint16_t _raw;
} ads131_reg_clock_t;

typedef union {
    __PACKED_STRUCT {
        uint8_t PGAGAIN0: 3; uint8_t _res1: 1;
        uint8_t PGAGAIN1: 3; uint8_t _res2: 1;
        uint8_t PGAGAIN2: 3; uint8_t _res3: 1;
        uint8_t PGAGAIN3: 3; uint8_t _res4: 1;
    };
    uint16_t _raw;
} ads131_reg_gain1_t;

typedef union {
    __PACKED_STRUCT {
        uint8_t MUX: 2; uint8_t DCBLK_DIS0: 1; uint8_t _res1: 3;
        uint16_t PHASE: 10;
    };
    uint16_t _raw;
} ads131_reg_chx_cfg_t;

typedef struct {
    ads131_gain_e Gain;
    uint8_t enabled;
} ads131_ch_info_t;

typedef void    (*Lock_f)(void);
typedef void    (*Unlock_f)(void);
typedef void    (*CSPin_f)(uint8_t Sig);
typedef void    (*SYNCPin_f)(uint8_t Sig);
typedef void    (*DelayMs_f)(uint32_t ms);
typedef uint8_t (*SPITransfer_f)(uint8_t *Tx, uint8_t *Rx, uint32_t len);

typedef struct {
    int32_t       ChannelRaw[ADS131_MAX_CHANNELS_CHIPSET];
    ads131Float_t ChannelOffset[ADS131_MAX_CHANNELS_CHIPSET];
    ads131Float_t ChannelVoltageMv[ADS131_MAX_CHANNELS_CHIPSET];
} ads131_channels_val_t;

typedef struct {
    ads131_model_e DeviceModel;
    uint8_t  nChannels;
    uint8_t  WordSize;
    uint32_t oscFreq;
    struct {
        Lock_f        Lock;
        Unlock_f      Unlock;
        CSPin_f       CSPin;
        SYNCPin_f     SYNCPin;
        DelayMs_f     DelayMs;
        SPITransfer_f SPITransfer;
        SPITransfer_f SPITransferIRQ;
    } fxn;
    struct {
        ads131_ch_info_t    ChInfo[ADS131_MAX_CHANNELS_CHIPSET];
        ads131Float_t       ReferenceVoltage;
        uint8_t             Initialized;
        uint32_t            kSamples;
        uint8_t             BufferForRxIrq[32];
        uint8_t             BufferForTxIrq[32];
        ads131_channels_val_t readChannBuff;
    } _intern;
} ads131_t;

ads131_err_e ads131_init(ads131_t *Ads131);
ads131_err_e ads131_unlock(ads131_t *Ads131);
ads131_err_e ads131_write_reg(ads131_t *Ads131, uint32_t reg, uint16_t val);
ads131_err_e ads131_read_reg(ads131_t *Ads131, uint32_t reg, uint16_t *val);
ads131_err_e ads131_set_gain(ads131_t *Ads131, ads131_channel_e Channel, ads131_gain_e GainLevel);
ads131_err_e ads131_set_channel_enable(ads131_t *Ads131, ads131_channel_e Channel, ads131_enable_e Enable);
ads131_err_e ads131_set_mux(ads131_t *Ads131, ads131_channel_e Channel, ads131_mux_e Mux);
ads131_err_e ads131_set_osr(ads131_t *Ads131, ads131_osr_value_e Osr);
ads131_err_e ads131_set_power_mode(ads131_t *Ads131, ads131_power_mode_e PowerMode);
ads131_err_e ads131_read_all_channel(ads131_t *Ads131, ads131_channels_val_t *val);
ads131_err_e ads131_read_one_channel(ads131_t *Ads131, ads131_channel_e Channel, uint32_t *raw, ads131Float_t *miliVolt);
ads131_channels_val_t ads131_spi_transfer_irq(ads131_t *Ads131);
ads131_err_e ads131_read_all_channel_irq(ads131_t *Ads131);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_ADS131M0X_ADS131M0X_H_ */