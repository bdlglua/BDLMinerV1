# Les quatre validations V2

Le 2 novembre 2026 à 09:48:37 UTC, le propriétaire a fourni une lecture réelle
des deux cartes en **2.0.0-beta.3 confirmée** : M5 à 1,005929 MH/s / 48 °C IMU,
Gamma à 1,069368 TH/s / 49 °C ASIC, connecté en Stratum V2, 525 MHz, 70 shares
acceptées et une stale. Ce sont des valeurs instantanées ; la stale existe déjà
avant les nouveaux essais. Le propriétaire confirme ensuite que les espaces
vides et les icônes des réglages sont corrigés **sur les deux cartes**.

| Point | État avant les nouveaux essais |
|---|---|
| 1. Affichage local | Espaces vides et icônes validés par le propriétaire sur M5 et Gamma en bêta 3. |
| 2. Gamma : OTA et stabilité | Heure bêta 3 terminée : 3 604 s, 1,071274 TH/s, +141 acceptées, aucun nouveau rejet ; `review` après un timeout récupéré. OTA bêta 4 confirmée et contrôle de 122 s réussi à 1,073214 TH/s. |
| 3. Rollback et réseau | Reset USB, rollback, réinstallation/confirmation bêta 4 et contrôle court du M5 validés. Premier essai réseau `pool` non validé ; nouvel essai avec blacklist en mode `wifi` demandé. |
| 4. S3/S2/C3 et NerdNOS | Aucun de ces matériels de test disponible chez le propriétaire ; mesures physiques en attente. |

## Heure du Gamma : résultat reçu

Le rapport reçu à 10:55:03 UTC contient 713 lectures réussies, le même boot
`6a012c1`, et une fenêtre de 3 604 secondes. Il reste `review` : un timeout HTTP
à +805,063 s a été récupéré, mais la santé pendant cet intervalle est inconnue.
Les températures au début et à la fin sont de 50 °C ASIC ; ce ne sont pas les
maxima de toute l'heure. Le statut régulateur `0x0002` est un avertissement hors
bits de coupure, conservé dans le rapport. La stale présente au départ n'a pas
augmenté. L'heure est terminée : l'OTA du Gamma peut maintenant être observée.

Commande utilisée, conservée pour reproductibilité :

Depuis le kit actuel en bêta 3, dans un terminal séparé :

```bash
python3 tools/hardware_suite.py --config bdlhub.local.json --miner gamma --seconds 3600 --output gamma-stability-beta3.json
```

Garder le Gamma sous tension, sur la même version et connecté pendant ce test.
Ne pas faire son OTA ni couper son réseau avant la fin. Le M5 peut être testé
en parallèle si ses manipulations n'affectent pas le Gamma. Le rapport réel
indique sa version : un résultat bêta 3 ne devient pas un résultat bêta 4.

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
