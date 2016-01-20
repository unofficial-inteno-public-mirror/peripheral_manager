/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/
#include "config.h"
#include <syslog.h>
#include <time.h>
#include "log.h"
#include "button.h"
#include "led.h"
#include "touch_sx9512.h"
#include "prox_px3220.h"


static struct ubus_context *global_ubus_ctx;
static struct blob_buf bblob;

static void button_ubus_interface_event(struct ubus_context *ubus_ctx, char *button, button_state_t pressed);

static void button_ubus_interface_event(struct ubus_context *ubus_ctx, char *button, button_state_t pressed)
{
	char s[UBUS_BUTTON_NAME_PREPEND_LEN+BUTTON_MAX_NAME_LEN];
	s[0]=0;
	strcat(s, UBUS_BUTTON_NAME_PREPEND);
	strcat(s, button);
	blob_buf_init(&bblob, 0);
	blobmsg_add_string(&bblob, "action", pressed ? "pressed" : "released");
	ubus_send_event(ubus_ctx, s, bblob.head);
}


/* used to map in the driver buttons to a function button */
struct button_drv_list {
        struct list_head list;
        struct timespec pressed_time;
        struct button_drv *drv;
};

/**/
struct function_button {
	struct list_head list;
        char *name;
        int dimming;
        char *hotplug;
	char *hotplug_long;
        int minpress;
        int longpress;                          /* negative value means valid if  mintime < time < abs(longpress ) */
                                                /* positive value means valid if time > longpreass */
                                                /* zero value means valid if time > mintime */
	struct list_head drv_list;              /* list of all driver button that is needed to activate this button function */
};

/* PUT every button from drivers into a list */
struct drv_button_list{
	struct list_head list;
	struct button_drv *drv;
};

/* list of all driver buttons added by drivers. */
static LIST_HEAD(drv_buttons_list);

/* list containing all function buttons read from config file */
static LIST_HEAD(buttons);


void button_add( struct button_drv *drv)
{
	struct drv_button_list *drv_node = malloc(sizeof(struct drv_button_list));

	DBG(1,"called with button name [%s]", drv->name);
	drv_node->drv = drv;

	list_add(&drv_node->list, &drv_buttons_list);
}

static struct button_drv *get_drv_button(const char *name)
{
	struct list_head *i;
	list_for_each(i, &drv_buttons_list) {
		struct drv_button_list *node = list_entry(i, struct drv_button_list, list);
                if (! strcmp(node->drv->name, name))
                        return node->drv;
	}
        return NULL;
}

#if 0
static struct function_button *get_button(const char *name)
{
	struct list_head *i;
	list_for_each(i, &buttons) {
		struct function_button *node = list_entry(i, struct function_button, list);
                if (! strcmp(node->name, name))
                        return node;
	}
        return NULL;
}
#endif


//! Read state for single button
static button_state_t read_button_state(const char *name)
{
	struct list_head *i;
#ifdef HAVE_BOARD_H
	/* sx9512 driver needs to read out all buttons at once */
	/* so call it once at beginning of scanning inputs  */
	sx9512_check();
	/* same for px3220 */
	px3220_check();
#endif
	list_for_each(i, &buttons) {
		struct list_head *j;
		struct function_button *node = list_entry(i, struct function_button, list);
		if(!strcmp(node->name, name)) {
			button_state_t state=BUTTON_ERROR;
			list_for_each(j, &node->drv_list) {
				struct button_drv_list *drv_node = list_entry(j, struct button_drv_list, list);
				if(drv_node->drv) {
					if(drv_node->drv->func->get_state(drv_node->drv))
						return BUTTON_PRESSED;
					else
						state=BUTTON_RELEASED;
				}
			}
			return state;
		}
	}
	return BUTTON_ERROR;
}

struct button_status {
	char name[BUTTON_MAX_NAME_LEN];
	button_state_t state;
};

struct button_status_all {
	int n;
	struct button_status status[BUTTON_MAX];
};


//! Read states for all buttons
static struct button_status_all * read_button_states(void)
{
	static struct button_status_all p;
	struct list_head *i;
        p.n=0;
#ifdef HAVE_BOARD_H
	/* sx9512 driver needs to read out all buttons at once */
	/* so call it once at beginning of scanning inputs  */
	sx9512_check();
	/* same for px3220 */
	px3220_check();
#endif
	list_for_each(i, &buttons) {
		struct list_head *j;
		button_state_t state=BUTTON_ERROR;
		struct function_button *node = list_entry(i, struct function_button, list);
		strcpy(p.status[p.n].name, node->name);
		list_for_each(j, &node->drv_list) {
			struct button_drv_list *drv_node = list_entry(j, struct button_drv_list, list);
			if(drv_node->drv) {
				if(drv_node->drv->func->get_state(drv_node->drv))
					state=BUTTON_PRESSED;
				else
					state=BUTTON_RELEASED;
			}
		}
		p.status[p.n].state = state;
		p.n++;
	}
	return &p;
}

static void dump_drv_list(void)
{
	struct list_head *i;
	list_for_each(i, &drv_buttons_list) {
		struct drv_button_list *node = list_entry(i, struct drv_button_list, list);
		DBG(1,"button name = [%s]",node->drv->name);
	}
}

static void dump_buttons_list(void)
{
	struct list_head *i;
	list_for_each(i, &buttons) {
		struct function_button *node = list_entry(i, struct function_button, list);
		DBG(1,"button name = [%s]",node->name);
                {
                        struct list_head *j;
                        list_for_each(j, &node->drv_list) {
                                struct button_drv_list *drv_node = list_entry(j, struct button_drv_list, list);
                                if(drv_node->drv != NULL)
                                        DBG(1,"%13s drv button name = [%s]","",drv_node->drv->name);
                        }
                        DBG(1,"%13s minpress = %d","",node->minpress);
                        DBG(1,"%13s longpress = %d","",node->longpress);
                }
	}
}


//! Run the hotplug command associated with function button
//! @retval 0 ok
static int button_hotplug_cmd(const char *name, bool longpress)
{
	struct list_head *i;
	list_for_each(i, &buttons) {
		struct function_button *node = list_entry(i, struct function_button, list);
		if(!strcmp(node->name, name)) {
			char str[512];
			char *hotplug = node->hotplug;
			if(longpress && node->hotplug_long)
				hotplug = node->hotplug_long;
			if(!hotplug)
				return 1;
			DBG(1, "send key %s [%s] to system %s", node->name, hotplug, longpress ? "(longpress)" : "");
			snprintf(str, 512, "ACTION=register INTERFACE=%s /sbin/hotplug-call button &", hotplug);
			system(str);
			syslog(LOG_INFO, "%s",str);
			return 0;
		}
	}
	return 1;
}


static int timer_started(struct button_drv_list *button_drv)
{
        if (button_drv->pressed_time.tv_sec == 0 )
                if (button_drv->pressed_time.tv_nsec == 0 )
                        return 0;
        return 1;
}

static void timer_start(struct button_drv_list *button_drv)
{
        clock_gettime(CLOCK_MONOTONIC, &button_drv->pressed_time);
}

static void timer_stop(struct button_drv_list *button_drv)
{
        button_drv->pressed_time.tv_sec = 0;
        button_drv->pressed_time.tv_nsec = 0;
}


static button_press_type_t timer_valid(struct button_drv_list *button_drv, int mtimeout, int longpress)
{
        struct timespec now;
        int sec;
        int nsec;
	int time_elapsed;

        if (timer_started(button_drv)) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                sec =  now.tv_sec  - button_drv->pressed_time.tv_sec;
                nsec = now.tv_nsec - button_drv->pressed_time.tv_nsec;
		time_elapsed = sec*1000 + nsec/1000000;
                if ( mtimeout < time_elapsed) {
			if (longpress && (longpress < time_elapsed))
				return BUTTON_PRESS_LONG;
			return BUTTON_PRESS_SHORT;
                }
        }
	return BUTTON_PRESS_NONE;
}

#define BUTTON_TIMEOUT 100
static void button_handler(struct uloop_timeout *timeout);
static struct uloop_timeout button_inform_timer = { .cb = button_handler };

static void button_handler(struct uloop_timeout *timeout)
{
 	struct list_head *i;
	int r;
//        DBG(1, "");

#ifdef HAVE_BOARD_H

        /* sx9512 driver needs to read out all buttons at once */
        /* so call it once at beginning of scanning inputs  */
        sx9512_check();
        /* same for px3220 */
        px3220_check();
#endif

        /* clean out indicator status, set by any valid press again if we find it */
        led_pressindicator_set(PRESS_NONE);

	list_for_each(i, &buttons) {
                struct list_head *j;
		struct function_button *node = list_entry(i, struct function_button, list);

                list_for_each(j, &node->drv_list) {
                        struct button_drv_list *drv_node = list_entry(j, struct button_drv_list, list);
                        if (drv_node->drv) {
                                button_press_type_t time_type;
                                button_state_t st = drv_node->drv->func->get_state(drv_node->drv);

                                if (st == BUTTON_PRESSED ) {
                                        if (! timer_started(drv_node)) {
                                                timer_start(drv_node);
                                                DBG(1, " %s pressed", drv_node->drv->name);
//						button_ubus_interface_event(global_ubus_ctx, node->name, BUTTON_PRESSED);
                                        }

                                        time_type = timer_valid(drv_node, node->minpress, node->longpress);
					if( time_type == BUTTON_PRESS_LONG )
						led_pressindicator_set(PRESS_LONG);
					if( time_type == BUTTON_PRESS_SHORT )
						led_pressindicator_set(PRESS_SHORT);
                                }

                                if (st == BUTTON_RELEASED ) {
                                        if (timer_started(drv_node)) {
                                                DBG(1, " %s released", drv_node->drv->name);
                                                if((r=timer_valid(drv_node, node->minpress, node->longpress))) {
							button_ubus_interface_event(global_ubus_ctx, node->name, BUTTON_RELEASED);
							if(node->dimming)
								led_dimming();
                                                        DBG(1, " %s released timer_valid=%d", drv_node->drv->name,r);
							button_hotplug_cmd(node->name, r==BUTTON_PRESS_LONG);
						}
                                        }
                                        timer_stop(drv_node);
                                }
//                                DBG(1, " %s state = %d", drv_node->drv->name,st);
                        }
                }
        }
	uloop_timeout_set(&button_inform_timer, BUTTON_TIMEOUT);
}


static int button_state_method(struct ubus_context *ubus_ctx, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
//	button_state_t state = read_button_state(obj->name+UBUS_BUTTON_NAME_PREPEND_LEN);
	blob_buf_init(&bblob, 0);

	switch(read_button_state(obj->name+UBUS_BUTTON_NAME_PREPEND_LEN)) {
	case BUTTON_RELEASED:
		blobmsg_add_string(&bblob, "state", "released");
		break;
	case BUTTON_PRESSED:
		blobmsg_add_string(&bblob, "state", "pressed");
		break;
	default:
		blobmsg_add_string(&bblob, "state", "error");
	}
	ubus_send_reply(ubus_ctx, req, bblob.head);
	return 0;
}


static int button_press_method(struct ubus_context *ubus_ctx, struct ubus_object *obj,
				struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	button_hotplug_cmd(obj->name+UBUS_BUTTON_NAME_PREPEND_LEN, 0);
	blob_buf_init(&bblob, 0);
	ubus_send_reply(ubus_ctx, req, bblob.head);
	return 0;
}


static int button_press_long_method(struct ubus_context *ubus_ctx, struct ubus_object *obj,
			       struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	button_hotplug_cmd(obj->name+UBUS_BUTTON_NAME_PREPEND_LEN, 1);
	blob_buf_init(&bblob, 0);
	ubus_send_reply(ubus_ctx, req, bblob.head);
	return 0;
}


static int buttons_state_method(struct ubus_context *ubus_ctx, struct ubus_object *obj,
	struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	int i;
	static struct button_status_all *p;
	p = read_button_states();
	blob_buf_init(&bblob, 0);
	for(i=0;i < p->n; i++) {
		switch(p->status[i].state) {
		case BUTTON_RELEASED:
			blobmsg_add_string(&bblob, p->status[i].name, "released");
			break;
		case BUTTON_PRESSED:
			blobmsg_add_string(&bblob, p->status[i].name, "pressed");
			break;
		default:
			blobmsg_add_string(&bblob, p->status[i].name, "error");
		}
	}
	ubus_send_reply(ubus_ctx, req, bblob.head);
	return 0;
}


static const struct ubus_method button_methods[] = {
//	{ .name = "status", .handler = button_status_method },
	{ .name = "state", .handler = button_state_method },
	{ .name = "press", .handler = button_press_method },
	{ .name = "press_long", .handler = button_press_long_method },
};

static struct ubus_object_type button_object_type = UBUS_OBJECT_TYPE("button", button_methods);


static const struct ubus_method buttons_methods[] = {
	{ .name = "state", .handler = buttons_state_method },
};

static struct ubus_object_type buttons_object_type = UBUS_OBJECT_TYPE("buttons", buttons_methods);

static struct ubus_object buttons_object = { .name = "buttons", .type = &buttons_object_type, .methods = buttons_methods, .n_methods = ARRAY_SIZE(buttons_methods), };

void button_init( struct server_ctx *s_ctx)
{
        struct uci_context *button_ctx = uci_alloc_context();
	struct uci_package *button_package = NULL;
        char config_file[1024];
	struct uci_element *elem;
        const char *s;
	int default_minpress = 100;

	LIST_HEAD(buttonnames);

        snprintf(config_file, 1024, "%s/%s", s_ctx->config_path, "buttons");
	if (uci_load(button_ctx, config_file, &button_package))
		return;

        uci_foreach_element(&button_package->sections, elem) {
                struct function_button *function;
                struct uci_section *sec = uci_to_section(elem);
                struct uci_option *opt;

                if (! sec->e.name)
                {
                        syslog(LOG_WARNING, "Button config without a name, check %s",
                               config_file);
                        continue;
                }

		function = malloc(sizeof(struct function_button));
		memset(function,0,sizeof(struct function_button));

                function->name = strdup(sec->e.name);

                /* read out dimming */
                s = uci_lookup_option_string(button_ctx, sec, "dimming");
                DBG(1, "%s: dimming = [%s]", function->name, s);
                if (s){
                        function->dimming = 1;
                }else
                        function->dimming = 0;

                /* read out minpress */
                s = uci_lookup_option_string(button_ctx, sec, "minpress");
                DBG(1, "%s: minpress = [%s]", function->name, s);
                if (s){
                        function->minpress =  strtol(s,0,0);
                }else
                        function->minpress =  default_minpress;

                /* read out long_press */
                s = uci_lookup_option_string(button_ctx, sec, "longpress");
                DBG(1, "%s: longpress = [%s]", function->name, s);
                if (s){
                        function->longpress =  strtol(s,0,0);
                }

                /* read out hotplug option */
                s = uci_lookup_option_string(button_ctx, sec, "hotplug");
                DBG(1, "%s: hotplug = [%s]", function->name, s);
                if (s){
                        function->hotplug = strdup(s);
                }

		/* read out hotplug option for longpress */
                s = uci_lookup_option_string(button_ctx, sec, "hotplug_long");
		DBG(1, "%s: hotplug_long = [%s]", function->name, s);
		if (s){
			function->hotplug_long = strdup(s);
		}

                /* read out list of driver buttons that need to be active to activate this
                   button */
                opt = uci_lookup_option(button_ctx, sec, "button");
		DBG(1, "%s: button driver = option", function->name);
                if (opt) {
                        struct uci_element *e;
                        int num = 0;
                        DBG(1, "option type %x ",(int)opt->type);
                        DBG(1, "option section %p ",opt->section);
                        DBG(1, "option elem-name[%s]",opt->e.name);

                        INIT_LIST_HEAD(&function->drv_list);

                        uci_foreach_element(&opt->v.list, e) {
                                struct button_drv_list *new_button;
                                DBG(1,"e type %x",e->type);
                                DBG(1,"e name %s",e->name);

                                new_button = malloc(sizeof(struct button_drv_list));
                                memset(new_button,0,sizeof(struct button_drv_list));
                                DBG(1," A %ld %ld",sizeof(struct button_drv_list),sizeof(new_button));

                                new_button->drv = get_drv_button(e->name);

                                if(new_button->drv == NULL) {
                                        syslog(LOG_WARNING, "%s wanted drv button [%s] but it can't be found. check spelling.",
                                               function->name,
                                               e->name);
                                        free(new_button);
                                }else {
                                        num++;
                                        list_add( &new_button->list, &function->drv_list);
                                }
                        }

                        if (num == 0)
                                syslog(LOG_WARNING, "Function  %s did not have any mapping to a driver button", function->name);

                }else
                        syslog(LOG_WARNING, "Function  %s did not have any mapping to a driver button", function->name);

                list_add(&function->list, &buttons);

		/* register each button with ubus */
                {
                        struct ubus_object *ubo;
                        int r;
                        char name[UBUS_BUTTON_NAME_PREPEND_LEN+BUTTON_MAX_NAME_LEN];

                        /* register buttons object with ubus */
                        if((r = ubus_add_object(s_ctx->ubus_ctx, &buttons_object)))
                                DBG(1,"Failed to add object: %s", ubus_strerror(r));

                        ubo = malloc(sizeof(struct ubus_object));
                        memset(ubo, 0, sizeof(struct ubus_object));

                        snprintf(name, UBUS_BUTTON_NAME_PREPEND_LEN+BUTTON_MAX_NAME_LEN, "%s%s",
                                 UBUS_BUTTON_NAME_PREPEND,
                                 function->name);
                        ubo->name      = strdup(name);
                        ubo->methods   = button_methods;
                        ubo->n_methods = ARRAY_SIZE(button_methods);
                        ubo->type      = &button_object_type;
                        if((r = ubus_add_object(s_ctx->ubus_ctx, ubo)))
                                DBG(1,"Failed to add object: %s", ubus_strerror(r));
                }
        }

	global_ubus_ctx=s_ctx->ubus_ctx;

	uloop_timeout_set(&button_inform_timer, BUTTON_TIMEOUT);

	dump_drv_list();
        dump_buttons_list();
}

