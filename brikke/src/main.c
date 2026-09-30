/*
 * AS3933 SPI-diagnose
 *
 * Prover alle fire SPI-modusene og leser R5 og R6 i hver.
 * Default er R5=0x69 og R6=0x96.
 *
 * Etterpa kjorer den kontinuerlig SPI-trafikk sa signalene kan males
 * med skop uten a mase med reset-timing:
 *   P1.07 CS   - skal ga HOY under hver overforing
 *   P1.04 SCL  - klokkeburst, 16 pulser per lesing
 *   P1.05 SDI  - kommandoen ut av nRF
 *   P1.06 SDO  - svaret fra AS3933
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define AS3933_MODE_WRITE 0x00
#define AS3933_MODE_READ  0x40
#define AS3933_MODE_CMD   0xC0

#define AS3933_CMD_PRESET_DEFAULT 0x04

/* Ikke const - vi endrer operation underveis */
static struct spi_dt_spec as3933 = SPI_DT_SPEC_GET(
	DT_NODELABEL(as3933),
	SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	0);

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

static int kommando(uint8_t kode)
{
	uint8_t tx[1] = { AS3933_MODE_CMD | (kode & 0x3F) };
	const struct spi_buf tx_buf = { .buf = tx, .len = 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&as3933, &tx_set);
}

static void sett_modus(int m)
{
	uint16_t op = SPI_WORD_SET(8) | SPI_TRANSFER_MSB;

	if (m & 1) {
		op |= SPI_MODE_CPHA;
	}
	if (m & 2) {
		op |= SPI_MODE_CPOL;
	}
	as3933.config.operation = op;
}

int main(void)
{
	uint8_t r5, r6;
	int err;
	int traff = -1;

	k_msleep(500);

	printk("\n\n=== AS3933 SPI-diagnose ===\n\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI-bussen er ikke klar\n");
		return -ENODEV;
	}

	if (gpio_is_ready_dt(&wake)) {
		gpio_pin_configure_dt(&wake, GPIO_INPUT);
	}

	printk("Modus  R5    R6    (forventet 0x69 0x96)\n");
	printk("-------------------------------------------\n");

	for (int m = 0; m < 4; m++) {
		sett_modus(m);

		kommando(AS3933_CMD_PRESET_DEFAULT);
		k_msleep(20);

		r5 = 0xAA;
		r6 = 0xAA;

		err = les(5, &r5);
		if (err) {
			printk("  %d    spi_transceive ga %d\n", m, err);
			continue;
		}
		les(6, &r6);

		printk("  %d    0x%02X  0x%02X  %s\n", m, r5, r6,
		       (r5 == 0x69 && r6 == 0x96) ? "<-- TREFF" : "");

		if (r5 == 0x69 && r6 == 0x96) {
			traff = m;
		}
	}

	printk("\n");

	if (traff >= 0) {
		printk("SPI virker i modus %d.\n", traff);
		printk("Sett den fast i main.c og ga videre.\n\n");
		sett_modus(traff);
	} else {
		printk("Ingen modus traff.\n\n");
		printk("Kjorer na kontinuerlig lesing i modus 1.\n");
		printk("Sett skopet pa disse, i denne rekkefolgen:\n");
		printk("  P1.04 SCL  - kommer det klokkeburst?\n");
		printk("  P1.07 CS   - gar den HOY under bursten?\n");
		printk("  P1.05 SDI  - ser du 0x45 sendt ut?\n");
		printk("  P1.06 SDO  - svarer brikka noe i det hele tatt?\n\n");
		sett_modus(1);
	}

	/* Kontinuerlig trafikk, lett a probe */
	while (1) {
		les(5, &r5);
		les(6, &r6);

		printk("R5=0x%02X  R6=0x%02X  WAKE=%d\n",
		       r5, r6, gpio_pin_get_dt(&wake));

		k_msleep(500);
	}

	return 0;
}
