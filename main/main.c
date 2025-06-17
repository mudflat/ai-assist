#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "flash_store.h"
#include "wifi_connect.h"
#include "sdkconfig.h"

#define TAG "STEP5_STT"

const char *ssid = CONFIG_WIFI_SSID;
const char *password = CONFIG_WIFI_PASSWORD;
const char *openai_key = CONFIG_OPENAI_API_KEY;

#define BUTTON_GPIO GPIO_NUM_1

// I2S MIC (INMP441)
#define I2S_MIC_WS_IO    GPIO_NUM_4
#define I2S_MIC_SCK_IO   GPIO_NUM_5
#define I2S_MIC_SD_IO    GPIO_NUM_2

// I2S SPEAKER (MAX98357A)
#define I2S_SPK_WS_IO    GPIO_NUM_38
#define I2S_SPK_SCK_IO   GPIO_NUM_35
#define I2S_SPK_SD_IO    GPIO_NUM_14

#define SAMPLE_RATE      16000
#define BUFFER_SIZE      4096
#define BEEP_HZ          1000
#define BEEP_MS          100

static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;
static bool was_pressed = false;

static void init_button()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static void init_i2s_rx()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_LEFT | I2S_STD_SLOT_RIGHT,
            .ws_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_MIC_SCK_IO,
            .ws = I2S_MIC_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_MIC_SD_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S RX initialized");
}

static void init_i2s_tx()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        //.clk_cfg = {
            //.sample_rate_hz = SAMPLE_RATE,
            //.clk_src = I2S_CLK_SRC_DEFAULT,
            //.mclk_multiple = I2S_MCLK_MULTIPLE_256,
            //.bclk_div = 4,  // 16-bit mono @ 16kHz → 256x oversample
        //},
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SPK_SCK_IO,
            .ws = I2S_SPK_WS_IO,
            .dout = I2S_SPK_SD_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "I2S TX initialized");
}

static void play_beep()
{
    const int samples = SAMPLE_RATE * BEEP_MS / 1000;
    int16_t *sine_wave = heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    if (!sine_wave) {
        ESP_LOGE(TAG, "Failed to allocate sine wave buffer");
        return;
    }

    float volume = 0.5f;
    for (int i = 0; i < samples; i++) {
        float theta = (2.0f * M_PI * BEEP_HZ * i) / SAMPLE_RATE;
        sine_wave[i] = (int16_t)(volume * 32767.0f * sinf(theta));
    }

    size_t bytes_written;
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle, sine_wave, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY));
    free(sine_wave);
    ESP_LOGI(TAG, "Beep played");
}

bool is_button_pressed()
{
    static int stable_state = 1;
    static int last_state = 1;
    static TickType_t last_debounce_time = 0;

    int current_state = gpio_get_level(BUTTON_GPIO);
    TickType_t now = xTaskGetTickCount();

    if (current_state != last_state) {
        last_debounce_time = now;
    }

    if ((now - last_debounce_time) > pdMS_TO_TICKS(50)) {
        if (current_state != stable_state) {
            stable_state = current_state;
            last_state = current_state;
            return stable_state == 0;  // Press detected on falling edge
        }
    }

    last_state = current_state;
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting...");
    ESP_ERROR_CHECK(wifi_connect_sta(ssid, password, pdMS_TO_TICKS(10000)));

    uint8_t *buffer = heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    int16_t *converted = heap_caps_malloc(BUFFER_SIZE * 2, MALLOC_CAP_DEFAULT);

    init_button();
    flash_store_init(NULL);
    init_i2s_rx();
    init_i2s_tx();
    ESP_LOGI(TAG, "TX sample rate: %d Hz, BCLK div: %d", SAMPLE_RATE, 4);

    while (1) {
        if (is_button_pressed()) {
            TickType_t start_tick = xTaskGetTickCount();

            flash_store_reset();
            ESP_LOGI(TAG, "Recording...");
            size_t total_written = 0;
            while (gpio_get_level(BUTTON_GPIO) == 0) {
                size_t bytes_read = 0;
                if (i2s_channel_read(rx_handle, buffer, BUFFER_SIZE, &bytes_read, portMAX_DELAY) == ESP_OK) {
                    //total_written += bytes_read;

                    //int16_t *samples_in = (int16_t *)buffer;
                    //flash_store_write(samples_in, bytes_read);


                    int16_t *stereo_samples = (int16_t *)buffer;
                    size_t sample_count = bytes_read / sizeof(int16_t);

                    for (size_t i = 0, j = 0; i < sample_count; i += 2, j++) {
                        converted[j] = stereo_samples[i];  // Only LEFT channel
                    }
                    size_t mono_bytes = (sample_count / 2) * sizeof(int16_t);
                    flash_store_write(converted, mono_bytes);
                    total_written += mono_bytes;
                    ESP_LOGI(TAG, "Sample[0]=%d L, Sample[1]=%d R (should be noise)", stereo_samples[0], stereo_samples[1]);
                }
            }

            ESP_LOGI(TAG, "Final record duration: %.2fs (samples: %d @ 16kHz)", total_written / 2 / 16000.0, total_written / 2);

            TickType_t duration_ticks = xTaskGetTickCount() - start_tick;
            ESP_LOGI(TAG, "Held button for %.2f seconds", duration_ticks * portTICK_PERIOD_MS / 1000.0f);

            //play_beep();
            vTaskDelay(pdMS_TO_TICKS(100));

            flash_store_seek_start();
            ESP_LOGI(TAG, "Playing...");
            vTaskDelay(pdMS_TO_TICKS(500));

            TickType_t startreplay_tick = xTaskGetTickCount();

            size_t bytes_read, total_played = 0;
            while (flash_store_read(buffer, BUFFER_SIZE, &bytes_read) == ESP_OK && bytes_read > 0) {
                total_played += bytes_read;

                int16_t *samples = (int16_t *)buffer;
                size_t sample_count = bytes_read / sizeof(int16_t);

                //for (size_t i = 0; i < sample_count; i++) {
                    //int32_t boosted = samples[i] * 2;
                    //if (boosted > INT16_MAX) boosted = INT16_MAX;
                    //if (boosted < INT16_MIN) boosted = INT16_MIN;
                    //converted[i] = (int16_t)boosted;
                //}

                size_t bytes_written;
                i2s_channel_write(tx_handle, samples, sample_count * sizeof(int16_t), &bytes_written, portMAX_DELAY);

                //i2s_channel_write(tx_handle, converted, sample_count * sizeof(int16_t), &bytes_written, portMAX_DELAY);

                ESP_LOGI(TAG, "Playback: read=%d bytes (stereo), mono_samples=%d, written=%d", bytes_read, sample_count, bytes_written);
            }

            TickType_t durationreplay_ticks = xTaskGetTickCount() - startreplay_tick;
            ESP_LOGI(TAG, "Replay for %.2f seconds", durationreplay_ticks * portTICK_PERIOD_MS / 1000.0f);
            ESP_LOGI(TAG, "Final playback duration: %.2fs (samples: %d @ 16kHz)", total_played / 2 / 16000.0, total_played / 2);

            // Stop I2S TX cleanly
            ESP_ERROR_CHECK(i2s_channel_disable(tx_handle));
            
            // Mute SD line to speaker
            gpio_set_direction(I2S_SPK_SD_IO, GPIO_MODE_OUTPUT);
            gpio_set_level(I2S_SPK_SD_IO, 0);
            ESP_LOGI(TAG, "Audio TX disabled");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!buffer || !converted) {
            ESP_LOGE(TAG, "Failed to allocate audio buffers");
            return;
        }
    }
    free(buffer);
    free(converted);
}

