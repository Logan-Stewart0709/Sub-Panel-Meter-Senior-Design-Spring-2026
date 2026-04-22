#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "mqtt_client.h"

#include "ADS131M0x.h"
#include "adc_hal.h"
#include "metering.h"

//WiFi credentials
#define WIFI_SSID        "NICKS-LAPTOP 3488"
#define WIFI_PASSWORD    "AU67375r"

//MQTT broker
#define MQTT_BROKER_URI  "mqtt://mqtt-dashboard.com:1883"
#define MQTT_USERNAME    ""
#define MQTT_PASSWORD    ""
#define MQTT_CLIENT_ID   "gary_esp32_test"
#define MQTT_TOPIC       "test/ourTestTopic"

static const char *TAG = "MAIN";

//Event bits
#define EVT_PKT_DISPLAY   (1 << 0)
#define EVT_PKT_NETWORK   (1 << 1)
#define EVT_PKT_ALL       (EVT_PKT_DISPLAY | EVT_PKT_NETWORK)
#define EVT_MQTT_READY    (1 << 2)

static EventGroupHandle_t s_evt;

//Shared ADC state
typedef struct {
    ads131_channels_val_t a, b, c;
} snapshot_t;

static snapshot_t                 s_latest;
static SemaphoreHandle_t          s_latest_mutex;

static metering_packet_t          s_pending_pkt;
static metering_network_payload_t s_pending_payload;
static SemaphoreHandle_t          s_payload_mutex;

//ADC info captured after init
static uint8_t  s_adc_ch[3]  = {0, 0, 0};
static uint32_t s_adc_sps[3] = {0, 0, 0};

//MQTT client handle
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

//Payload getter
void metering_get_latest_payload(metering_network_payload_t *out)
{
    xSemaphoreTake(s_payload_mutex, portMAX_DELAY);
    *out = s_pending_payload;
    xSemaphoreGive(s_payload_mutex);
}

//Fast ADC task
static void adc_task(void *arg)
{
    adc_hal_all_t *adcs = (adc_hal_all_t *)arg;
    ads131_channels_val_t a, b, c;

    //Wait for MQTT before opening metering window so first packet isn't dropped
    ESP_LOGI(TAG, "Waiting for MQTT connection before starting metering...");
    xEventGroupWaitBits(s_evt, EVT_MQTT_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "MQTT ready — starting metering window");
    metering_reset();

    for (;;) {
        if (adc_hal_read_all(adcs, &a, &b, &c) != ADS131_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        metering_add_sample(0, a.ChannelVoltageMv[1], a.ChannelVoltageMv[0]);
        metering_add_sample(1, b.ChannelVoltageMv[1], b.ChannelVoltageMv[0]);
        metering_add_sample(2, c.ChannelVoltageMv[1], c.ChannelVoltageMv[0]);

        if (xSemaphoreTake(s_latest_mutex, 0) == pdTRUE) {
            s_latest.a = a;
            s_latest.b = b;
            s_latest.c = c;
            xSemaphoreGive(s_latest_mutex);
        }

        metering_packet_t pkt;
        if (metering_poll(&pkt) == ESP_OK) {
            xSemaphoreTake(s_payload_mutex, portMAX_DELAY);
            s_pending_pkt = pkt;
            metering_get_network_payload(&pkt, &s_pending_payload);
            xSemaphoreGive(s_payload_mutex);
            xEventGroupSetBits(s_evt, EVT_PKT_ALL);
        }
    }
}

//Display task
static void display_task(void *arg)
{
    snapshot_t snap;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (xSemaphoreTake(s_latest_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            snap = s_latest;
            xSemaphoreGive(s_latest_mutex);
            ESP_LOGI(TAG, "Ph1  V:%8.3f mV  I:%8.3f mV  |"
                          "  Ph2  V:%8.3f mV  I:%8.3f mV  |"
                          "  Ph3  V:%8.3f mV  I:%8.3f mV",
                     snap.a.ChannelVoltageMv[1], snap.a.ChannelVoltageMv[0],
                     snap.b.ChannelVoltageMv[1], snap.b.ChannelVoltageMv[0],
                     snap.c.ChannelVoltageMv[1], snap.c.ChannelVoltageMv[0]);
        }

        EventBits_t bits = xEventGroupGetBits(s_evt);
        if (bits & EVT_PKT_DISPLAY) {
            xSemaphoreTake(s_payload_mutex, portMAX_DELAY);
            metering_log_packet(&s_pending_pkt);
            xSemaphoreGive(s_payload_mutex);
            
            xEventGroupClearBits(s_evt, EVT_PKT_DISPLAY);
        }
    }
}

//Network task
static void network_task(void *arg)
{
    metering_network_payload_t payload;
    char msg[640];

    for (;;) {
        xEventGroupWaitBits(s_evt, EVT_PKT_NETWORK,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        metering_get_latest_payload(&payload);

        snprintf(msg, sizeof(msg),
            "{"
            "\"pkt\":%lu,"
            "\"ch\":[%d,%d,%d],"
            "\"sps\":[%lu,%lu,%lu],"
            "\"v_rms\":[%.3f,%.3f,%.3f],"
            "\"v_peak\":[%.3f,%.3f,%.3f],"
            "\"i_rms\":[%.4f,%.4f,%.4f],"
            "\"i_peak\":[%.4f,%.4f,%.4f],"
            "\"v_spk\":[%lu,%lu,%lu],"
            "\"v_spk_pk\":[%.3f,%.3f,%.3f],"
            "\"i_spk\":[%lu,%lu,%lu],"
            "\"i_spk_pk\":[%.4f,%.4f,%.4f]"
            "}",
            (unsigned long)payload.packet_index,
            s_adc_ch[0], s_adc_ch[1], s_adc_ch[2],
            (unsigned long)s_adc_sps[0], (unsigned long)s_adc_sps[1], (unsigned long)s_adc_sps[2],
            payload.voltage_rms[0],  payload.voltage_rms[1],  payload.voltage_rms[2],
            payload.voltage_peak[0], payload.voltage_peak[1], payload.voltage_peak[2],
            payload.current_rms[0],  payload.current_rms[1],  payload.current_rms[2],
            payload.current_peak[0], payload.current_peak[1], payload.current_peak[2],
            (unsigned long)payload.voltage_spikes[0],
            (unsigned long)payload.voltage_spikes[1],
            (unsigned long)payload.voltage_spikes[2],
            payload.voltage_spike_peak[0],
            payload.voltage_spike_peak[1],
            payload.voltage_spike_peak[2],
            (unsigned long)payload.current_spikes[0],
            (unsigned long)payload.current_spikes[1],
            (unsigned long)payload.current_spikes[2],
            payload.current_spike_peak[0],
            payload.current_spike_peak[1],
            payload.current_spike_peak[2]);

        if (s_mqtt_client != NULL) {
            esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC, msg, 0, 1, 0);
            ESP_LOGI(TAG, "Published packet #%lu", (unsigned long)payload.packet_index);
        } else {
            ESP_LOGW(TAG, "MQTT not connected — packet #%lu dropped",
                     (unsigned long)payload.packet_index);
        }

        xEventGroupClearBits(s_evt, EVT_PKT_NETWORK);
    }
}

//MQTT event handler
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_evt, EVT_MQTT_READY);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            xEventGroupClearBits(s_evt, EVT_MQTT_READY);
            break;
        default:
            break;
    }
}

//MQTT init
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri                              = MQTT_BROKER_URI;
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
    mqtt_cfg.broker.verification.use_global_ca_store         = false;
    mqtt_cfg.broker.verification.crt_bundle_attach           = NULL;
    mqtt_cfg.credentials.client_id                           = MQTT_CLIENT_ID;
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username                      = MQTT_USERNAME;
        mqtt_cfg.credentials.authentication.password      = MQTT_PASSWORD;
    }

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, MQTT_EVENT_ANY,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

//WiFi event handler
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected — retrying...");
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected — IP: " IPSTR, IP2STR(&event->ip_info.ip));
        mqtt_app_start();
    }
}

//WiFi init
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid,     WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode  = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable     = true;
    wifi_config.sta.pmf_cfg.required    = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done");
}

//app_main
extern "C" void app_main(void);

void app_main(void)
{
    ESP_LOGI(TAG, "=== ADS131M02 three-phase metering ===");

    ESP_ERROR_CHECK(nvs_flash_init());

    //Init ADC first, LEDC/MCLK must start before WiFi claims the clock tree
    static adc_hal_all_t adcs;
    esp_err_t err = adc_hal_init(&adcs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_hal_init failed: %s — halting", esp_err_to_name(err));
        return;
    }

    //Capture ch and sps before nChannels gets forced to 2
    s_adc_ch[0]  = adcs.a.nChannels;
    s_adc_ch[1]  = adcs.b.nChannels;
    s_adc_ch[2]  = adcs.c.nChannels;
    s_adc_sps[0] = adcs.a._intern.kSamples;
    s_adc_sps[1] = adcs.b._intern.kSamples;
    s_adc_sps[2] = adcs.c._intern.kSamples;

    s_evt           = xEventGroupCreate();
    s_latest_mutex  = xSemaphoreCreateMutex();
    s_payload_mutex = xSemaphoreCreateMutex();

    //WiFi starts after ADC is already running
    wifi_init_sta();

    xTaskCreatePinnedToCore(adc_task,     "adc",     4096, &adcs, 10, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL,   2, NULL, 0);
    xTaskCreatePinnedToCore(network_task, "network", 4096, NULL,   3, NULL, 0);

    //Keep app_main alive
    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}