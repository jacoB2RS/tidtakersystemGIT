/*
 * Brikken: AS3933 feltdeteksjon - justerbar terskel
 *
 * Status 2026-10-02:
 *   SPI virker (R5=0x69, R6=0x96), modus 1, CS aktiv hoy.
 *   WAKE virker etter at ledningen ble flyttet fra P1.14 til P1.11.
 *   Stoygulv k1 = 15-19. Metning k1 = 31 naer senderen.
 *   Med losest toleranse utloser WAKE pa stoy alene.
 *
 * MAL: irq skal sta stille med senderen AV, og oke med den PA.
 *
 * Juster de tre verdiene under. Prosedyre:
 *   1. Sender AV. Ok DEMPING til irq slutter a oke helt.
 *   2. Sender PA, spolen 2 cm unna. irq skal oke igjen.
 *   3. Flytt spolen unna og finn avstanden der den slutter.
 *
 * Far du ikke begge til a stemme, ligger stoyen for naer signalet,
 * og da trengs monstergjenkjenning (EN_WPAT) - som krever at
 * senderen modulerer baerebolgen.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* ---- SKRUENE ---- */

/* R4<3:0> gain reduction.
 * 0x00 ingen, 0x04 -4dB, 0x05 -8dB, 0x08 -12dB,
 * 0x09 -16dB, 0x0C -20dB, 0x0D -24dB
 */
#define GAIN_REDUKSJON 0x09

/* R1<4> antennedemper. 1 demper rett pa inngangen. */
#define DEMPER_PA 1

/* R4<5:4> demperesistor, 0-3. Hoyere = kraftigere demping. */
#define DEMPE_RES 1

/* R2<1:0> frekvenstoleranse.
 * 0 = 16+/-6 losest, 1 = 16+/-4, 2 = 16+/-2 strammest
 */
#define TOLERANSE 2

/* ---- resten ---- */

#define AS3933_MODE_WRITE 0x00
#define AS3933_MODE_READ  0x40
#define AS3933_MODE_CMD   0xC0

#define CMD_CLEAR_WAKE     0x00
#define CMD_RESET_RSSI     0x01
#define CMD_CLEAR_FALSE    0x03
#define CMD_PRESET_DEFAULT 0x04
#define CMD_CALIB_RCO_LC   0x05

#define REG_RSSI1 10
#define REG_RSSI2 11
#define REG_RSSI3 12

#define SPI_OPER (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | \
		  SPI_CS_ACTIVE_HIGH | SPI_MODE_CPHA)

static const struct spi_dt_spec as3933 =
	SPI_DT_SPEC_GET(DT_NODELABEL(as3933), SPI_OPER, 0);

static const struct gpio_dt_spec wake =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_wake), gpios);

static struct gpio_callback wake_cb;
static volatile uint32_t wake_antall;

static void wake_handler(const struct device *dev,
			 struct gpio_callback *cb, uint32_t pins)
{
	wake_antall++;
}

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
	uint32_t forrige = 0;

	k_msleep(500);

	printk("\n\n=== AS3933 terskeljustering ===\n\n");

	if (!spi_is_ready_dt(&as3933) || !gpio_is_ready_dt(&wake)) {
		printk("FEIL: SPI eller GPIO ikke klar\n");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&wake, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_EDGE_RISING);
	gpio_init_callback(&wake_cb, wake_handler, BIT(wake.pin));
	gpio_add_callback(wake.port, &wake_cb);

	kommando(CMD_PRESET_DEFAULT);
	k_msleep(20);

	les(5, &v);
	printk("SPI: R5 = 0x%02X %s\n", v, v == 0x69 ? "(ok)" : "(AVVIK)");

	skriv(1, DEMPER_PA ? 0x10 : 0x00);
	skriv(2, TOLERANSE & 0x03);
	skriv(3, 0x00);
	skriv(4, ((DEMPE_RES & 0x03) << 4) | (GAIN_REDUKSJON & 0x0F));
	skriv(8, 0x00);
	k_msleep(20);

	kommando(CMD_CALIB_RCO_LC);
	k_msleep(100);
	skriv(3, 0x00);

	kommando(CMD_CLEAR_FALSE);
	kommando(CMD_CLEAR_WAKE);
	k_msleep(20);

	printk("\nInnstillinger:\n");
	printk("  gain reduction  0x%02X\n", GAIN_REDUKSJON);
	printk("  antennedemper   %s\n", DEMPER_PA ? "pa" : "av");
	printk("  dempe-resistor  %d\n", DEMPE_RES);
	printk("  toleranse       %d  (0 losest, 2 strammest)\n\n", TOLERANSE);

	for (uint8_t r = 0; r <= 8; r++) {
		les(r, &v);
		printk("R%-2u = 0x%02X\n", r, v);
	}

	printk("\nSender AV: irq skal sta stille.\n");
	printk("Sender PA, 2 cm: irq skal oke.\n\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;
		uint32_t na;

		kommando(CMD_RESET_RSSI);
		k_msleep(100);

		les(REG_RSSI1, &r1);
		les(REG_RSSI2, &r2);
		les(REG_RSSI3, &r3);

		na = wake_antall;

		printk("k1=%2u ", r1 & 0x1F);
		soyle(r1 & 0x1F);
		printk("   k2=%2u k3=%2u  irq=%u %s\n",
		       r2 & 0x1F, r3 & 0x1F, na,
		       na != forrige ? "<-- WAKE" : "");

		forrige = na;

		kommando(CMD_CLEAR_WAKE);
		k_msleep(300);
	}

	return 0;
}
