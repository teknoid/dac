// bubble sort by count
static void bubble_sort_count(station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->count > x->count) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by count
static void bubble_sort_count_meta(meta_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->count > x->count) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by signal
static void bubble_sort_signal(meta_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->signal > x->signal) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by time stamp
static void bubble_sort_ts(meta_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->ts > x->ts) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by ssid
static void bubble_sort_ssid(meta_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (strcmp(y->ssid, x->ssid) < 0) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by name
static void bubble_sort_name(meta_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->ccount - 1; i++)
		for (int j = 0; j < s->ccount - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (strcmp(y->name, x->name) < 0) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}
}
