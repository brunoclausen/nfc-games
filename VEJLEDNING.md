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
4. **Watch** starter selv ved login. Grøn LED = klar.  
5. Læg et bundet tag på læseren. Tag det af for at stoppe.

Du skal **ikke** åbne koden. Du skal **ikke** bygge noget.

---

## Dine tags lige nu

| Tag (UID) | Spil |
| --- | --- |
| `1D2B7022960000` | Silent Hill 4: The Room |
| `1DB45322960000` | Agony UNRATED |
| `1D505E22960000` | Resident Evil Code Veronica X (USA) Disc 1 |

Listen ligger i `~/.config/nfc-games/tags.conf`. Den følger **denne PC**, ikke GitHub.

---

## Åbn menuen

Dobbeltklik **NFC Games** i app-menuen, eller kør:

```bash
~/Applications/nfc-games-x86_64.AppImage
```

Skriv et **nummer** og Enter.

| Nr. | Hvad |
| --- | --- |
| 1 | Watch (lyt efter tags) |
| 2 | Vis Steam-spil |
| 6 | Bind nyt tag til et spil |
| 7 | Læs tag |
| 8 | Vis gemte tags |
| 9 | Fjern tag |
| 13 | Vis læser-firmware (skal ligne ACR122U216) |
| 15 | Sprog |
| 0 | Afslut |

---

## Bind et nyt tag (nyt spil)

Spillet skal **allerede** ligge i Steam (eller som non-Steam-genvej).

1. Åbn menuen.  
2. Vælg **6**.  
3. Skriv spillets **navn** (eller Steam-appid hvis flere hedder det samme).  
4. Læg det tomme tag på læseren, når den beder om det.  
5. Tagget er nu bundet.

Bind **ikke** tags til `boot-windows` eller Proton/runtime-værktøjer.

---

## Hvis det ikke virker

**LED er rød**  
Watch kører ikke. Tjek at du er logget ind. Eller åbn menuen og vælg **1**.

**Ingen reaktion på tag**  
- Steam skal køre.  
- Tagget skal være bundet (menu **8**).  
- Træk læseren ud og sæt den i igen.  
- Firmware (menu **13**) skal ligne `ACR122U216`.

**Spillet lukker mens tagget ligger på**  
Det skulle være rettet. Læg tagget fast på læseren; LED skal blive **gul**.

**LED bliver rød når du tager tagget af**  
Den skal blive **grøn**. Hvis den er rød, kører watch ikke.

---

## Hvor tingene ligger

| Hvad | Hvor |
| --- | --- |
| Programmet (AppImage) | `~/Applications/nfc-games-x86_64.AppImage` |
| Dine tags | `~/.config/nfc-games/tags.conf` |
| Sprog | `~/.config/nfc-games/nfc.conf` |
| Kilde + test på Gitea | http://192.168.1.3:3002/app/nfc-games |
| Test-kørsler | http://192.168.1.3:3002/app/nfc-games/actions |
| AppImage på Gitea | http://192.168.1.3:3002/app/nfc-games/releases |
| GitHub (offentlig, valgfri) | https://github.com/brunoclausen/nfc-games |

---

## Hvis du (eller Grok) ændrer programmet

Du behøver det **ikke** til hverdag.

```bash
cd ~/nfc-games
./scripts/full-test.sh --hw
./scripts/upload.sh --build
```

Gitea tester og bygger AppImage automatisk, når koden pushes, **hvis denne PC er tændt**.

GitHub opdateres først automatisk, når deploy key + lille token er sat (se README). Indtil da er Gitea sandheden.

---

## Husk

- Kun Linux (Bazzite), kun ACR122U, kun Steam.  
- Ikke Windows, ikke andre læsere.  
- Grøn = klar. Gul = spil kører. Rød = værktøjet kører ikke.
