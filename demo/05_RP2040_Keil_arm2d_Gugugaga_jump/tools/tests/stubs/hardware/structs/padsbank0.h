#include <stdint.h>
static struct { uint32_t io[30]; } test_pads;
#define padsbank0_hw (&test_pads)
