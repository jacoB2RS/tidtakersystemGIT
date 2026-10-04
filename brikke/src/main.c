/*
 * Brikken: folger DAT og WAKE i sanntid
 *
 * Forrige test viste at DAT lever, men at den ser ut til a gi en
 * KORT PULS ved deteksjon i stedet for a ligge hoy sa lenge feltet
 * star pa. Det avgjorende sporsmalet er derfor:
 *
 *   Faller DAT og WAKE av seg selv nar feltet forsvinner,
 *   eller blir de laast til vi rydder med clear_wake?
 *
 * Kantmetoden trenger BEGGE flanker: inn OG ut. Faller de ikke av
 * seg selv, har vi ingen utgangsflanke a tidsstemple.
 *
 * Denne versjonen rydder derfor IKKE med clear_wake (sett
 * RYDD_HVERT_SEKUND til 1 hvis dere vil sammenligne).
 *
 * Utskriften er en tekst-skop: hver linje dekker 500 ms, hvert
 * tegn 10 ms.
 *   '.'  lav hele tiden
 *   '#'  hoy hele tiden
 *   '-'  vekslet i lopet av de 10 ms
 *
 * PROSEDYRE
 *   1. Start med spolen langt unna, senderen kontinuerlig pa.
 *      Begge rader skal vaere rene prikker.
 *   2. For spolen rolig inn til 2 cm, hold i 2 sekunder,
 *      og trekk den rolig ut igjen.
 *   3. Se pa hva som skjer NAR DU TREKKER DEN UT.
 *
 * Tre utfall:
 *   A) begge rader gar tilbake til prikker nar spolen trekkes ut
 *        -> vi har bade inn- og utflanke. Metode 3 kan bygges.
 *   B) radene blir staende '#' etter at spolen er ute
 *        -> laast. Utflanken ma hentes et annet sted: enten
 *           programvare-terskel pa RSSI, eller modulert baerebolge
 *           slik at DAT gir et pulstog mens feltet star pa.
 *   C) korte pulser med jevne mellomrom mens spolen star stille
 *        -> en automatisk timeout (R7 T_OUT) rearmer kretsen.
 *           Da ma T_OUT slas av for noe av dette gir mening.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* Sett til 1 for a sammenligne med rydding en gang i sekundet */
#define RYDD_HVERT_SEKUND 0

#define GAIN_REDUKSJON 0x09
#define DEMPER_PA      1
#define DEMPE_RES      1
#define TOLERANSE      2

#define AS3933_MODE_WRITE 0x00
#define AS3933_MODE_READ  0x40
#define AS3933_MODE_CMD   0xC0

#define CMD_CLEAR_WAKE     0x00
#define CMD_RESET_RSSI     0x01
#define CMD_CLEAR_FALSE    0x03
#define CMD_PRESET_DEFAULT 0x04
#define CMD_CALIB_RCO_LC   0x05

#define SPI_OPER (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | \
		  SPI_CS_ACTIVE_HIGH | SPI_MODE_CPHA)

static const struct spi_dt_spec as3933 =
	SPI_DT_SPEC_GET(DT_NODELABEL(as3933), SPI_OPER, 0);

static const struct gpio_dt_spec wake =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_wake), gpios);

static const struct gpio_dt_spec dat =
	GPIO_DT_SPEC_GET(DT_NODELABEL(as3933_dat), gpios);

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

/* 500 prover a 1 ms = ett vindu pa 500 ms */
#define PROVER 500
#define PER_TEGN 10

static uint8_t prov_dat[PROVER];
static uint8_t prov_wake[PROVER];

static void tegn_rad(const char *navn, const uint8_t *p)
{
	printk("%s ", navn);

	for (int i = 0; i < PROVER; i += PER_TEGN) {
		int sum = 0;

		for (int j = 0; j < PER_TEGN; j++) {
			sum += p[i + j];
		}

		if (sum == 0) {
			printk(".");
		} else if (sum == PER_TEGN) {
			printk("#");
		} else {
			printk("-");
		}
	}
	printk("\n");
}

int main(void)
{
	uint8_t v;

	k_msleep(500);

	printk("\n\n=== AS3933: folger DAT og WAKE ===\n\n");

	if (!spi_is_ready_dt(&as3933) || !gpio_is_ready_dt(&wake) ||
	    !gpio_is_ready_dt(&dat)) {
		printk("FEIL: SPI eller GPIO ikke klar\n");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&wake, GPIO_INPUT);
	gpio_pin_configure_dt(&dat, GPIO_INPUT);

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

	/* R7 styrer blant annet automatisk timeout pa vekkesignalet.
	 * Noter verdien og slaa den opp i databladet - den avgjor om
	 * kretsen rearmer seg selv uten at vi ber om det.
	 */
	les(7, &v);
	printk("R7 = 0x%02X  <- sjekk T_OUT-feltet i databladet\n", v);

	printk("rydding: %s\n\n",
	       RYDD_HVERT_SEKUND ? "clear_wake hvert sekund" : "AV");

	printk("Hvert tegn = 10 ms. '.' lav  '#' hoy  '-' vekslet\n");
	printk("For spolen inn, hold, og trekk den ut igjen.\n\n");

	while (1) {
		static uint32_t vindu;

		for (int i = 0; i < PROVER; i++) {
			prov_dat[i] = gpio_pin_get_dt(&dat) ? 1 : 0;
			prov_wake[i] = gpio_pin_get_dt(&wake) ? 1 : 0;
			k_busy_wait(1000);
		}

		printk("\n[%4u]\n", vindu++);
		tegn_rad("DAT ", prov_dat);
		tegn_rad("WAKE", prov_wake);

		if (RYDD_HVERT_SEKUND && (vindu % 2) == 0) {
			kommando(CMD_CLEAR_WAKE);
		}
	}

	return 0;
}
