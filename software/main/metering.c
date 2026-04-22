#include "metering.h"

#include <math.h>       //sqrtf, fabsf
#include <float.h>      //FLT_MAX
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "METERING";

#define WINDOW_MS   (METERING_WINDOW_SECONDS * 1000U)
#define NUM_PHASES  3
#define SQRT2       1.41421356f

//Per-phase accumulator
typedef struct {
    double   v_sum_sq;
    float    v_min;
    float    v_max;
    uint32_t v_spikes;
    float    v_spike_peak_mv;   
    double   i_sum_sq;
    float    i_min;
    float    i_max;
    uint32_t i_spikes;
    float    i_spike_peak_mv;
} phase_acc_t;

//Window state
static struct {
    phase_acc_t ph[NUM_PHASES];
    uint32_t    n;
    TickType_t  window_start;
    uint32_t    packet_index;
} s_acc;

//Public reset
void metering_reset(void)
{
    for (int i = 0; i < NUM_PHASES; i++) {
        s_acc.ph[i].v_sum_sq       = 0.0;
        s_acc.ph[i].v_min          =  FLT_MAX;
        s_acc.ph[i].v_max          = -FLT_MAX;
        s_acc.ph[i].v_spikes       = 0;
        s_acc.ph[i].v_spike_peak_mv = 0.0f;
        s_acc.ph[i].i_sum_sq       = 0.0;
        s_acc.ph[i].i_min          =  FLT_MAX;
        s_acc.ph[i].i_max          = -FLT_MAX;
        s_acc.ph[i].i_spikes       = 0;
        s_acc.ph[i].i_spike_peak_mv = 0.0f;
    }
    s_acc.n            = 0;
    s_acc.window_start = xTaskGetTickCount();
}

//Public add sample
void metering_add_sample(int phase, float voltage_mv, float current_mv)
{
    if (phase < 0 || phase >= NUM_PHASES) return;
    phase_acc_t *p = &s_acc.ph[phase];

    //Voltage
    p->v_sum_sq += (double)voltage_mv * (double)voltage_mv;
    if (voltage_mv < p->v_min) p->v_min = voltage_mv;
    if (voltage_mv > p->v_max) p->v_max = voltage_mv;
    if (fabsf(voltage_mv) >= METERING_V_SPIKE_MV) {
        p->v_spikes++;
        if (fabsf(voltage_mv) > p->v_spike_peak_mv)
            p->v_spike_peak_mv = fabsf(voltage_mv);
    }

    //Current
    p->i_sum_sq += (double)current_mv * (double)current_mv;
    if (current_mv < p->i_min) p->i_min = current_mv;
    if (current_mv > p->i_max) p->i_max = current_mv;
    if (fabsf(current_mv) >= METERING_I_SPIKE_MV) {
        p->i_spikes++;
        if (fabsf(current_mv) > p->i_spike_peak_mv)
            p->i_spike_peak_mv = fabsf(current_mv);
    }

    if (phase == 0) s_acc.n++;
}

//Public poll
esp_err_t metering_poll(metering_packet_t *pkt)
{
    TickType_t elapsed = xTaskGetTickCount() - s_acc.window_start;
    if (elapsed < pdMS_TO_TICKS(WINDOW_MS)) return ESP_ERR_NOT_FOUND;

    pkt->packet_index = s_acc.packet_index++;
    pkt->sample_count = s_acc.n;

    for (int i = 0; i < NUM_PHASES; i++) {
        phase_acc_t      *p = &s_acc.ph[i];
        metering_phase_t *o = &pkt->phase[i];

        //RMS
        o->voltage_rms_mv = (s_acc.n > 0) ? sqrtf((float)(p->v_sum_sq / s_acc.n)) : 0.0f;
        o->current_rms_mv = (s_acc.n > 0) ? sqrtf((float)(p->i_sum_sq / s_acc.n)) : 0.0f;

        //Scaled values
        o->voltage_rms = o->voltage_rms_mv * METERING_PT_RATIOS[i];
        o->current_rms = o->current_rms_mv * METERING_CT_RATIOS[i];

        //Amplitude = RMS * sqrt(2)
        o->voltage_amplitude = o->voltage_rms * SQRT2;
        o->current_amplitude = o->current_rms * SQRT2;

        //Min/max raw mV
        o->voltage_min_mv = (p->v_min ==  FLT_MAX) ? 0.0f : p->v_min;
        o->voltage_max_mv = (p->v_max == -FLT_MAX) ? 0.0f : p->v_max;
        o->current_min_mv = (p->i_min ==  FLT_MAX) ? 0.0f : p->i_min;
        o->current_max_mv = (p->i_max == -FLT_MAX) ? 0.0f : p->i_max;

        //Peak to peak in raw mV and scaled units
        o->voltage_p2p_mv = o->voltage_max_mv - o->voltage_min_mv;
        o->current_p2p_mv = o->current_max_mv - o->current_min_mv;
        o->voltage_p2p    = o->voltage_p2p_mv * METERING_PT_RATIOS[i];
        o->current_p2p    = o->current_p2p_mv * METERING_CT_RATIOS[i];

        //Spikes, convert peak from mV to real units
        o->voltage_spikes     = p->v_spikes;
        o->voltage_spike_peak = p->v_spike_peak_mv * METERING_PT_RATIOS[i];
        o->current_spikes     = p->i_spikes;
        o->current_spike_peak = p->i_spike_peak_mv * METERING_CT_RATIOS[i];
    }

    s_acc.window_start += pdMS_TO_TICKS(WINDOW_MS);

    uint32_t saved_index = s_acc.packet_index;
    metering_reset();
    s_acc.packet_index = saved_index;

    return ESP_OK;
}

//Public network payload
void metering_get_network_payload(const metering_packet_t *pkt,
                                  metering_network_payload_t *out)
{
    out->packet_index = pkt->packet_index;
    for (int i = 0; i < 3; i++) {
        const metering_phase_t *p = &pkt->phase[i];
        out->voltage_rms[i]        = p->voltage_rms;
        out->voltage_peak[i]       = p->voltage_amplitude;
        out->voltage_p2p[i]        = p->voltage_p2p;
        out->current_rms[i]        = p->current_rms;
        out->current_peak[i]       = p->current_amplitude;
        out->current_p2p[i]        = p->current_p2p;
        out->voltage_spikes[i]     = p->voltage_spikes;
        out->voltage_spike_peak[i] = p->voltage_spike_peak;
        out->current_spikes[i]     = p->current_spikes;
        out->current_spike_peak[i] = p->current_spike_peak;
    }
}

//Public log packet
void metering_log_packet(const metering_packet_t *pkt)
{
    ESP_LOGI(TAG, "+----------+----------+----------+----------+----------+----------+----------+");
    ESP_LOGI(TAG, "| PACKET #%-4lu                                          %lu samples / 30 s |",
             (unsigned long)pkt->packet_index,
             (unsigned long)pkt->sample_count);
    ESP_LOGI(TAG, "+----------+----------+----------+----------+----------+----------+----------+");
    ESP_LOGI(TAG, "|  Phase   |  Vrms    |  Vpeak   |  Arms    |  Apeak   | V spikes | I spikes |");
    ESP_LOGI(TAG, "|          |    V     |    V     |    A     |    A     | cnt  max | cnt  max |");
    ESP_LOGI(TAG, "+----------+----------+----------+----------+----------+----------+----------+");

    const char *names[] = { "A / Ph1", "B / Ph2", "C / Ph3" };
    for (int i = 0; i < NUM_PHASES; i++) {
        const metering_phase_t *p = &pkt->phase[i];
        ESP_LOGI(TAG, "| %-7s  | %8.3f | %8.3f | %8.4f | %8.4f |%3lu %5.2f |%3lu %5.3f |",
                 names[i],
                 p->voltage_rms,       p->voltage_amplitude,
                 p->current_rms,       p->current_amplitude,
                 (unsigned long)p->voltage_spikes, p->voltage_spike_peak,
                 (unsigned long)p->current_spikes, p->current_spike_peak);
    }

    ESP_LOGI(TAG, "+----------+----------+----------+----------+----------+----------+----------+");
}