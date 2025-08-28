#include <pthread.h>

#include "target.h"

#define BUFSIZE			256
#define SLASH			"/"

#ifndef STATE
#define STATE			"/var/lib/mcp"
#endif

#ifndef RUN
#define RUN				"/run/mcp"
#endif

#ifndef TMP
#define TMP				"/tmp"
#endif

#ifndef RAM
#define RAM				"/ram"
#endif

#ifndef WORK
#define WORK			"/work"
#endif

// register a module in the MCP's execution context
#define MCP_REGISTER(name, prio, init, stop, loop) \
  static void __attribute__((constructor(102 + prio))) register_##name(void) \
  { mcp_register("\""#name"\"", prio, init, stop, loop); };

typedef int (*init_t)();
typedef void (*stop_t)();
typedef void (*loop_t)();
typedef struct _mcp_module mcp_module_t;

struct _mcp_module {
	const char *name;
	init_t init;
	stop_t stop;
	loop_t loop;
	pthread_t thread;
	mcp_module_t *next;
};

typedef struct mcp_state_t {
	int notifications_lcd;
	int notifications_sound;
	int notifications_desktop;
} mcp_state_t;
extern mcp_state_t *mcp;

typedef struct mcp_config_t {
	int daemonize;
	int interactive;
} mcp_config_t;
extern mcp_config_t *cfg;

void mcp_register(const char*, const int, const init_t, const stop_t, const loop_t);
int mcp_main(int argc, char **argv);
int mcp_status_get(const void*, const void*);
void mcp_status_set(const void*, const void*, int);
void mcp_system_shutdown(void);
void mcp_system_reboot(void);
