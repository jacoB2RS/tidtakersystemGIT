/*
 * Slojfetest for P1.04 - P1.07
 *
 * Tester om nRF54L15 faktisk kan drive og lese disse pinnene, eller om
 * de analoge svitsjene mot debuggeren (UART1) holder dem fast.
 *
 * KOBLING: ta AS3933 helt ut. Sett EN jumper direkte mellom
 *   P1.05  og  P1.06
 * Ingenting annet.
 *
 * Programmet driver P1.05 hoy og lav, og leser P1.06.
 * Folger P1.06 etter, er pinnene i orden.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define PORT DT_NODELABEL(gpio1)

#define UT_PIN  5   /* P1.05 */
#define INN_PIN 6   /* P1.06 */

int main(void)
{
	const struct device *p1 = DEVICE_DT_GET(PORT);
	int feil = 0;

	k_msleep(500);

	printk("\n\n=== Slojfetest P1.05 -> P1.06 ===\n\n");

	if (!device_is_ready(p1)) {
		printk("FEIL: gpio1 er ikke klar\n");
		return -ENODEV;
	}

	gpio_pin_configure(p1, UT_PIN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(p1, INN_PIN, GPIO_INPUT);

	for (int i = 0; i < 4; i++) {
		int forventet = i % 2;

		gpio_pin_set(p1, UT_PIN, forventet);
		k_msleep(50);

		int lest = gpio_pin_get(p1, INN_PIN);

		printk("P1.05 satt %d  ->  P1.06 leste %d   %s\n",
		       forventet, lest,
		       lest == forventet ? "ok" : "AVVIK");

		if (lest != forventet) {
			feil++;
		}
	}

	printk("\n");

	if (feil == 0) {
		printk("Pinnene virker. nRF-siden er i orden.\n");
		printk("Feilen ligger da i AS3933 eller i selve SPI-oppsettet.\n");
	} else {
		printk("%d avvik.\n\n", feil);
		printk("P1.06 folger ikke P1.05. Mulige arsaker:\n");
		printk("  1. Jumperen mellom P1.05 og P1.06 mangler kontakt\n");
		printk("  2. UART1 er fortsatt tilkoblet debuggeren\n");
		printk("     -> koble den fra i Board Configurator\n");
		printk("  3. Feil pinner paa headeren\n");
	}

	/* Fortsett a veksle, sa det kan males med multimeter paa P1.05.
	 * 1 Hz: multimeteret vil vise rundt 1,65 V i snitt.
	 */
	printk("\nVeksler na P1.05 hvert sekund. Mal med multimeter.\n");

	while (1) {
		gpio_pin_toggle(p1, UT_PIN);
		printk("P1.05=%d  P1.06=%d\n",
		       gpio_pin_get(p1, UT_PIN),
		       gpio_pin_get(p1, INN_PIN));
		k_msleep(1000);
	}

	return 0;
}
