/*
 * Brikken: AS3933 kantmaling + ESB-sending
 *
 * Steg 2 i tidtakersystemet.
 *
 * Brikka maler RSSI i tett lokke (ca 195 us per maling) inn i en
 * ringbuffer. Nar feltet passerer en terskel, tar den opp resten av
 * passeringen, finner de to terskelkryssingene, og sender resultatet
 * trdlost til porten.
 *
 * ESB-delen er kopiert ordrett fra esb_ptx-eksempelet i NCS. Den
 * eneste endringen er at LOG-makroene er byttet til printk, fordi
 * eksempelets loggniva-symbol ikke finnes utenfor eksempelet.
 *
 * MERK: ingen tidsstempling pa portsiden enna. Brikka regner ut
 * alderen pa midtpunktet i det oyeblikket pakken skrives, men
 * sendingen utloses fra programvare, sa det er noen hundre
 * mikrosekund usikkerhet i den. Det er steg 4 som fjerner den.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include <esb.h>

/* ---------- maleoppsett ---------- */

#define SETTLE_US 100	/* ventetid etter reset_RSSI for avlesning */
#define TRIGGER   13	/* starter opptak. Gulvet er 9. */
#define BEKREFT   3	/* malinger pa rad for en trigging godtas */

#define N    6000	/* ringbuffer, ca 1,17 sekund */
#define POST 4500	/* ovre grense for malinger etter trigging */
#define RO_KRAV 600	/* malinger under terskel for opptaket avsluttes */

#define MIN_SPENN      8	/* trinn mellom gulv og topp */
#define MIN_VARIGHET_US 5000	/* kortere passering finnes ikke */

/* Dempet oppsett. Gir gulv 9 og 23 pa 2 cm = 14 trinn a jobbe med.
 * Uten demping blir gulvet 15-19 og spennet mindre.
 */
#define GAIN_REDUKSJON 0x09
#define DEMPER_PA      1
#define DEMPE_RES      1
#define TOLERANSE      2

/* ---------- AS3933 ---------- */

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

/* ---------- pakkeformat ---------- */

#define BRIKKE_ID 0x01

/*
 *  0     brikke-id
 *  1     sekvensnummer
 *  2-5   varighet i us        (t2 - t1)
 *  6-9   alder i us           (sendeoyeblikk - midtpunkt)
 *  10    topp-RSSI
 *  11    gulv-RSSI
 *  12-15 midtpunkt - topp i us, med fortegn
 */
#define PAKKE_LEN 16

static uint8_t sekvens;

/* ---------- maledata ---------- */

static uint8_t  ring_v[N];
static uint32_t ring_t[N];
static uint8_t  v[N];	/* utpakket, eldste forst */
static uint32_t t[N];

struct resultat {
	bool     gyldig;
	uint32_t t1;
	uint32_t t2;
	uint32_t midt;
	uint8_t  topp;
	uint8_t  gulv;
	int32_t  avvik;	/* midtpunkt - topp */
};

/* ---------- ESB, ordrett fra esb_ptx ---------- */

static bool ready = true;
static struct esb_payload rx_payload;
static struct esb_payload tx_payload;

void event_handler(struct esb_evt const *event)
{
	ready = true;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		break;
	case ESB_EVENT_TX_FAILED:
		printk("ESB: TX FAILED\n");
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			/* ACK-nyttelast fra porten. Ikke brukt enna. */
		}
		break;
#if IS_ENABLED(CONFIG_ESB_MPSL_TIMESLOT)
	case ESB_EVENT_TIMESLOT_FAILED:
		printk("ESB: feil i timeslot\n");
		break;
#endif
	}
}

int esb_initialize(void)
{
	int err;
	uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4,
				  0xC5, 0xC6, 0xC7, 0xC8};

	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.retransmit_delay = 600;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PTX;
	config.selective_auto_ack = true;
	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err) {
		return err;
	}

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}

	return 0;
}

/* ---------- AS3933-hjelpere ---------- */

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

	/* Ma gjores for frekvensdeteksjon virker */
	kommando(CMD_CALIB_RCO_LC);
	k_msleep(100);
	skriv(3, 0x00);	/* kalibreringen setter R3 = 0x20 uten a bli bedt om det */

	kommando(CMD_CLEAR_FALSE);
	kommando(CMD_CLEAR_WAKE);
	k_msleep(20);
}

/* ---------- analyse ---------- */

/* Lineaer interpolasjon mellom prove i-1 og i */
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

static void analyser(struct resultat *r)
{
	uint32_t i1 = 0, i2 = 0, i_topp = 0, sist = 0;
	uint8_t topp = 0, gulv = 31;
	int32_t terskel;
	bool funnet = false;

	r->gyldig = false;

	for (uint32_t i = 0; i < N; i++) {
		if (v[i] > topp) {
			topp = v[i];
		}
		if (v[i] < gulv) {
			gulv = v[i];
		}
	}

	if (topp - gulv < MIN_SPENN) {
		printk("forkastet: spenn %u trinn (gulv %u, topp %u)\n",
		       topp - gulv, gulv, topp);
		return;
	}

	/* Terskelen velges etter opptaket, midt mellom gulv og topp.
	 * Der er flanken brattest, og malingen blir uavhengig av hvor
	 * naer brikka passerte.
	 */
	terskel = (gulv + topp + 1) / 2;

	/*
	 * ANKRE I SISTE PASSERING.
	 *
	 * Bufferet dekker 1,17 sekund og kan inneholde halen av en
	 * tidligere passering. Leter vi etter toppen i hele bufferet,
	 * og to pukler nar samme toppverdi, havner "midten av platået"
	 * i dalen mellom dem. Da blir t1 og t2 riktige mens
	 * toppreferansen er fullstendig feil.
	 *
	 * Derfor: finn siste maling over terskelen - det er slutten pa
	 * passeringen som nettopp utloste opptaket - og arbeid bakover
	 * derfra.
	 */
	for (uint32_t i = N - 1; i >= 1; i--) {
		if (v[i] >= terskel) {
			sist = i;
			funnet = true;
			break;
		}
	}

	if (!funnet || sist + 1 >= N) {
		printk("forkastet: fant ingen utgangsflanke\n");
		return;
	}

	i2 = sist + 1;

	for (uint32_t i = sist; i >= BEKREFT; i--) {
		if (v[i] < terskel && v[i - 1] < terskel &&
		    v[i - 2] < terskel) {
			i1 = i + 1;
			break;
		}
	}

	if (!i1) {
		printk("forkastet: fant ingen inngangsflanke\n");
		return;
	}

	r->t1 = kryss(i1, terskel);
	r->t2 = kryss(i2, terskel);

	if (r->t2 - r->t1 < MIN_VARIGHET_US) {
		printk("forkastet: varighet %u us\n", r->t2 - r->t1);
		return;
	}

	/*
	 * Topp og platå regnes BARE innenfor passeringen. Toppen er et
	 * plata, ikke et punkt - RSSI har 5 bits, sa naer maksimum star
	 * verdien stille i mange malinger. Forste prove som nar
	 * toppverdien ligger for tidlig og gir systematisk skjevhet.
	 */
	{
		uint8_t lokal_topp = 0;
		uint32_t forste = i1, siste = i1;
		bool sett = false;

		for (uint32_t i = i1; i <= i2; i++) {
			if (v[i] > lokal_topp) {
				lokal_topp = v[i];
			}
		}
		for (uint32_t i = i1; i <= i2; i++) {
			if (v[i] != lokal_topp) {
				continue;
			}
			if (!sett) {
				forste = i;
				sett = true;
			}
			siste = i;
		}

		i_topp = (forste + siste) / 2;
		r->topp = lokal_topp;
	}

	r->gulv = gulv;
	r->midt = (r->t1 + r->t2) / 2;
	r->avvik = (int32_t)(r->midt - t[i_topp]);
	r->gyldig = true;
}

/* ---------- sending ---------- */

static void legg_u32(uint8_t *p, uint32_t x)
{
	p[0] = (uint8_t)(x);
	p[1] = (uint8_t)(x >> 8);
	p[2] = (uint8_t)(x >> 16);
	p[3] = (uint8_t)(x >> 24);
}

static void send_resultat(const struct resultat *r)
{
	uint32_t na;
	uint32_t alder;
	int err;

	tx_payload.pipe = 0;
	tx_payload.length = PAKKE_LEN;
	tx_payload.noack = false;

	tx_payload.data[0] = BRIKKE_ID;
	tx_payload.data[1] = sekvens++;
	legg_u32(&tx_payload.data[2], r->t2 - r->t1);

	/* Alderen regnes sa sent som mulig for skrivingen */
	na = k_cyc_to_us_floor32(k_cycle_get_32());
	alder = na - r->midt;
	legg_u32(&tx_payload.data[6], alder);

	tx_payload.data[10] = r->topp;
	tx_payload.data[11] = r->gulv;
	legg_u32(&tx_payload.data[12], (uint32_t)r->avvik);

	ready = false;
	esb_flush_tx();

	err = esb_write_payload(&tx_payload);
	if (err) {
		printk("ESB: skriving feilet, err %d\n", err);
		ready = true;
		return;
	}

	printk("sendt  varighet %u us  alder %u us  topp %u  gulv %u  "
	       "avvik %d us\n",
	       r->t2 - r->t1, alder, r->topp, r->gulv, r->avvik);
}

/* ---------- hovedprogram ---------- */

int main(void)
{
	uint8_t r5;
	uint32_t hode = 0;
	uint32_t teller = 0;
	uint32_t igjen = 0;
	uint32_t pa_rad = 0;
	uint32_t ro = 0;
	bool trigget = false;
	uint8_t maks = 0, min = 31;
	int err;

	k_msleep(500);

	printk("\n\n=== Brikke: kantmaling + ESB ===\n\n");

	if (!spi_is_ready_dt(&as3933)) {
		printk("FEIL: SPI ikke klar\n");
		return 0;
	}

	err = esb_initialize();
	if (err) {
		printk("FEIL: ESB-init feilet, err %d\n", err);
		return 0;
	}
	printk("ESB klar (PTX)\n");

	sett_opp_as3933();

	les(5, &r5);
	printk("SPI: R5 = 0x%02X %s\n", r5, r5 == 0x69 ? "(ok)" : "(AVVIK)");
	printk("TRIGGER=%d  buffer=%d malinger\n\n", TRIGGER, N);
	printk("Hold spolen unna de forste 1,2 sekundene.\n\n");

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

		pa_rad = (m >= TRIGGER) ? pa_rad + 1 : 0;

		if (!trigget) {
			if (pa_rad >= BEKREFT && teller > N) {
				trigget = true;
				igjen = POST;
				/* Ingen printk her. En utskrift blokkerer
				 * pa UART-en i ca 3 ms, og det hullet
				 * havner noyaktig der t1 skal males.
				 */
			} else if ((teller % 1250) == 0) {
				if (maks + 2 < TRIGGER) {
					printk("venter...  maks=%2u  min=%2u%s\n",
					       maks, min,
					       teller <= N ?
					       "  (fyller buffer)" : "");
				}
				maks = 0;
				min = 31;
			}
			continue;
		}

		/*
		 * Opptaket avsluttes nar feltet har vaert borte i RO_KRAV
		 * malinger, ikke etter et fast etterlop. Et fast etterlop
		 * pa 4500 malinger er 878 ms, og da er hendelsen nesten et
		 * sekund gammel nar pakken gar ut.
		 */
		if (m < TRIGGER - 2) {
			ro++;
		} else {
			ro = 0;
		}

		igjen--;

		if (ro >= RO_KRAV || igjen == 0) {
			struct resultat r;

			for (uint32_t k = 0; k < N; k++) {
				uint32_t i = (hode + k) % N;

				v[k] = ring_v[i];
				t[k] = ring_t[i];
			}

			analyser(&r);

			if (r.gyldig) {
				send_resultat(&r);
			}

			trigget = false;
			teller = 0;
			pa_rad = 0;
			ro = 0;
			maks = 0;
			min = 31;
		}
	}

	return 0;
}
