#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t wifi_connect_sta(const char *ssid, const char *password, TickType_t timeout_ticks);
