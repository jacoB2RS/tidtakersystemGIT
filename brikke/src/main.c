/*
 * Brikken: AS3933 feltdeteksjon med WAKE
 *
 * Endringer fra forrige versjon:
 *   - Calib_RCO_LC kalles ved oppstart. RC-oscillatoren MA kalibreres
 *     for frekvensdeteksjon virker. Det var grunnen til at WAKE alltid
 *     sto pa 0.
 *   - Gain boost av, og gain reduction pa, sa signalet ikke metter pa 31
 *     og stoygulvet faller.
 *   - Strammere frekvenstoleranse, 16+/-2 i stedet for 16+/-6, sa
 *     tilfeldig stoy ikke utloser WAKE.
 *
 * Mal fra loggen 2026-10-02 uten demping: stoygulv 13-19, metning 31.
 * Juster GAIN_REDUKSJON til stoygulvet ligger pa 0-3.
 *
 * R4<3:0> GR fra databladet:
 *   0x00  ingen reduksjon
 *   0x04  -4 dB
 *   0x05  -8 dB
 *   0x08  -12 dB
 *   0x09  -16 dB
 *   0x0C  -20 dB
 *   0x0D  -24 dB
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define AS3933_MODE_WRITE 0x00
#define AS3933_MODE_READ  0x40
#define AS3933_MODE_CMD   0xC0

#define CMD_CLEAR_WAKE     0x00
#define CMD_RESET_RSSI     0x01
#define CMD_TRIM_OSC       0x02
#define CMD_CLEAR_FALSE    0x03
#define CMD_PRESET_DEFAULT 0x04
#define CMD_CALIB_RCO_LC   0x05

#define REG_R0     0
#define REG_R1     1
#define REG_R2     2
#define REG_R4     4
#define REG_R8     8
#define REG_RSSI1 10
#define REG_RSSI2 11
#define REG_RSSI3 12

/* Juster denne. Start pa 0x05 (-8 dB). */
#define GAIN_REDUKSJON 0x05

/* Modus 1: CPHA satt, CPOL av */
#define SPI_OPER (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | \
		  SPI_CS_ACTIVE_HIGH | SPI_MODE_CPHA)

static const struct spi_dt_spec as3933 =
	SPI_DT_SPEC_GET(DT_NODELABEL(as3933), SPI_OPER, 0);

static const struct gpio_dt_spec wake =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_wake), gpios);

static int les(uint8_t reg, uint8_t *ut)
{
	uint8_t tx[2] = { AS3933_MODE_READ | (reg & 0x3F), 0x00 };
	uint8_t rx[2] = { 0, 0 };

	const struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	const struct spi_buf rx_buf = { .buf = rx, .len = 2 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	int err = spi_transceive_dt(&as3933, &tx_set, &rx_set);

	if (err == 0) {
		*ut = rx[1];
	}
	return err;
}

static int skriv(uint8_t reg, uint8_t verdi)
{
	uint8_t tx[2] = { AS3933_MODE_WRITE | (reg & 0x3F), verdi };
	const struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&as3933, &tx_set);
}

static int kommando(uint8_t kode)
{
	uint8_t tx[1] = { AS3933_MODE_CMD | (kode & 0x3F) };
	const struct spi_buf tx_buf = { .buf = tx, .len = 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&as3933, &tx_set);
}

static void soyle(uint8_t v)
{
	for (int i = 0; i < 32; i++) {
		printk("%c", i < v ? '#' : '.');
	}
}

int main(void)
{
	uint8_t v;
	uint8_t wake_teller = 0;

	k_msleep(500);

	printk("\n\n=== AS3933 feltdeteksjon ===\n\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI-bussen er ikke klar\n");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&wake)) {
		printk("FEIL: WAKE-pinnen er ikke klar\n");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&wake, GPIO_INPUT);

	kommando(CMD_PRESET_DEFAULT);
	k_msleep(20);

	les(5, &v);
	printk("SPI: R5 = 0x%02X %s\n\n", v, v == 0x69 ? "(ok)" : "(AVVIK)");

	/*
	 * R1 = 0x00  intern RC-osc, ingen monstergjenkjenning
	 * R2 = 0x02  gain boost AV, frekvenstoleranse 16+/-2 (strammest)
	 * R4         gain reduction
	 * R8 = 0x00  band 95-150 kHz (dekker 125 kHz)
	 */
	skriv(REG_R1, 0x00);
	skriv(REG_R2, 0x02);
	skriv(REG_R4, GAIN_REDUKSJON & 0x0F);
	skriv(REG_R8, 0x00);
	k_msleep(20);

	/* MA gjores for frekvensdeteksjon virker */
	printk("Kalibrerer RC-oscillator mot antennetanken...\n");
	kommando(CMD_CALIB_RCO_LC);
	k_msleep(100);

	kommando(CMD_CLEAR_FALSE);
	kommando(CMD_CLEAR_WAKE);
	k_msleep(20);

	for (uint8_t r = 0; r <= 8; r++) {
		les(r, &v);
		printk("R%-2u = 0x%02X\n", r, v);
	}

	printk("\nStoygulvet bor ligge pa 0-3 na.\n");
	printk("Ligger det hoyere: ok GAIN_REDUKSJON i koden.\n");
	printk("Metter det fortsatt pa 31 langt unna: ok mer.\n\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;
		int w;

		kommando(CMD_RESET_RSSI);
		k_msleep(100);

		les(REG_RSSI1, &r1);
		les(REG_RSSI2, &r2);
		les(REG_RSSI3, &r3);
		w = gpio_pin_get_dt(&wake);

		r1 &= 0x1F;
		r2 &= 0x1F;
		r3 &= 0x1F;

		if (w) {
			wake_teller++;
		}

		printk("k1=%2u ", r1);
		soyle(r1);
		printk("   k2=%2u k3=%2u  WAKE=%d  (%u)\n",
		       r2, r3, w, wake_teller);

		kommando(CMD_CLEAR_WAKE);
		k_msleep(300);
	}

	return 0;
}
