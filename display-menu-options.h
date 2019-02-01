
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef void (*func)(void);

typedef struct menuoption_t {
    char *name;
    char *descr;
    func fptr;
} menuoption_t;

void menu_next(menuoption_t *options, int size);

void next_m0(void);
void next_m1(void);
void next_m2(void);

menuoption_t m0[] = {
    { "0.1", "(1)", next_m1 },
    { "0.2", "(2)", next_m2 },
    { "0.3", "(3)", show_selection },
    { "0.4", "(4)", show_selection },
    { "Exit", "(exit)", menu_exit }
};

menuoption_t m1[] = {
    { "1.1", "(1)", show_selection },
    { "1.2", "(2)", show_selection },
    { "1.3", "(3)", show_selection },
    { "1.4", "(4)", show_selection },
    { "Return", "(return)", next_m0 }
};

menuoption_t m2[] = {
    { "2.1", "(1)", show_selection },
    { "2.2", "(2)", show_selection },
    { "2.3", "(3)", show_selection },
    { "2.4", "(4)", show_selection },
    { "Return", "(return)", next_m0 }
};

void next_m0() {
	menu_next(m0, ARRAY_SIZE(m0));
}

void next_m1() {
	menu_next(m1, ARRAY_SIZE(m1));
}

void next_m2() {
	menu_next(m2, ARRAY_SIZE(m2));
}
