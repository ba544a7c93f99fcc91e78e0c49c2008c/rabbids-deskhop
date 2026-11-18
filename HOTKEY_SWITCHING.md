# Basculement par hotkey uniquement, sans touche fantôme

Cette mise à jour fait basculer les sorties **uniquement au clavier** (jamais par
le bord de l'écran) et **garantit qu'aucune partie de la combinaison n'est
transmise au PC** (ni la touche, ni le modificateur). Pensé pour deux ordinateurs
indépendants, chacun avec son écran.

---

## 1. Les combinaisons

| Combinaison (défaut) | Effet | Switch lock |
|---|---|---|
| **Scroll Lock + F3** | Toggle A↔B (boucle) | respecté |
| **Scroll Lock + F1** | Va **toujours** sur A (garanti) | ignoré (forcé) |
| **Scroll Lock + F2** | Va **toujours** sur B (garanti) | ignoré (forcé) |

Configurable dans `src/include/user_config.h` :

```c
#define HOTKEY_SWITCH_MODIFIER 0                   // modificateur optionnel (0 = aucun)
#define HOTKEY_SWITCH_ARM      HID_KEY_SCROLL_LOCK // touche d'armement maintenue
#define HOTKEY_SELECT_A        HID_KEY_F1          // toujours A
#define HOTKEY_SELECT_B        HID_KEY_F2          // toujours B
#define HOTKEY_SWITCH_TOGGLE   HID_KEY_F3          // toggle A<->B
#define HOTKEY_BUFFER_TIMEOUT_MS 60
```

> **Pourquoi Scroll Lock ?** C'est statistiquement la touche la moins utilisée sur
> un clavier PC de bureau, donc la retenir ne coûte quasiment rien au quotidien.
> Comme le combo est avalé, l'état Scroll Lock n'est même jamais basculé sur le PC.
>
> **Clavier Apple** (pas de Scroll Lock) : utiliser `F13` (claviers pleine taille)
> ou un modificateur de droite comme `HOTKEY_SWITCH_ARM`. DeskHop lit le clavier
> *physique*, pas l'OS : c'est le clavier branché qui détermine les touches
> disponibles, pas le fait qu'un des PC soit un Mac.

---

## 2. Garantie « zéro octet » (anti-fantôme)

La solution retenue est le **buffering strict avec anticipation** : une touche
n'est transmise au PC que lorsqu'elle **ne peut plus** faire partie d'un hotkey.

Mécanique (`src/keyboard.c`, `process_keyboard_report`) :

1. **Préfixe gardé** — si tout ce qui est appuyé est un *sous-ensemble* d'un
   hotkey « gardé » (`report_is_guarded_prefix`), le rapport est **retenu**
   (withheld), jamais envoyé.
2. **Combo complété** — le handler s'exécute et **rien** n'est transmis ; on
   continue d'avaler les touches encore enfoncées jusqu'au relâchement complet
   (`hotkey_swallow_until_release`), pour que les modificateurs ne fuient pas
   *après* la bascule.
3. **Divergence** — dès qu'une touche hors de tout hotkey gardé est appuyée, on
   relâche immédiatement l'état courant (latence ≈ 1 rapport, soit ~1–8 ms). Les
   rapports HID étant absolus, envoyer l'état courant suffit (pas besoin de
   rejouer l'intermédiaire).
4. **Timeout** — si on reste pile sur le préfixe sans compléter ni diverger
   (p. ex. on maintient les modificateurs avec la souris), `hotkey_buffer_flush_task`
   relâche après `HOTKEY_BUFFER_TIMEOUT_MS` (défaut 60 ms), puis laisse passer
   jusqu'au relâchement (`hotkey_passthrough`).

Seuls les hotkeys marqués `guard = true` (les trois de bascule) déclenchent ce
mécanisme ; les autres hotkeys gardent le comportement d'origine.

### Pourquoi la frappe normale n'est pas pénalisée

Le préfixe gardé est **Scroll Lock**, que personne ne maintient au quotidien.
Toute touche normale (lettres, Ctrl+C, Maj+clic…) n'est pas un sous-ensemble du
hotkey → elle passe **immédiatement**, sans aucune retenue. Seul le maintien de
Scroll Lock seul subit le timeout — ce qui n'arrive en pratique jamais.

---

## 3. Alternatives envisagées (et pourquoi écartées)

### A. Ne rien faire de spécial — modificateur « inoffensif » qui fuite
Combo classique (p. ex. Left Ctrl + une touche). La touche de déclenchement est
avalée ; seul le modificateur de tête fuite.
- **+** Le plus simple, zéro code, zéro latence.
- **+** Un modificateur seul ne produit aucun caractère → sans effet pratique.
- **−** Pas « zéro octet » au sens strict : le modificateur part réellement.
- **Verdict** : rejeté car l'objectif était *aucun* octet, pas « inoffensif ».

### B. Combo à deux modificateurs + F‑touche (sans buffering)
Maintenir Right Ctrl + Right Shift puis F1/F2.
- **+** Très simple ; la touche F n'est jamais transmise.
- **+** Ne fuient que des modificateurs inertes.
- **−** Le *premier* modificateur appuyé fuite encore (on ne peut pas presser deux
  touches à la micro‑seconde près).
- **Verdict** : rejeté pour la même raison que A (il reste une fuite de modificateur).

### C. Double‑tap d'une touche sûre (standard des KVM matériels)
Double appui sur Scroll Lock / Right Ctrl (comme Aten & co).
- **+** Standard éprouvé ; une touche sûre est inoffensive même si elle fuite.
- **+** Geste simple à une main.
- **−** Ne transmet jamais qu'un modificateur/lock inerte, mais **le transmet
  quand même** → toujours pas « zéro octet ».
- **−** Logique de timing (fenêtre de double‑tap) → faux positifs/négatifs possibles.
- **−** Difficile d'avoir A et B *dédiés* avec une seule touche.
- **Verdict** : rejeté car l'objectif strict « zéro octet » n'est pas atteint, et
  les touches dédiées A/B sont moins naturelles.

### D. Buffering strict avec anticipation — **CHOISI**
Retenir tout préfixe de hotkey jusqu'à résolution.
- **+** Seule méthode **littéralement zéro octet** (ni touche ni modificateur).
- **+** Touches A/B dédiées + toggle, tout est possible.
- **+** Divergence → relâche en ~1 rapport, donc la frappe normale n'est pas gênée
  (d'autant que le préfixe est une touche inutilisée).
- **−** Plus de code (machine à états, tâche de flush).
- **−** Maintenir le préfixe exact avec la souris subit jusqu'à `TIMEOUT` ms de
  latence (atténué par le choix des modificateurs de droite).
- **−** Avec **deux** claviers simultanés, l'état combiné pourrait laisser fuir un
  modificateur retenu (cas limite, voir §5).

### Variante de la méthode D non retenue : withhold basé sur « modificateurs d'armement complets »
Ne retenir qu'une fois **tous** les modificateurs d'armement présents (on laisse
passer un seul modificateur).
- **+** Aucune latence, même en maintenant un modificateur de droite seul.
- **−** Le premier des deux modificateurs fuite → pas « zéro octet ».
- **Verdict** : écartée au profit du sous‑ensemble strict (zéro octet exigé).

---

## 4. Désactivation du basculement par le bord

```c
// src/include/user_config.h
#define SWITCH_HOTKEY_ONLY 1   // 1 = hotkey seul (pas de bord) ; 0 = comportement d'origine
```

`do_screen_switch()` (`src/mouse.c`) sort immédiatement si le drapeau est actif :
la souris ne change plus jamais de sortie, le hotkey continue de fonctionner. Le
réglage est persistant (`config_t.switch_hotkey_only`, flash).

---

## 5. Limites

- **Maintien de la touche d'armement avec la souris** (Scroll Lock + clic) :
  jusqu'à `HOTKEY_BUFFER_TIMEOUT_MS` de latence. Avec Scroll Lock ce cas ne se
  produit jamais en pratique ; il ne deviendrait sensible qu'avec une touche
  d'armement réellement utilisée (p. ex. un modificateur).
- **Deux claviers branchés en même temps** : l'état clavier est combiné
  (`combine_kbd_states`) ; un préfixe retenu sur un clavier pourrait partir via un
  rapport de l'autre. Cas limite (la majorité des montages ont un seul clavier).

---

## 6. Fichiers concernés

| Fichier | Rôle |
|---|---|
| `src/keyboard.c` | Buffering anti‑fantôme, `report_is_guarded_prefix`, `hotkey_buffer_flush_task`, table `hotkeys[]` |
| `src/include/keyboard.h` | Déclaration de la tâche de flush |
| `src/include/structs.h` | Champ `guard` du hotkey + état de buffering dans `device_t` |
| `src/include/user_config.h` | `HOTKEY_SWITCH_*`, `HOTKEY_SELECT_A/B`, `HOTKEY_BUFFER_TIMEOUT_MS`, `SWITCH_HOTKEY_ONLY` |
| `src/handlers.c` | `select_output_a/b_hotkey_handler` |
| `src/mouse.c` | `do_screen_switch()` : sortie anticipée si hotkey‑only |
| `src/main.c` | Enregistre la tâche de flush (core1) |
| `src/defaults.c` | Valeur par défaut de `switch_hotkey_only` |

> Note : la version de config passe à 9 (commune avec la réplication d'identité
> USB) → réglages réinitialisés au premier démarrage après mise à jour.
