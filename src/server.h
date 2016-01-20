#ifndef SERVER_H
#define SERVER_H
#include <libubus.h>
#include "ucix.h"

struct server_ctx {
	struct uci_context *uci_ctx;
	struct ubus_context *ubus_ctx;
	const char *config_path;
};

void server_start( struct uci_context *uci_ctx, struct ubus_context *ubus_ctx, const char *config_path);

#endif /* SERVER_H */
