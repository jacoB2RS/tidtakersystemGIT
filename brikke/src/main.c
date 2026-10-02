/*
 * Brikken: AS3933 feltdeteksjon, WAKE pa avbrudd
 *
 * Endringer:
 *   - WAKE leses med GPIO-avbrudd i stedet for polling. Databladet
 *     har en timeout pa vekkesignalet (R7<7:5> T_OUT), sa en kort
 *     puls kan forsvinne mellom to pollinger. Avbruddet laser den
 *     fast uansett hvor kort den er.
 *   - Frekvenstoleransen losnet fra 16+/-2 til 16+/-6. Strammest
 *     mulig krever at RC-kalibreringen er presis; losere gir
 *     deteksjon selv om kalibreringen er litt av.
 *   - Antennedemper (ATT_ON) kan slas pa med DEMPER_PA hvis
 *     signalet metter pa 31 langt fra senderen. Gain reduction
 *     alene viste seg ikke a senke RSSI.
 *   - R3 skrives eksplisitt til 0x00. Kalibreringen satte den til
 *     0x20 uten at vi ba om det.
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
#define CMD_CLEAR_FALSE    0x03
#define CMD_PRESET_DEFAULT 0x04
#define CMD_CALIB_RCO_LC   0x05

#define REG_RSSI1 10
#define REG_RSSI2 11
#define REG_RSSI3 12

/* Juster disse to hvis signalet metter eller ikke utloser */
#define GAIN_REDUKSJON 0x05   /* R4<3:0>: 0x00 ingen, 0x05 -8dB, 0x09 -16dB */
#define DEMPER_PA      0      /* 1 = antennedemper pa, demper inngangen */

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

	k_msleep(500);

	printk("\n\n=== AS3933 feltdeteksjon, WAKE pa avbrudd ===\n\n");

	if (!spi_is_ready_dt(&as3933) || !gpio_is_ready_dt(&wake)) {
		printk("FEIL: SPI eller GPIO ikke klar\n");
		return -ENODEV;
	}

	/* WAKE som avbrudd pa stigende flanke */
	gpio_pin_configure_dt(&wake, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_EDGE_RISING);
	gpio_init_callback(&wake_cb, wake_handler, BIT(wake.pin));
	gpio_add_callback(wake.port, &wake_cb);

	kommando(CMD_PRESET_DEFAULT);
	k_msleep(20);

	les(5, &v);
	printk("SPI: R5 = 0x%02X %s\n\n", v, v == 0x69 ? "(ok)" : "(AVVIK)");

	/*
	 * R1: bit4 ATT_ON antennedemper, resten 0
	 *     -> intern RC-osc, ingen monstergjenkjenning
	 * R2 = 0x00  gain boost av, frekvenstoleranse 16+/-6 (losest)
	 * R3 = 0x00  fabrikkverdi
	 * R4         gain reduction
	 * R8 = 0x00  band 95-150 kHz
	 */
	skriv(1, DEMPER_PA ? 0x10 : 0x00);
	skriv(2, 0x00);
	skriv(3, 0x00);
	skriv(4, GAIN_REDUKSJON & 0x0F);
	skriv(8, 0x00);
	k_msleep(20);

	printk("Kalibrerer RC-oscillator...\n");
	kommando(CMD_CALIB_RCO_LC);
	k_msleep(100);

	/* Kalibreringen kan endre R3 - sett den tilbake */
	skriv(3, 0x00);

	kommando(CMD_CLEAR_FALSE);
	kommando(CMD_CLEAR_WAKE);
	k_msleep(20);

	for (uint8_t r = 0; r <= 8; r++) {
		les(r, &v);
		printk("R%-2u = 0x%02X\n", r, v);
	}

	printk("\nirq teller hver gang WAKE gar hoy.\n");
	printk("Stiger den nar spolen naermer seg, virker deteksjonen.\n\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;

		kommando(CMD_RESET_RSSI);
		k_msleep(100);

		les(REG_RSSI1, &r1);
		les(REG_RSSI2, &r2);
		les(REG_RSSI3, &r3);

		printk("k1=%2u ", r1 & 0x1F);
		soyle(r1 & 0x1F);
		printk("   k2=%2u k3=%2u  niva=%d  irq=%u\n",
		       r2 & 0x1F, r3 & 0x1F,
		       gpio_pin_get_dt(&wake),
		       wake_antall);

		kommando(CMD_CLEAR_WAKE);
		k_msleep(300);
	}

	return 0;
}
