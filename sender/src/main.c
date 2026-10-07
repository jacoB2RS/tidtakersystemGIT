/*
 * Porten: 125 kHz LF-sender + ESB-mottak
 *
 * Steg 2 i tidtakersystemet.
 *
 * Brettet gjor to jobber samtidig. PWM-en lager baerebolgen pa
 * 125 kHz og kjorer i maskinvare uten at CPU-en er involvert, sa
 * feltet star uavbrutt mens radioen lytter.
 *
 * ESB-delen er kopiert ordrett fra esb_prx-eksempelet i NCS. Endret:
 *   - LOG-makroer byttet til printk
 *   - ingen periodisk ACK-nyttelast; porten svarer bare nar brikka
 *     sender. Eksempelets lokke som skrev en pakke hvert 550. ms er
 *     fjernet, den hadde ingen hensikt her.
 *
 * MERK: porten tidsstempler ikke mottaket enna. Den skriver bare ut
 * det brikka sendte. Tidsstemplingen er steg 3.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>
#include <esb.h>

/* 125 kHz = 8 us periode */
#define PERIODE_125K_NS 8000u

static const struct pwm_dt_spec lf = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_lf));

#define PAKKE_LEN 16

static struct esb_payload rx_payload;

/*
 * Portens egen klokke.
 *
 * k_uptime_ticks() er 64 bit og monoton - den ruller ikke rundt.
 * Opplosningen er CONFIG_SYS_CLOCK_TICKS_PER_SEC, satt til 32768 i
 * prj.conf, altsa 30,5 us per tikk. Det er 33 ganger finere enn
 * millisekundbudsjettet.
 *
 * Tidsstempelet tas forst i avbruddsrutinen, for noe annet gjores.
 * Da er forsinkelsen fra radioen mottok pakken til vi leser klokka
 * noen mikrosekund.
 */
static uint64_t na_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

static uint64_t forrige_passering;
static bool har_forrige;

/*
 * Avbruddsrutinen tar tidsstempelet og legger pakken til side.
 * Utskriften skjer i hovedlokka.
 *
 * Grunnen: to linjer pa 115200 baud blokkerer i rundt 13 ms, og en
 * printk inne i ESB-stackens avbrudd ville holdt radioen opptatt
 * hele den tiden.
 */
struct hendelse {
	uint64_t mottak;
	uint8_t  data[PAKKE_LEN];
	uint8_t  len;
};

static struct hendelse ko;
static volatile bool ko_full;

static uint32_t hent_u32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void vis_passering(const uint8_t *d, uint8_t len, uint64_t mottak)
{
	uint32_t varighet, alder;
	uint64_t passering;
	int32_t avvik;
	uint8_t id, seq, topp, gulv;

	if (len < PAKKE_LEN) {
		printk("kort pakke, len %u - ignorert\n", len);
		return;
	}

	id       = d[0];
	seq      = d[1];
	varighet = hent_u32(&d[2]);
	alder    = hent_u32(&d[6]);
	topp     = d[10];
	gulv     = d[11];
	avvik    = (int32_t)hent_u32(&d[12]);

	/* Dette er hele poenget: passeringen plasseres pa portens egen
	 * tidsakse, uten at de to brettene har synkroniserte klokker.
	 */
	passering = mottak - alder;

	printk("brikke %u  #%3u | PASSERTE %llu.%06llu s",
	       id, seq, passering / 1000000ULL, passering % 1000000ULL);

	if (har_forrige) {
		uint64_t d = passering - forrige_passering;

		printk(" | siden forrige %llu.%03llu s",
		       d / 1000000ULL, (d % 1000000ULL) / 1000ULL);
	}
	printk("\n");

	printk("         varighet %7u us | alder %7u us | topp %2u gulv %2u"
	       " | avvik %6d us%s\n",
	       varighet, alder, topp, gulv, avvik,
	       (avvik > (int32_t)(varighet / 10) ||
		avvik < -(int32_t)(varighet / 10)) ? "  <-- SKJEV" : "");

	forrige_passering = passering;
	har_forrige = true;
}

void event_handler(struct esb_evt const *event)
{
	int err;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		break;
	case ESB_EVENT_TX_FAILED:
		printk("ESB: TX FAILED\n");
		break;
	case ESB_EVENT_RX_RECEIVED: {
		/* Tidsstempel forst. Alt annet kan vente. */
		uint64_t mottak = na_us();

		while ((err = esb_read_rx_payload(&rx_payload)) == 0) {
			if (ko_full) {
				continue;	/* hovedlokka henger etter */
			}
			ko.mottak = mottak;
			ko.len = rx_payload.length;
			for (int i = 0; i < PAKKE_LEN &&
					i < rx_payload.length; i++) {
				ko.data[i] = rx_payload.data[i];
			}
			ko_full = true;
		}
		if (err && err != -ENODATA) {
			printk("ESB: feil ved lesing av pakke\n");
		}
		break;
	}
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
	config.bitrate = ESB_BITRATE_2MBPS;
	config.mode = ESB_MODE_PRX;
	config.event_handler = event_handler;
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

int main(void)
{
	int err;

	k_msleep(500);

	printk("\n\n=== Porten: LF-sender + ESB-mottak ===\n\n");

	if (!pwm_is_ready_dt(&lf)) {
		printk("FEIL: PWM ikke klar\n");
		return 0;
	}

	/* 50 %% duty, kontinuerlig. Burst er feil her: da folger
	 * feltet burstene i stedet for passeringen.
	 */
	err = pwm_set_dt(&lf, PERIODE_125K_NS, PERIODE_125K_NS / 2u);
	if (err) {
		printk("FEIL: pwm_set_dt, err %d\n", err);
		return 0;
	}
	printk("LF-felt pa, 125 kHz kontinuerlig\n");

	err = esb_initialize();
	if (err) {
		printk("FEIL: ESB-init feilet, err %d\n", err);
		return 0;
	}

	err = esb_start_rx();
	if (err) {
		printk("FEIL: esb_start_rx, err %d\n", err);
		return 0;
	}
	printk("ESB klar (PRX), venter pa passeringer\n");
	printk("Klokkeopplosning: %u us per tikk\n\n",
	       1000000u / CONFIG_SYS_CLOCK_TICKS_PER_SEC);

	while (1) {
		if (ko_full) {
			uint8_t d[PAKKE_LEN];
			uint64_t mottak = ko.mottak;
			uint8_t len = ko.len;

			for (int i = 0; i < PAKKE_LEN; i++) {
				d[i] = ko.data[i];
			}
			ko_full = false;

			vis_passering(d, len, mottak);
		}
		k_msleep(2);
	}

	return 0;
}
