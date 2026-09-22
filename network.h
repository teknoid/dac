#include <sys/socket.h>
#include <arpa/inet.h>

#define NETWORK_LINEBUF			2048
#define NETWORK_DESCRIPTION		64

typedef struct _server server_t;
typedef struct _connection connection_t;
typedef int (*handler_t)(connection_t *conn);

typedef struct description_t {
	uint64_t mac;
	char description[NETWORK_DESCRIPTION + 1];
} description_t;

struct _server {
	int sock;
	int port;
	char *description;
	char *command;
	pthread_t thread;
	handler_t handler;
	struct sockaddr address;
	socklen_t addr_len;
};

struct _connection {
	int sock;
	char *description;
	pthread_t thread;
	handler_t handler;
	struct sockaddr address;
	socklen_t addr_len;
	char ip[16];
	FILE *stream;
	char line[NETWORK_LINEBUF + 1];
	char copy[NETWORK_LINEBUF + 1];
	unsigned int line_count;
};

int load_ethers();
int load_ieee();

const char* resolve_ip(const char *hostname);
const char* get_ieee_ou(uint64_t mac);
const char* get_ethers_name(uint64_t mac);

uint64_t string2mac(const char *mac);
void mac2string(char *smac, uint64_t mac);
void mac2name(char *name, uint64_t mac, size_t size);
void mac2name_grep(char *buf, uint64_t mac, size_t size);
void mac2ou(char *ou, uint64_t mac, size_t size);
void mac2ou_grep(char *ou, uint64_t mac, size_t size);

int init_server(server_t *server, char *description, int port, handler_t handler);
int init_popen(server_t *local, char *description, char *command, handler_t handler);
int init_socket_nb(const char *addr, const char *port);
