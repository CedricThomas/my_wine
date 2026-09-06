# `my_wine` — guide de présentation

Référentiel terminologique pour les notes : Portable Executable (PE), Executable and Linkable Format (ELF), Application Binary Interface (ABI), Dynamic Link Library (DLL), Relative Virtual Address (RVA), Virtual Address (VA), Import Lookup Table (ILT), Import Address Table (IAT), Thread Environment Block (TEB), Process Environment Block (PEB), Structured Exception Handling (SEH), Thread Local Storage (TLS), Address Space Layout Randomization (ASLR), Simple DirectMedia Layer 2 (SDL2) et Application Programming Interface (API). Sur les slides, chaque développement apparaît de nouveau avant la première occurrence de son abréviation.

Format visé : **30 minutes** — environ 23 minutes de présentation, 3 minutes de démonstration et 4 minutes de marge/questions.

Le public connaît déjà `m_ldso`. Ne pas refaire le cours sur `mmap`, les relocations ou la résolution de symboles : utiliser `ld.so` comme méthode, puis concentrer le temps sur les écarts entre le contrat d'un PE Windows et celui d'un processus Linux.

## Fil narratif

> `ld.so` m'a appris à construire, dans le bon ordre, les invariants nécessaires au premier saut. `my_wine` reprend cette méthode puis ajoute un bridge explicite chaque fois que Windows et Linux ne partagent plus le même contrat.

Les quatre écarts qui structurent le récit sont :

1. l'image : Linux ne mappe pas spontanément un PE ;
2. les symboles : les DLL et fonctions Windows n'existent pas côté host ;
3. le processus : TEB, PEB, handles et SEH sont absents ;
4. l'exécution : ABI, pile, syscalls et fautes n'ont pas la même sémantique.

Le point important n'est pas le volume de code. Doom95 a demandé beaucoup de boilerplate parce qu'il exerce une large surface Win32. Le cœur du talk est constitué des mécanismes réutilisables qui rendent cette intégration possible.

## Timing et notes slide par slide

| # | Slide | Temps | Message à dire |
|---:|---|---:|---|
| 1 | Titre | 0:35 | « Je repars de la méthode de `ld.so`, mais cette fois l'environnement attendu n'existe pas : il faut combler les écarts entre un PE Windows et Linux. » |
| 2 | `ld.so` m'a donné la méthode | 1:05 | Faire lire les trois verbes : lire, construire, mesurer. Le loader n'est pas seulement un morceau de code réutilisé ; c'est une méthode pour rendre vrais des invariants avant le premier saut. |
| 3 | Quatre trous à combler | 1:10 | Donner la carte du talk. Chaque trou demandera soit une conversion de données, soit une conversion de contrôle, soit une conversion de sémantique. |
| 4 | Séparer PE32 / PE32+ | 0:55 | Suivre visuellement l'arbre du wrapper vers les deux processus natifs. Ils évitent de simuler la largeur des pointeurs, de la pile et des appels système host. |
| 5 | Pipeline complète | 1:05 | Un seul passage gauche → droite. `ld.so` fournit mapper, reloger, résoudre, transférer ; `my_wine` ajoute la reconstruction du contrat Windows. |
| 6 | PE = recette d'image | 0:45 | Rappel rapide : `MZ → e_lfanew → PE → sections`. En ELF on suivait les program headers ; ici on suit sections et data directories. |
| 7 | Trois coordonnées d'adresse | 0:40 | Lire les trois cartes de gauche à droite, puis une seule équation : `pointeur = actual_base + RVA`. Ne pas développer davantage sauf question. |
| 8 | Reconstruire l'image | 0:55 | Lire d'abord le passage disque→mémoire, puis le flux du bas : réserver, copier, reloger et enfin appliquer les permissions. Insister sur l'ordre, déjà familier depuis `ld.so`. |
| 9 | Relocations PE | 0:45 | Même besoin qu'en ELF, autre encodage : blocs par page, `HIGHLOW` en 32-bit, `DIR64` en 64-bit. |
| 10 | Demande → adresse callable | 0:55 | Suivre l'organigramme : ILT = demande, résolution = travail du loader, IAT = réponse callable. Une fois patchée, l'IAT dirige réellement les appels du guest. |
| 11 | Résolution par niveaux | 0:50 | Partir du symbole demandé et suivre les deux branches : table interne d'abord, exports d'une DLL PE chargée ensuite. L'implémentation trouvée est souvent mon propre runtime. |
| 12 | Qu'est-ce qu'un registre ? | 1:40 | Prendre le temps de poser le modèle mental. Un registre est une petite case matérielle dans le processeur, pas une variable en mémoire. Les registres généraux transportent valeurs et arguments ; RSP/ESP désigne la pile ; FS/GS contient une base utilisée pour fabriquer une adresse. Ne pas encore parler de TEB. |
| 13 | Comment fonctionne FS/GS | 1:45 | Lire l'exemple de gauche à droite : le guest demande `GS:[0x30]`, le processeur calcule `base_GS + 0x30`, puis atteint un champ de la TEB sans recevoir son adresse en argument. Ensuite seulement suivre FS/GS → TEB → PEB. PE32 utilise FS, PE32+ utilise GS. Préciser qu'ici le registre fournit une base implicite ; il ne redécoupe pas toute la mémoire. |
| 14 | Pile guest | 1:35 | Pivot du talk. Lire d'abord la pile verticale, puis le flux allouer→initialiser→basculer. La première instruction guest doit déjà voir une pile Windows crédible. |
| 15 | NT ≠ Linux | 1:25 | Partir de la racine puis opposer les deux branches. Un opcode `syscall` n'est pas universel : le numéro, les arguments, les handles et le résultat appartiennent au contrat NT. |
| 16 | Thunk de 23 octets | 1:40 | La largeur des quatre couleurs représente réellement 2, 7, 10 et 4 octets. Le thunk préserve, identifie et transfère ; il ne traduit pas. Préciser que beaucoup d'imports `ntdll` vont encore directement vers des handlers C. |
| 17 | Conversion du contexte | 1:45 | Suivre les deux swimlanes. Sauver registres/flags/pile guest, basculer sur la pile Linux, décoder les arguments, appeler le handler, puis restaurer. La pile n'est jamais recopiée. |
| 18 | Signaux Linux / exceptions Windows | 1:35 | Suivre les cinq nœuds jusqu'à la sortie sûre. `siginfo_t` et `ucontext` donnent l'adresse et les registres ; la pile alternative reste utilisable même si la pile guest est cassée. Rappeler la limite actuelle. |
| 19 | Zone sans libc | 1:20 | Lire les trois zones d'exécution. Le risque connu dans `ld.so` revient ici à chaque frontière guest→host : restaurer pile et segment host avant SDL2/libc, puis remettre le contexte guest. |
| 20 | Doom95 comme preuve | 0:55 | Montrer la frame, puis immédiatement séparer les deux colonnes. Doom prouve l'intégration ; son coût vient largement du boilerplate et de la largeur des API. Les mécanismes précédents sont la contribution réutilisable. |
| 21 | Conclusion | 0:40 | Reprendre la phrase : même processeur, mais contrats différents. `ld.so` donne la méthode ; les bridges explicites donnent le runtime. Pause, puis questions. |

Total oral estimé : **24:00**.

## Démonstration proposée — 3 minutes

Préparer deux terminaux, déjà placés à la racine du dépôt.

### 1. Montrer le wrapper et les deux architectures

```bash
./my_wine samples/hello_world/hello_world.exe
./my_wine samples/hello_world_32/hello_world_32.exe
```

Message : la même commande choisit `my_wine64` ou `my_wine32` après inspection du PE. Le wrapper illustre la décision d'architecture, sans y consacrer plus d'une minute.

### 2. Montrer la cible d'intégration

```bash
./scripts/unpack_samples.sh doom95
./my_wine samples/unpacked/doom95/DOOM95.EXE
```

Si la démonstration graphique est risquée, utiliser la capture préparée et garder seulement `hello_world` en direct. Ne pas compiler pendant la présentation.

## Transitions utiles avec `m_ldso`

- Slide 2 : « Je ne réutilise pas seulement des fonctions de loader ; je réutilise une façon de raisonner par invariants. »
- Slide 5 : « Même pipeline jusqu'au handoff, mais contrat final beaucoup plus large. »
- Slide 6 : « Dans ELF je suivais surtout les program headers ; dans PE je suis les sections et les data directories. »
- Slide 9 : « Même raison de reloger, autre encodage des corrections. »
- Slide 11 : « `ld.so` trouvait l'implémentation dans une bibliothèque ; ici je dois souvent la fournir. »
- Slide 12 : « C'est ici que le projet cesse d'être seulement un loader de format. »
- Slide 14 : « L'image est prête ; il faut maintenant fabriquer l'état depuis lequel elle va s'exécuter. »
- Slide 19 : « Le problème rencontré une fois au démarrage de `ld.so` réapparaît à chaque traversée du bridge. »
- Slide 20 : « Doom est le test qui additionne toutes ces frontières, pas une frontière supplémentaire. »

## Questions probables

**Est-ce de l'émulation ?**  
Pas au niveau processeur : le code x86 32-bit et 64-bit est exécuté nativement. Le projet traduit et simule une partie de l'environnement Windows en user space.

**Pourquoi deux binaires plutôt qu'un processus 64-bit unique ?**  
Un vrai processus 32-bit fournit naturellement des pointeurs, une pile, un ABI et des syscalls i386 cohérents. Cela évite de virtualiser toutes les hypothèses d'adresse d'un guest PE32.

**Un syscall Windows peut-il être envoyé directement à Linux ?**  
Non. Même instruction processeur ne signifie pas même table de numéros ni même sémantique. Il faut intercepter l'appel NT et traduire ses arguments, objets et résultats.

**Que contient exactement un thunk ?**  
Le minimum pour préserver l'état nécessaire, charger l'identité de l'appel NT et rejoindre un dispatcher. La traduction elle-même se fait ensuite dans le dispatcher et ses handlers.

**Pourquoi générer des thunks s'ils ne sont pas encore la route principale ?**  
Ils préparent un passage centralisé et donnent une adresse callable distincte par appel NT. Aujourd'hui, les imports `ntdll` courants utilisent encore souvent les handlers directs ; le slide montre l'architecture réelle, pas une couverture déjà universelle.

**Pourquoi une pile de signal alternative ?**  
Parce qu'un crash guest peut avoir endommagé sa pile. Le handler Linux doit disposer d'une pile indépendante pour lire `siginfo_t`/`ucontext`, produire un diagnostic et terminer sans appeler une libc dans un contexte invalide.

**Pourquoi ne pas utiliser Wine directement ?**  
Le but est pédagogique et expérimental : garder une chaîne assez petite pour suivre chaque frontière et chaque conversion, pas remplacer Wine.

**Qu'est-ce qui empêche d'exécuter n'importe quel `.exe` ?**  
La couverture API et les modèles runtime incomplets : threading/TLS, Unicode, synchronisation, fichiers et de nombreuses API restent partiellement pris en charge.

## Si le temps manque

- Passer les slides 6 à 11 en quatre minutes : le public connaît déjà la logique générale du loader.
- Ne pas couper les slides 14 à 19 : pile guest, syscalls NT, thunk, dispatcher, signaux et frontière libc portent l'idée nouvelle.
- Sur Doom95, montrer la capture et faire seulement la distinction « preuve d'intégration / boilerplate ».
