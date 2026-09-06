# Objectif DOOM — notes de présentation

Présentation en français, conçue pour un public informatique ayant déjà suivi le talk sur `ld.so`. **30 minutes : 21 minutes d’explications, 3 minutes de démonstration, 6 minutes de questions et marge.** Les quatre slides de réserve ne font pas partie du parcours chronométré.

## Intention et conduite

Origine du projet : l’essor de Wine et Proton a suscité l’envie de comprendre comment un programme Windows peut tourner sous Linux. Cette curiosité a donné naissance à `my_wine`, avec DOOM95 comme cible concrète.

La popularité est ici le déclencheur personnel du projet. Ne pas ajouter de chiffres d’adoption ni présenter le projet comme une reproduction de toute l’architecture de Wine ou de Proton.

L’ambre représente le programme Windows ; le turquoise représente Linux et les étapes acquises. Le parcours en bas de chaque slide montre fichier → image mémoire → état Windows → appels traduits → DOOM. Il représente une progression pédagogique, pas une télémétrie ni la preuve qu’un test vient de réussir.

Exploiter les acquis de `ld.so` : mapping, relocations, résolution des symboles et transfert du contrôle. Les rappeler par analogie, sans refaire leur démonstration. En revanche, prendre le temps d’expliquer les registres, la Stack et les conventions d’appel avant de montrer le thunk.

DOOM95 suit le parcours **32 bits** du début à la fin. La comparaison 64 bits reste en réserve. Les adresses des exemples mémoire et Stack sont pédagogiques, pas une capture d’exécution du jeu.

Chaque acronyme est développé avant son premier usage dans le parcours. Dire le terme complet et sa signification ; ne pas compter sur le glossaire final pour combler une définition manquante. Les noms de registres et de fonctions sont des identifiants : expliquer leur rôle, sans leur inventer un développement.

## Transitions visibles, sans dépendre des notes

Les bandeaux assurent les passages importants du raisonnement :

- 6 → 7 : la carte du fichier donne les informations nécessaires pour construire l’image.
- 8 → 9 : les noms sont branchés ; l’état de la première instruction reste à préparer.
- 12 → 13 : après les règles de la Stack vient l’identité Windows du programme.
- 13 → 14 : l’état est prêt ; on suit maintenant un appel à `WriteFile`, déjà introduit en slide 8.
- 14 → 15 : distinguer le handler direct du mécanisme commun de répartition, avant de nommer le thunk.
- 15 → 16 : après le transfert au dispatcher, suivre son travail et le chemin du retour.
- 16 → 17 : un chemin de retour ne suffit pas ; le contexte Linux a des conditions à respecter.
- 17 → 18 : annoncer les besoins image/entrées/son, puis présenter la bibliothèque multimédia qui y répond.
- 18 → 19 : les trois services deviennent les trois preuves de la démonstration.

Les notes donnent les détails oraux, mais ces enchaînements doivent rester compréhensibles avec les slides seules.

## Minutage

| Slide | Sujet | Durée | Cumul |
|---:|---|---:|---:|
| 1 | Objectif DOOM | 0:45 | 0:45 |
| 2 | D’où vient ce projet ? | 1:00 | 1:45 |
| 3 | Les instructions savent déjà tourner | 1:00 | 2:45 |
| 4 | ld.so nous a donné la méthode | 1:00 | 3:45 |
| 5 | Ce qu’il faut mettre en mémoire pour DOOM | 1:15 | 5:00 |
| 6 | Portable Executable | 1:00 | 6:00 |
| 7 | Construire une image cohérente | 1:00 | 7:00 |
| 8 | Brancher les fonctions manquantes | 1:00 | 8:00 |
| 9 | L’image est prête | 1:00 | 9:00 |
| 10 | Les cases de travail du processeur | 1:00 | 10:00 |
| 11 | Appeler, c’est aussi préparer le retour | 1:30 | 11:30 |
| 12 | Une bonne adresse ne suffit pas | 1:15 | 12:45 |
| 13 | Windows existe aussi dans la mémoire | 1:00 | 13:45 |
| 14 | Écrire des octets : deux contrats | 1:15 | 15:00 |
| 15 | Un thunk : 15 octets | 1:30 | 16:30 |
| 16 | Un appel, deux Stacks, un retour | 1:30 | 18:00 |
| 17 | Chaque passage a ses conditions | 1:00 | 19:00 |
| 18 | Le relais multimédia côté Linux | 1:00 | 20:00 |
| 19 | Le moment de vérité | 3:00 | 23:00 |
| 20 | Charger les octets, reconstruire le monde | 1:00 | 24:00 |

Repères : ouvrir la mémoire à **3:45**, les registres à **9:00**, la traduction à **13:45**, la démonstration à **20:00**, conclure à **23:00**. Fin à **24:00**.

## Notes slide par slide

### 1 — Objectif DOOM · 0:45

Commencer avec le résultat visé : « À la fin, je veux lancer cette commande et jouer à un programme Windows depuis Linux. » Nommer DOOM95 comme la version Windows en 32 bits, pour éviter de confondre le projet avec la compilation d’un port Linux du jeu.

Faire suivre la carte de mission du fichier au système hôte. Elle est une illustration originale du parcours, pas une capture du jeu. Ne pas annoncer une exécution réussie avant la démonstration.

Transition : « Pourquoi avoir voulu fabriquer ce passage moi-même ? »

### 2 — D’où vient ce projet ? · 1:00

Raconter simplement l’origine du projet : Wine et Proton gagnent en visibilité, cela éveille une curiosité technique, puis vient l’envie d’essayer avec un petit projet. Le texte à l’écran est un paragraphe de contexte, sans guillemets ni effet de citation. Ne pas le déclamer ; enchaîner directement sur les trois repères.

Développer le nom récursif **Wine Is Not an Emulator**. Dans le cadre du talk, le code x86 tourne sur un processeur compatible ; le travail concerne la compatibilité avec Windows. Proton est l’outil de Valve qui s’appuie sur Wine et des composants destinés notamment aux jeux. `my_wine` est un terrain d’exploration personnel à couverture limitée.

Transition : « Qu’est-ce qui manque vraiment lorsqu’on change de système ? »

### 3 — Les instructions savent déjà tourner · 1:00

Le processeur exécute des instructions machine, dont l’assembleur est une notation lisible. Le mini-exemple charge 42 puis ajoute 1 ; `eax` est le nom d’une case de travail, détaillée plus loin.

Préciser la condition : machine x86 compatible avec l’exécution 32 bits. Ne pas généraliser cette affirmation à une machine d’une autre architecture.

Pointer les trois besoins concrets : charger le fichier Windows, préparer la mémoire du programme et fournir les fonctions Windows. La mémoire comprend aussi les structures qui décrivent l’état attendu par le programme. Une fonction Windows n’est pas nécessairement un appel au noyau ; elle peut effectuer tout son travail en espace utilisateur.

Transition : « Pour la première absence, nous avons déjà une boîte à outils. »

### 4 — ld.so nous a donné la méthode · 1:00

Suivre les cinq opérations numérotées de gauche à droite. Lire repère les informations du fichier ; mapper rend le code et les données accessibles en mémoire ; relocaliser corrige les pointeurs concernés par un changement de base ; résoudre trouve l’adresse des fonctions demandées ; exécuter transfère le contrôle au point d’entrée.

Prendre l’exemple visible de `puts` : le programme connaît le nom, le chargeur cherche la fonction et branche son adresse. Distinguer cette recherche d’adresse de la correction d’un pointeur déjà présent. Le parcours est simplifié : dans un chargeur réel, certaines relocalisations utilisent justement le résultat de la résolution d’un symbole ; les deux opérations peuvent donc être entrelacées.

Le chargeur dynamique Linux ne fournit pas à lui seul tous les services Linux : le noyau et les bibliothèques sont déjà présents. Dans notre environnement minimal, une partie des attentes Windows doit être reconstruite.

Transition : « Arrêtons-nous sur le mapping : qu’est-ce que nous devons construire en mémoire pour DOOM ? »

### 5 — Ce qu’il faut mettre en mémoire pour DOOM · 1:15

Une seule idée : charger le fichier ne crée pas toutes les zones mémoire nécessaires au jeu. À gauche, l’exécutable sur disque ; à droite, ce que le programme utilisera dans son processus.

Suivre les deux groupes. En ambre, le code et les données sont chargés selon les indications du fichier ; certaines données sont initialisées à zéro. En turquoise, le runtime prépare le Heap pour les allocations et la Stack pour les appels et les retours. Le dessin regroupe leurs rôles, pas leurs adresses réelles ni des régions nécessairement contiguës. Le Heap peut obtenir de nouvelles zones au fil de l’exécution.

La slide prépare deux suites : les en-têtes du fichier en slide 6, puis la Stack et les registres en slides 10–12. Garder la distinction mémoire virtuelle/physique pour la réserve ; ne pas ajouter ici un second cours parallèle.

Transition : « Commençons par le code et les données : où le fichier indique-t-il comment les charger ? » La slide suivante apporte la réponse avec le format Windows.

### 6 — Portable Executable · 1:00

Développer **Portable Executable (PE)** et nommer PE32 pour le format 32 bits de DOOM95. Rappeler **Executable and Linkable Format (ELF)** avant de réutiliser l’abréviation du talk précédent.

La comparaison met en correspondance des rôles, pas des structures identiques : segments de chargement d’un côté, sections de l’autre. Les en-têtes indiquent entre autres la base préférée et le point d’entrée ; les répertoires donnent accès aux tables utiles.

Transition : « Lire cette carte nous permet de placer les octets. »

### 7 — Construire une image cohérente · 1:00

Définir **Relative Virtual Address (RVA)** : déplacement depuis la base de l’image. Le préfixe 0x indique une écriture hexadécimale, en base 16. Faire une seule addition, puis expliquer la correction de base.

Les relocations de base désignent les emplacements à corriger : on n’ajoute pas aveuglément le delta à tous les nombres ou à toutes les instructions. Ne pas confondre offset de fichier et déplacement dans l’image.

Le schéma regroupe les responsabilités du chargement. Dans le code, permissions, écriture des imports et correctifs peuvent être organisés en plusieurs passes ; ce n’est pas une transcription ligne à ligne des deux chargeurs.

Transition : « Une adresse de code interne est prête. Mais que deviennent les fonctions externes ? »

### 8 — Brancher les fonctions manquantes · 1:00

Développer **Dynamic-Link Library (DLL)**, bibliothèque liée dynamiquement. Puis **Import Lookup Table (ILT)**, liste des demandes, et **Import Address Table (IAT)**, table des adresses résolues.

Trois états avec deux pressions sur → : demande visible ; apparition de la résolution ; apparition du pointeur branché. Nommer `WriteFile` comme exemple sans encore détailler ses arguments.

Relier au symbole `puts` du talk précédent. Un handler est la fonction qui traite une demande. Distinguer fonctions internes et exports de bibliothèques réellement chargées. Garder la notion de stub pour la réserve D ; elle n’est pas nécessaire au parcours principal.

Transition : « Nous avons des pointeurs. Est-ce suffisant pour démarrer ? »

### 9 — L’image est prête · 1:00

C’est le pivot du talk. Une image chargée ne signifie pas que les instructions trouveront une Stack correcte, l’état Windows attendu ou des fonctions qui respectent les bonnes conventions.

Faire lire les trois éléments encore à fournir. Le pied de page passe à « état Windows ».

Transition : « Pour voir ce qui se passe au premier saut, il faut quelques repères sur le processeur. »

### 10 — Les cases de travail du processeur · 1:00

Un registre contient une petite quantité de données directement dans le processeur. `EAX` sert de registre général et souvent de résultat entier ; `EIP` suit le flot d’instructions ; `ESP` est le pointeur de Stack.

Lire `mov eax, [esp]` dans l’ordre : adresse dans ESP → lecture de quatre octets en mémoire → valeur dans EAX. Les crochets indiquent l’accès à la mémoire. Le registre ESP et la Stack ne sont pas la même chose.

Transition : « Regardons ce pointeur bouger pendant un appel. »

### 11 — Appeler, c’est aussi préparer le retour · 1:30

Trois états avec deux pressions sur →. Les adresses diminuent vers le bas du dessin.

Après `push 42`, ESP vaut 0x0FFC. `call fonction` empile l’adresse de l’instruction de retour à 0x0FF8 puis transfère le contrôle. La fonction peut ensuite utiliser sa Stack ; on omet ici son prologue et ses variables locales pour isoler appel et retour.

Le `ret` sans opérande récupère l’adresse et remonte ESP à 0x0FFC. L’argument 42 reste sur la Stack. « Libérée » signifie disponible pour réutilisation, pas effacée ni rendue au système.

Transition : « Qui retire cet argument ? C’est une règle que les deux côtés doivent partager. »

### 12 — Une bonne adresse ne suffit pas · 1:15

Développer **Application Binary Interface (ABI)** : contrat binaire, notamment les règles d’appel. À l’entrée de l’exemple, l’adresse de retour est à ESP et les arguments à ESP + 4 et ESP + 8.

Pour `__stdcall` en 32 bits, les arguments sont empilés de droite à gauche et la fonction appelée les retire ; avec `__cdecl`, c’est l’appelant. Un entier simple revient généralement dans EAX. Ne pas étendre ce dessin à tous les types de retour ou toutes les conventions.

Le démarrage de l’image n’est pas cet appel à deux arguments : le chargeur prépare une Stack et une adresse de retour contrôlée, puis saute. Vérifier le code assembleur plutôt que reprendre ses anciens commentaires sur l’alignement, qui mélangent parfois 32 et 64 bits.

Transition : « Même sans appeler une fonction, le programme peut lire son identité Windows. »

### 13 — Windows existe aussi dans la mémoire · 1:00

Définir un thread : un fil d’exécution dans le processus. Introduire le registre de segment FS comme un mécanisme d’adressage particulier.

Développer **Thread Environment Block (TEB)** puis **Process Environment Block (PEB)**. En 32 bits, l’accès `FS:[0x30]` lit, dans l’état du thread, le pointeur vers l’état du processus. FS sélectionne un segment dont la base participe au calcul d’adresse ; ce n’est pas un registre général contenant directement ce pointeur.

Le runtime installe les structures nécessaires avant de démarrer le code invité.

Transition : « Le programme peut maintenant demander des services. »

### 14 — Écrire des octets : deux contrats · 1:15

Développer **Application Programming Interface (API)** : fonctions proposées au programme. Invité désigne le programme Windows ; hôte désigne Linux.

Suivre le chemin effectivement présent dans `kernel32_console.c` : `WriteFile` reçoit un handle, identifiant de ressource Windows, le résout en descripteur Linux, demande une écriture au noyau, puis rend un succès ou un échec et le nombre d’octets écrits.

Le handler vérifie qu’un thunk existe mais ne l’appelle pas. Il utilise ici un appel système Linux direct. Les opérations asynchrones demandées par le paramètre `lpOverlapped` ne sont pas implémentées par cette fonction. Ce dessin ne promet donc pas toute la sémantique de Windows.

Transition : « Le dépôt contient aussi un mécanisme commun pour répartir des services. »

### 15 — Un thunk : 15 octets · 1:30

Définir thunk avant de lire le code : petit adaptateur généré qui transmet un identifiant à un répartiteur, le dispatcher. Développer **New Technology (NT)** pour situer la famille de services Windows bas niveau.

Le dessin correspond aux instructions émises en 32 bits : 1 + 5 + 5 + 2 + 1 + 1 = 15 octets. `EBP` est un registre à préserver, `EAX` porte ici l’adresse du dispatcher et `EDX` l’identifiant du service. Au retour, EAX peut porter le résultat.

Ne pas raconter que chaque appel de DOOM passe ici : de nombreux imports de la bibliothèque système Windows `ntdll.dll` rejoignent directement leurs handlers. Le thunk n’intercepte pas à lui seul une instruction d’appel système quelconque.

Transition : « À l’intérieur du dispatcher, il faut pouvoir travailler puis retrouver l’appelant. »

### 16 — Un appel, deux Stacks, un retour · 1:30

Le temps se lit toujours de gauche à droite. Les deux bandes horizontales restent visibles dès le début : ambre pour Windows, turquoise pour Linux. Le trajet pointillé annonce le parcours complet ; les flèches pleines montrent le chemin expliqué.

Trois états avec deux pressions sur → : 1–2, entrée et sauvegarde en haut ; 3–4, descente vers la Stack Linux et traitement ; 5, remontée vers l’invité avec restauration et résultat. Suivre la ligne avec le pointeur plutôt que lire des colonnes indépendantes.

Les deux Stacks appartiennent au même processus. Changer de Stack consiste à charger un autre pointeur de Stack après avoir sauvegardé celui de l’invité ; ce n’est ni copier la Stack ni démarrer un second processus.

Le dispatcher 32 bits sauvegarde notamment les registres concernés et les indicateurs du processeur, traite l’appel sur une Stack préparée, puis restaure l’état et transmet le résultat dans EAX. Le thunk finit par restaurer EBP et revenir à l’appelant.

Transition : « Ce mécanisme est utile, mais ses garanties ont des limites précises. »

### 17 — Chaque passage a ses conditions · 1:00

Trois limites concrètes. Un numéro de service Windows n’a pas le même sens pour Linux. Le projet ne capture pas toutes les instructions d’appel système arbitraires. Une telle instruction peut donc entrer dans Linux avec un contrat incompatible.

La bibliothèque standard C, `libc`, et les bibliothèques multimédias demandent un contexte approprié. Les ponts multimédias du dépôt gèrent Stack et segments selon l’architecture ; ne pas attribuer automatiquement ce travail au seul dispatcher, ni dire que toute présence d’un état Windows interdit libc.

Enfin Linux reçoit les fautes matérielles. Une Stack alternative aide au diagnostic, sans fournir toute la traduction des exceptions Windows.

Transition : « Nous savons passer dans le contexte Linux. DOOM a maintenant besoin d’une image, des touches et du son. Quelle bibliothèque peut nous fournir ces trois services ? » Faire lire cette question dans le bandeau de la slide ; ne pas annoncer encore de sigle.

### 18 — Le relais multimédia côté Linux · 1:00

Répondre à la question laissée à l’écran : « Pour ces trois besoins, j’ai choisi la bibliothèque Simple DirectMedia Layer, version 2. » Développer **Simple DirectMedia Layer, version 2 (SDL2)** avant de réutiliser le nom. Ce n’est pas un nouveau mécanisme de chargement : c’est le relais multimédia appelé côté Linux par les ponts de `my_wine`.

Suivre la ligne affichage de gauche à droite : DirectDraw, surfaces et palette, bibliothèque multimédia, fenêtre. Inverser explicitement le sens pour les entrées : clavier ou souris Linux → événements → messages Windows → jeu. Les effets sonores passent par DirectSound et le mélange audio.

Distinguer la musique : les services multimédias Windows via `winmm.dll` utilisent notamment FluidSynth. Si la question arrive, développer **Musical Instrument Digital Interface (MIDI)** avant d’employer le sigle et expliquer qu’il s’agit d’événements musicaux à synthétiser, pas d’un flux d’échantillons sonores prêt à jouer.

Transition : « Il est temps de vérifier que ces pièces fonctionnent ensemble. »

### 19 — Le moment de vérité · 3:00

Le terminal doit être ouvert à la racine du dépôt, les binaires construits et les fichiers du jeu décompressés avant le talk.

Lancer une seule commande. Montrer une image, entrer dans le jeu et déplacer le joueur ; faire entendre un effet si la sortie audio est disponible. Relier chaque preuve à une ligne de la slide précédente.

Si le lancement échoue, appliquer le repli décrit plus bas. Ne pas déboguer en direct ni attribuer à `my_wine` une vidéo provenant d’une autre exécution.

Transition : « Ce que nous venons d’observer donne un sens concret à tous ces contrats. » Si le jeu n’a pas fonctionné : « Même lorsque l’intégration échoue, nous savons désormais quelle frontière examiner. »

### 20 — Charger les octets, reconstruire le monde · 1:00

Parcourir le schéma complet. Relier la curiosité de départ au résultat pédagogique : on peut désormais suivre des adresses, une Stack et un appel à travers la frontière.

Le parcours affiché comme terminé indique que l’explication est achevée, pas qu’une compatibilité générale a été démontrée. Nommer les limites : couverture partielle des fonctions, des threads et des exceptions ; validation programme par programme.

Ouvrir les questions. Garder la réserve accessible avec A, sans la dérouler automatiquement.

## Démonstration : préparation et repli

À préparer avant la présentation, depuis la racine du dépôt :

```bash
# Seulement si les fichiers du jeu ne sont pas déjà décompressés :
./scripts/unpack_samples.sh doom95

# Commande montrée au public :
./my_wine samples/unpacked/doom95/DOOM95.EXE
```

Le script de décompression remplace les fichiers de sa destination : ne pas le relancer sur une installation contenant des modifications à conserver. Ne pas compiler ou télécharger pendant le talk.

Vérifier avec le même écran et la même sortie audio que dans la salle : ouverture du jeu, démarrage d’une partie, mouvement, effet sonore, puis fermeture. Enregistrer une courte capture locale de cette exécution pour le repli si possible. La nommer avec sa date et identifier le binaire utilisé.

Repli simple, après l’avoir validé sur la machine de présentation :

```bash
./my_wine samples/hello_world/hello_world.exe
```

Si le jeu ne démarre pas au bout de 20 secondes, annoncer le problème et utiliser la capture locale vérifiée. Si aucune capture n’est disponible, revenir au schéma d’intégration et au petit exécutable. Ne pas présenter une illustration comme une preuve que le jeu a tourné.

**État de préparation lors de la refonte :** les fichiers DOOM95 sont déjà décompressés et les dépendances dynamiques du runtime 32 bits sont résolues sur cette machine, notamment les bibliothèques multimédias. Cela ne valide pas une partie jouable. Aucune capture de jeu vérifiée n’est fournie avec cette présentation ; l’essai interactif et l’enregistrement de repli restent à effectuer avant le talk.

## Navigation et réserve

Ouvrir `my_wine.html` directement dans un navigateur, sans serveur ni connexion réseau. Présentation prévue pour un écran 16:9 ; le canevas se redimensionne avec des marges sur les autres formats.

- Flèches gauche/droite, Espace ou Entrée : progresser ; les slides 8, 11 et 16 ont chacune trois états.
- Début / Fin : première / dernière slide du parcours actuel. La fin du parcours principal ne passe pas automatiquement en réserve.
- A ou bouton Réserve : ouvrir les annexes ; A ou Retour : retrouver la slide et l’étape quittées.
- F : plein écran. ? : aide. Échap : fermer l’aide.
- `#slide-1` à `#slide-24` : liens directs. Un lien ouvre le premier état du schéma.
- L’impression montre toutes les étapes et les quatre slides de réserve.

Réserve A : comparaison 32/64 bits. Réserve B : fautes, signaux et **Structured Exception Handling (SEH)**, exceptions structurées Windows. Réserve C : lexique des acronymes. Réserve D : vocabulaire des ponts et références, dont la notion de **stub** : implémentation minimale d’une fonction, qui peut ne donner qu’une réponse partielle ou fixe. Un stub ne garantit pas le comportement complet attendu par le programme.

Repère mémoire à développer en réserve C si nécessaire : **Random Access Memory (RAM)** désigne la mémoire vive physique. La mémoire virtuelle est la carte d’adresses utilisée par le processus ; Linux et le matériel gèrent la correspondance avec les pages physiques. Réserver des adresses ne signifie pas que toutes les pages résident immédiatement en mémoire physique. Heap et Stack sont des usages de la mémoire, pas des puces distinctes.

## Sources et vérifications techniques

Les références externes précisent les concepts ; le code du dépôt décide de ce que `my_wine` implémente réellement.

- [Proton, présentation officielle de Valve](https://github.com/ValveSoftware/Proton) : rôle de Proton et relation avec Wine.
- [Microsoft, format Portable Executable](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) : en-têtes, sections, imports, adresses relatives et relocations.
- [Microsoft, convention __stdcall](https://learn.microsoft.com/en-us/cpp/cpp/stdcall) : passage des arguments et nettoyage de la Stack.

Points d’ancrage locaux :

- `src/wrapper_main.c` : choix du runtime selon le format du fichier.
- `src/loader/pe32_run_guest.S` et `src/loader/pe32_entry.c` : préparation et transfert initial, registre de Stack et contexte Windows.
- `src/loader/import_table.c` : imports vers fonctions internes, dont de nombreux handlers directs.
- `src/msvcrt/kernel32_console.c` : chemin réel de `WriteFile`, vérification du thunk puis écriture Linux directe.
- `src/syscall/thunk_gen.c` : séquence de 15 octets en 32 bits ; variante de 23 octets en 64 bits.
- `src/syscall/dispatcher_entry_asm.S` : sauvegarde, Stack Linux et restauration.
- `src/backend/sdl2/rb_sdl2_priv.h` : ponts de contexte et de Stack pour les appels multimédias.
- `docs/architecture/backend.md` : affichage, événements, effets sonores et musique.

Les documents historiques contiennent parfois des formulations trop générales, par exemple une interception universelle des appels système. Ne pas les recopier quand le code montre un chemin plus limité.

## Contrôle du temps

Si le temps manque, raccourcir les rappels de chargement et la comparaison des formats. Préserver la Stack, le contrat d’appel, la distinction handler/thunk et l’aller-retour entre contextes. Garder les détails 64 bits et les exceptions pour les questions. À 20:00, passer à la démonstration ; à 23:00, revenir au débriefing.
