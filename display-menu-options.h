typedef void (*func)(char *);

typedef struct menuoption_t {
    char *name;
    char *descr;
    func fptr;
} menuoption_t;

menuoption_t menuoptions[] = {
    { "1", "(1)", show_selection },
    { "2", "(2)"  , show_selection},
    { "3", "(3)", show_selection },
    { "4", "(4)", show_selection },
    { "Exit", "(exit)", menu_exit }
};
