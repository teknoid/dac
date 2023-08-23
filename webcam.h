#define WEBCAM_START			"su -c \"/server/www/webcam/webcam-start.sh\" hje"
#define WEBCAM_START_RESET		"su -c \"/server/www/webcam/webcam-start.sh reset\" hje"
#define WEBCAM_STOP				"su -c \"/server/www/webcam/webcam-stop.sh\" hje"
#define WEBCAM_STOP_TIMELAPSE	"su -c \"/server/www/webcam/webcam-stop.sh timelapse &\" hje"

// webcam off: ↑earlier, ↓later
#define WEBCAM_SUNDOWN			1

// webcam on: ↑later ↓earlier
#define WEBCAM_SUNRISE			1
