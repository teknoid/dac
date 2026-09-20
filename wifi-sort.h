// bubble sort by count
static void bubble_sort_count_small(small_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (y->count > x->count) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by count
static void bubble_sort_count_big(big_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (y->count > x->count) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by signal
static void bubble_sort_signal(big_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (y->signal > x->signal) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by time stamp
static void bubble_sort_ts(big_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (y->ts_last > x->ts_last) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by ssid
static void bubble_sort_ssid(big_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (strcmp(y->ssid, x->ssid) < 0) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}

// bubble sort by name
static void bubble_sort_name(big_station_t *s) {
	s->dirty = 0;
	for (int i = 0; i < s->mcount - 1; i++)
		for (int j = 0; j < s->mcount - i - 1; j++) {
			mac_t *x = s->pmacs[j];
			mac_t *y = s->pmacs[j + 1];
			if (strcmp(y->name, x->name) < 0) {
				s->pmacs[j] = y;
				s->pmacs[j + 1] = x;
				s->dirty++;
			}
		}
}
