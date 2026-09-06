# `my_wine` — guide de présentation

Format visé : **30 minutes** — 21 minutes de présentation, 3 minutes de démonstration et 6 minutes de marge/questions.

Le public a déjà vu la présentation `ld.so` et se souvient de ses mécanismes principaux : mapping, relocations, résolution des symboles et handoff. Ne pas refaire ce cours. Les slides 5 à 9 servent seulement à reconnecter cette méthode au nouveau problème.

## Fil narratif

> Wine et Proton montrent qu'un programme Windows peut s'exécuter sur Linux sans émuler son processeur. `my_wine` réduit ce problème à une taille pédagogique : `ld.so` fournit la méthode pour charger l'image, puis chaque hypothèse Windows absente devient un bridge explicite.

La présentation suit quatre questions :

1. comment reconstruire l'image PE ?
2. qu'observe le guest dès la première instruction ?
3. comment passer d'un contexte Windows à un contexte Linux puis revenir ?
4. comment vérifier que tous ces ponts fonctionnent ensemble ?

## Timing slide par slide

| # | Slide | Temps | Message oral |
|---:|---|---:|---|
| 1 | `my_wine` — du loader à la compatibilité | 0:35 | Reprendre la promesse de la fin de `ld.so` : Doom95 était le teaser, cette présentation explique le chemin qui y mène. |
| 2 | Wine ne simule pas un processeur | 0:55 | Le CPU x86 sait déjà exécuter le code x86. Wine traduit l'environnement et les services attendus par l'application. Ne pas détailler l'architecture de Wine. |
| 3 | Proton applique cette idée au jeu | 1:00 | Situer Proton comme outil de compatibilité de Valve fondé sur Wine et des composants spécialisés pour le jeu. Rester au niveau des couches et des objectifs. |
| 4 | Pourquoi une version minuscule ? | 0:55 | Le projet ne concurrence pas Wine : il sacrifie la couverture pour rendre chaque frontière observable et compréhensible. |
| 5 | `ld.so` était le prologue | 0:55 | Faire lire les cinq verbes. Le pipeline connu reste valide, mais le premier saut ne clôt plus l'histoire. |
| 6 | Route : du format au contrat | 0:40 | Annoncer les quatre parties. Répéter la question directrice : « qu'est-ce que le programme observe maintenant ? » |
| 7 | Un PE est une recette d'image | 0:55 | Comparer brièvement sections/data directories aux segments/program headers ELF. Ne pas lire chaque champ. |
| 8 | Du fichier à l'image exécutable | 1:10 | Suivre réserver → copier → reloger → protéger. Donner une seule équation : `adresse = base réelle + RVA`. |
| 9 | Les imports sont des promesses | 1:10 | ILT = demande, résolution = loader, IAT = pointeurs appelables. Dans `my_wine`, la cible est souvent un handler interne. |
| 10 | Le premier saut ne suffit pas | 1:10 | Pivot principal. Même une image parfaite échoue si la pile, le processus, l'ABI et les services Windows n'existent pas. |
| 11 | Donner une identité Windows | 1:15 | Définir un registre en une phrase, puis suivre GS/FS + offset → TEB → PEB. PE32 utilise FS ; PE32+ utilise GS. |
| 12 | Construire la pile et respecter l'ABI | 1:15 | Montrer ce qui est observable au handoff : largeur, alignement, arguments et retour. Relier les deux backends à de vrais processus 32 et 64-bit. |
| 13 | Une API devient une adresse callable | 1:10 | Opposer handlers internes et exports d'une DLL PE chargée. Un pointeur correct ne garantit pas encore une sémantique correcte. |
| 14 | Un syscall NT n'est pas Linux | 1:15 | Même opcode, mais table de numéros, arguments, handles et statuts différents. Il faut traduire le contrat entier. |
| 15 | Le thunk capture l'appel | 1:20 | La barre matérialise réellement 23 octets. Le thunk préserve, identifie et transfère ; le dispatcher traduit. Mentionner honnêtement les handlers directs actuels. |
| 16 | Le dispatcher change de monde | 1:15 | Suivre les deux couloirs : sauver, basculer sur la pile Linux, décoder, appeler, restaurer. La pile guest n'est pas recopiée. |
| 17 | Quand Linux redevient utilisable | 1:10 | La libc et SDL2 exigent le contexte host. Le risque vu une fois dans `ld.so` revient à chaque traversée du bridge. |
| 18 | Une faute devient un signal | 1:10 | Linux reçoit d'abord la faute. La pile alternative rend le diagnostic sûr ; préciser que la traduction SEH complète n'existe pas encore. |
| 19 | Doom95 additionne les frontières | 1:00 | Le jeu valide loader, état du processus, Win32, affichage, son et entrées. Distinguer DirectSound/SDL2 de WinMM/MIDI/FluidSynth. |
| 20 | Démonstration | 3:00 | Lancer Doom95, montrer une interaction courte, puis relier le résultat à DirectDraw/DirectSound/SDL2. Ne pas compiler en direct. |
| 21 | Charger le format, reconstruire le contrat | 0:45 | Conclure avec les deux temps du projet : `ld.so` prépare l'image ; `my_wine` rend son monde crédible. |

Total cible : **24:00**, démonstration comprise.

## Démonstration

Préparer un terminal à la racine du dépôt et décompresser Doom95 avant la présentation :

```bash
./scripts/unpack_samples.sh doom95
./my_wine samples/unpacked/doom95/DOOM95.EXE
```

Limiter la démonstration à trois preuves visibles :

- une frame est rendue ;
- une entrée clavier ou souris traverse la boucle de messages ;
- le son démarre si la configuration de la salle le permet.

Préparer deux replis indépendants :

```bash
./my_wine samples/hello_world/hello_world.exe
```

et une capture ou courte vidéo locale de Doom95. Ne jamais dépendre du réseau pendant la démonstration.

## Transitions importantes

- Slide 1 → 2 : « Avant mon implémentation, regardons le système qui prouve déjà que l'idée fonctionne. »
- Slide 3 → 4 : « Proton cherche la compatibilité à grande échelle ; moi, je cherchais à voir les engrenages. »
- Slide 5 → 6 : « Le loader donne donc le point de départ, pas encore l'environnement. »
- Slide 9 → 10 : « Imaginons maintenant que toute cette partie soit parfaite : est-ce que le programme peut vraiment démarrer ? »
- Slide 12 → 13 : « Le processus existe ; il va maintenant demander des services. »
- Slide 16 → 17 : « Basculer de pile permet d'appeler un handler, mais cela décide aussi quand le code Linux est sûr. »
- Slide 18 → 19 : « Doom additionne précisément toutes ces traversées, y compris quand elles échouent. »
- Slide 20 → 21 : « Ce que nous venons de voir tient dans la différence entre charger des octets et reconstruire leur contrat. »

## Questions probables

**Est-ce de l'émulation ?**

Pas au niveau du processeur : le code x86 est exécuté nativement. Le projet reconstruit ou traduit une petite partie de l'environnement Windows en espace utilisateur.

**Quelle est la différence avec Wine ?**

Wine vise une vaste compatibilité applicative et possède une architecture de production construite sur plusieurs décennies. `my_wine` est un laboratoire pédagogique avec une couverture volontairement limitée.

**Et Proton ?**

Proton est l'outil de compatibilité de Valve pour Steam Play. Il s'appuie notamment sur Wine et sur des composants orientés jeu. La présentation ne prétend pas reproduire son architecture.

**Pourquoi deux binaires ?**

Un vrai processus 32-bit fournit naturellement des pointeurs, une pile et une ABI i386 cohérents. Le wrapper choisit `my_wine32` ou `my_wine64` après lecture du PE.

**Pourquoi ne pas envoyer un syscall Windows directement à Linux ?**

Le numéro, les arguments, les objets manipulés et le code de retour appartiennent au contrat du noyau. L'opcode seul ne suffit pas.

**Que se passe-t-il si le programme exécute directement une instruction `syscall` Windows ?**

Dans l'implémentation actuelle, `my_wine` intercepte surtout les appels NT pendant la résolution des imports : l'entrée IAT de `ntdll!Nt*` pointe vers un handler interne ou vers un thunk. Il ne capture pas encore toutes les instructions `syscall` arbitraires présentes dans le code guest ; un syscall NT inline risquerait donc d'entrer dans Linux avec un numéro et une convention incompatibles.

Le vrai Wine ajoute un filet plus général sous Linux avec **Syscall User Dispatch**. Le noyau autorise les syscalls provenant des zones host de Wine, mais transforme en `SIGSYS` ceux émis depuis le code Windows. Wine lit alors le numéro NT et les registres dans le contexte du signal, redirige l'exécution vers son dispatcher, traduit l'opération — parfois avec plusieurs syscalls Linux ou un échange avec `wineserver` — puis restaure le contexte Windows. Ce n'est donc jamais un simple remapping « numéro NT → numéro Linux ».

**Tous les syscalls passent-ils par les thunks ?**

Non. L'infrastructure de thunks et du dispatcher existe, mais beaucoup d'imports `ntdll` courants rejoignent encore directement leurs handlers C.

**Pourquoi SDL2 ?**

Le backend adapte la surface utile à Doom95 : DirectDraw et DirectSound passent par SDL2, les événements SDL2 alimentent les entrées, et WinMM/MIDI utilise FluidSynth pour la musique.

**Peut-il lancer n'importe quel `.exe` ?**

Non. La couverture API, le threading, TLS, Unicode, la synchronisation et plusieurs modèles runtime restent incomplets.

## Contrôle du temps

- À **5:00**, commencer la route du talk.
- À **10:00**, être sur le pivot « le premier saut ne suffit pas ».
- À **17:00**, être dans le dispatcher ou la zone libc.
- À **21:00**, lancer la démonstration.
- Si le temps manque, condenser les slides 7 à 9 ; ne pas couper les slides 10, 16, 17 et 19.
