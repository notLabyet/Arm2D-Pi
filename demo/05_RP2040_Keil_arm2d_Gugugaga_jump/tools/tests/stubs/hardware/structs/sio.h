#include <stdint.h>
static struct { uint32_t gpio_in, gpio_oe; } test_sio;
#define sio_hw (&test_sio)
