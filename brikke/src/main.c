/*
 * Brikken: RSSI-skop med ringbuffer, trigger og kantanalyse
 *
 * Versjon 2. Endringer:
 *   - Dumpen er kortet kraftig ned. Forrige versjon skrev 6000
 *     linjer med tid og verdi, rundt 6 sekunder pa 115200 baud.
 *     Na skrives tiden en gang i hodet og bare verdiene etterpa,
 *     siden maleintervallet er jevnt pa ca 196 us.
 *   - Brikka regner selv ut t1, t2, varighet og midtpunkt med
 *     interpolasjon, og skriver ut malepunktene rundt hver
 *     kryssing. Da ser dere svaret med en gang, uten a plotte.
 *
 * MERK OM VENTETIDEN
 *   Etter trigging tar den POST malinger til for den dumper.
 *   4500 x 196 us = 880 ms. Det er meningen: vi ma fa med det
 *   som skjer ETTER at spolen passerte. Foler det som om den
 *   "ikke gir seg", er det den sekunden pluss utskriften.
 *
 * TERSKEL brukes bade til a utlose opptaket og til a finne t1/t2.
 * BEKREFT er hvor mange malinger pa rad som ma vaere pa samme side
 * av terskelen for en kryssing godtas. Det er hysteresen var.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#define SETTLE_US 100
#define TERSKEL   13
#define BEKREFT   3

#define N    6000
#define POST 4500

/*
 * TILBAKE TIL DEMPET OPPSETT.
 *
 * Jeg skrudde dempingen av for a vinne rekkevidde. Malingene sier
 * at det var feil vei:
 *
 *   dempet   (-16 dB):  gulv  9,  2 cm -> 23   = 14 trinn a jobbe med
 *   udempet:            gulv 15-19, metning 31 =  12 trinn, og den
 *                       metter tidlig
 *
 * Gulvet her er ikke termisk stoy, det er oppfanget stoy fra
 * omgivelsene. Demping for forsterkeren fjerner stoyen og
 * forsterkeren sitt eget gulv blir staende - derfor faller gulvet
 * mer enn signalet.
 *
 * Det dempede oppsettet har altsa storre brukbart spenn. Vi bruker
 * det.
 */
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

#define REG_RSSI1 10

#define SPI_OPER (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | \
		  SPI_CS_ACTIVE_HIGH | SPI_MODE_CPHA)

static const struct spi_dt_spec as3933 =
	SPI_DT_SPEC_GET(DT_NODELABEL(as3933), SPI_OPER, 0);

static uint8_t  ring_v[N];
static uint32_t ring_t[N];

/* Utpakket, eldste forst */
static uint8_t  v[N];
static uint32_t t[N];

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

static uint8_t en_maling(void)
{
	uint8_t x = 0;

	kommando(CMD_RESET_RSSI);
	k_busy_wait(SETTLE_US);
	les(REG_RSSI1, &x);

	return x & 0x1F;
}

static void sett_opp_as3933(void)
{
	kommando(CMD_PRESET_DEFAULT);
	k_msleep(20);

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
}

/* Lineaer interpolasjon mellom prove i-1 og i, i mikrosekund */
static uint32_t kryss(uint32_t i, int32_t niva)
{
	int32_t va = v[i - 1];
	int32_t vb = v[i];
	int32_t dt = (int32_t)(t[i] - t[i - 1]);

	if (vb == va) {
		return t[i];
	}
	return t[i - 1] + (uint32_t)(((niva - va) * dt) / (vb - va));
}

static void vindu(const char *navn, uint32_t midt)
{
	uint32_t fra = midt > 8 ? midt - 8 : 0;
	uint32_t til = midt + 8 < N ? midt + 8 : N - 1;

	printk("%s (prove %u):\n", navn, midt);
	for (uint32_t i = fra; i <= til; i++) {
		printk("  %7u us  %2u %s\n",
		       t[i] - t[0], v[i], i == midt ? "<--" : "");
	}
}

static void analyser(void)
{
	uint32_t i1 = 0, i2 = 0;
	uint8_t topp = 0, gulv = 31;
	uint32_t i_topp = 0;
	int32_t terskel;

	for (uint32_t i = 0; i < N; i++) {
		if (v[i] > topp) {
			topp = v[i];
		}
		if (v[i] < gulv) {
			gulv = v[i];
		}
	}

	/*
	 * Toppen er et PLATA, ikke et punkt. RSSI har 5 bits, sa naer
	 * maksimum star verdien stille i mange malinger. Tar vi forste
	 * prove som nar toppverdien, legger vi toppen for tidlig, og
	 * "midtpunkt - topp" blir systematisk positiv.
	 *
	 * Vi bruker midten av platået i stedet.
	 */
	{
		uint32_t forste = 0, siste = 0;
		bool funnet = false;

		for (uint32_t i = 0; i < N; i++) {
			if (v[i] == topp) {
				if (!funnet) {
					forste = i;
					funnet = true;
				}
				siste = i;
			}
		}
		i_topp = (forste + siste) / 2;
		printk("\ntoppplata: prove %u til %u, %u malinger\n",
		       forste, siste, siste - forste + 1);
	}

	/*
	 * TERSKELEN VELGES ETTERPA, UT FRA KURVEN.
	 *
	 * Brikka har tidsstempel pa hver eneste maling, sa den trenger
	 * ikke bestemme terskelen for passeringen - bare etter. Da kan
	 * den legges midt mellom gulv og topp, der flanken er brattest
	 * og kryssingen skarpest.
	 *
	 * Det gjor ogsa malingen uavhengig av hvor naer brikka passerte.
	 * En fast terskel ville ligget nede i foten pa en naer passering
	 * og oppe pa toppen av en fjern en.
	 */
	terskel = (gulv + topp + 1) / 2;

	printk("\n=== ANALYSE ===\n");
	printk("gulv %u, topp %u ved %u us\n", gulv, topp, t[i_topp] - t[0]);
	printk("terskel %d (midt mellom gulv og topp)\n", terskel);

	if (topp - gulv < 6) {
		printk("for svakt. spennet ma vaere minst 6 trinn.\n");
		printk("Ga naermere senderen.\n");
		return;
	}

	/*
	 * Kryssingene sokes UTOVER FRA TOPPEN, ikke forfra. Soker vi
	 * forfra, fanger vi forste gang stoyen tilfeldig vipper over
	 * terskelen. Toppen er derimot utvetydig.
	 */
	for (uint32_t i = i_topp; i >= 2; i--) {
		if (v[i] < terskel && v[i - 1] < terskel &&
		    v[i - 2] < terskel) {
			i1 = i + 1;
			break;
		}
	}

	for (uint32_t i = i_topp + 1; i + 2 < N; i++) {
		if (v[i] < terskel && v[i + 1] < terskel &&
		    v[i + 2] < terskel) {
			i2 = i;
			break;
		}
	}

	if (!i1) {
		printk("fant ingen inngangskryssing\n");
		return;
	}
	if (!i2) {
		printk("fant inngang men ingen utgang - feltet forsvant "
		       "ikke innenfor opptaket\n");
		vindu("FLANKE INN", i1);
		return;
	}

	uint32_t t1 = kryss(i1, terskel);
	uint32_t t2 = kryss(i2, terskel);
	uint32_t midt = (t1 + t2) / 2;

	printk("t1        %u us\n", t1 - t[0]);
	printk("t2        %u us\n", t2 - t[0]);
	printk("varighet  %u us\n", t2 - t1);
	printk("midtpunkt %u us\n", midt - t[0]);

	/*
	 * Symmetritest. For en jevn passering skal midtpunktet falle
	 * sammen med toppen. Avviket er et direkte mal pa hvor ujevn
	 * farten var, og er den beste kvalitetsindikatoren vi har.
	 */
	{
		int32_t avvik = (int32_t)(midt - t[i_topp]);

		printk("midtpunkt - topp  %d us", avvik);
		if (avvik > (int32_t)((t2 - t1) / 10) ||
		    avvik < -(int32_t)((t2 - t1) / 10)) {
			printk("   <-- SKJEV, over 10%% av varigheten");
		}
		printk("\n");
	}

	vindu("FLANKE INN", i1);
	vindu("FLANKE UT", i2);
}

static void dump(uint32_t hode)
{
	for (uint32_t k = 0; k < N; k++) {
		uint32_t i = (hode + k) % N;

		v[k] = ring_v[i];
		t[k] = ring_t[i];
	}

	printk("\n--- OPPTAK ---\n");
	printk("antall %d, varighet %u us, snitt %u ns per prove\n",
	       N, t[N - 1] - t[0], ((t[N - 1] - t[0]) * 1000) / (N - 1));

	analyser();

	printk("\n---VERDIER--- (bare rssi, jevnt intervall)\n");
	for (uint32_t k = 0; k < N; k++) {
		printk("%u%c", v[k], ((k % 50) == 49) ? '\n' : ',');
	}
	printk("\n---SLUTT---\n\n");
}

int main(void)
{
	uint8_t r5;
	uint32_t hode = 0;
	uint32_t teller = 0;
	bool trigget = false;
	uint32_t igjen = 0;
	uint8_t maks = 0, min = 31;
	uint8_t gulv_maks = 0;
	uint32_t pa_rad = 0;
	bool foreslatt = false;

	k_msleep(500);

	printk("\n\n=== AS3933 RSSI-skop v2 ===\n\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI ikke klar\n");
		return -ENODEV;
	}

	sett_opp_as3933();

	les(5, &r5);
	printk("SPI: R5 = 0x%02X %s\n", r5, r5 == 0x69 ? "(ok)" : "(AVVIK)");

	/* Les tilbake det vi skrev. Kalibreringen har endret registre
	 * bak ryggen var for, sa vi sjekker at dempingen faktisk er av.
	 */
	{
		uint8_t r;

		for (uint8_t i = 0; i <= 8; i++) {
			les(i, &r);
			printk("R%-2u = 0x%02X\n", i, r);
		}
		les(4, &r);
		printk("-> gain reduction %u, demperesistor %u",
		       r & 0x0F, (r >> 4) & 0x03);
		les(1, &r);
		printk(", antennedemper %s\n\n",
		       (r & 0x10) ? "PA" : "av");
	}
	printk("SETTLE_US=%d  TERSKEL=%d  BEKREFT=%d  buffer=%d\n\n",
	       SETTLE_US, TERSKEL, BEKREFT, N);
	printk("VIKTIG: hold spolen LANGT UNNA de forste 1,2 sekundene.\n");
	printk("Det er da stoygulvet males. Ligger spolen i feltet da,\n");
	printk("maler den signalet og kaller det gulv.\n\n");
	printk("Beveg spolen og se at maks folger avstanden.\n");
	printk("Nar en maling nar %d gar opptaket, og den maler ca\n", TERSKEL);
	printk("0,9 sekund til for den skriver ut. Det er meningen.\n\n");

	while (1) {
		uint8_t m = en_maling();

		ring_v[hode] = m;
		ring_t[hode] = k_cyc_to_us_floor32(k_cycle_get_32());
		hode = (hode + 1) % N;
		teller++;

		if (m > maks) {
			maks = m;
		}
		if (m < min) {
			min = m;
		}

		/* Stoygulv males mens bufferet fylles forste gang */
		if (teller <= N) {
			if (m > gulv_maks) {
				gulv_maks = m;
			}
			if (teller == N && !foreslatt) {
				foreslatt = true;
				printk("\nStoygulvet toppet pa %u.\n", gulv_maks);
				printk("Foreslatt TERSKEL = %u. Na star den "
				       "pa %d.\n\n", gulv_maks + 3, TERSKEL);
			}
		}

		/* Enkeltspiker skal ikke trigge - krev BEKREFT pa rad */
		pa_rad = (m >= TERSKEL) ? pa_rad + 1 : 0;

		if (!trigget) {
			if (pa_rad >= BEKREFT && teller > N) {
				/* INGEN printk her. En utskrift tar ca 3 ms
				 * og river et hull i malingene akkurat der
				 * flanken ligger.
				 */
				trigget = true;
				igjen = POST;
			} else if ((teller % 1250) == 0) {
				/* Skriv bare nar det er rolig. En utskrift
				 * blokkerer pa UART-en i ca 3 ms, og et slikt
				 * hull rett for triggingen odelegger t1.
				 */
				if (maks + 2 < TERSKEL) {
					printk("venter...  maks=%2u  min=%2u%s\n",
					       maks, min,
					       teller <= N ?
					       "  (fyller buffer)" : "");
				}
				maks = 0;
				min = 31;
			}
		} else if (--igjen == 0) {
			dump(hode);

			trigget = false;
			teller = 0;
			maks = 0;
			min = 31;
			pa_rad = 0;
			gulv_maks = 0;
			foreslatt = false;

			printk("Venter pa neste.\n\n");
		}
	}

	return 0;
}
