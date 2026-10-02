/*
 * Brikken: AS3933 feltstyrkemaler
 *
 * SPI-modus 1 (CPOL=0, CPHA=1). Bekreftet mot R5=0x69 / R6=0x96.
 * CS er aktiv hoy - bade GPIO_ACTIVE_HIGH i overlay og
 * SPI_CS_ACTIVE_HIGH her.
 *
 * Oppsett for forste feltmaling:
 *   R1 = 0x00  intern RC-oscillator (ingen krystall montert),
 *              ingen monstergjenkjenning -> WAKE gar hoy pa
 *              ren baerebolge alene
 *   R2 = 0x20  +3 dB forsterkning (G_BOOST)
 *   R0 = 0x0E  alle tre kanaler aktive (fabrikkverdi)
 *
 * RSSI er peak-hold, sa reset_RSSI kalles hver runde for at
 * avlesningen skal folge feltet i sanntid.
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
#define CMD_PRESET_DEFAULT 0x04

#define REG_R0     0
#define REG_R1     1
#define REG_R2     2
#define REG_RSSI1 10
#define REG_RSSI2 11
#define REG_RSSI3 12

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

/* Enkel sojlegraf, 0-31 */
static void soyle(uint8_t v)
{
	for (int i = 0; i < 32; i++) {
		printk("%c", i < v ? '#' : '.');
	}
}

int main(void)
{
	uint8_t v;

	k_msleep(500);

	printk("\n\n=== AS3933 feltstyrkemaler ===\n\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI-bussen er ikke klar\n");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&wake)) {
		printk("FEIL: WAKE-pinnen er ikke klar\n");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&wake, GPIO_INPUT);

	/* Kjent utgangspunkt */
	kommando(CMD_PRESET_DEFAULT);
	k_msleep(20);

	/* Bekreft at SPI fortsatt snakker */
	les(5, &v);
	if (v != 0x69) {
		printk("ADVARSEL: R5 leste 0x%02X, forventet 0x69\n", v);
		printk("SPI svarer ikke riktig. Sjekk oppkoblingen.\n\n");
	} else {
		printk("SPI ok (R5 = 0x69)\n");
	}

	/* Intern RC, ingen monstergjenkjenning */
	skriv(REG_R1, 0x00);
	/* +3 dB forsterkning */
	skriv(REG_R2, 0x20);
	k_msleep(20);

	les(REG_R0, &v);
	printk("R0 = 0x%02X  (kanaler aktive)\n", v);
	les(REG_R1, &v);
	printk("R1 = 0x%02X  (0x00 = RC-osc, frekvensdeteksjon)\n", v);
	les(REG_R2, &v);
	printk("R2 = 0x%02X  (0x20 = gain boost)\n\n", v);

	printk("Start senderen og hold spolen naer sendespolen.\n");
	printk("k1 er kanalen med spolen. k2 og k3 er ikke koblet.\n\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;

		kommando(CMD_RESET_RSSI);
		k_msleep(100);

		les(REG_RSSI1, &r1);
		les(REG_RSSI2, &r2);
		les(REG_RSSI3, &r3);

		r1 &= 0x1F;
		r2 &= 0x1F;
		r3 &= 0x1F;

		printk("k1=%2u ", r1);
		soyle(r1);
		printk("   k2=%2u k3=%2u  WAKE=%d\n",
		       r2, r3, gpio_pin_get_dt(&wake));

		kommando(CMD_CLEAR_WAKE);
		k_msleep(300);
	}

	return 0;
}
