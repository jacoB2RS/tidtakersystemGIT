/*
 * Brikken - steg 1: bekreft SPI-kommunikasjon med AS3933
 *
 * Gjor ingenting med spoler eller wake ennaa. Leser bare registre og
 * skriver dem ut. Naar defaultverdiene stemmer, virker SPI-en, og
 * forst da er det vits i a ga videre.
 *
 * Testen: R5 = 0x69 og R6 = 0x96 er default wake-up-monsteret.
 * De to verdiene er umulige a treffe ved flaks - ser du dem, snakker
 * du med brikken.
 *
 * SPI-protokoll (16 bit, MSB forst):
 *   bit 15-14  00 = skriv, 01 = les, 11 = direktekommando
 *   bit 13-8   registeradresse (0-19) eller kommandokode
 *   bit 7-0    data
 *
 * CS er AKTIV HOY. Se overlay-fila.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define AS3933_MODE_WRITE 0x00
#define AS3933_MODE_READ  0x40
#define AS3933_MODE_CMD   0xC0

/* Direktekommandoer */
#define AS3933_CMD_CLEAR_WAKE     0x00
#define AS3933_CMD_RESET_RSSI     0x01
#define AS3933_CMD_TRIM_OSC       0x02
#define AS3933_CMD_CLEAR_FALSE    0x03
#define AS3933_CMD_PRESET_DEFAULT 0x04
#define AS3933_CMD_CALIB_RCO_LC   0x05

/* RSSI per kanal, read-only */
#define AS3933_REG_RSSI1 10
#define AS3933_REG_RSSI2 11
#define AS3933_REG_RSSI3 12

static const struct spi_dt_spec as3933 =
	SPI_DT_SPEC_GET(DT_NODELABEL(as3933),
			SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
			SPI_MODE_CPHA,       /* mode 1: sample paa fallende flanke */
			0);

static const struct gpio_dt_spec wake =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_wake), gpios);

static int as3933_les(uint8_t reg, uint8_t *ut)
{
	uint8_t tx[2] = { AS3933_MODE_READ | (reg & 0x3F), 0x00 };
	uint8_t rx[2] = { 0, 0 };

	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	int err = spi_transceive_dt(&as3933, &tx_set, &rx_set);

	if (err == 0) {
		*ut = rx[1];
	}
	return err;
}

static int as3933_skriv(uint8_t reg, uint8_t verdi)
{
	uint8_t tx[2] = { AS3933_MODE_WRITE | (reg & 0x3F), verdi };

	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&as3933, &tx_set);
}

static int as3933_kommando(uint8_t kode)
{
	uint8_t tx[1] = { AS3933_MODE_CMD | (kode & 0x3F) };

	const struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&as3933, &tx_set);
}

/* Defaultverdier fra databladet, R0-R8 */
static const uint8_t forventet[9] = {
	0x0E, 0x03, 0x00, 0x00, 0x00, 0x69, 0x96, 0x0B, 0x00
};

int main(void)
{
	uint8_t v;
	int err;
	int feil = 0;

	printk("\n=== AS3933 SPI-test ===\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI-bussen er ikke klar\n");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&wake)) {
		printk("FEIL: WAKE-pinnen er ikke klar\n");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&wake, GPIO_INPUT);

	/* Sett kjente verdier for lesing */
	err = as3933_kommando(AS3933_CMD_PRESET_DEFAULT);
	if (err) {
		printk("FEIL: kunne ikke sende preset_default (%d)\n", err);
		return err;
	}
	k_msleep(10);

	printk("\nReg  Lest  Forventet\n");
	printk("---------------------\n");

	for (uint8_t r = 0; r <= 8; r++) {
		err = as3933_les(r, &v);
		if (err) {
			printk("R%-2u  FEIL (%d)\n", r, err);
			feil++;
			continue;
		}

		printk("R%-2u  0x%02X  0x%02X   %s\n",
		       r, v, forventet[r],
		       v == forventet[r] ? "ok" : "AVVIK");

		if (v != forventet[r]) {
			feil++;
		}
	}

	printk("\n");

	if (feil == 0) {
		printk("SPI virker. Ga videre til spole og avstemming.\n");
	} else {
		printk("%d avvik.\n\n", feil);
		printk("Alt 0x00 eller 0xFF -> sjekk i denne rekkefolgen:\n");
		printk("  1. CS ma vaere AKTIV HOY (vanligste feilen)\n");
		printk("  2. SPI-modus: prov SPI_MODE_CPHA av/pa\n");
		printk("  3. SDI/SDO byttet om\n");
		printk("  4. VDD paa AS3933 (2,4-3,6 V), felles jord\n");
	}

	/* Les RSSI kontinuerlig - nyttig naar spolen kobles paa */
	printk("\nRSSI-avlesning hvert sekund:\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;

		as3933_les(AS3933_REG_RSSI1, &r1);
		as3933_les(AS3933_REG_RSSI2, &r2);
		as3933_les(AS3933_REG_RSSI3, &r3);

		printk("RSSI  k1=%2u  k2=%2u  k3=%2u   WAKE=%d\n",
		       r1 & 0x1F, r2 & 0x1F, r3 & 0x1F,
		       gpio_pin_get_dt(&wake));

		k_msleep(1000);
	}

	return 0;
}
