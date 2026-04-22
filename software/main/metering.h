#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//Scaling PT and CT turns ratios
//Convert ADC AC millivolts to real volts / amps
//Global ratios
#define METERING_PT_RATIO   0.0134f
#define METERING_CT_RATIO   0.000411167f

//Per phase PT ratios, set each individually to calibrate
#define METERING_PT_RATIO_PH1   METERING_PT_RATIO*.94
#define METERING_PT_RATIO_PH2   METERING_PT_RATIO*.732
#define METERING_PT_RATIO_PH3   METERING_PT_RATIO*1.27

//Per phase CT ratios, set each individually to calibrate
#define METERING_CT_RATIO_PH1   METERING_CT_RATIO*1.33
#define METERING_CT_RATIO_PH2   METERING_CT_RATIO*.97
#define METERING_CT_RATIO_PH3   METERING_CT_RATIO*1.55

static const float METERING_PT_RATIOS[3] = {
    METERING_PT_RATIO_PH1,
    METERING_PT_RATIO_PH2,
    METERING_PT_RATIO_PH3,
};

static const float METERING_CT_RATIOS[3] = {
    METERING_CT_RATIO_PH1,
    METERING_CT_RATIO_PH2,
    METERING_CT_RATIO_PH3,
};

//Nominal values
#define METERING_NOMINAL_VOLTAGE    12.0f
#define METERING_NOMINAL_CURRENT    2.25f

//Packet window duration
#define METERING_WINDOW_SECONDS     (1 * 30U)

//For sine wave estimate peaks from RMS
#define METERING_SQRT2              1.41421356f

//Spike threshold, triggers at around ADC saturation (±1200 mV)
#define METERING_V_SPIKE_MV 1140.0f
#define METERING_I_SPIKE_MV 1140.0f
//Per phase values in one packet
typedef struct {
    //Raw AC centered ADC values
    float    voltage_rms_mv;
    float    current_rms_mv;

    //Scaled RMS
    float    voltage_rms;
    float    current_rms;

    //Sine estimated peak from RMS
    float    voltage_amplitude;
    float    current_amplitude;

    //AC centered min/max in ADC mV
    float    voltage_min_mv;
    float    voltage_max_mv;
    float    current_min_mv;
    float    current_max_mv;

    //Peak to peak in ADC mV and scaled units
    float    voltage_p2p_mv;
    float    current_p2p_mv;
    float    voltage_p2p;
    float    current_p2p;

    //Spikes
    uint32_t voltage_spikes;
    float    voltage_spike_peak;
    uint32_t current_spikes;
    float    current_spike_peak;
} metering_phase_t;

//3 phase packet
typedef struct {
    uint32_t         packet_index;
    uint32_t         sample_count;
    metering_phase_t phase[3];
} metering_packet_t;

//Network payload
typedef struct {
    uint32_t packet_index;

    float    voltage_rms[3];
    float    voltage_peak[3];
    float    voltage_p2p[3];

    float    current_rms[3];
    float    current_peak[3];
    float    current_p2p[3];

    uint32_t voltage_spikes[3];
    float    voltage_spike_peak[3];

    uint32_t current_spikes[3];
    float    current_spike_peak[3];
} metering_network_payload_t;

//Reset all accumulators
void metering_reset(void);

//Feed one sample into the accumulators
void metering_add_sample(int phase, float voltage_mv, float current_mv);

//Poll packet completion
esp_err_t metering_poll(metering_packet_t *pkt);

//Flatten packet for network
void metering_get_network_payload(const metering_packet_t *pkt,
                                  metering_network_payload_t *out);

//Log packet
void metering_log_packet(const metering_packet_t *pkt);

#ifdef __cplusplus
}
#endif