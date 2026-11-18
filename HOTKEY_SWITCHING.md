# Basculement par hotkey, sans touche fantôme

Bascule des sorties **uniquement au clavier** (jamais par le bord de l'écran),
avec **aucune touche transmise au PC**. La méthode retenue est celle, éprouvée,
des KVM matériels (Aten & co) : **double-tap d'une touche morte, puis un
sélecteur**.

---

## 1. Les combinaisons

| Séquence (défaut) | Effet |
|---|---|
| **Scroll Lock, Scroll Lock, F1** | Va **toujours** sur A |
| **Scroll Lock, Scroll Lock, F2** | Va **toujours** sur B |
| **Scroll Lock, Scroll Lock, F3** | Toggle A↔B |

On tape Scroll Lock deux fois, puis, dans la fenêtre, on presse F1/F2/F3.

Configurable dans `src/include/user_config.h` :

```c
#define HOTKEY_LEADER_KEY           HID_KEY_SCROLL_LOCK
#define HOTKEY_SELECT_A             HID_KEY_F1
#define HOTKEY_SELECT_B             HID_KEY_F2
#define HOTKEY_SWITCH_TOGGLE        HID_KEY_F3
#define HOTKEY_LEADER_DOUBLE_TAP    1     // 1 = double-tap (standard KVM), 0 = simple tap
#define HOTKEY_LEADER_TAP_WINDOW_MS 400   // écart max entre les deux taps
#define HOTKEY_LEADER_TIMEOUT_MS    1500  // fenêtre pour presser le sélecteur
```

> **Pourquoi Scroll Lock + double-tap ?** Scroll Lock est la touche la moins
> utilisée sur un clavier PC et c'est une touche *morte* : la retenir ne coûte
> rien, et comme on l'avale son état n'est jamais basculé sur le PC. Le
> **double-tap** évite d'armer sur un appui accidentel — c'est exactement ce que
> font les KVM matériels depuis des décennies.
>
> **Clavier Apple** (pas de Scroll Lock) : mettre `HOTKEY_LEADER_KEY HID_KEY_F13`.
> DeskHop lit le clavier *physique*, pas l'OS.

**Choix du temps :** 1500 ms laisse le temps de presser le sélecteur sans se
presser (le `timeoutlen` de Vim pour les séquences est à 1000 ms ; même ordre de
grandeur). Une touche divergente annule immédiatement, donc cette durée n'impacte
que le cas « j'ai armé puis rien ».

---

## 2. Garantie « zéro octet » et fonctionnement

Machine à états dans `src/keyboard.c` (`handle_output_leader`), évaluée à chaque
rapport clavier :

```
IDLE ──(tap Scroll Lock seul)──► FIRST_TAP ──(2e tap < 400 ms)──► ARMED
                                      │                              │
                                 (timeout / autre touche)      (F1/F2/F3)──► bascule ──► SWALLOW ──(relâche)──► IDLE
                                      ▼                              │
                                    IDLE                      (timeout / autre touche) ──► IDLE
```

- Les taps du leader sont **retenus** (jamais envoyés) → aucune fuite.
- **Sélecteur** dans la fenêtre → bascule ; ni le leader ni le sélecteur ne sont
  transmis ; on avale les touches encore enfoncées jusqu'au relâchement (`SWALLOW`).
- **Touche divergente** (on tape autre chose) → la séquence est abandonnée
  **immédiatement**, la touche part normalement (latence nulle). Scroll Lock étant
  morte, on la **jette** simplement (invisible).
- **Expiration** : évaluée paresseusement au rapport suivant — pas de tâche
  périodique. Si on presse F1 après la fenêtre, il part au PC comme un F1 normal.

Les autres hotkeys (gaming, config, firmware…) passent toujours par la table
`hotkeys[]` classique et ne sont pas concernés.

---

## 3. Alternatives envisagées (avantages / inconvénients)

### A. Laisser fuiter un modificateur « inoffensif »
Combo classique (p. ex. Left Ctrl + touche) ; seule la touche de tête fuite.
- **+** Le plus simple, zéro latence.
- **−** Un modificateur part réellement → pas « zéro octet ».
- **Verdict** : rejeté (objectif = aucun octet).

### B. Accord à modificateurs de droite + F‑touche, sans buffering
Maintenir Right Ctrl + Right Shift puis F1/F2.
- **+** Simple ; la touche F ne part jamais.
- **−** Le premier modificateur appuyé fuite encore.
- **Verdict** : rejeté (fuite résiduelle de modificateur).

### C. Accord « gardé » avec buffering par sous‑ensemble (anciennement retenu)
Retenir tout préfixe d'un accord gardé (Right Ctrl + Right Shift + F…) jusqu'à
résolution.
- **+** Vrai zéro octet ; A/B dédiés + toggle.
- **−** Il faut **maintenir** l'accord (3 touches simultanées) — moins ergonomique.
- **−** Maintenir le préfixe avec la souris subit un court timeout.
- **−** Plus de code (détection de sous‑ensemble, tâche de flush).
- **Verdict** : remplacé par D, plus simple à faire et à utiliser.

### D. Double‑tap d'une touche morte + sélecteur — **CHOISI**
Le standard des KVM matériels (Aten…).
- **+** Vrai zéro octet (leader avalé, sélecteur avalé).
- **+** Éprouvé et familier ; geste simple en deux temps, **sans maintien**.
- **+** Le double‑tap évite l'armement accidentel.
- **+** Touche divergente → annulation immédiate, aucune gêne pour la frappe.
- **−** Deux appuis au lieu d'un (c'est précisément la protection anti‑accident).
- **−** Fenêtre de temps à régler (défaut 1500 ms), mais sans impact sur la frappe.

### Variante simple‑tap (non activée par défaut)
`HOTKEY_LEADER_DOUBLE_TAP 0` : un seul tap de Scroll Lock arme.
- **+** Un geste de moins.
- **−** Plus sensible à un appui accidentel de Scroll Lock (sans conséquence réelle
  car morte, mais pourrait armer inutilement).

---

## 4. Désactivation du basculement par le bord

```c
// src/include/user_config.h
#define SWITCH_HOTKEY_ONLY 1   // 1 = hotkey seul (pas de bord) ; 0 = comportement d'origine
```

`do_screen_switch()` (`src/mouse.c`) sort immédiatement si le drapeau est actif.
Réglage persistant (`config_t.switch_hotkey_only`, flash).

---

## 5. Limites

- **Expiration paresseuse** : un leader armé puis laissé tel quel reste armé
  jusqu'au prochain appui (où l'expiration est vérifiée). Sans conséquence : le
  prochain sélecteur hors fenêtre part simplement au PC.
- **Deux claviers simultanés** : l'état clavier est combiné
  (`combine_kbd_states`) ; une touche retenue sur un clavier pourrait partir via un
  rapport de l'autre. Cas limite (la plupart des montages ont un seul clavier).

---

## 6. Fichiers concernés

| Fichier | Rôle |
|---|---|
| `src/keyboard.c` | Machine à états `handle_output_leader`, intégration dans `process_keyboard_report` |
| `src/include/structs.h` | État du leader dans `device_t` |
| `src/include/user_config.h` | `HOTKEY_LEADER_*`, `HOTKEY_SELECT_A/B`, `SWITCH_HOTKEY_ONLY` |
| `src/handlers.c` | `select_output_a/b_hotkey_handler` |
| `src/mouse.c` | `do_screen_switch()` : sortie anticipée si hotkey‑only |
| `src/defaults.c` | Valeur par défaut de `switch_hotkey_only` |

> Note : la version de config passe à 9 (commune avec la réplication d'identité
> USB) → réglages réinitialisés au premier démarrage après mise à jour.
