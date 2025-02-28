#include <curl/curl.h>

// curl response buffer and parser function
typedef struct _response response_t;

struct _response {
	char *buffer;
	size_t buflen;
	size_t size;
};

typedef int (parser_t)(response_t *r);

CURL* curl_init(const char *url, response_t *memory);
int curl_perform(CURL *curl, response_t *memory, parser_t *parser);
int curl_oneshot(const char *url);
