# Git for dette prosjektet

Skrevet for to personer som jobber parallelt på hver sin del av
tidtakersystemet. Kommandoene er de du faktisk trenger, ikke hele Git.

## Modellen: fire steder en fil kan være

```
Arbeidsmappa  →  Staging  →  Lokalt repo  →  GitHub
   (endrer)      (git add)   (git commit)   (git push)
```

Dette er hele Git. Alt annet er variasjoner.

- **Arbeidsmappa** — filene du redigerer i VS Code
- **Staging** — det du har plukket ut til neste commit
- **Lokalt repo** — historikken på din maskin
- **GitHub** — historikken dere deler

Staging finnes fordi du ofte endrer fem ting men bare vil committe to av
dem. Har du endret både `main.c` og måleloggen, kan du committe dem hver
for seg med hver sin melding.

## Førstegangsoppsett

```bash
git config --global user.name "Ditt navn"
git config --global user.email "din@epost.no"
git config --global init.defaultBranch main
```

E-posten bør være den samme som på GitHub, ellers kobles ikke commitene
til kontoen din.

## Starte repoet

```bash
cd tidtakersystem
git init
git add .
git commit -m "Første commit: sender virker, tank avstemt til 125 kHz"
```

Lag så et tomt repo på GitHub (uten README — dere har allerede en), og:

```bash
git remote add origin https://github.com/<bruker>/tidtakersystem.git
git push -u origin main
```

Den andre henter det med:

```bash
git clone https://github.com/<bruker>/tidtakersystem.git
```

## Daglig syklus

```bash
git status                    # hva har endret seg?
git diff                      # nøyaktig hvilke linjer?
git add docs/maalinger.md     # plukk ut det som hører sammen
git commit -m "Legg til rekkeviddemåling med uavstemt spole"
git push
```

`git status` er kommandoen du kjører oftest. Den forteller alltid hvor du
står, og foreslår hva du kan gjøre.

**`git add .` legger til alt.** Greit når alt hører sammen, farlig når du
har halvferdige eksperimenter liggende. Vær spesifikk når du er i tvil.

## Commit-meldinger

En commit skal være én endring som gir mening alene.

```
Bra:  "Flytt dempemotstand nederst i tanken for målbarhet"
      "Rett pinout: IN B er pin 4, ikke pin 8"
      "Legg til burst-modus, 20 ms på / 2 s av"

Dårlig: "fikset"
        "wip"
        "endringer"
```

Skriv i imperativ — "legg til", ikke "la til". Det leses som en
instruksjon til kodebasen.

Om seks måneder skal `git log --oneline` fortelle dere hva som skjedde.
Det er hele poenget med å bruke Git på et læringsprosjekt.

## Branching — når dere jobber parallelt

Dere har naturlig todeling: én på TX og analog, én på AS3933 og SPI. Det
er akkurat det brancher er til for.

```bash
git checkout -b as3933-spi     # lag og bytt til branch
# ... jobb, commit flere ganger ...
git push -u origin as3933-spi  # første push av branchen
```

Når det virker, slå det sammen til `main`:

```bash
git checkout main
git pull                       # hent det den andre har pushet
git merge as3933-spi
git push
```

Regelen: **`main` skal alltid bygge.** Eksperimenter lever på brancher.

Alternativt kan dere bruke Pull Requests på GitHub i stedet for å merge
lokalt. Da får den andre sett over endringen først. Det er verdt å lære,
fordi det er slik all profesjonell utvikling fungerer.

## Merge-konflikter

Skjer når dere begge har endret samme linjer. Git setter merker i fila:

```
<<<<<<< HEAD
motstand: 10 Ω
=======
motstand: 20 Ω
>>>>>>> as3933-spi
```

Rediger fila så den blir riktig, slett merkene, så:

```bash
git add <fila>
git commit
```

Konflikter er ikke farlige og ikke tegn på at noe er ødelagt. De betyr
bare at Git ikke kan gjette hvem som har rett.

**Slik unngår dere de fleste:** `git pull` før dere begynner å jobbe, og
push ofte. Konflikter oppstår når kode ligger lenge usynkronisert.

## Nyttige kommandoer

```bash
git log --oneline --graph      # historikken visuelt
git diff HEAD~1                # hva endret siste commit?
git checkout -- <fil>          # kast endringer i én fil
git restore --staged <fil>     # ta ut av staging, behold endringen
git stash                      # legg vekk endringer midlertidig
git stash pop                  # hent dem tilbake
```

`git stash` er nyttig når den andre har pushet noe du trenger, men du er
midt i noe halvferdig.

## Angre ting

| Situasjon | Kommando |
|---|---|
| Feil i commit-melding, ikke pushet | `git commit --amend` |
| Vil angre siste commit, beholde endringene | `git reset --soft HEAD~1` |
| Vil kaste siste commit helt | `git reset --hard HEAD~1` |
| Vil angre en commit som er pushet | `git revert <hash>` |

`git reset --hard` sletter arbeid permanent. `git revert` lager en ny
commit som reverserer den gamle, og er trygt på delte brancher.

## Hva som ikke skal i repoet

Se `.gitignore`. Kort sagt: `build/`-mapper, `.vscode/`, og alt som
genereres av byggesystemet.

Grunnen er at byggeartefakter er store, endrer seg hver gang, og kan
gjenskapes. De ville fylt historikken med støy.

## Rutine for dette prosjektet

1. `git pull` når du setter deg ned
2. Jobb på egen branch hvis det er større enn en liten fiks
3. Commit hver gang noe virker — ikke bare ved dagens slutt
4. **Legg målinger i `docs/maalinger.md` samme dag.** Tall dere ikke
   skriver ned, er tall dere må måle på nytt
5. `git push` før du går

## Å lære mer

- `git help <kommando>` virker offline og er bedre enn de fleste tutorials
- https://learngitbranching.js.org — interaktiv, visuell, gratis
- Pro Git (gratis bok): https://git-scm.com/book/no/v2
