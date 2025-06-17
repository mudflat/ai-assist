#include "flash_store.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_vfs.h"
#include "esp_spi_flash.h"
#include "wear_levelling.h"
#include <stdio.h>
#include <string.h>

#define FLASH_PART_NAME "storage"
#define FLASH_LOG_TAG "FLASH_STORE"
#define TEMP_FILE_PATH "/spiflash/temp_audio.raw"

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static FILE *s_file = NULL;

esp_err_t flash_store_init(wl_handle_t *out_handle)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiflash",
        .partition_label = FLASH_PART_NAME,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&conf), FLASH_LOG_TAG, "Failed to register SPIFFS");
    ESP_RETURN_ON_ERROR(esp_spiffs_info(FLASH_PART_NAME, NULL, NULL), FLASH_LOG_TAG, "Failed to query SPIFFS");

    s_file = fopen(TEMP_FILE_PATH, "wb+");
    if (!s_file) {
        ESP_LOGE(FLASH_LOG_TAG, "Failed to create temp file");
        return ESP_FAIL;
    }

    if (out_handle) *out_handle = s_wl_handle;
    return ESP_OK;
}

esp_err_t flash_store_write(const void *data, size_t size)
{
    if (!s_file) return ESP_FAIL;
    size_t written = fwrite(data, 1, size, s_file);
    fflush(s_file); // <- Ensure data is written to disk
    return (written == size) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_store_read(void *data, size_t max_size, size_t *bytes_read)
{
    if (!s_file) return ESP_FAIL;
    if (feof(s_file)) {
        *bytes_read = 0;
        return ESP_OK;
    }

    *bytes_read = fread(data, 1, max_size, s_file);
    return (*bytes_read > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_store_reset(void)
{
    if (s_file) {
        fclose(s_file);
        s_file = fopen(TEMP_FILE_PATH, "wb+");
        if (!s_file) {
            ESP_LOGE(FLASH_LOG_TAG, "Failed to reset file");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t flash_store_seek_start(void)
{
    if (s_file) {
        fclose(s_file);
        s_file = fopen(TEMP_FILE_PATH, "rb");
        if (!s_file) {
            ESP_LOGE(FLASH_LOG_TAG, "Failed to reopen file for reading");
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

//size_t flash_store_size(void)
//{
    //return last_size;
//}
