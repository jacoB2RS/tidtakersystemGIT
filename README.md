# Tidtakersystem

LF wake-up + BLE tidtakersystem bygget på nRF54L15.

Porten sender et 125 kHz magnetfelt i korte pip. Brikken som løperen bærer
sover til den kjenner feltet, våkner, og sender tidsstempel tilbake over
Bluetooth.

## Arkitektur

```
PORTEN                              BRIKKEN
nRF54L15                            nRF54L15
   │ PWM 125 kHz                       │ SPI
TC4427A                             AS3933
   │                                   │
LC-tank  ────── 125 kHz felt ──────► 3D-spole
   │
   └────────── Bluetooth ◄─────────────┘
```

### Hvorfor tidsstempelet ligger i brikken

BLE advertising kan bli forsinket opp mot 10 ms tilfeldig. En løper i
20 km/t flytter seg 5 cm på den tiden. Derfor måler brikken selv tiden
fra vekking til sending, og legger det tallet i pakken. Porten trekker
det fra mottakstidspunktet.

## Mapper

| Mappe | Innhold |
|---|---|
| `sender/` | Zephyr-app for porten (PWM 125 kHz) |
| `brikke/` | Zephyr-app for brikken (AS3933 + BLE) — ikke startet |
| `docs/` | Målinger, hardware-notater, Git-guide |

## Status

- [x] PWM 125 kHz ut av nRF54L15
- [x] TC4427A driver LC-tank
- [x] Tanken avstemt til 125 kHz, ca. 70 mA topp i spolen
- [x] Felt bekreftet i lufta, målbart til ca. 8 cm med uavstemt løs spole
- [ ] 3D-spole + AS3933 på breakout
- [ ] SPI-kommunikasjon nRF ↔ AS3933
- [ ] Wake på feltdeteksjon
- [ ] BLE med tidsstempel
- [ ] Verifisering mot kablet referanse

## Hardware

Se `docs/hardware.md` for koblingsskjema og komponentverdier.
Se `docs/maalinger.md` for alle målinger med dato.

## Bygge

```bash
cd sender
west build -b nrf54l15dk/nrf54l15/cpuapp
west flash
```

Krever VDD:IO satt til 3,3 V i Board Configurator (nRF Connect for Desktop).
Standard er 1,8 V, som er under TC4427A sin terskel.
