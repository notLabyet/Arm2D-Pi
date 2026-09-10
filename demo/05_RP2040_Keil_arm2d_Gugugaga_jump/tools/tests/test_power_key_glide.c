/* gcc -std=c11 -Wall -Wextra -Werror -Itools/tests/stubs
 * tools/tests/test_power_key_glide.c -o _compile_check/test_power_key_glide.exe */
#include <assert.h>
#include <stdio.h>
#include "../../application/power_key_service.c"
static bool input_high, keep;
void gpio_init(unsigned p) { (void)p; }
void gpio_set_function(unsigned p, unsigned f) { (void)p; (void)f; }
void gpio_set_dir(unsigned p, bool out) { (void)p; (void)out; }
void gpio_pull_up(unsigned p) { (void)p; }
void gpio_put(unsigned p, bool v) { assert(p == POWER_KEEP_PIN); keep=v; }
bool gpio_get(unsigned p) { assert(p == POWER_UP_CHECK_PIN); return input_high; }
int main(void)
{
    /* Power-on hold must never become a game jump. */
    power_key_service_poll(0); power_key_service_poll(1);
    input_high=true; power_key_service_poll(10); power_key_service_poll(30);
    assert(keep && !power_key_service_consume_press());
    input_high=false; power_key_service_poll(100); power_key_service_poll(107);
    assert(!power_key_service_is_pressed());
    power_key_service_poll(108);
    assert(power_key_service_is_pressed() && power_key_service_consume_press());
    assert(!power_key_service_consume_press());
    power_key_service_set_airborne(true);
    power_key_service_poll(1500);
    assert(keep && power_key_service_is_pressed());
    /* Touchdown during the same hold cannot suddenly switch power off. */
    power_key_service_set_airborne(false); power_key_service_poll(2500);
    assert(keep);
    /* A short release bounce must not clear the inhibit latch. */
    input_high=true; power_key_service_poll(2501);
    input_high=false; power_key_service_poll(2506); power_key_service_poll(3600);
    assert(keep);
    input_high=true; power_key_service_poll(3610); power_key_service_poll(3639);
    assert(power_key_service_is_pressed());
    power_key_service_poll(3640);
    assert(!power_key_service_is_pressed());
    /* A fresh hold on the ground still retains the original shutdown action. */
    input_high=false; power_key_service_poll(4000); power_key_service_poll(4020);
    power_key_service_poll(4999); assert(keep);
    power_key_service_poll(5000);
    assert(!keep && !power_key_service_is_pressed());
    puts("PASS: startup exclusion, debounce, one press, airborne/landing hold, bounce, fresh ground shutdown");
    return 0;
}
