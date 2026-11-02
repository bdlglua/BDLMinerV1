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
| 2. Gamma : OTA et stabilité | Bêta 3 actuellement confirmée ; transition OTA complète non observée. Nouvel essai d'une heure à exécuter/terminer. |
| 3. Rollback et réseau | Procédure ci-dessous prête ; aucun résultat physique encore fourni. |
| 4. S3/S2/C3 et NerdNOS | Aucun de ces matériels de test disponible chez le propriétaire ; mesures physiques en attente. |

## Commencer l'heure du Gamma en parallèle

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
`ls -l /dev/serial/by-id/`. Le helper Linux `tools/reset_serial.py --port <port-M5>`
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
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind wifi --output network-m5-beta4.json
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

## OTA du Gamma après son heure de stabilité

Depuis le nouveau kit, lorsque le test d'une heure est terminé :

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
