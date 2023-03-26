#define WIDTH				20
#define HEIGHT				7

#define HEADER				0
#define MAINAREA			2
#define FOOTER				6
#define CENTER				3

#define SCROLLDELAY			6

#define	WHITE				1
#define RED					2
#define YELLOW				3
#define GREEN				4
#define YELLOWONBLUE		5
#define REDONWHITE			6
#define CYANONBLUE			7

#define FULLSCREEN_CHAR		'*'

void display_menu_mode(void);
void display_fullscreen_number(int value);
void display_fullscreen_string(char *value);
