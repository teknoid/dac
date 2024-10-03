#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "curl.h"
#include "utils.h"

static size_t curl_callback(const char *data, size_t size, size_t nmemb, const void *userdata) {
	response_t *r = (response_t*) userdata;
	size_t chunksize = size * nmemb;

	// initial buffer allocation
	if (r->buffer == NULL) {
		r->buffer = malloc(chunksize + 1);
		r->buflen = chunksize + 1;
		xdebug("CURL callback() malloc %d", r->buflen);
	}

	// check if we need to extend the buffer
	if (r->buflen < r->size + chunksize + 1) {
		r->buffer = realloc(r->buffer, r->buflen + chunksize + 1);
		r->buflen += chunksize + 1;
		xdebug("CURL callback() realloc %d", r->buflen);
	}

	// copy/append chunk data
	memcpy(&r->buffer[r->size], data, chunksize);
	r->size += chunksize;
	r->buffer[r->size] = 0;

	return chunksize;
}

CURL* curl_init(const char *url, response_t *memory) {
	CURL *curl = curl_easy_init();
	if (curl != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
		if (memory != NULL)
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, memory);
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 4096);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 22L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 33L);
		curl_easy_setopt(curl, CURLOPT_MAXAGE_CONN, 333L);
	}
	return curl;
}

int curl_perform(CURL *curl, response_t *memory, parser_t *parser) {
	if (memory != NULL)
		memory->size = 0;

	CURLcode ret = curl_easy_perform(curl);
	if (ret != CURLE_OK)
		return xerrr(1, "CURL curl perform error %d: %s", ret, curl_easy_strerror(ret));

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200)
		return xerrr(1, "CURL got response code %d", http_code);

	if (parser != NULL)
		return (parser)(memory);

	return 0;
}
