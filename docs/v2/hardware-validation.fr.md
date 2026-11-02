# Les quatre validations V2

Le 2 novembre 2026 à 09:48:37 UTC, le propriétaire a fourni une lecture réelle
des deux cartes en **2.0.0-beta.3 confirmée** : M5 à 1,005929 MH/s / 48 °C IMU,
Gamma à 1,069368 TH/s / 49 °C ASIC, connecté en Stratum V2, 525 MHz, 70 shares
acceptées et une stale. Ce sont des valeurs instantanées ; la stale existe déjà
avant les nouveaux essais. Le propriétaire confirme ensuite que les espaces
vides et les icônes des réglages sont corrigés **sur les deux cartes**.

| Point | État actuel |
|---|---|
| 1. Affichage local | Espaces vides et icônes validés par le propriétaire sur M5 et Gamma en bêta 3. |
| 2. Gamma : OTA et stabilité | OTA bêta 4 confirmée et [heure avec trace HTTP](#nouvelle-heure-du-gamma-avec-trace-http) réussie : 3 604 s, 1,070391 TH/s, +119 acceptées, aucun nouveau rejet, 715 lectures sans erreur. Dashboard fermé, hub arrêté selon l'opérateur. Ancienne heure bêta 3 conservée en `review`. |
| 3. Rollback et réseau | Reset USB, rollback, réinstallation/confirmation bêta 4 et contrôle court du M5 validés. Reprise après blacklist validée sur le même boot à 1,0052 MH/s sur 10 s ; premier essai `pool` conservé non validé. |
| 4. S3/S2/C3 et NerdNOS | Recherche de mesures externes terminée et [sourcée](../audits/2026-11-02-board-hashrate-research.md). Aucun de ces matériels disponible chez le propriétaire ; mesures BDLMiner physiques en attente. |

## Heure du Gamma : résultat reçu

Le rapport initial bêta 3 reçu à 10:55:03 UTC contient 713 lectures réussies, le même boot
`6a012c1`, et une fenêtre de 3 604 secondes. Il reste `review` : un timeout HTTP
à +805,063 s a été récupéré, mais la santé pendant cet intervalle est inconnue.
Les températures au début et à la fin sont de 50 °C ASIC ; ce ne sont pas les
maxima de toute l'heure. Le statut régulateur `0x0002` est un avertissement hors
bits de coupure, conservé dans le rapport. La stale présente au départ n'a pas
augmenté. L'OTA et les contrôles bêta 4 décrits ci-dessous ont ensuite eu lieu.

Commande utilisée, conservée pour reproductibilité :

Depuis le kit actuel en bêta 3, dans un terminal séparé :

```bash
python3 tools/hardware_suite.py --config bdlhub.local.json --miner gamma --seconds 3600 --output gamma-stability-beta3.json
```

Garder le Gamma sous tension, sur la même version et connecté pendant ce test.
Ne pas faire son OTA ni couper son réseau avant la fin. Le M5 peut être testé
en parallèle si ses manipulations n'affectent pas le Gamma. Le rapport réel
indique sa version : un résultat bêta 3 ne devient pas un résultat bêta 4.

## Nouvelle heure du Gamma avec trace HTTP

Le propriétaire a fourni `gamma-beta4-hour-trace.json`, enregistré le
2 novembre 2026 à **13:23:53.091316 UTC** : résultat **`passed`** en
`2.0.0-beta.4` confirmée. Les 715 lectures réussies gardent le boot `93c65b31`.
L'uptime passe de 5 038 à 8 642 s, soit **3 604 secondes** ; les compteurs passent
de 5 386 598 000 000 000 à 9 244 286 000 000 000 hashes, donnant
**1,070390677026 TH/s**. Les shares acceptées passent de 182 à 301 (**+119**) et
les rejets restent à un, la stale déjà présente au départ.

Les 715 requêtes HTTP sont toutes des lectures de statistiques : **aucune
exception, aucun timeout ni récupération**, moyenne **47,898 ms**, maximum
**987,599 ms**. Aucune requête n'atteint le seuil lent d'une seconde ; aucun
diagnostic de récupération n'a donc été déclenché. Les températures au début
et à la fin sont de 51 °C ASIC / 57 °C régulateur, à 525 MHz et 100 % de puissance.
Ce ne sont pas des maxima sur toute l'heure. L'avertissement régulateur `0x0002`
reste enregistré hors bits de coupure ; aucun défaut de santé n'a été observé.

Cette fenêtre valide l'heure bêta 4 sous les conditions déclarées : **dashboard
fermé, hub arrêté**. Elle ne détermine pas la cause du timeout initial et ne
valide pas une heure avec dashboard/hub actifs. L'ancien rapport reste `review`.
Le protocole utilisé est conservé ci-dessous pour reproduction.

Pour obtenir une mesure et comprendre une éventuelle récidive,
`tools/trace_hardware.py` reprend exactement le contrôle
compteurs/santé existant : délai HTTP de huit secondes, deux nouvelles tentatives
au maximum et statut `review` dès qu'une erreur de lecture a été récupérée.
Aucun réglage, redémarrage ou firmware n'est envoyé.

Sur le PC du réseau local, arrêter le hub s'il tourne et fermer les onglets du
dashboard Gamma pour cette mesure isolée. Garder le Gamma allumé, connecté,
avec ses réglages actuels ; ne pas faire d'OTA ou de coupure réseau pendant
l'heure. Depuis le kit bêta 4 contenant le nouveau script :

```bash
python3 tools/trace_hardware.py --config bdlhub.local.json --miner gamma --seconds 3600 --dashboard closed --hub stopped --output gamma-beta4-hour-trace.json
cat gamma-beta4-hour-trace.json
```

Le script peut rester silencieux jusqu'à la fin. Il mesure chaque requête sans
ajouter d'appel au fonctionnement normal. Les options `--dashboard` et `--hub`
enregistrent les conditions déclarées par l'opérateur ; elles ne ferment rien.
Les durées séparent l'ouverture/réception des en-têtes (`open_headers`, incluant
DNS/TCP/proxy éventuels) de la lecture du corps (`read_body`). Ce découpage ne
localise pas à lui seul le problème entre PC, Wi-Fi et serveur du mineur.

Après une erreur de transport récupérée seulement, jusqu'à trois fois, il lit
les compteurs de diagnostic et le journal. Il conserve uniquement des nombres
autorisés et les durées des messages `[loop] stalled` ; ni journal brut, ni
en-têtes, ni texte d'exception réseau ne sont enregistrés. Ces deux appels
supplémentaires sont eux aussi chronométrés. Les 32 premières requêtes lentes
(au moins une seconde) ou en erreur sont détaillées ; les suivantes sont comptées.

`passed` (code 0) valide cette nouvelle fenêtre sans erreur observée, sous ces
conditions. `review` (code 2) conserve l'incident et fournit les traces à examiner.
`failed` (code 1) indique un défaut observé ou une lecture non récupérée. Un
nouveau succès ne prouve pas la cause du timeout bêta 3. Le serveur HTTP peut
attendre des en-têtes incomplets dans son parseur synchrone ; c'est une piste
issue du code, sans lien démontré avec l'incident réel. Le contrôle de 122 s et
cette heure bêta 4 sont conservés comme deux observations distinctes.

## Préparer le nouveau kit bêta 4

Télécharger `v2-linux-test-kit` depuis la CI réussie de `v2/development` et
extraire tout le ZIP dans un **nouveau dossier**. Conserver l'ancien kit et les
réglages privés. Le nouveau `kit.json` doit annoncer `2.0.0-beta.4`.

```bash
sha256sum -c SHA256SUMS.txt
```

Les images sont non signées : installation manuelle depuis les réglages de la
carte. Utiliser uniquement `firmware/<carte>-firmware.bin` pour l'OTA. Les fichiers
`*-factory.bin` servent à l'installation/récupération USB.

## Rollback du M5 avant sa confirmation normale

Pour un reset USB automatique, identifier d'abord le port du M5 avec
`ls -l /dev/serial/by-id/`. Ce dossier peut être absent même si un port série
existe. Dans ce cas, avec un câble de données reliant directement M5 et PC :

```bash
python3 -c 'import glob; print("\n".join(sorted(glob.glob("/dev/ttyUSB*")+glob.glob("/dev/ttyACM*"))) or "Aucun port serie detecte")'
lsusb
```

Le premier relevé du propriétaire ne montre aucun de ces ports et aucun pont
USB série identifiable pour le M5. Vérifier d'abord le câble de données et le
port USB du PC ; le diagnostic d'un pilote/permission nécessite un périphérique
USB identifié. Ne pas deviner `/dev/ttyUSB0` ni choisir un périphérique sans lien
avec le M5.

Un relevé ultérieur du propriétaire montre `/dev/ttyACM0`. Le reset de préparation
est maintenant vérifié : `2f867bc` / uptime 6 346 s avant, puis `209bd24` / 38 s
après, toujours en bêta 3 confirmée et reconnecté au pool. Ce reset seul ne valide
pas encore le rollback du bootloader.

Le rollback a ensuite été observé à 11:15:50 UTC : bêta 3 confirmée sur
`185f7662` → bêta 4 en essai sur `da0776f2` → bêta 3 confirmée sur `1f4bc50a`, avec
`reset_requested: true`. La réinstallation normale en bêta 4 reste à confirmer.

La réinstallation a ensuite été confirmée à 11:22:04 UTC : `1f4bc50a` en bêta 3,
puis 83 observations en essai bêta 4 sur `ab2a323b`, enfin confirmation sur ce
même boot. À 175 s, le M5 indique 1,003914 MH/s, 47 °C IMU et un pool connecté.
Le contrôle court des compteurs et la coupure Internet dédiée restent à terminer.

Le contrôle court est maintenant réussi : 25 lectures sur 122 s, à
**1,004855 MH/s**, avec 47 °C IMU aux deux extrémités et aucune erreur de lecture
signalée. Le pool reste compatible malgré les shares rares attendues à difficulté 1.

Le helper Linux `tools/reset_serial.py --port <port-M5>`
utilise seulement DTR/RTS : aucun octet série, firmware ou effacement n'est envoyé.
Vérifier ce reset sur le M5 encore en bêta 3 confirmée, avant tout upload, puis
attendre son retour au pool. Utiliser uniquement le port identifié du M5.
L'observateur de rollback ci-dessous peut alors recevoir
`--reset-command 'python3 tools/reset_serial.py --port <port-M5>'` : il exécutera
ce reset une seule fois, dès que la bêta 4 est réellement observée en essai.
Remplacer `<port-M5>` par le chemin identifié avant de lancer ces commandes.
Sans cette option, le reset décrit plus bas doit être effectué manuellement.

Depuis le nouveau kit, **avant tout upload sur le M5** :

```bash
python3 tools/check_hardware.py ota-monitor http://192.168.1.40 --previous 2.0.0-beta.3 --expected 2.0.0-beta.4 --expect rollback --timeout 900 --output rollback-m5-beta4.json
```

Ouvrir les réglages locaux du M5 et envoyer
`firmware/m5stickc-plus2-firmware.bin`. Attendre que le terminal annonce :
`Pending trial observed for firmware 2.0.0-beta.4` puis `reset now`.

Si aucun `--reset-command` n'est configuré, effectuer alors un **reset physique
ou série matériel documenté** avant les
trois minutes de confirmation automatique. Ne pas utiliser « Redémarrer » dans
la page web : ce bouton confirme le firmware avant de redémarrer. Ne pas effacer
les réglages. Le M5 possède une batterie : débrancher seulement son USB peut ne
pas provoquer de reset. S'il n'y a pas de moyen de reset matériel identifié,
laisser l'essai se confirmer et noter le rollback non effectué ; ne pas improviser
un appui long susceptible d'effacer la configuration.

Une réussite exige : bêta 3 confirmée avant upload, bêta 4 en essai sur un nouveau
boot, puis retour en bêta 3 confirmée sur un troisième boot. L'observateur vérifie
ces identifiants. Une confirmation arrivée avant le reset ne valide pas le rollback.

Après le retour en bêta 3, relancer un observateur en mode confirmation :

```bash
python3 tools/check_hardware.py ota-monitor http://192.168.1.40 --previous 2.0.0-beta.3 --expected 2.0.0-beta.4 --expect confirm --timeout 900 --output ota-m5-beta4.json
```

Envoyer à nouveau le même `m5stickc-plus2-firmware.bin`, puis laisser le M5 miner
sans reset jusqu'à confirmation. Vérifier son état et son débit :

```bash
python3 tools/hardware_suite.py --config bdlhub.local.json --miner m5 --seconds 120 --output m5-beta4-smoke.json
```

## Coupure et reprise réseau du M5 confirmé

Le M5 doit être confirmé et miner avant ce test. Le PC d'observation doit rester
connecté. Pendant l'heure du Gamma, couper uniquement la liaison Wi-Fi du M5
avec une fonction par appareil de la box, si elle existe. Un blocage d'Internet
seul ne coupe pas nécessairement l'API locale et ne convient pas au test `wifi`.

Si cette coupure isolée n'est pas possible, attendre la fin de l'heure du Gamma,
relier le PC par Ethernet, puis tester une courte coupure de l'accès Wi-Fi.
Ne pas débrancher l'alimentation du mineur ni enregistrer un faux SSID/mot de passe.

```bash
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind wifi --timeout 420 --output network-m5-beta4-blacklist.json
```

Attendre `Observer armed`, effectuer la coupure, puis **rétablir dès le message
`Restore the connection now`**. À la fin, répondre `oui` seulement si la liaison
du mineur a effectivement été coupée/rétablie et si le PC est resté connecté.
L'observateur ne modifie aucun réglage et ne peut pas restaurer la box à ta place.

Il exige deux erreurs de transport successives, le même boot et dix secondes de
minage sain avec progression des compteurs après reconnexion. Une panne HTTP
seule ne prouve pas une coupure Wi-Fi : sans confirmation de l'opérateur, le
résultat reste `review` (code 2). Authentification, TLS, données invalides, reboot,
régression des compteurs et défaut matériel ne sont pas des succès réseau.
Un échec ou Ctrl+C impose de vérifier que la connexion a bien été rétablie.

Si la box peut seulement bloquer Internet pour le M5 tout en laissant son API
locale accessible, utiliser plutôt :

```bash
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind pool --timeout 420 --output network-m5-beta4.json
```

Le détecteur de silence du pool est de cinq minutes en V1/V2. Garder le blocage
jusqu'au message demandant la restauration ; `--timeout 420` laisse le temps
d'observer cette déconnexion. Ce test valide une reprise du pool après blocage
Internet, pas une perte de la liaison Wi-Fi.

Une blacklist de box qui rend aussi l'API locale inaccessible nécessite le mode
`wifi`/perte de connectivité locale, avec le PC toujours connecté. Le premier
essai du propriétaire, lancé en mode `pool`, a conservé 189 lectures où le pool
restait connecté, puis trois timeouts HTTP en fin de fenêtre ; il n'est pas validé.
Après retrait de la blacklist, le M5 est revenu en minage sur le même boot.
Le nouvel essai conserve un nom de fichier distinct. **Retirer le M5 de la
blacklist dès le message de restauration, pendant que le script tourne encore**,
pour qu'il puisse mesurer la reprise. Le retirer aussi si le test expire/échoue.

Le nouveau rapport `network-m5-beta4-blacklist.json` est maintenant `passed` :
coupure observée, restauration et manipulation confirmées par l'opérateur, boot
`ab2a323b` inchangé. Les compteurs passent de 1 553 819 000 à 1 563 871 000 entre
les uptimes 1 627 et 1 637 s : **1,0052 MH/s après reprise**. La moyenne affichée
sur 60 ticks inclut encore la pause ; le débit live est revenu à environ 1 MH/s.
La santé pendant la coupure HTTP reste non observée. Ce résultat valide la reprise
après blacklist avec confirmation de l'opérateur ; le premier essai en mode
`pool` reste conservé en échec.

## OTA du Gamma après son heure de stabilité

L'OTA a maintenant été observée : bêta 3 / boot `6a012c1`, puis bêta 4 en essai
sur `93c65b31` (85 observations), puis confirmation sur ce même nouveau boot à
11:02:45 UTC. À 175 s, le Gamma indique 1,073901 TH/s, 51 °C ASIC, six shares
acceptées et aucun rejet. Le contrôle après OTA a ensuite réussi sur 25 lectures,
122 s et le même boot : **1,073214 TH/s**, +3 shares acceptées, aucun nouveau rejet
ni erreur de lecture signalée. L'horodatage du défaut mémorisé est inchangé au
relevé de 11:09:41 UTC. Cela reste un contrôle court en bêta 4.

Le champ persistant `last_fault` signale une erreur I²C horodatée 10:59:37 UTC,
avant le nouveau démarrage observé. L'état courant est en minage, statut
régulateur 0 et tension cœur 1,144531 V. La cause de l'erreur conservée n'est pas
établie ; comparer son horodatage au prochain relevé pour détecter une récidive.

Commande OTA utilisée, conservée pour reproductibilité :

```bash
python3 tools/check_hardware.py ota-monitor http://192.168.1.135 --previous 2.0.0-beta.3 --expected 2.0.0-beta.4 --expect confirm --timeout 900 --output ota-gamma-beta4.json
```

Envoyer uniquement `firmware/bitaxe-gamma-601-firmware.bin` depuis les réglages du
Gamma. Garder alimentation et refroidissement en place, puis laisser l'essai se
confirmer. Après confirmation :

```bash
python3 tools/hardware_suite.py --config bdlhub.local.json --miner gamma --seconds 120 --output gamma-beta4-smoke.json
sh check-miners.sh
```

Le cas `counters_and_health` doit être `passed`. Un test d'heure interrompu n'est
pas validé ; un test terminé après des erreurs de lecture reste `review`. Les
cas OTA/réseau/écran marqués `skipped` par la suite ne deviennent pas des succès.
Les rapports OTA et réseau séparés fournissent leurs propres observations.

## Autres cartes : protocole prêt, exécution en attente

Il faut une vraie carte S3/S2/C3 ordinaire, et une vraie NerdNOS pour son ASIC.
Le Gamma est piloté par un S3 mais calcule avec son BM1370 : ce n'est pas une
mesure du SHA du S3. Utiliser le firmware exact de chaque carte.

Ajouter le matériel réel à `bdlhub.local.json`. Enregistrer modèle, révision de
puce, commit, réglages et journal de démarrage. Sur S2/S3/C3, le journal bêta 4
compare les deux boucles puis indique `resumable SHA transfers: direct` ou
`buffered`. Ce débit court du noyau ne remplace pas un débit soutenu au pool.

Comparer ensuite les compteurs sur des fenêtres de dix minutes avec mêmes
réglages/pool/charge d'affichage, avant et après mise à jour ; répéter en inversant
l'ordre pour limiter les effets de chauffe. Le commit `fa2f61d` fournit le point
de départ antérieur à l'optimisation des transferts. Exemple une fois un mineur
`s3` réellement configuré :

```bash
python3 tools/hardware_suite.py --config bdlhub.local.json --miner s3 --seconds 600 --output s3-measurement.json
```

Aucun chiffre de gain ni 1 MH/s sur ces familles n'est validé à ce stade.

La [recherche publiée](../audits/2026-11-02-board-hashrate-research.md) donne des
repères : environ 0,34 à 0,40 MH/s pour des S3 sous d'autres firmwares, jusqu'à
0,402 MH/s annoncé pour le C3, aucun chiffre S2 de minage assez documenté retenu.
NerdNOS utilise un BM1397 : les références sont en GH/s, avec un essai externe
d'une heure autour de 190 GH/s à 300 MHz. Ces résultats ne valident pas nos
binaires. Les anciens chiffres élevés du S3 dans SparkMiner ont d'ailleurs été
retirés par le projet à cause de shares invalides : le compteur seul ne suffit pas.
