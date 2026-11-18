# Injection clavier/souris via le port COM

Permet de piloter **l'autre PC** en envoyant des séquences clavier/souris
prédéfinies sur un **port COM (série/CDC)** exposé par la carte contrôleur. Le
vocabulaire des *steps* est aligné sur le projet `remote-hid`, pour un même
modèle mental d'un projet à l'autre.

```
  PC A ──USB── Carte A (DH_INJECT)            Carte B (build normal)  ──USB── PC B
   │           expose un port COM                HID uniquement                │
   └─ tu envoies des commandes ──► parse ──► UART ──► handlers HID ──► injecté ┘
```

- La carte **contrôleur** (p. ex. A) est buildée avec `DH_INJECT` → elle expose
  un port COM en plus du HID. Tu y envoies des commandes.
- La carte **cible** (p. ex. B) est buildée normalement → **HID uniquement, pas
  de COM**. Elle n'a besoin d'**aucune** modification : ses handlers
  `handle_keyboard_uart_msg` / `handle_mouse_abs_uart_msg` injectent déjà tout
  rapport reçu par UART, indépendamment de la sortie active.

> ⚠️ **Sécurité.** Injecter des frappes arbitraires dans un PC depuis l'autre
> carte est une capacité puissante (c'est la surface durcie par le commit
> « harden UART protocol against remote firmware triggers »). Elle est donc
> **opt-in à la compilation** (`DH_INJECT`), jamais activée par défaut : une
> carte buildée normalement n'expose aucun port COM et ne parse aucune commande.

---

## 1. Build

```bash
# Carte contrôleur (expose le COM + injection)
cmake -S . -B build-ctrl -DDH_INJECT=ON && cmake --build build-ctrl

# Carte cible (HID uniquement)
cmake -S . -B build && cmake --build build
```

Flashe `build-ctrl/deskhop.uf2` sur le contrôleur, `build/deskhop.uf2` sur la
cible. (Note : les deux images diffèrent — ne compte pas sur le cross-flash
A↔B pour celle-ci.)

## 2. Utilisation

Ouvre le port COM du contrôleur (p. ex. `/dev/ttyACM0`, `COM3`) et envoie **une
commande par ligne**. Une séquence = plusieurs lignes exécutées dans l'ordre.
Chaque ligne reçoit `ok` ou `err`.

```bash
printf 'target remote\n type hello\n key enter\n' > /dev/ttyACM0
```

## 3. Vocabulaire des commandes (steps)

Le vocabulaire et les **options combinables** sont calqués sur les outils de
`remote-hid` (`keyboard_key`, `keyboard_shortcut`, `mouse_click`, `mouse_drag`,
`mouse_down`/`mouse_up`…), adaptés à une syntaxe en ligne.

| Commande | remote-hid | Effet |
|---|---|---|
| `type <texte>` | `keyboard_type` | Tape le texte littéral (ASCII, layout US) |
| `key <chord>` | `keyboard_key` | Tape **un** accord : presse puis relâche. Ex. `key enter`, `key ctrl+alt+del` |
| `shortcut <chord> [chord…]` | `keyboard_shortcut` | Tape **une suite** d'accords dans l'ordre. Ex. `shortcut ctrl+k ctrl+s` |
| `key_down <chord>` (`down`) | `key_down` | **Presse et maintient** (modificateur seul autorisé : `down ctrl`) |
| `key_up <chord>` (`up`) | `key_up` | Relâche l'accord indiqué |
| `release_all` (`release`) | `release_all` | Relâche toutes les touches **et boutons souris** |
| `move <dx> <dy>` | `mouse_move` | Déplacement souris **relatif** (déplace un bouton maintenu → glisser) |
| `moveto <x> <y>` | — | Déplacement souris **absolu** (0..32767) |
| `click [button] [count] [mods]` | `mouse_click` | Clic ; `count` = répétitions (1..9), `mods` tenus pendant le clic. Ex. `click left 2`, `click right ctrl+shift` |
| `mouse_down [button]` (`mdown`) | `mouse_down` | **Presse et maintient** un bouton souris |
| `mouse_up [button]` (`mup`) | `mouse_up` | Relâche un bouton souris maintenu |
| `drag <dx> <dy> [button] [mods]` | `mouse_drag` | Presse le bouton, déplace en **relatif**, relâche. Ex. `drag 120 0 left` |
| `scroll <n> [mods]` | `mouse_scroll` | Molette (n signé) ; `mods` tenus (p. ex. `scroll -3 ctrl` pour zoomer) |
| `delay <ms>` (`sleep`) | wait step | Attend n millisecondes |
| `target <local\|remote>` | — | Où injecter : `local` = PC du contrôleur, `remote` = l'autre PC (défaut **remote**) |
| `# ...` | — | Commentaire (ignoré) |

**Boutons** : `left` (défaut), `right`, `middle`.
**Modificateurs** : `ctrl`/`primary`, `shift`, `alt`, `gui`/`win`/`cmd`/`super`,
combinables par `+` (`ctrl+shift`). `primary` = Ctrl ici (cibles PC ;
contrairement à `remote-hid` il n'y a pas de détection d'OS côté carte).

Les tokens de `click`/`drag` (bouton, count, modificateurs) sont acceptés **dans
n'importe quel ordre**. Les maintiens (`key_down`/`key_up`, `mouse_down`/`mouse_up`)
persistent **à travers** les steps suivants — p. ex. `mouse_down` puis plusieurs
`move` compose un glisser manuel — d'où l'importance de terminer par `release_all`.

### Exemple de séquence

```
target remote
shortcut ctrl+k ctrl+s      # raccourci multi-accords (style VS Code)
key_down ctrl
click left 2                # double-clic ctrl maintenu
key_up ctrl
type echo controlled from A
key enter
delay 500
moveto 16000 12000
drag 120 0 left             # glisser 120px vers la droite, bouton gauche
scroll -3 ctrl              # zoom arrière
release_all
```

## 3b. Plan de contrôle (`/commandes`)

Faute de serveur MCP (le Pico est un périphérique USB, pas un hôte), le port COM
est **auto-descriptif** : les commandes préfixées par `/` n'injectent **jamais**
rien et répondent toujours en local (même file pleine).

| Commande | Effet |
|---|---|
| `/help` (`/?`) | Référence lisible des commandes (le « contrat d'usage ») |
| `/contract` | Grammaire compacte **parsable** : une ligne `contract <N>` puis N lignes, une par verbe |
| `/status` | Instantané : `target`, `queued` (steps en file), `held_mod`/`held_keys`/`held_btn` (état tenu), `dropped` (lignes refusées cumulées) |
| `/version` | `DeskHop-inject <n>` (détection de capacités côté script) |

> **`ok` = mis en file, pas « exécuté ».** Une séquence longue se joue à
> ~250 rapports/s. Pour savoir quand c'est terminé, *poll* `/status` jusqu'à
> `queued=0`. `dropped` qui augmente = file saturée (ralentir) ou ligne trop longue.

| `/log on` / `/log off` | (Dé)abonne le **flux d'événements** (ci-dessous). Off par défaut |

```
/status
status target=remote queued=12 held_mod=0x01 held_keys=0 held_btn=0x01 dropped=0 \
       log=on board=a output=a pc_a=up pc_b=up kbd=up mouse=up link=up
```

Champs de présence de `/status` :
- `board` : rôle de la carte contrôleur (a/b) ; `output` : sortie active (clavier/souris physiques).
- `pc_a` / `pc_b` : `up`/`down` = PC présent sur ce port ; `?` = port de l'**autre** carte alors que le lien est coupé (inconnu).
- `kbd` / `mouse` : un clavier/une souris est branché **en entrée** (sur l'une ou l'autre carte).
- `link` : heartbeat de l'autre carte frais (`up`) ou non (`down`).

### Flux d'événements (`/log on`)

Lignes `evt ...` **asynchrones** (préfixe `evt`, à dispatcher à part de `ok`/`err`).
`/log on` renvoie d'abord un instantané complet, puis une ligne à chaque changement :

```
evt switch output=a        # le clavier/souris physique bascule vers A (ou B)
evt pc port=b state=up     # un PC vient d'apparaître / disparaître sur un port
evt kbd state=down         # clavier débranché de l'entrée
evt mouse state=up
evt link state=down        # on a perdu le heartbeat de l'autre carte
```

> **Présence des deux ports.** Chaque carte connaît directement **son** côté (son
> PC + le clavier/souris branchés sur elle) et apprend l'autre côté via le
> **heartbeat (1 Hz)** étendu. Donc : détection d'un (dé)branchage en ~1 s ;
> quand `link=down`, le port d'en face est `?` (inconnu). Les deux cartes doivent
> tourner cette version du firmware (sinon le port d'en face reste vu `down`).

## 4. Fonctionnement technique

- **Parser pur** (`src/inject.c`, testé) : transforme une ligne en `inject_cmd_t`
  (tables ASCII→HID et noms de touches incluses). Aucune dépendance matérielle.
- **Exécuteur** (`src/inject_exec.c`, compilé seulement sous `DH_INJECT`) :
  - `tud_cdc_rx_cb` accumule les octets du COM en lignes ;
  - chaque ligne est parsée puis **étendue** en *steps* bas niveau poussés dans
    une file (anneau de 256) ; l'état des touches tenues est suivi pour émettre
    des rapports HID **absolus** corrects (`key_down`/`key_up`/`release_all`) ;
  - `inject_task` (core0) émet un step par tic, en respectant un espacement
    inter-step (~4 ms) et les steps `delay`.
- **Cible** : `local` → file HID locale (`queue_kbd_report`/`queue_mouse_report`) ;
  `remote` → `KEYBOARD_REPORT_MSG` / `MOUSE_REPORT_MSG` sur l'UART, injectés par
  la carte d'en face. L'injection ignore la sortie active (on vise un PC précis).

## 5. Limites

- **Layout US** pour `type` (les caractères sans touche US sont ignorés).
  Comme `remote-hid`, un vrai support multi-layout serait une évolution.
- Les **maintiens** non relâchés restent actifs : terminer par `release_all`.
- **Pas d'humanisation** : DeskHop est un injecteur transparent, donc les options
  `remote-hid` de rythme/courbe (`precise`, `humanize`, `speed`, `mode`, `typos`)
  n'ont pas d'équivalent — tout est émis « précis » à cadence fixe (~250 rapports/s).
- Pas de `mouse_path` (liste de points lissée) : approximable par des `move`
  successifs. Évolution possible, alignée sur `remote-hid`.
- L'injection partage l'état clavier/souris avec la frappe réelle : éviter
  d'injecter pendant que le vrai clavier/souris contrôle la même cible.
- Les rapports émis sont **absolus** (état complet). Conséquence : `type`/`key`
  d'une touche déjà **maintenue** par un `key_down` ne génère pas de nouvelle
  frappe (l'état ne change pas). Relâcher d'abord (`key_up`/`release_all`).
- Une ligne trop longue, ou une ligne reçue alors que la file est saturée, est
  **refusée** (`err`) plutôt que tronquée (aucune touche ne reste coincée) ; la
  retaper une fois la file vidée. Longueur max d'un `type` : 47 caractères.
- Coordonnées validées : `move`/`drag` ±32767, `moveto` 0..32767, `scroll`
  ±127 (hors plage → `err`, jamais de débordement/inversion).

## 6. Fichiers

| Fichier | Rôle |
|---|---|
| `src/inject.c` / `src/include/inject.h` | Parser pur + tables ASCII/HID |
| `src/inject_exec.c` | CDC RX, file de steps, état tenu, émission locale/UART (sous `DH_INJECT`) |
| `src/usb_descriptors.c`, `src/include/tusb_config.h` | CDC activé via `DH_HAS_CDC` (= `DH_DEBUG` ou `DH_INJECT`) |
| `src/main.c`, `src/include/tasks.h` | Tâche `inject_task` (core0, sous `DH_INJECT`) |
| `CMakeLists.txt` | Option `DH_INJECT` |
| `tests/test_inject.c` | Tests du parser (hôte, `cd tests && make`) |
