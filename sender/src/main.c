/*
 * LF-sender for tidtakersystem
 *
 * nRF54L15 P1.11 -> TC4427A -> LC-tank (968 uH + 1650 pF + 10 ohm)
 * Malt 2026-09-22: 70 mA topp i spolen, ca. 57 V maks over kondensatoren.
 *
 * Krever VDD:IO = 3,3 V (Board Configurator). Standard 1,8 V ligger
 * under TC4427A sin terskel pa ca. 2 V.
 *
 * Tre testmoduser, bytt MODE og bygg pa nytt:
 *   1 KONTINUERLIG - fast 125 kHz, for a sjekke signal og driver
 *   2 BURST        - 20 ms pa, 2 s av, som i virkelig drift
 *   3 SVEIP        - 110-145 kHz for a finne resonansen
 *
 * Mal over dempemotstanden, ikke over kondensatoren: en kabel uten
 * 10x probe legger til ca. 200 pF og flytter avstemmingen flere kHz.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>

#define MODE_KONTINUERLIG 1
#define MODE_BURST        2
#define MODE_SVEIP        3

#define MODE MODE_BURST

/* 16 MHz / 128 = 125,0 kHz */
#define PERIODE_125K_NS 8000u

#define BURST_MS 20
#define PAUSE_MS 2000

/*
 * Sveip. N er antall klokketikk pa 16 MHz: f = 16 MHz / N.
 * N=128 -> 125,0 kHz. N=110 -> 145 kHz. N=145 -> 110 kHz.
 */
#define SVEIP_N_MIN    110u
#define SVEIP_N_MAX    145u
#define SVEIP_DVELE_MS 1500

static const struct pwm_dt_spec lf = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_lf));

/* Duty 50 % gir sterkest grunnharmonisk */
static int lf_pa(uint32_t periode_ns)
{
	return pwm_set_dt(&lf, periode_ns, periode_ns / 2u);
}

static int lf_av(uint32_t periode_ns)
{
	return pwm_set_dt(&lf, periode_ns, 0);
}

int main(void)
{
	int err;

	if (!pwm_is_ready_dt(&lf)) {
		printk("FEIL: PWM-enheten er ikke klar\n");
		return -ENODEV;
	}

#if MODE == MODE_KONTINUERLIG

	printk("Modus: kontinuerlig 125 kHz\n");
	printk("Skop pa P1.11: firkant, 8,0 us periode, 0-3,3 V\n");

	err = lf_pa(PERIODE_125K_NS);
	if (err) {
		printk("FEIL: pwm_set_dt ga %d\n", err);
		return err;
	}

	/* PWM gar i maskinvare, CPU trenger ikke gjore noe */
	k_sleep(K_FOREVER);

#elif MODE == MODE_BURST

	printk("Modus: burst, %d ms pa / %d ms av\n", BURST_MS, PAUSE_MS);

	while (1) {
		err = lf_pa(PERIODE_125K_NS);
		if (err) {
			printk("FEIL: pwm_set_dt ga %d\n", err);
			return err;
		}
		k_msleep(BURST_MS);

		lf_av(PERIODE_125K_NS);
		k_msleep(PAUSE_MS);
	}

#elif MODE == MODE_SVEIP

	printk("Modus: sveip %u-%u kHz\n",
	       16000u / SVEIP_N_MAX, 16000u / SVEIP_N_MIN);
	printk("Se etter storst Pk-Pk over dempemotstanden\n\n");

	while (1) {
		for (uint32_t n = SVEIP_N_MIN; n <= SVEIP_N_MAX; n++) {
			/* periode = n / 16 MHz, i nanosekunder */
			uint32_t periode_ns = (n * 125u) / 2u;
			uint32_t f_hz = 16000000u / n;

			printk("N=%3u  %6u Hz\n", n, f_hz);

			lf_pa(periode_ns);
			k_msleep(SVEIP_DVELE_MS);
			lf_av(periode_ns);
			k_msleep(50);
		}
		printk("--- sveip ferdig, starter pa nytt ---\n\n");
		k_msleep(1000);
	}

#else
#error "Ugyldig MODE"
#endif

	return 0;
}
