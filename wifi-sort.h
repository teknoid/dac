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

// copy clients in sorted order and then copy all back
static void reorganize_small(small_station_t *s) {
	if (s->dirty < DIRTY)
		return; // not needed

	//	xdebug("WIFI station %s reorganization needed", NAME(s));
	small_station_t copy;
	ZERO(copy);
	int count = 0;
	for (mac_t **mm = s->pmacs; *mm; mm++)
		memcpy(&(copy.macs[count++]), MM, MAC_SIZE);
	memcpy(&s->macs, &copy.macs, MAC_SIZE * CLIENTS);
}

static void reorganize_big(big_station_t *s) {
	if (s->dirty < DIRTY)
		return; // not needed

	//	xdebug("WIFI station %s reorganization needed", NAME(s));
	big_station_t copy;
	ZERO(copy);
	int count = 0;
	for (mac_t **mm = s->pmacs; *mm; mm++)
		memcpy(&(copy.macs[count++]), MM, MAC_SIZE);
	memcpy(&s->macs, &copy.macs, MAC_SIZE * CLIENTS4);
}

// update client pointer
static void pointers_small(small_station_t *s) {
	int count = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->macs[i].mac)
			s->pmacs[count++] = &s->macs[i];
	s->pmacs[count] = 0; // null terminate
	s->mcount = count;
}

static void pointers_big(big_station_t *s) {
	int count = 0;
	for (int i = 0; i < CLIENTS4; i++)
		if (s->macs[i].mac)
			s->pmacs[count++] = &s->macs[i];
	s->pmacs[count] = 0; // null terminate
	s->mcount = count;
}

static void sort_count_small(small_station_t *s) {
	pointers_small(s);
	bubble_sort_count_small(s);
	reorganize_small(s);
	pointers_small(s);
}

static void sort_count_big(big_station_t *s) {
	pointers_big(s);
	bubble_sort_count_big(s);
	reorganize_big(s);
	pointers_big(s);
}

static void sort_signal(big_station_t *s) {
	pointers_big(s);
	bubble_sort_signal(s);
	reorganize_big(s);
	pointers_big(s);
}

static void sort_ts(big_station_t *s) {
	pointers_big(s);
	bubble_sort_ts(s);
	reorganize_big(s);
	pointers_big(s);
}

static void sort_ssid(big_station_t *s) {
	pointers_big(s);
	bubble_sort_ssid(s);
	reorganize_big(s);
	pointers_big(s);
}

static void sort_name(big_station_t *s) {
	pointers_big(s);
	bubble_sort_name(s);
	reorganize_big(s);
	pointers_big(s);
}
