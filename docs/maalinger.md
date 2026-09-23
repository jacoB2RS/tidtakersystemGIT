# Måleliste

Alle målinger med dato, utstyr og oppsett. Ny måling legges nederst.

---

## 2026-09-22 — Induktans på RLB9012-102KL

Utstyr: Philips PM6306 RCL-meter, 125 kHz, seriemodell, zero trim kjørt.

| Spole | L | Rs |
|---|---|---|
| 1 | 968,8 µH | 7,209 Ω |
| 2 | 960,8 µH | 7,3 Ω |
| 3 | 965,0 µH | 7,2 Ω |

Spredning mellom spolene: 0,8 %. Samme kondensator passer til alle tre.

Q for spolen alene: X_L / Rs = 761 / 7,2 ≈ 105.

Merk: databladet oppgir 2,9 Ω DC. Forskjellen er skinneffekt og
kjernetap ved 125 kHz, og det er AC-tallet som bestemmer Q.

---

## 2026-09-22 — Avstemming av TX-tank

Oppsett: TC4427A på 5 V, P1.11 PWM, skop på kondensatoren (kabel, 1×).

### 20 Ω demping, 1500 + 150 pF

| Frekvens | Pk-Pk |
|---|---|
| 121 kHz | 91,6 V |
| 125 kHz | 99,6 V |
| 129 kHz | 62,8 V |

Toppen ligger på eller like under 125 kHz.

### Demping

| R demping | Maximum over C |
|---|---|
| 39,4 Ω | ca. 27 V |
| 20 Ω | 52 V |
| 10 Ω | ca. 59 V |

Å halvere fra 20 til 10 Ω ga bare 13 % mer. Mesteparten av tapet ligger
i driveren på 5 V, breadboard-kontaktene og spolen — ikke i
dempemotstanden. Valgte 10 Ω med god margin til 100 V.

---

## 2026-09-22 — Avstemming målt over motstanden

Motstanden flyttet nederst i kjeden, kabel over den. Da påvirker ikke
kabelens ~200 pF avstemmingen.

| Kondensator | Pk-Pk over 10 Ω @ 125 kHz |
|---|---|
| 1650 pF (1500 ∥ 150) | 1,40 V → 1,32 V ved ny måling |
| 1500 pF alene | 1,32 V |

Forskjellen er innenfor måleusikkerheten (breadboard-kontakter,
håndkapasitans, μ'-gjennomsnittet som ikke har stabilisert seg).
**Valgte 1650 pF.**

Strøm i tanken: 1,4 V / 10 Ω = **70 mA topp**.
Beregnet spenning over C: 70 mA × 772 Ω ≈ 54 V topp + 2,5 V DC ≈ 57 V maks.

### Viktig lærdom

Den første avstemmingen (der 150 pF ble tatt ut) var målt med kabelen
over kondensatoren. Kabelen la til ca. 200 pF og dro resonansen ned, så
det så ut som tanken hadde for mye kapasitans. Med korrekt målepunkt
viste det seg at 1650 pF var riktig hele tiden.

**Regel: mål tanken over dempemotstanden, aldri over kondensatoren.**

---

## 2026-09-22 — Rekkevidde, uavstemt løs spole

Oppsett: løs RLB9012 rett på skopkabelen, ingen kondensator, ingen
galvanisk forbindelse til senderen. Spolene side om side på breadboard.

| Avstand | Pk-Pk |
|---|---|
| 3 cm | 472 mV |
| ~8 cm | forsvinner i støygulvet (13–30 mV) |

De 8 cm er grensen for **målemetoden**, ikke for systemet:

- Skopets støygulv med denne kabelen er ca. 30 mV Pk-Pk
- AS3933 reagerer på ca. 80 µV RMS — flere hundre ganger mindre
- Mottakerspolen var uavstemt. Med 680 pF og resonans blir signalet
  10–30× større
- Både sender og mottaker var RLB9012, en 12 mm drossel — ikke en antenne

Feltet faller som 1/r³, så 1000× bedre følsomhet gir grovt 10× rekkevidde.

### Neste forbedringer, i rekkefølge etter effekt

1. Større sendeantenne (rammespole 20–50 cm) — størst utslag
2. Avstemt mottaker (kommer med 3D-spole + 680 pF)
3. Høyere drivspenning på TC4427A (krever C med høyere spenningstål)

---

## TODO

- [ ] Induktans per akse på 3DC11LP-0238J (tre akser, mål hver for seg)
- [ ] Faktisk kapasitans på 1500/150/680 pF (±5 % toleranse)
- [ ] Kabelens egen kapasitans (for å kompensere ved RX-måling)
- [ ] Rekkevidde med avstemt mottakerspole
- [ ] Rekkevidde med AS3933
- [ ] Noe enda kulere, bro