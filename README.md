# NerdMiner V3

Mineur Bitcoin « loterie » (solo) pour **M5StickC Plus2** et les cartes ESP32 compatibles.
Il se connecte à un pool Stratum V1 (par défaut `public-pool.io:21496`), calcule des
SHA‑256d et, avec énormément de chance, peut trouver un bloc entier dont la récompense
est versée à **votre** adresse.

> Ordre de grandeur : un ESP32 fait ~50 à 300 kH/s, le réseau Bitcoin ~10^21 H/s.
> C'est un ticket de loterie et un objet pédagogique, pas une source de revenu.

## Cartes supportées

| Environnement PlatformIO | Cartes | Écran |
|---|---|---|
| `m5stickc-plus2` (défaut) | M5StickC Plus2 | ST7789V2 135×240 |
| `m5stack-esp32` | StickC / StickC Plus, Core Basic/Gray/Fire, Core2, Tough, Atom Lite/Matrix, Stamp Pico… (détection automatique) | selon le modèle |
| `m5stack-s3` | AtomS3 / S3R / S3 Lite, CoreS3 / SE, Cardputer / ADV, StampS3, Dial, DinMeter, StickS3… | selon le modèle |
| `lilygo-t-display` | LilyGO TTGO T‑Display (ESP32) | ST7789 135×240 |
| `lilygo-t-display-s3` | LilyGO T‑Display‑S3 | ST7789 170×320 (parallèle) |
| `esp32-2432s028r` | ESP32‑2432S028R « Cheap Yellow Display » | ILI9341 320×240 |
| `esp32-headless` | toute carte ESP32 (LED GPIO2) | — (série) |
| `esp32s3-headless` | toute carte ESP32‑S3 | — |
| `esp32s2-headless` | toute carte ESP32‑S2 | — |
| `esp32c3-headless` | toute carte ESP32‑C3 | — |

Moteurs de hachage :

* **Logiciel** sur chaque cœur, SHA‑256d optimisé (midstate, 3 premiers tours et
  `W[16..17]` précalculés par job, rejet anticipé au tour 61).
* **Matériel** sur ESP32‑S2/S3/C3 (accélérateur SHA avec reprise d'état). Il est
  vérifié au démarrage sur le bloc genesis ; en cas d'échec le logiciel prend le relais.
  L'ESP32 d'origine (dont le M5StickC Plus2) ne permet pas la reprise d'état : il mine
  en logiciel sur ses deux cœurs.

## Compiler et flasher

Prérequis : [PlatformIO](https://platformio.org/) (`pip install platformio`).

```sh
pio run -e m5stickc-plus2 -t upload -t monitor   # M5StickC Plus2
pio run -e esp32-headless -t upload -t monitor   # n'importe quel ESP32
pio run                                          # compile toutes les cartes
```

Carte headless : ajoutez `-DLED_PIN=<gpio>` dans `build_flags` si votre LED n'est pas sur GPIO2.

## Première configuration

1. Au premier démarrage la carte ouvre un point d'accès Wi‑Fi
   **`NerdMinerV3-XXXX`**, mot de passe **`MineYourCoins`** (sur écran : QR code à scanner).
2. Ouvrez **http://192.168.4.1**, choisissez votre Wi‑Fi et renseignez :
   * **Pool host / port** : `public-pool.io` / `21496` par défaut
     (autres : `solo.ckpool.org` / `3333`, ou votre propre pool) ;
   * **BTC address** : votre adresse de réception, éventuellement suivie de `.nomDuWorker` ;
   * **Pool password** : `x` en général ;
   * **Suggested difficulty** : difficulté de share suggérée au pool ;
   * **Timezone** : fuseau horaire au format POSIX TZ, par défaut l'heure de Paris
     `CET-1CEST,M3.5.0,M10.5.0/3` (`UTC0`, `EST5EDT,M3.2.0,M11.1.0`, `JST-9`…) ;
   * **Price currency** : devise du cours BTC (`USD`, `EUR`, `GBP`, `CAD`, `CHF`, `AUD`, `JPY`).
3. La carte redémarre le minage automatiquement. Les stats sont visibles sur
   https://web.public-pool.io en cherchant votre adresse.

Pour rouvrir le portail : **maintenir le bouton A pendant le démarrage** (2 s).
Pour tout effacer (Wi‑Fi + réglages) : **maintenir A 5 s** pendant le fonctionnement.

## Boutons

| | M5StickC Plus2 | T‑Display / T‑Display‑S3 | CYD / headless |
|---|---|---|---|
| Écran suivant | A (façade) | bouton droit (GPIO35 / GPIO14) | BOOT |
| Luminosité 100→60→30→10→éteint | B (côté) ou Power | bouton BOOT | — |
| Réinitialisation (5 s) | A | bouton droit | BOOT |

Écrans, dans l'ordre :

1. **Mining** : hashrate, shares, meilleure difficulté, difficulté pool, blocs, 32‑bit, température ;
2. **Clock** : heure (NTP), date, cours du BTC, hashrate, hauteur de bloc, frais rapides, shares ;
3. **Global** : cours du BTC et sats par unité de devise, hashrate du réseau, transactions en
   attente, frais (rapide / 30 min / 1 h), prochain ajustement de difficulté, et le temps moyen
   qu'il faudrait à *votre* mineur pour trouver un bloc seul ;
4. **Network** : hauteur de bloc, prochain halving, difficulté réseau, total de hashs, pool, IP/RSSI ;
5. **Info** : carte, firmware, moteurs, RAM libre, adresse.

Les données Clock / Global viennent de l'API publique [mempool.space](https://mempool.space)
(rafraîchies toutes les 5 minutes, en HTTPS) et l'heure de `pool.ntp.org`.

### Fonds d'écran (style NerdMinerV2)

Comme sur NerdMinerV2, chaque écran est une image plein écran où les panneaux et les
étiquettes sont déjà dessinés ; le firmware ne peint que les valeurs, champ par champ
(petit sprite par champ : pas de scintillement, pas de tampon plein écran en RAM).

| Résolution | Cartes |
|---|---|
| 240×135 | M5StickC Plus2 / Plus, Cardputer, T‑Display |
| 320×170 | T‑Display‑S3 |
| 320×240 | M5Stack Core / Core2 / Tough / CoreS3, CYD |

Les autres tailles d'écran (AtomS3, StickC d'origine, orientation portrait…) gardent
l'interface dessinée par le code. Aperçus : `tools/preview/*.png`.

Pour modifier le design, éditez `tools/make_backgrounds.py` (couleurs, panneaux, positions
des champs) puis régénérez — la mise en page des champs est produite en même temps que
les images, ils restent donc synchronisés :

```sh
pip install pillow numpy
python3 tools/make_backgrounds.py
```

## Tableau de bord web

Une fois connectée, la carte sert une page de suivi en lecture seule, utile surtout pour les
cartes sans écran :

* **http://<IP de la carte>/** ou **http://nerdminer-xxxx.local/** (mDNS ; `xxxx` = fin de
  l'adresse MAC, affichée dans le moniteur série) ;
* **/api/stats** : les mêmes données en JSON, pour Home Assistant, un script, etc.

```sh
curl http://nerdminer-xxxx.local/api/stats
```

## Moniteur série (115200 bauds)

```
[miner] hardware SHA: not supported
[miner] miner0 engine=sw core=1
[pool] job 265612f height=972641 clean=1 diff=1
[web] dashboard: http://192.168.1.20/ or http://nerdminer-a1b2.local/
[market] 5/5 ok | 1 BTC = 72543 EUR | fees 3/2/1 | net 1.04e+21 H/s | retarget +3.50% in 1067
[stats] 48.12 kH/s | shares 0 ok / 0 rejected | best 0.0213 (ever 0.0213) | blocks 0 | Mining
```

## À savoir

* `public-pool.io` impose aujourd'hui une difficulté de share minimale de **1**
  (≈ 4,3 milliards de hashs) : avec un ESP32, comptez plusieurs heures entre deux shares
  acceptés. La « meilleure difficulté » affichée progresse, elle, en continu.
* La meilleure difficulté de tous les temps est sauvegardée en flash (au plus une fois par minute).
* ESP32‑C6/H2 : non supportés par la plateforme `espressif32@6.9.0` (Arduino 2.x) utilisée ici.

## Architecture

```
src/
  core/            code portable (testé sur PC)
    sha256.*       SHA-256, midstate, noyau de minage optimisé
    work.*         job Stratum -> en-tête 80 octets, coinbase, merkle, difficultés
    stratum.*      machine à états Stratum V1 (subscribe/authorize/notify/submit)
  hw_sha.*         moteur matériel ESP32-S2/S3/C3 + auto-test
  miner.*          tâches de minage FreeRTOS, file de shares, statistiques
  net.*            tâche réseau : connexion pool, reconnexion, envoi des shares
  market.*         heure NTP + données mempool.space (cours, frais, hashrate, ajustement)
  web.*            tableau de bord HTTP + /api/stats, mDNS
  settings.*       NVS + portail WiFiManager
  board.*          écran / boutons / LED / batterie par carte
  ui.*             écrans (fonds bitmap + champs, ou rendu procédural)
  bg/              fonds d'écran RGB565 générés + layout des champs
tools/
  make_backgrounds.py   générateur des fonds (Pillow)
test/host/         tests PC : ./test/host/run.sh  et  ./test/host/run.sh pool
```

Tests sur PC (vérifie SHA‑256d sur le bloc genesis, l'assemblage d'en‑tête et le protocole) :

```sh
./test/host/run.sh                       # tests unitaires hors-ligne
./test/host/run.sh pool public-pool.io 21496 <adresse> 900   # vrai pool, s'arrête au 1er share
```
