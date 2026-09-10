#include <stdint.h>
static struct { struct { uint32_t status, ctrl; } io[30]; } test_iobank;
#define iobank0_hw (&test_iobank)
