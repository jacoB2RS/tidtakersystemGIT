/*
 * Brikken: test av DAT-pinnen pa AS3933
 *
 * Hensikt: finne ut om DAT (pin 15) gjor noe i ren frekvens-
 * deteksjonsmodus, altsa uten monstergjenkjenning. Hele
 * kantmetoden (metode 3) hviler pa at DAT folger feltet, sa
 * dette ma avklares for vi bygger noe mer.
 *
 * Kobling, i tillegg til det som allerede star:
 *   AS3933 pin 15 (DAT) -> P1.12
 *
 * Senderen skal sta i MODE_KONTINUERLIG under testen. Med burst
 * folger DAT burstene, ikke feltet, og da maler vi feil ting.
 *
 * Tre mulige utfall:
 *   A) niva=0 hele tiden, flanker=0 bade med og uten felt
 *        -> DAT er dod i denne modusen. Kantmetoden ma bruke
 *           RSSI-terskel i programvare i stedet, eller vi ma
 *           sla pa monstergjenkjenning.
 *   B) niva folger feltet: 0 uten, 1 med (eller omvendt)
 *        -> perfekt. Da er DAT en ren feltindikator og kan
 *           kobles rett til GPIOTE for maskinvare-tidsstempling.
 *   C) flanker teller raskt nar feltet star pa
 *        -> DAT gir ut demodulert data/klokke. Da er den fortsatt
 *           brukbar: "det kommer flanker" = "felt til stede", men
 *           vi ma filtrere i stedet for a se pa ett enkelt niva.
 *
 * Noter hvilket utfall dere far, og ved hvilken avstand.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* ---- samme skruer som i brikke-tune ---- */

#define GAIN_REDUKSJON 0x09   /* R4<3:0>, -16 dB */
#define DEMPER_PA      1      /* R1<4> ATT_ON */
#define DEMPE_RES      1      /* R4<5:4> */
#define TOLERANSE      2      /* R2<1:0>, 16+/-2 strammest */

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

static const struct gpio_dt_spec dat =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_dat), gpios);

static struct gpio_callback wake_cb;
static struct gpio_callback dat_cb;

static volatile uint32_t wake_antall;
static volatile uint32_t dat_flanker;

static void wake_handler(const struct device *dev,
			 struct gpio_callback *cb, uint32_t pins)
{
	wake_antall++;
}

static void dat_handler(const struct device *dev,
			struct gpio_callback *cb, uint32_t pins)
{
	dat_flanker++;
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

/*
 * Les DAT-nivaet mange ganger pa rad og tell hvor stor andel som
 * var hoy. Skiller et fast niva fra en pinne som veksler for fort
 * til at ett enkelt oyeblikksbilde sier noe.
 */
static uint32_t dat_andel_hoy(void)
{
	uint32_t hoy = 0;

	for (int i = 0; i < 1000; i++) {
		if (gpio_pin_get_dt(&dat) == 1) {
			hoy++;
		}
		k_busy_wait(10);   /* 1000 x 10 us = 10 ms vindu */
	}
	return hoy / 10;           /* prosent */
}

int main(void)
{
	uint8_t v;
	uint32_t forrige_wake = 0;
	uint32_t forrige_dat = 0;

	k_msleep(500);

	printk("\n\n=== AS3933: test av DAT-pinnen ===\n\n");

	if (!spi_is_ready_dt(&as3933) || !gpio_is_ready_dt(&wake) ||
	    !gpio_is_ready_dt(&dat)) {
		printk("FEIL: SPI eller GPIO ikke klar\n");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&wake, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&wake, GPIO_INT_EDGE_RISING);
	gpio_init_callback(&wake_cb, wake_handler, BIT(wake.pin));
	gpio_add_callback(wake.port, &wake_cb);

	/* DAT: begge flanker, slik at vi fanger opp enhver aktivitet */
	gpio_pin_configure_dt(&dat, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&dat, GPIO_INT_EDGE_BOTH);
	gpio_init_callback(&dat_cb, dat_handler, BIT(dat.pin));
	gpio_add_callback(dat.port, &dat_cb);

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

	for (uint8_t r = 0; r <= 8; r++) {
		les(r, &v);
		printk("R%-2u = 0x%02X\n", r, v);
	}

	printk("\nSENDEREN SKAL STA KONTINUERLIG.\n");
	printk("Kjor forst uten felt, sa med spolen 2 cm unna.\n");
	printk("Se pa dat-kolonnene: niva, %% hoy, og flanker.\n\n");

	while (1) {
		uint8_t r1 = 0, r2 = 0, r3 = 0;
		uint32_t w, d, andel;
		int niva;

		kommando(CMD_RESET_RSSI);
		k_msleep(600);

		les(REG_RSSI1, &r1);
		les(REG_RSSI2, &r2);
		les(REG_RSSI3, &r3);

		niva = gpio_pin_get_dt(&dat);
		andel = dat_andel_hoy();

		w = wake_antall;
		d = dat_flanker;

		printk("k1=%2u ", r1 & 0x1F);
		soyle(r1 & 0x1F);
		printk("  k2=%2u k3=%2u | dat niva=%d %3u%% flanker=%u (+%u) | irq=%u %s\n",
		       r2 & 0x1F, r3 & 0x1F,
		       niva, andel, d, d - forrige_dat,
		       w, w != forrige_wake ? "<-- WAKE" : "");

		forrige_wake = w;
		forrige_dat = d;

		kommando(CMD_CLEAR_WAKE);
	}

	return 0;
}
