# Consulter ses mineurs à distance

Ouvre **[Mes mineurs](https://bdlminer-stats.bdlminer-bot.workers.dev/remote)** depuis
ton téléphone, même hors du Wi-Fi de la maison. Les cartes envoient directement
leurs mesures en HTTPS vérifié. Aucun PC ou Raspberry Pi à laisser allumé,
aucun port de la box à ouvrir. Chaque carte doit utiliser **BDLMiner 2.3.0 ou
ultérieur** et avoir accès à Internet.

## Configuration

1. Mets chaque mineur à jour depuis ses réglages locaux ou avec le firmware
   adapté de la [dernière version stable](https://github.com/bdlglua/BDLMinerV2/releases/latest).
2. Sur **Mes mineurs**, clique sur **Créer ma clé**, puis **Copier la clé**.
   Conserve-la dans ton gestionnaire de mots de passe : elle donne accès aux
   mesures privées et ne peut pas être récupérée en cas de perte.
3. Sur chaque mineur, ouvre **Réglages → Réseau → Suivi privé à distance**.
   Colle la même clé, donne-lui un nom, puis active le suivi.
   Clique sur **Enregistrer et redémarrer**.
4. Sur **Mes mineurs**, saisis cette clé puis clique sur **Connexion**.
   Le premier envoi est prévu environ une minute après le démarrage, puis toutes
   les cinq minutes. Le Wi-Fi, la synchronisation de l'heure et la mémoire
   disponible pour HTTPS peuvent retarder un envoi.

Le suivi privé est **désactivé par défaut**. Il est indépendant des statistiques
anonymes : tu peux activer l'un et désactiver l'autre.

## Les mesures

Tu retrouves le hashrate, les températures disponibles, les shares acceptées et
rejetées, l'uptime, la meilleure difficulté, la puissance effective, la version,
la cryptomonnaie et l'état de connexion à la pool. L'historique couvre **sept
jours**, avec des vues de 1 h, 6 h, 24 h et 7 j. Les interruptions et les
redémarrages laissent des coupures sur le graphique.

« En ligne » signifie que le dernier envoi date de moins de **11 minutes** ; son
âge est indiqué. Une carte hors ligne conserve sa dernière mesure sur sa carte,
mais ne contribue plus au total en ligne. La page consulte uniquement les
données : les réglages, mises à jour, redémarrages et calibrages restent locaux.

## Confidentialité et arrêt

La clé aléatoire de 256 bits reste en mémoire dans le navigateur pendant la
consultation. Elle n'est placée ni dans les liens ni dans son stockage permanent.
**Déconnexion** efface la clé et les données de la page. **Copier ma clé** permet
d'en conserver une copie pendant la connexion. La clé est masquée dans les
réglages du mineur ; une sauvegarde protégée peut la contenir.

Les mesures privées contiennent le nom choisi et un identifiant pseudonyme
calculé sur la carte. Aucune adresse de paiement, worker, adresse MAC brute,
adresse IP locale, clé Wi-Fi, mot de passe web ou adresse de pool privée n'est
envoyée. Les pools publiques reconnues sont représentées par un groupe fixe.
Les données privées ne rejoignent pas les statistiques publiques.

Le service Cloudflare sépare les espaces par clé, dont seule l'empreinte sert à
nommer le stockage. L'opérateur d'hébergement peut accéder aux mesures stockées ;
elles ne sont pas chiffrées de bout en bout. Les adresses des requêtes servent
seulement aux limites temporaires en mémoire. Les détails techniques et l'API
sont décrits dans le [guide complet](remote-access.md).

Pour arrêter les envois, désactive le suivi sur chaque carte : le bouton
s'applique immédiatement. Les anciennes mesures expirent après sept jours.
Pour les supprimer tout de suite, ouvre **Gérer mes données privées → Effacer
les données et révoquer la clé**. L'ancienne clé est alors définitivement bloquée
grâce à un marqueur minimal de révocation. Pour reprendre, crée une nouvelle clé
et remplace-la sur toutes les cartes. Jusqu'à **32 mineurs** peuvent partager un
espace privé.
