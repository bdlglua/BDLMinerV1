const FR={'Dashboard':'Tableau de bord','Settings':'Réglages','Log':'Journal','Hashrate':'Hashrate','Shares':'Shares',
'rejected':'rejetées','pool diff':'diff pool','Best difficulty':'Meilleure difficulté','best ever':'record','32-bit shares':'Shares 32 bits',
'1 every':'1 toutes les','on average':'en moyenne','Block chance':'Chance de bloc','day':'jour','year':'an','Blocks found':'Blocs trouvés',
'lifetime':'depuis le début','Total hashes':'Hashes totaux','uptime':'en marche depuis','Block height':'Hauteur de bloc','net diff':'diff réseau',
'Lifetime':'Depuis le début','hashes':'hashes','shares':'shares','running':'en marche','Hashrate history':'Historique du hashrate',
'5-min averages, last':'moyennes sur 5 min, depuis','avg':'moy.','range':'de','first point after':'premier point après','min of mining':'min de minage',
'Seen by the pool':'Vu par la pool','best':'meilleure','worker(s)':'worker(s)','min ago':'min','no stats API known for this pool':'pas d\'API de statistiques connue pour cette pool',
'Best shares':'Meilleures shares','BTC price':'Prix du BTC','sats':'sats','Fees sat/vB':'Frais sat/vB','fast / 30 min / 1 h':'rapide / 30 min / 1 h',
'Network hashrate':'Hashrate du réseau','tx in mempool':'tx dans la mempool','Next retarget':'Prochain ajustement','in':'dans','blocks':'blocs','done':'fait',
'Pool (backup)':'Pool (secours)','Pool':'Pool','Payout address':'Adresse de paiement','not a valid Bitcoin address':'adresse Bitcoin invalide',
'Device':'Appareil','night mode':'mode nuit','last reset':'dernier redémarrage','WiFi':'WiFi','Miners':'Miners','All miners':'Tous les miners',
'miners':'miners','no answer':'pas de réponse','online':'en ligne','ONLINE':'EN LIGNE','OFFLINE':'HORS LIGNE','NO CONNECTION':'PAS DE CONNEXION',
'Updated':'Mis à jour','market data':'données marché','min old':'min','mining paused':'minage en pause','power limited':'puissance limitée','battery saver: half speed':'économie de batterie : mi-régime',
'Recent blocks':'Derniers blocs','mining now':'en cours de minage','your best':'ta meilleure','ago':'il y a','Firmware update':'Mise à jour du firmware',
'Update available':'Mise à jour disponible','Install':'Installer','up to date':'à jour',
'Save and restart':'Enregistrer et redémarrer','Backup':'Sauvegarde','Download a backup':'Télécharger une sauvegarde','Restore a backup':'Restaurer une sauvegarde',
'Update':'Mettre à jour','Not a backup file':'Ce n\'est pas un fichier de sauvegarde','Restore this backup? The miner restarts afterwards.':'Restaurer cette sauvegarde ? Le miner redémarre ensuite.',
'Saved, restarting… reload in 15 s':'Enregistré, redémarrage… recharge dans 15 s','Upload failed':'Échec de l\'envoi',
'Refreshes every 5 s. Times are since the last start.':'Actualisé toutes les 5 s. Heures depuis le dernier démarrage.',
'backup_help':'Le fichier de sauvegarde contient tous les réglages (mots de passe compris : garde-le privé) et les statistiques à vie : meilleure difficulté, les 10 meilleures shares, hashes, shares, blocs, durée de fonctionnement. Le restaurer sur un autre miner lui donne aussi ces statistiques.',
'Display':'Affichage','Notifications':'Notifications','Safety':'Sécurité','Network':'Réseau'};
// Settings labels by key.
const FR_SET={pool:'Hôte de la pool',port:'Port de la pool',addr:'Adresse BTC (éventuellement adresse.worker)',pass:'Mot de passe de la pool',
diff:'Difficulté de share suggérée',ua:'User agent (pool.nerdminers.org demande NerdMinerV2)',pool2:'Pool de secours (vide = aucune)',port2:'Port de la pool de secours',
rotateh:'Alterner pool principale / secours toutes les N heures (0 = seulement en cas de panne)',bright:'Luminosité (%)',rot:'Rotation (0-3)',
autorot:'Rotation avec l\'accéléromètre (appareils M5)',lang:'Langue : 0 anglais, 1 français',theme:'Thème (0 orange, 1 vert, 2 bleu, 3 violet, 4 rose)',
cycle:'Écran suivant toutes les N secondes (0 = non)',nightfrom:'Mode nuit à partir de (heure 0-23, 255 = non)',nightto:'Mode nuit jusqu\'à (heure 0-23)',
beep:'Bip à chaque share 32 bits',tz:'Fuseau horaire (POSIX TZ, ex. CET-1CEST,M3.5.0,M10.5.0/3 ou UTC0)',fiat:'Devise du prix (USD, EUR, GBP, CAD, CHF, AUD, JPY)',
ntfy:'Sujet ntfy (vide = non)',ntfysrv:'Serveur ntfy',notify32:'Notifier aussi chaque share 32 bits',power:'Puissance de minage sur USB et batterie (0–100 %, 0 = pause)',maxtemp:'Pause du minage au-dessus de cette température en °C (0 = non)',
rebooth:'Redémarrer après N heures sans share acceptée (0 = non)',saver:'Économie de batterie : écran faible et minage à mi-régime sur batterie',
wifi2:'WiFi de secours 1 : nom',wpass2:'WiFi de secours 1 : mot de passe',wifi3:'WiFi de secours 2 : nom',wpass3:'WiFi de secours 2 : mot de passe',
mqtt:'Serveur MQTT pour Home Assistant (vide = non)',mqttport:'Port MQTT',mqttuser:'Utilisateur MQTT',mqttpass:'Mot de passe MQTT',
peers:'Autres miners du tableau de bord (IP ou noms, séparés par des virgules)',webpass:'Mot de passe web (réglages et mise à jour depuis le tableau de bord)',
update:'Mises à jour depuis GitHub : 0 non, 1 vérifier et proposer, 2 installer automatiquement',
count:'Compteur anonyme d\'utilisation : une fois par jour, télécharge un fichier de 2 octets de la release de cette version (rien n\'est envoyé)'};
function t(s){return LANG=='fr'&&FR[s]||s}

Object.assign(FR,{
 'Connection failed. Please try again.':'Connexion impossible. Réessaie.',
 'Request timed out. Please try again.':'Le délai de réponse est dépassé. Réessaie.',
 'Invalid response from the miner.':'Réponse du miner invalide.',
 'Authentication required. Reload this page and sign in.':'Authentification requise. Recharge la page et reconnecte-toi.',
 'Loading settings…':'Chargement des réglages…','Retry':'Réessayer',
 'Enter a valid value.':'Saisis une valeur valide.',
 'The temperature must be 0 or at least 30 °C.':'La température doit être 0 ou au moins 30 °C.',
 'The hour must be 0–23 or 255 to disable night mode.':'L’heure doit être comprise entre 0 et 23, ou 255 pour désactiver le mode nuit.',
 'The difficulty must be greater than zero.':'La difficulté doit être supérieure à zéro.',
 'Backup restored. The miner is restarting…':'Sauvegarde restaurée. Le miner redémarre…',
 'Select a firmware file first.':'Sélectionne d’abord un fichier de firmware.',
 'Use a firmware .bin file, not a factory image.':'Utilise le fichier firmware .bin, pas l’image factory.',
 'Invalid firmware file.':'Fichier de firmware invalide.',
 'Upload complete. Validating firmware…':'Envoi terminé. Validation du firmware…',
 'Firmware updated. The miner is restarting…':'Firmware mis à jour. Le miner redémarre…',
 'Upload timed out. Please try again.':'Le délai d’envoi est dépassé. Réessaie.',
 'Upload cancelled.':'Envoi annulé.',
 'Installing…':'Installation…','Installation requested.':'Installation demandée.',
 'GitHub updates':'Mises à jour GitHub','Check now':'Vérifier maintenant',
 'Checking…':'Vérification…','Check requested.':'Vérification demandée.',
 'Updates disabled in settings.':'Mises à jour désactivées dans les réglages.',
 'Waiting for the first check.':'En attente de la première vérification.',
 'Latest release checked.':'Dernière release vérifiée.',
 'Could not refresh the log.':'Impossible d’actualiser le journal.'
});
Object.assign(FR,{
 'Unsupported backup version':'Version de sauvegarde non prise en charge',
 'Invalid backup settings':'Réglages de sauvegarde invalides',
 'Missing backup statistics':'Statistiques absentes de la sauvegarde',
 'Invalid or incomplete backup statistics':'Statistiques de sauvegarde invalides ou incomplètes',
 'Invalid text setting':'Valeur de texte invalide',
 'Setting outside its allowed range':'Valeur hors de la plage autorisée',
 'Temperature must be 0 or at least 30 C':'La température doit être 0 ou au moins 30 °C',
 'Night hours must be 0-23 or 255':'Les heures doivent être comprises entre 0 et 23, ou 255',
 'Difficulty must be a finite positive number':'La difficulté doit être un nombre positif valide',
 'Invalid checkbox setting':'Valeur de case à cocher invalide',
 'Pool host and payout address cannot be empty':'L’hôte de la pool et l’adresse de paiement sont obligatoires',
 'No settings to save':'Aucun réglage à enregistrer',
 'No firmware file uploaded':'Aucun fichier de firmware envoyé',
 'Only one firmware file is allowed':'Un seul fichier de firmware est autorisé',
 'Empty firmware file':'Le fichier de firmware est vide',
 'Firmware update or restart pending; try again afterwards':'Une mise à jour ou un redémarrage est en cours. Réessaie ensuite'
});
function errorText(e){return e&&e.message?e.message:t('Connection failed. Please try again.')}
async function request(url,options={},format='json'){
 const {timeout=8000,...init}=options,c=new AbortController();
 const timer=setTimeout(()=>c.abort(),timeout);
 try{
  const r=await fetch(url,{cache:'no-store',...init,signal:c.signal});
  const body=await r.text();
  if(r.status===401)throw Error(t('Authentication required. Reload this page and sign in.'));
  if(!r.ok)throw Error(t(body)||'HTTP '+r.status);
  if(format==='text')return body;
  try{return JSON.parse(body)}catch(e){throw Error(t('Invalid response from the miner.'))}
 }catch(e){
  if(e.name==='AbortError')throw Error(t('Request timed out. Please try again.'));
  if(e instanceof TypeError)throw Error(t('Connection failed. Please try again.'));
  throw e;
 }finally{clearTimeout(timer)}
}

document.addEventListener('DOMContentLoaded',()=>{document.documentElement.lang=LANG;document.querySelectorAll('[data-t]').forEach(e=>e.textContent=t(e.dataset.t))});

Object.assign(FR,{
 'Skip to content':'Aller au contenu','Workspace':'Espace de travail','Served by your miner':'Hébergé par ton miner','Local control. No cloud required.':'Contrôle local, sans cloud.',
 'LOCAL':'LOCAL','MINING OVERVIEW':'VUE DU MINAGE','Your miner, at a glance.':'Ton miner, en un coup d’œil.','Live performance, pool activity and device health.':'Performances en direct, activité de la pool et état de la carte.',
 'Tune performance':'Régler la puissance','Connecting to your miner…':'Connexion à ton miner…','Pool & wallet':'Pool et portefeuille','Your connection to the network':'Ta connexion au réseau','Bitcoin network':'Réseau Bitcoin','Public market data':'Données publiques du marché','Device health':'État de la carte','Temperature, memory and connectivity':'Température, mémoire et connexion',
 'MAKE IT YOURS':'À TON IMAGE','Settings & maintenance':'Réglages et maintenance','Tune your miner. Your settings stay on this device.':'Règle ton miner. Tes paramètres restent sur cette carte.','Find a setting':'Trouver un réglage','Search settings…':'Rechercher un réglage…','No matching settings.':'Aucun réglage correspondant.',
 'Changes take effect after restart.':'Les changements s’appliquent après redémarrage.','Select the firmware .bin for your board. Settings and statistics are kept.':'Choisis le firmware .bin de ta carte. Les réglages et les statistiques sont conservés.','Firmware file':'Fichier de firmware','Restart miner':'Redémarrer le miner','Restart the device while keeping your settings and statistics.':'Redémarre la carte en conservant ses réglages et ses statistiques.','Restart this miner?':'Redémarrer ce miner ?','Restarting…':'Redémarrage…',
 'UNDER THE HOOD':'DIAGNOSTIC','Activity log':'Journal d’activité','Connection events, shares and device diagnostics.':'Connexions, shares et diagnostics de la carte.','Pause':'Pause','Resume':'Reprendre','Download log':'Télécharger le journal','Filter the log':'Filtrer le journal','Search log entries…':'Rechercher dans le journal…','DEVICE OUTPUT':'JOURNAL DE LA CARTE','No matching entries.':'Aucune entrée correspondante.',
 'Full performance':'Pleine puissance','Balanced':'Équilibré','Quiet':'Discret','Configured limit':'Limite configurée','Effective power':'Puissance effective','Temperature':'Température','Memory':'Mémoire','Live (10 s)':'En direct (10 s)','1-minute average':'Moyenne sur 1 minute','Hardware SHA':'SHA matériel','Software SHA':'SHA logiciel','Download via USB':'Télécharger pour USB','This board requires a USB firmware update.':'Cette carte nécessite une mise à jour du firmware par USB.',
 'At 100%, mining runs at full available speed. Temperature protection can still reduce it.':'À 100 %, le minage utilise toute la vitesse disponible. La protection thermique peut encore la réduire.',
 'Saved settings are kept when upgrading. Select Full performance to request 100%.':'Les réglages existants sont conservés à la mise à jour. Choisis Pleine puissance pour demander 100 %.',
 'No usable temperature sensor.':'Aucun capteur de température utilisable.','Temperature protection is limiting mining.':'La protection thermique limite le minage.','Temperature protection has paused mining.':'La protection thermique a mis le minage en pause.','Mining is paused by your power setting.':'Le minage est en pause avec ton réglage de puissance.','Mining is paused during a firmware update.':'Le minage est en pause pendant la mise à jour du firmware.','Mining is waiting for fresh supervision.':'Le minage attend une nouvelle mesure de supervision.','Battery saver is limiting mining.':'L’économie de batterie limite le minage.','Mining is limited by your power setting.':'Ton réglage de puissance limite le minage.','Waiting for a pool connection.':'En attente d’une connexion à la pool.','Public market data is temporarily unavailable. Mining can continue.':'Les données du marché sont temporairement indisponibles. Le minage peut continuer.',
 'Set a web password in the setup portal first (hold the main button while the miner starts).':'Définis d’abord un mot de passe web dans le portail de configuration (maintiens le bouton principal au démarrage).','GitHub updates are disabled or busy':'Les mises à jour GitHub sont désactivées ou occupées'
});
function translatePage(){
 document.documentElement.lang=LANG;
 document.querySelectorAll('[data-t]').forEach(e=>e.textContent=t(e.dataset.t));
 document.querySelectorAll('[data-placeholder]').forEach(e=>e.placeholder=t(e.dataset.placeholder));
 const page=location.pathname==='/settings'?'Settings':location.pathname==='/log'?'Log':'Dashboard';
 document.getElementById('page-title').textContent=t(page);
 document.querySelectorAll('[data-nav]').forEach(a=>{if(a.dataset.nav===location.pathname)a.setAttribute('aria-current','page')});
}
document.addEventListener('DOMContentLoaded',translatePage);
// Page visibility and online events share one scheduler: a slow request can
// never overlap the next refresh, including when a hidden tab is reopened.
function startPolling(task,delay){
 let timer=0,running=false,stopped=false;
 async function run(){
  if(stopped||running)return;
  clearTimeout(timer);
  if(document.hidden){timer=setTimeout(run,delay);return}
  running=true;
  try{await task()}finally{running=false;if(!stopped)timer=setTimeout(run,delay)}
 }
 const wake=()=>{if(!document.hidden)run()};
 document.addEventListener('visibilitychange',wake);window.addEventListener('online',wake);run();
 return {refresh:run,stop(){stopped=true;clearTimeout(timer);document.removeEventListener('visibilitychange',wake);window.removeEventListener('online',wake)}};
}
Object.assign(FR,{'Show':'Voir','Hide':'Masquer','unsaved changes':'modifications non enregistrées','largest block':'plus grand bloc'});
Object.assign(FR,{'A service could not start: not enough memory. Restart the miner.':'Un service n’a pas pu démarrer par manque de mémoire. Redémarre le miner.'});
Object.assign(FR,{'Throttling starts at':'Le ralentissement commence à','Mining pauses at':'Le minage s’arrête à','Thermal protection is disabled.':'La protection thermique est désactivée.'});
