#pragma once

#include "esp_err.h"
#include "wear_levelling.h"

esp_err_t flash_store_init(wl_handle_t *out_handle);
esp_err_t flash_store_write(const void *data, size_t size);
esp_err_t flash_store_read(void *data, size_t max_size, size_t *bytes_read);
esp_err_t flash_store_reset(void);
esp_err_t flash_store_seek_start(void);
//size_t flash_store_size(void);


//esp_err_t flash_store_init();
//esp_err_t flash_store_write(size_t offset, const void *data, size_t len);
//esp_err_t flash_store_erase();
//wl_handle_t flash_store_get_handle(void);