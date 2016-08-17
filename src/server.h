#ifndef SERVER_H
#define SERVER_H
#include <libubus.h>
#include "ucix.h"
#include <time.h>
#include <sys/time.h>

struct server_ctx {
	struct uci_context *uci_ctx;
	struct ubus_context *ubus_ctx;
	const char *config_path;
	time_t starttime;
};

void server_start( struct uci_context *uci_ctx, struct ubus_context *ubus_ctx, const char *config_path);

#endif /* SERVER_H */
