# Hardware

## Senderen (porten)

### Koblingsskjema

```
nRF54L15 DK                        Breadboard
───────────────────────────────────────────────────────
P1.11                       →  TC4427A pin 2  (IN A)
P30/P31/P32 pin 1  (5 V)    →  TC4427A pin 6  (VDD)
P30/P31/P32 pin 2  (GND)    →  jordskinne
```

### TC4427A pinout (PDIP-8, hakk til venstre)

```
pin 1  NC      ubrukt
pin 2  IN A    ← P1.11
pin 3  GND     → jord
pin 4  IN B    → jord          (MÅ kobles, ellers selvsvinging)
pin 5  OUT B   ubrukt
pin 6  VDD     ← 5 V
pin 7  OUT A   → tanken
pin 8  NC      ubrukt
```

100 nF mellom pin 6 og pin 3, under 5 mm ledning. Ikke valgfritt —
de raske strømrykkene kommer fra denne kondensatoren, ikke fra USB.

### LC-tanken

```
pin 7 → 968 µH → 1500 pF ∥ 150 pF → 10 Ω → GND
```

Motstanden står nederst (mot jord) med vilje: da kan strømmen i tanken
måles med en vanlig kabel uten at kabelens kapasitans havner parallelt
med resonanskondensatoren og flytter avstemmingen.

| Komponent | Verdi | Del |
|---|---|---|
| Spole | 968 µH målt | RLB9012-102KL (1 mH ±10 %) |
| Resonans-C | 1650 pF | FKP2 1500 pF ∥ 150 pF, 100 VDC |
| Demping | 10 Ω | FMP100 / MFR50, 1 W |

### Hvorfor dempemotstanden er der

Spolen alene har Q ≈ 105 ved 125 kHz. Uten demping blir spenningen over
kondensatoren flere hundre volt, og WIMA-kondensatorene tåler 100 VDC.

| R total | Q | V topp over C |
|---|---|---|
| 14 Ω (ingen demping) | 54 | 268 V — sprenger C |
| 24 Ω | 31 | 155 V — sprenger C |
| ~44 Ω | 17 | 87 V |
| ~54 Ω | 14 | 70 V |

Målt med 10 Ω: ca. 59 V maks. Trygt.

### Merk: likespenningsforskyvning

Driveren gir firkant 0–5 V, altså 2,5 V DC-gjennomsnitt. Spolen og
motstanden slipper likestrøm gjennom, kondensatoren blokkerer den, så
hele 2,5 V havner oppå vekselspenningen over kondensatoren.
Det er **Maximum**, ikke halve Peak-to-Peak, som skal sammenlignes med
kondensatorens 100 V.

## Brikken (ikke bygget ennå)

```
3DC11LP-0238J (2,38 mH/akse) ∥ 680 pF per akse → AS3933 → SPI → nRF54L15
```

680 pF med 2,38 mH gir 125,1 kHz. AS3933 har i tillegg intern
trimkapasitans per kanal.

AS3933 sitter på LCQT-TSSOP16 breakout (0,65 mm pitch).

## Kjente fallgruver

- **VDD:IO står på 1,8 V som standard** på nRF54L15 DK. TC4427A vil ha
  ca. 2 V for å se "høy". Sett 3,3 V i Board Configurator.
- **P6 er ikke 5 V.** Den er for strømmåling og ekstern VDD:nRF (1,8–3,6 V).
  5 V kommer fra P30/P31/P32 pin 1.
- **Ledige P1-pinner er bare P1.11 og P1.12.** Resten er krystall (00/01),
  NFC (02/03), UART1 (04–07), knapper (08/09/13) og LED (10/14).
- **Kondensatorer parallelt legges sammen, i serie blir de mindre.**
  1500 og 150 i serie gir 136 pF → resonans på 438 kHz, ingen feltstyrke.
- **Kabel uten 10× probe belaster tanken** med ca. 200 pF og flytter
  resonansen flere kHz ned. Mål over motstanden i stedet.
