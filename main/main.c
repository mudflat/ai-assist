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
#define BUFFER_SIZE      1024
#define BEEP_HZ          1000
#define BEEP_MS          200

static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;

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
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .slot_mask = I2S_STD_SLOT_LEFT
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
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .slot_mask = I2S_STD_SLOT_LEFT
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

void app_main(void)
{
    ESP_LOGI(TAG, "Booting...");
    ESP_ERROR_CHECK(wifi_connect_sta(ssid, password, pdMS_TO_TICKS(10000)));

    init_button();
    flash_store_init(NULL);
    init_i2s_rx();
    init_i2s_tx();

    uint8_t buffer[BUFFER_SIZE];

    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            flash_store_reset();
            ESP_LOGI(TAG, "Recording...");
            while (gpio_get_level(BUTTON_GPIO) == 0) {
                size_t bytes_read = 0;
                if (i2s_channel_read(rx_handle, buffer, BUFFER_SIZE, &bytes_read, portMAX_DELAY) == ESP_OK) {
                    flash_store_write(buffer, bytes_read);
                }
            }
            ESP_LOGI(TAG, "Recording done");

            play_beep();
            vTaskDelay(pdMS_TO_TICKS(100));

            flash_store_seek_start();
            ESP_LOGI(TAG, "Playing...");
            //ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
            vTaskDelay(pdMS_TO_TICKS(500));

            size_t bytes_read;
            while (flash_store_read(buffer, BUFFER_SIZE, &bytes_read) == ESP_OK && bytes_read > 0) {
                size_t bytes_written;
                i2s_channel_write(tx_handle, buffer, bytes_read, &bytes_written, portMAX_DELAY);
            }
            ESP_LOGI(TAG, "Playback complete");
            
            //ESP_ERROR_CHECK(i2s_channel_disable(tx_handle));
            //gpio_set_level(I2S_SPK_SD_IO, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

