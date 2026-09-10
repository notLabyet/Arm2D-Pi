#ifndef TEST_HARDWARE_TIMER_H
#define TEST_HARDWARE_TIMER_H
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t absolute_time_t;
static uint64_t host_time_us,host_alarm_target;
static void (*host_alarm_callback)(unsigned);
static inline absolute_time_t get_absolute_time(void) { return host_time_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t/1000); }
static inline absolute_time_t delayed_by_us(absolute_time_t t,uint64_t us) { return t+us; }
static inline int hardware_alarm_claim_unused(bool required) { (void)required;return 0; }
static inline void hardware_alarm_set_callback(unsigned n,void (*cb)(unsigned)) { (void)n;host_alarm_callback=cb; }
static inline bool hardware_alarm_set_target(unsigned n,absolute_time_t t) { (void)n;host_alarm_target=t;return t<=host_time_us; }
static inline void hardware_alarm_force_irq(unsigned n) { host_alarm_callback(n); }
static inline uint32_t time_us_32(void) { return (uint32_t)host_time_us; }
#endif
