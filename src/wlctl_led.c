#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <board.h>
#include "led.h"
#include "log.h"
#include "server.h"

void wlctl_led_init(struct server_ctx *s_ctx);

typedef enum {
	LOW,
	HI,
	UNKNOWN,
} active_t;

struct wlctl_led_data {
	int addr;
	active_t active;
	int state;
	struct led_drv led;
};

static char wlctl_ledno[2];
static char wlctl_ledbh[2];
static char * wlctl_argv[] = {
	"/usr/sbin/wlctl", "ledbh", wlctl_ledno, wlctl_ledbh, NULL
};

static int wlctl_led_set_state(struct led_drv *drv, led_state_t state)
{
	struct wlctl_led_data *p = (struct wlctl_led_data *)drv->priv;
	int ledbh = 0;
	pid_t child_pid;

	if ((int)state == p->state) {
		DBG(4,"skipping set");
		return state;
	}

	p->state = state;

	if (state) {
		if(p->active)
			ledbh = 1;
	} else {
		if(!p->active)
			ledbh = 1;
	}

	wlctl_ledno[0] = '0' + p->addr;
	wlctl_ledbh[0] = '0' + ledbh;

	DBG(2, "%s spawn %s %s %s %s",  drv->name,
	    wlctl_argv[0], wlctl_argv[1], wlctl_argv[2], wlctl_argv[3]);

	child_pid = fork();
	if (!child_pid) {
		execv(wlctl_argv[0], wlctl_argv);
		abort();
	}

	/* Child waited on by libubox */
	return state;
}

static led_state_t wlctl_led_get_state(struct led_drv *drv)
{
	struct wlctl_led_data *p = (struct wlctl_led_data *)drv->priv;
	DBG(1, "state for %s",  drv->name);

	return p->state;
}

static struct led_drv_func wlctl_func = {
	.set_state = wlctl_led_set_state,
	.get_state = wlctl_led_get_state,
};

void wlctl_led_init(struct server_ctx *s_ctx)
{
	LIST_HEAD(leds);
	struct ucilist *node;

	DBG(1, " ");

	ucix_get_option_list(s_ctx->uci_ctx, "hw" ,"wlctl_leds", "leds", &leds);
	list_for_each_entry(node, &leds, list) {
		struct wlctl_led_data *data;
		const char *s;

		DBG(1, "value = [%s]", node->val);

		data = malloc(sizeof(struct wlctl_led_data));
		memset(data, 0, sizeof(struct wlctl_led_data));

		data->led.name = node->val;

		s = ucix_get_option(s_ctx->uci_ctx, "hw", data->led.name, "addr");
		DBG(1, "addr = [%s]", s);
		if (s)
			data->addr =  strtol(s, 0, 0);

		s = ucix_get_option(s_ctx->uci_ctx, "hw", data->led.name, "active");
		DBG(1, "active = [%s]", s);
		if (s) {
			data->active = UNKNOWN;
			if (!strncasecmp("hi", s, 3))
				data->active = HI;
			if (!strncasecmp("low", s, 3))
				data->active = LOW;
		}

		data->state = -1;
		data->led.func = &wlctl_func;
		data->led.priv = data;
		led_add(&data->led);
	}
}
