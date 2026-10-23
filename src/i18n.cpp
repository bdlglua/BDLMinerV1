#include "i18n.h"

#include <string.h>

#include "settings.h"

namespace {

struct Pair {
    const char *en, *fr;
};

const Pair kTexts[] = {
    // panel labels (background fields)
    {"BEST DIFF", "MEILL. DIFF"},
    {"POOL DIFF", "DIFF POOL"},
    {"BLOCKS", "BLOCS"},
    {"32-BIT", "32 BITS"},
    {"TEMP", "TEMP."},
    {"TIME", "HEURE"},
    {"BTC PRICE", "PRIX BTC"},
    {"BLOCK", "BLOC"},
    {"FEE sat/vB", "FRAIS sat/vB"},
    {"BITCOIN PRICE", "PRIX DU BITCOIN"},
    {"NET HASHRATE", "HASHRATE RESEAU"},
    {"MEMPOOL TX", "TX MEMPOOL"},
    {"FEES sat/vB  FAST / 30M / 1H", "FRAIS sat/vB  RAPIDE / 30M / 1H"},
    {"NEXT RETARGET", "AJUSTEMENT"},
    {"BLOCK HEIGHT", "HAUTEUR DE BLOC"},
    {"NETWORK DIFF", "DIFF RESEAU"},
    {"TOTAL HASHES", "HASHES TOTAUX"},
    {"BEST EVER", "RECORD"},
    {"BOARD", "CARTE"},
    {"MINING ENGINES", "MOTEURS DE MINAGE"},
    {"FREE RAM", "RAM LIBRE"},
    {"PAYOUT ADDRESS", "ADRESSE DE PAIEMENT"},
    {"BITCOIN  LOTTERY  MINER", "MINEUR  LOTERIE  BITCOIN"},
    {"A: next screen   B: brightness   hold A 5s: reset", "A: ecran suivant   B: luminosite   A 5 s: reset"},
    // header and values
    {"ONLINE", "EN LIGNE"},
    {"OFFLINE", "HORS LIGNE"},
    {"+1 SHARE", "+1 SHARE"},
    {"waiting for NTP", "attente de l'heure"},
    {"waiting for prices", "attente des prix"},
    {"prices %u min ago", "prix il y a %u min"},
    {"prices just now", "prix a l'instant"},
    {"retarget: %u blk", "ajustement: %u blocs"},
    {"halving in %u blocks", "halving dans %u blocs"},
    {"x hardware", "x materiel"},
    {"x software", "x logiciel"},
    {"%.0f d/block", "%.0f j/bloc"},
    {"%.0f yr/block", "%.0f ans/bloc"},
    {"%.1fe%d yr/block", "%.1fe%d ans/bloc"},
    {"1 in ", "1 sur "},
    {"1 per ", "1 par "},
    {"%.0fd %.0fh", "%.0fj %.0fh"},
    // drawn screens
    {"Clock", "Horloge"},
    {"Network", "Reseau"},
    {"Records", "Records"},
    {"Blocks", "Blocs"},
    {"Shares", "Shares"},
    {"Best", "Meilleure"},
    {"Fee", "Frais"},
    {"Fees", "Frais"},
    {"Net", "Reseau"},
    {"Retarget", "Ajustement"},
    {"Block", "Bloc"},
    {"Net diff", "Diff reseau"},
    {"Backup", "Secours"},
    {"Hashes", "Hashes"},
    {"Board", "Carte"},
    {"Engines", "Moteurs"},
    {"Address", "Adresse"},
    {"Best ever", "Record"},
    {"Block/day", "Bloc/jour"},
    {"Block/year", "Bloc/an"},
    {"Running", "En marche"},
    {" in ", " dans "},
    {"top", "top"},
    {"A:next  B:light  hold A 5s:reset", "A:suivant  B:lumiere  A 5s:reset"},
    {"one point every 10 s", "un point toutes les 10 s"},
    {"min %s  avg %s  max %sH/s  %s", "min %s  moy %s  max %sH/s  %s"},
    {"no block yet", "aucun bloc encore"},
    {"you", "toi"},
    {"mining", "en cours"},
    {"Hold A at boot: setup", "A au demarrage: config"},
    {"  -  hold A: setup", "  -  A: config"},
    {"WiFi setup", "Config WiFi"},
    {"Join WiFi:", "Rejoindre:"},
    {"Pass: ", "Mot de passe: "},
    {"Then open", "Puis ouvrir"},
    {"Connecting...", "Connexion..."},
    {"Warning", "Attention"},
    {"Invalid BTC address?", "Adresse BTC invalide ?"},
    {"Hold A at boot to fix", "A au demarrage pour corriger"},
    {"Reset", "Reinitialisation"},
    // GitHub update window
    {"Automatic update", "Mise a jour automatique"},
    {"in progress...", "en cours..."},
    {"Restarting...", "Redemarrage..."},
    {"Erasing settings...", "Effacement..."},
    {"Rebooting into setup", "Redemarrage en config"},
    {"Update", "Mise a jour"},
    {"Installing", "Installation"},
};

// Pool statuses, matched on their start.
const Pair kStatus[] = {
    {"Mining (backup pool)", "Minage (pool de secours)"},
    {"Mining", "Minage"},
    {"Connecting ", "Connexion "},
    {"Subscribing", "Abonnement"},
    {"Subscribe refused", "Abonnement refuse"},
    {"Auth failed: check address", "Refuse: verifier l'adresse"},
    {"Pool silent, reconnecting", "Pool muette, reconnexion"},
    {"Switching pool", "Changement de pool"},
    {"Pool unreachable", "Pool injoignable"},
    {"Disconnected", "Deconnecte"},
    {"WiFi lost, reconnecting", "WiFi perdu, reconnexion"},
    {"Starting", "Demarrage"},
};

bool french() { return settings.language == 1; }

}  // namespace

const char *tr(const char *en) {
    if (!french() || !en) return en;
    for (const Pair &p : kTexts)
        if (!strcmp(p.en, en)) return p.fr;
    return en;
}

String trStatus(const char *status) {
    if (!french()) return status;
    for (const Pair &p : kStatus) {
        size_t n = strlen(p.en);
        if (!strncmp(status, p.en, n)) return String(p.fr) + (status + n);
    }
    return status;
}

String trDate(const struct tm &t) {
    static const char *kDaysFr[] = {"dim", "lun", "mar", "mer", "jeu", "ven", "sam"};
    static const char *kMonthsFr[] = {"janv", "fevr", "mars", "avr", "mai", "juin",
                                      "juil", "aout", "sept", "oct", "nov", "dec"};
    char buf[32];
    if (french())
        snprintf(buf, sizeof(buf), "%s %d %s %d", kDaysFr[t.tm_wday], t.tm_mday, kMonthsFr[t.tm_mon], t.tm_year + 1900);
    else
        strftime(buf, sizeof(buf), "%a %d %b %Y", &t);
    return buf;
}
