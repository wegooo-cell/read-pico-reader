/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef int wl_handle_t;
#define WL_INVALID_HANDLE -1
esp_err_t wl_unmount(wl_handle_t);
size_t wl_size(wl_handle_t);
esp_err_t wl_read(wl_handle_t,size_t,void*,size_t);
