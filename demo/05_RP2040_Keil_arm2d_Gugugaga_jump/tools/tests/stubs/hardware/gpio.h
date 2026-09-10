#include <stdbool.h>
#define GPIO_FUNC_SIO 5
#define GPIO_OUT 1
#define GPIO_IN 0
void gpio_init(unsigned pin);
void gpio_set_function(unsigned pin, unsigned function);
void gpio_set_dir(unsigned pin, bool output);
void gpio_put(unsigned pin, bool value);
void gpio_pull_up(unsigned pin);
bool gpio_get(unsigned pin);
