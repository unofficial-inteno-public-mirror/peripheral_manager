#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "led.h"
#include "log.h"
#include "server.h"
#include "gpio.h"
#include "gpio_shift_register.h"

gpio_shift_register_t led_gpio_shift_register;

void gpio_led_init(struct server_ctx *s_ctx);

typedef enum {
	LOW,
	HI,
	UNKNOWN,
} active_t;

typedef enum {
	MODE_UNKNOWN,
	DIRECT,
	SHIFTREG_BRCM,
	SHIFTREG_GPIO,
	GPIO_LINUX,
} gpio_mode_t;

struct gpio_led_data {
	int addr;
	int delay;		/* time to wait for a gpio set to complete, HW workaround, do not set normally */
	active_t active;
	int state;
	gpio_mode_t mode;
	struct led_drv led;
};

static int gpio_led_set_state(struct led_drv *drv, led_state_t state)
{
	struct gpio_led_data *p = (struct gpio_led_data *)drv->priv;
	int bit_val = 0;

	if(state) {
		if(p->active)
			bit_val=1;
	} else {
		if(!p->active)
			bit_val=1;
	}

	p->state = state;

	switch (p->mode) {
	case DIRECT:
		gpio_set_state(p->addr, bit_val,p->delay);
		break;
	case GPIO_LINUX:
		gpio_linux_set(p->addr, bit_val);
		break;
	case SHIFTREG_BRCM:
		board_ioctl( BOARD_IOCTL_LED_CTRL, 0, 0, NULL, p->addr, bit_val);
		break;
	case SHIFTREG_GPIO:
		gpio_shift_register_cached_set(&led_gpio_shift_register, p->addr, bit_val);
		break;
	default:
		DBG(1,"access mode not supported [%d,%s]", p->mode, p->led.name);
	}

	return p->state;
}

static led_state_t gpio_led_get_state(struct led_drv *drv)
{
	struct gpio_led_data *p = (struct gpio_led_data *)drv->priv;
	DBG(1, "state for %s",  drv->name);

	return p->state;
}

static int gpio_led_support(struct led_drv *drv, led_state_t state)
{
	switch (state) {

	case OFF:
	case ON:
		return 1;
		break;

	default:
		return 0;
	}
	return 0;
}

static struct led_drv_func func = {
	.set_state = gpio_led_set_state,
	.get_state = gpio_led_get_state,
	.support   = gpio_led_support,
};

void gpio_led_init(struct server_ctx *s_ctx) {

	LIST_HEAD(leds);
	struct ucilist *node;
	int gpio_shiftreg_clk=-1, gpio_shiftreg_dat=-1, gpio_shiftreg_mask=-1, gpio_shiftreg_bits=0;
	char *s;

	DBG(1, "");

	if((s=ucix_get_option(s_ctx->uci_ctx, "hw", "board", "gpio_shiftreg_clk")))
		gpio_shiftreg_clk = strtol(s,0,0);
	DBG(1, "gpio_shiftreg_clk = [%d]", gpio_shiftreg_clk);
	if((s=ucix_get_option(s_ctx->uci_ctx, "hw", "board", "gpio_shiftreg_dat")))
		gpio_shiftreg_dat = strtol(s,0,0);
	DBG(1, "gpio_shiftreg_dat = [%d]", gpio_shiftreg_dat);
	if((s=ucix_get_option(s_ctx->uci_ctx, "hw", "board", "gpio_shiftreg_mask")))
		gpio_shiftreg_mask = strtol(s,0,0);
	DBG(1, "gpio_shiftreg_mask = [%d]", gpio_shiftreg_mask);
	if((s=ucix_get_option(s_ctx->uci_ctx, "hw", "board", "gpio_shiftreg_bits")))
		gpio_shiftreg_bits = strtol(s,0,0);
	DBG(1, "gpio_shiftreg_bits = [%d]", gpio_shiftreg_bits);

	ucix_get_option_list(s_ctx->uci_ctx, "hw" ,"gpio_leds", "leds", &leds);
	list_for_each_entry(node,&leds,list){
		struct gpio_led_data *data;
		const char *s;

		DBG(1, "value = [%s]",node->val);

		data = malloc(sizeof(struct gpio_led_data));
		memset(data,0,sizeof(struct gpio_led_data));

		data->led.name = node->val;

		s = ucix_get_option(s_ctx->uci_ctx, "hw" , data->led.name, "addr");
		DBG(1, "addr = [%s]", s);
		if (s) {
			data->addr =  strtol(s,0,0);
		}

		s = ucix_get_option(s_ctx->uci_ctx, "hw" , data->led.name, "delay");
		DBG(1, "delay = [%s]", s);
		if (s) {
			data->delay =  strtol(s,0,0);
		}

		s = ucix_get_option(s_ctx->uci_ctx, "hw" , data->led.name, "mode");
		DBG(1, "mode = [%s]", s);
		if (s) {

			if (!strncasecmp("direct",s,6))
				data->mode =  DIRECT;
			else if (!strncasecmp("linux",s,5))
				data->mode =  GPIO_LINUX;
			else if (!strncasecmp("sr",s,2))
				data->mode =  SHIFTREG_BRCM;
			else if (!strncasecmp("csr",s,3))
				data->mode =  SHIFTREG_GPIO;
			else
				DBG(1, "Mode %s : Not supported!", s);
		}

		s = ucix_get_option(s_ctx->uci_ctx, "hw" , data->led.name, "active");
		DBG(1, "active = [%s]", s);
		if (s) {
			data->active = UNKNOWN;
			if (!strncasecmp("hi",s,1))
				data->active = HI;
			if (!strncasecmp("low",s,3))
				data->active = LOW;
		}
		data->led.func = &func;
		data->led.priv = data;
		gpio_linux_output_init(data->addr);
		led_add(&data->led);
	}
	gpio_init();

	if (gpio_shiftreg_clk != -1)
		gpio_shift_register_init(&led_gpio_shift_register, gpio_shiftreg_clk, gpio_shiftreg_dat, gpio_shiftreg_mask, gpio_shiftreg_bits);
}
