# Sådan bruger du NFC-spil (nfc-games)

Denne side er til **dig derhjemme** — ikke til at kode.

Åbn den når som helst på Gitea:  
**http://192.168.1.3:3002/app/nfc-games/src/branch/main/VEJLEDNING.md**

---

## Hvad det gør

Du har en **ACR122U**-læser i USB. Hvert NFC-tag er bundet til ét spil i **Steam**.

| Du gør | Maskinen gør | LED |
| --- | --- | --- |
| Lægger tagget **på** læseren | Starter spillet | **Gul** + 2 bip |
| Lader tagget **ligge** | Spillet bliver kørende | **Gul** (fast) |
| Tager tagget **af** | Stopper spillet | **Grøn** + 1 bip |
| Intet program kører | — | **Rød** |

Kun **ét** spil ad gangen.

---

## Hver dag (det du skal huske)

1. Tænd **steam-win11** og log ind.  
2. Start **Steam** (skal køre).  
3. USB-læseren skal sidde i.  
4. **Grøn LED** = klar (lyt starter selv ved login).  
5. Læg et bundet tag på læseren. Tag det af for at stoppe.

Du skal **ikke** åbne koden. Du skal **ikke** vælge 1 i menuen hver dag.

Hvis LED er **rød**: kør `nfc lyt`, eller åbn **NFC-spil** og vælg **1**.

---

## Dine tags lige nu

| Tag (UID) | Spil |
| --- | --- |
| `1D2B7022960000` | Silent Hill 4: The Room |
| `1DB45322960000` | Agony UNRATED |
| `1D505E22960000` | Resident Evil Code Veronica X (USA) Disc 1 |
| `1D067A22960000` | Mario Kart Wii |

Listen ligger i `~/.config/nfc-games/tags.conf`. Den følger **denne PC**, ikke GitHub.

---

## Åbn menuen

Dobbeltklik **NFC-spil** i app-menuen, eller kør:

```bash
nfc
```

Skriv et **nummer** og Enter.

| Nr. | Hvad |
| --- | --- |
| 1 | Lyt efter tags (hvis LED er rød) |
| 2 | Bind nyt tag — vælg spil fra listen |
| 3 | Vis dine tags |
| 4 | Læs tagget på læseren |
| 5 | Fjern et tag |
| 6 | Skriv spilnavn på tagget (telefonen) |
| 7 | Sprog |
| 8 | Mere (teknik) |
| 0 | Afslut |

---

## Bind et nyt tag (nyt spil)

Spillet skal **allerede** ligge i Steam (eller som non-Steam-genvej).

1. Åbn menuen.  
2. Vælg **2**.  
3. Skriv **nummeret** ud for spillet (eller navnet).  
4. Læg det tomme tag på læseren, når den beder om det.  
5. Tagget er nu bundet, og spilnavnet skrives på tagget.

Bind **ikke** tags til `boot-windows` eller Proton/runtime-værktøjer.

---

## Telefonen viser at tagget er tomt

Det er **normalt**, indtil navnet er skrevet på tagget.

nfc-games starter spil ud fra taggets **UID** (gemt i `tags.conf` på PC’en). En telefon kigger efter **NDEF-tekst** på selve tagget. De tre tags du allerede har, blev kun bundet — der blev ikke skrevet tekst.

Sådan skriver du navnet på et **eksisterende** tag:

1. Åbn menuen.  
2. Vælg **19** (`nfc write`).  
3. Læg tagget på læseren.  
4. Scan med telefonen igen — den skal vise spilnavnet.

Nye tags får navnet skrevet automatisk, når du bruger **6** (`nfc add`).

Hvis skrivning fejler, virker PC’en stadig (UID). Så er tagget typisk ikke NTAG/Ultralight, eller det er låst. Fudan-kloner (UID der starter med `1D`) kan godt være Ultralight — `nfc write` skal kunne skrive dem.

---

## Hvis det ikke virker

**LED er rød**  
Lyt kører ikke — eller læseren er væk og den venter. Tjek at du er logget ind. Genstart med:

```bash
nfc lyt
```

Eller åbn menuen og vælg **1**.

**Ingen reaktion på tag**  
- Steam skal køre.  
- Tagget skal være bundet (menu **8**).  
- Træk læseren ud og sæt den i igen.  
- Firmware (menu **13**) skal ligne `ACR122U216`.

**Skift tag**  
Tag A af og læg B på. Watch skal skifte spil uden at gå via grøn først.

**Har kørt længe, tags reagerer dårligt**  
ACR122U-firmwaren bliver træt af lang polling. Watch slukker og tænder RF-feltet selv (hvert par minutter) — uden USB-reset. USB-reset er kun sidste udvej. Hjælper det ikke: Ctrl+C, åbn AppImage igen, vælg **1**. Træk læseren ud og sæt den i igen, hvis LED bliver ved med at være rød.

**Spillet lukker mens tagget ligger på**  
Det skulle være rettet. Læg tagget fast på læseren; LED skal blive **gul**.

**LED bliver rød når du tager tagget af**  
Den skal blive **grøn**. Hvis den er rød, kører watch ikke.

---

## Hvor tingene ligger

| Hvad | Hvor |
| --- | --- |
| Programmet (AppImage) | `~/Applications/nfc-games-x86_64.AppImage` (`nfc` på PATH) |
| Dine tags | `~/.config/nfc-games/tags.conf` |
| Sprog | `~/.config/nfc-games/nfc.conf` |
| Kilde + test på Gitea | http://192.168.1.3:3002/app/nfc-games |
| Test-kørsler | http://192.168.1.3:3002/app/nfc-games/actions |
| AppImage på Gitea | http://192.168.1.3:3002/app/nfc-games/releases |
| GitHub (offentlig, valgfri) | https://github.com/brunoclausen/nfc-games |

---

## Automatik, test og upload

Detaljer på wiki’en (gem wiki’en, ikke kun denne fil):

- http://192.168.1.3:3002/app/nfc-games/wiki/Automatik  
- http://192.168.1.3:3002/app/nfc-games/wiki/Upload  
- http://192.168.1.3:3002/app/nfc-games/wiki/Full-test  

**Kort:** Log ind, start Steam, grøn LED, læg tag på. Gitea tester og bygger AppImage selv, når koden pushes (PC tændt). GitHub-upload er sat op, men **ikke tændt** før deploy key + token.

Hvis Grok ændrer programmet (ikke hverdag):

```bash
cd ~/nfc-games
./scripts/full-test.sh --hw
./scripts/upload.sh --build
```

---

## Husk

- Kun Linux (Bazzite), kun ACR122U, kun Steam.  
- Ikke Windows, ikke andre læsere.  
- Grøn = klar. Gul = spil kører. Rød = værktøjet kører ikke.
