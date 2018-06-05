#include <wiringPi.h> // Benoetigte wiringPi-Library
#include <softPwm.h>  // Benoetigte Library für Software-PWM
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define LED_Pin 4    // wiringPi-Pin 4 / GPIO23
#define DELAY   10   // Pause

int main(void) {
	int i = 0; // Laufvariable
	printf("Raspberry Pi PWM-Programm mit wiringPi\n");
	if (wiringPiSetup() == -1)       // wiringPi initialisieren
		exit(EXIT_FAILURE);    // Fehler? -> Programmende
	pinMode(LED_Pin, OUTPUT);       // Pin als PWM-Ausgang nutzen
	softPwmCreate(LED_Pin, 0, 100); //Software PWM erstellen

	for (;;) {
		// LED aufblenden
		for (i = 0; i <= 100; i++) {
			softPwmWrite(LED_Pin, i);
			delay(DELAY);
		}
		// LED abblenden
		for (i = 100; i >= 0; i--) {
			softPwmWrite(LED_Pin, i);
			delay(DELAY);
		}
	}
	return EXIT_SUCCESS; // Programm erfolgreich beendet
}
