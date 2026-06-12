#pragma once

#include <assert.h>

#ifndef ESP_STATIC_ASSERT
#define ESP_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
