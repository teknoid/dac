#define WIDTH				20
#define HEIGHT				7

#define HEADER				0
#define MAINAREA			2
#define FOOTER				6

#define SCROLLDELAY			6

#define	WHITE				1
#define RED					2
#define YELLOW				3
#define GREEN				4
#define YELLOWONBLUE		5
#define REDONWHITE			6

#define FULLSCREEN_CHAR		'*'

int display_init(void);
void display_close(void);
void display_menu_open(void);
void display_menu_exit(void);
void display_handle(int c);
void display_fullscreen_int(int value);
void display_fullscreen_char(char *value);

void menu_setup(void);
void menu_open(void);
void menu_close(void);
void menu_down(void);
void menu_up(void);
void menu_select(void);

void show_selection(int);
