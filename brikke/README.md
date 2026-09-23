# Brikken

Ikke startet. Venter på at 3DC11LP-0238J skal ankomme.

## Plan

1. Lodd AS3933 på LCQT-TSSOP16 breakout (0,65 mm pitch, bruk flux og
   loddefletting)
2. Koble opp **én** akse først med 680 pF, ikke alle tre
3. SPI mot nRF54L15 — les tilbake et register for å bekrefte at
   kommunikasjonen virker før noe annet
4. Bruk AS3933s innebygde RSSI til å måle feltstyrke mens brikken flyttes.
   Det er den beste måten å verifisere avstemmingen på
5. LED som blinker når WAKE går høy — første virkelige bevis på at
   systemet fungerer ende til ende
6. Deretter de to andre aksene
7. Deretter sleep-modus (System OFF, wake på GPIO-avbrudd)
8. Til slutt BLE med tidsstempel

Ikke hopp over punkt 5. Når LF-delen virker alene, er BLE enkelt. Gjør
man begge samtidig, vet man ikke hvor feilen er.

## Åpne spørsmål

- **Nøyaktighetskrav.** Sett et tall, f.eks. ±10 ms og passeringspunkt
  innenfor 20 cm. Uten et tall er "det virker" ikke målbart.
- **Verifiseringsmetode.** Forslag: en ledning mellom kortene der porten
  setter en GPIO høy samtidig med LF-pipet, og brikken leser den direkte.
  Da har begge en felles referanse med kjent forsinkelse, og avviket i
  det trådløse systemet kan måles. Ledningen fjernes til slutt.
- **Flere brikker samtidig.** To løpere som passerer sammen gir kollisjon
  i BLE. Vanlig løsning er tilfeldig forsinkelse før sending — men den
  forsinkelsen må inn i tidsstempelet, ellers ødelegger den nettopp det
  den skulle fikse.
- **32,768 kHz krystall til AS3933?** Gir skarpere frekvensfiltrering og
  færre falske vekkinger enn den interne RC-oscillatoren. Krever to
  lastkondensatorer på ca. 12 pF.

## Strømbudsjett (anslag, ikke målt)

| Tilstand | Strøm |
|---|---|
| AS3933 lytter | ~2,7 µA |
| nRF54L15 System OFF | ~1 µA |
| Sum hvile | ~4 µA |
| BLE-sending | ~5 mA i noen ms |

Med et par passeringer om dagen er det hvilestrømmen som bestemmer
batterilevetiden.
