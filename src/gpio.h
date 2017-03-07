#ifndef GPIO_H
#define GPIO_H

#include "config.h"

#include <fcntl.h>
#include <sys/ioctl.h>

#ifdef HAVE_BOARD_H
	#include <board.h>
	#define gpio_linux_set(...)
	#define gpio_linux_get(...)
	#define gpio_linux_output_init(...)
	#define gpio_linux_input_init(...)

#else
	#define BOARD_IOCTL_SET_GPIO 1
	#define BOARD_IOCTL_LED_CTRL 2
	#define BOARD_IOCTL_GET_GPIO 3
	#define gpio_shift_register_cached_set(...)
	#define gpio_shift_register_init(...)

	int gpio_linux_set(int addr, int val);
	int gpio_linux_get(int addr);
	void gpio_linux_output_init(int addr);
	void gpio_linux_input_init(int addr);
#endif

typedef int gpio_t;

typedef enum {
	GPIO_STATE_LOW,
	GPIO_STATE_HIGH,
} gpio_state_t;

int board_ioctl_init(void);
int board_ioctl(int ioctl_id, int action, int hex, char* string_buf, int string_buf_len, int offset);

#define gpio_init() board_ioctl_init()
gpio_state_t gpio_get_state(gpio_t gpio);
void gpio_set_state(gpio_t gpio, gpio_state_t state,int udelay);

#endif /* GPIO_H */
