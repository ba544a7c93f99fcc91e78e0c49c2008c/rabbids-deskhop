# Réplication de l'identité USB (clonage clavier)

Cette mise à jour permet au switch de **reprendre l'identité USB du clavier branché**
et de la présenter **à l'identique sur les deux sorties A et B**, de façon dynamique
et persistante.

> **Périmètre.** On clone l'*identité* (VID, PID, bcdDevice, chaînes
> fabricant/produit/série), **pas** les descripteurs de rapport HID. Voir
> [Limites](#limites).

---

## 1. Objectif

Par défaut, chaque ordinateur voit un périphérique nommé `DeskHop Switch`
(`1209:C000`). Avec cette update, les deux ordinateurs voient le **même** VID/PID
et le **même** nom que le vrai clavier connecté. Les deux sorties restent
strictement identiques (« pas de variation entre A et B »), y compris après un
redémarrage.

Activé par défaut via `CLONE_USB_IDENTITY` (`src/include/user_config.h`).
En mode configuration (disque USB / page web), l'identité d'origine
`2e8a:107c` est **toujours** conservée, car la page de config web filtre dessus.

---

## 2. Vue d'ensemble

```
          ┌─────────────── Carte A (clavier branché) ───────────────┐
clavier ──┤ 1. capture identité (VID/PID/chaînes)                    │
   USB     │ 2. applique localement + sauve en flash                  │
          │ 3. se ré-énumère vers PC A                                │
          │ 4. envoie l'identité à B par UART ───────────┐           │
          └──────────────────────────────────────────────┼───────────┘
                                                          │ UART (liaison série)
          ┌───────────────── Carte B ────────────────────▼───────────┐
          │ 5. réassemble l'identité                                  │
   PC B ◀─┤ 6. applique + sauve en flash                              │
          │ 7. se ré-énumère vers PC B (même identité que A)          │
          └──────────────────────────────────────────────────────────┘
```

---

## 3. Fonctionnement technique

### 3.1 Capture (côté USB hôte, core1)

- À la connexion d'un clavier, `tuh_hid_mount_cb()` (`src/usb.c`) appelle
  `identity_request_capture()`, qui pose simplement un drapeau
  (`state->identity_capture_addr`). **Rien de bloquant** dans le callback.
- La tâche `identity_capture_task()` (`src/identity.c`, cadencée à 20 Hz sur
  core1) effectue ensuite les lectures de descripteurs, qui sont **bloquantes** :
  - `tuh_vid_pid_get()` → VID / PID ;
  - `tuh_descriptor_get_device_sync()` → `bcdDevice` ;
  - `tuh_descriptor_get_manufacturer_string_sync()` / `..._product_..._sync()` /
    `..._serial_..._sync()` → chaînes, converties d'UTF-16LE en ASCII par
    `utf16_desc_to_ascii()`.
- Le résultat remplit une structure `usb_identity_t` (voir §4), marquée
  `magic = USB_IDENTITY_MAGIC` et `valid = 1`.

Ces lectures tiennent en quelques millisecondes sur un clavier normal, bien en
dessous du watchdog (500 ms).

### 3.2 Application locale + persistance

`apply_identity()` :
- ignore les mises à jour identiques (évite une ré-énumération et de l'usure
  flash inutiles) ;
- copie l'identité dans `state->present_identity` (ce qui est annoncé à l'hôte)
  **et** dans `state->config.stored_identity` ;
- appelle `save_config()` → écriture dans le secteur `FLASH_CONFIG` ;
- arme `state->identity_reenum_pending`.

Au démarrage, `identity_init()` (appelée après `load_config()` dans
`src/setup.c`) recharge `stored_identity` dans `present_identity`, donc l'identité
est présentée **dès le boot**, sans variation.

### 3.3 Synchronisation entre les deux cartes (UART)

La structure `usb_identity_t` est **packée** et sert directement de format
sur le fil. Comme un paquet UART ne transporte que 8 octets utiles, l'identité
(~104 octets) est découpée :

- **Émission** — `identity_send_task()` (core1, 500 Hz) envoie un
  `IDENTITY_SYNC_MSG` par appel. Format des 8 octets de données :
  `[0] = offset`, `[1] = longueur (≤ 6)`, `[2..7] = payload`.
- **Réception** — `handle_identity_sync_msg()` (`src/identity.c`, appelée par le
  répartiteur UART de `src/uart.c`) réassemble dans `state->identity_rx_buf` :
  - bornes strictes (`offset`, `len`, `offset + len ≤ sizeof(usb_identity_t)`) ;
  - chunks attendus dans l'ordre ; un offset 0 réinitialise le réassemblage ;
  - une fois le buffer complet : validation `magic` + `valid`, forçage des
    terminateurs NUL des chaînes, puis `apply_identity()`.

La transmission n'est déclenchée que par une **capture locale**, jamais par une
réception → pas de boucle d'écho entre les deux cartes.

### 3.4 Ré-énumération (côté USB device, core0)

Le PC lit le descripteur *device* au branchement du **Pico**, pas du clavier.
Pour qu'il relise la nouvelle identité, la carte se déconnecte puis se reconnecte
sur son port USB :

- `identity_reenumerate_task()` (`src/main.c`, core0, 60 Hz) est une petite
  machine à états non bloquante : `tud_disconnect()` → attente ~120 ms
  (horloge µs, sans blocage) → `tud_connect()`.
- La ré-énumération est faite sur **chaque** carte (A après sa capture, B après
  réception), donc les deux PC relisent la même identité.

### 3.5 Présentation des descripteurs

Dans `src/usb_descriptors.c` :

- `tud_descriptor_device_cb()` : si une identité clonée est active
  (`identity_to_present()` ≠ NULL), un `tusb_desc_device_t` est rempli à la volée
  avec `idVendor` / `idProduct` / `bcdDevice`. Si le clavier n'a **pas** de numéro
  de série, `iSerialNumber` passe à `0` (aucune série annoncée) au lieu de l'ID
  unique du Pico — sinon A et B afficheraient des séries différentes.
- `tud_descriptor_string_cb()` : les chaînes fabricant/produit/série proviennent
  de l'identité clonée quand elle est active.
- `identity_to_present()` renvoie toujours `NULL` en mode config → identité
  d'origine préservée.

---

## 4. Structure de données

```c
/* src/include/structs.h — packée : sert aussi de format sur le fil UART */
#define USB_IDENTITY_MAGIC   0xD5
#define USB_IDENTITY_STR_LEN 32   /* NUL compris → 31 caractères utiles */

typedef struct __attribute__((packed)) {
    uint8_t  magic;   // USB_IDENTITY_MAGIC quand l'enregistrement est valide
    uint8_t  valid;   // 1 une fois une identité capturée
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd;
    char manufacturer[USB_IDENTITY_STR_LEN];
    char product[USB_IDENTITY_STR_LEN];
    char serial[USB_IDENTITY_STR_LEN];
} usb_identity_t;
```

Elle est stockée dans `config_t.stored_identity` (persistance flash) et dans
`device_t.present_identity` (état courant annoncé). Un `_Static_assert` dans
`src/defaults.c` garantit que `config_t` tient dans une page flash (256 octets).

La version de config passe de **8 à 9** : les réglages sauvegardés sont donc
réinitialisés aux valeurs par défaut au premier démarrage après mise à jour.

---

## 5. Configuration

### Clonage dynamique (défaut)

```c
// src/include/user_config.h
#define CLONE_USB_IDENTITY 1   // 0 = désactivé, garde l'identité statique ci-dessous
```

### Identité statique par carte (compilation, repli)

Alternative (ou valeur de repli avant toute capture) : fixer une identité par
carte à la compilation via les variables CMake `DH_USB_A_*` / `DH_USB_B_*`.
Voir [`misc/usb_identity/README.md`](misc/usb_identity/README.md) et le script
`misc/usb_identity/clone_usb_identity.py` (génère un profil à partir d'un
périphérique réel lu depuis `/sys` sous Linux).

---

## 6. Limites

- **Un port USB = un seul VID/PID.** Les sorties reprennent l'identité du
  **clavier**. La souris fonctionne mais son identité propre n'est pas répliquée.
- **Les descripteurs de rapport HID restent ceux de DeskHop** (clavier standard,
  souris *absolue*, consumer, system). DeskHop interprète puis re-normalise les
  rapports, et la carte B ne reçoit par l'UART que des rapports normalisés : les
  deux sorties doivent donc partager un format HID fixe. Un logiciel constructeur
  qui lit une interface HID vendor spécifique (RGB, macros) ne la retrouvera pas,
  même avec le bon VID/PID.
- **Course inter-cœurs** : `present_identity` est écrite par core1 et lue par
  core0 pendant l'énumération. L'écriture précède l'armement de la
  ré-énumération, donc la valeur est stable au moment où l'hôte relit les
  descripteurs ; aucun verrou n'est ajouté (cohérent avec le reste du firmware).

---

## 7. Fichiers concernés

| Fichier | Rôle |
|---|---|
| `src/identity.c` / `src/include/identity.h` | Capture, synchro UART, ré-énumération, persistance |
| `src/usb_descriptors.c` / `.h` | Descripteurs device + chaînes construits depuis l'identité |
| `src/include/structs.h` | Type `usb_identity_t`, champs config + état |
| `src/usb.c` | Déclenche la capture dans `tuh_hid_mount_cb()` |
| `src/uart.c` | Enregistre le handler `IDENTITY_SYNC_MSG` |
| `src/include/protocol.h` | Ajoute le type `IDENTITY_SYNC_MSG` |
| `src/main.c` | Ajoute les tâches (ré-énumération core0, capture/envoi core1) |
| `src/setup.c` | Appelle `identity_init()` après `load_config()` |
| `src/defaults.c` | Valeurs par défaut + `_Static_assert` sur `config_t` |
| `src/include/config.h` | `CURRENT_CONFIG_VERSION` 8 → 9 |
| `src/include/user_config.h` | Option `CLONE_USB_IDENTITY` |
| `misc/usb_identity/` | Identité statique à la compilation + outil de clonage |

---

## 8. Compilation

```bash
cmake -S . -B build && cmake --build build
# puis flasher build/deskhop.uf2 (lecteur en mode config, ou bouton BOOTSEL)
```
