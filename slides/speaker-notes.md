# Objectif DOOM — notes de présentation

[Ouvrir les slides](my_wine.html#slide-1) · **21 slides principales, 2 en réserve.**

Objectif : expliquer comment my_wine charge DOOM95 et réimplémente les fonctions Windows dont le jeu a besoin. Le public connaît déjà ld.so ; le parcours reste en x86 **32 bits** jusqu’à la réserve.

Les paragraphes sont des propositions de texte oral. **À montrer** indique un geste ou une animation ; **Transition** donne la phrase pour passer à la suite. Les précisions pour les questions sont regroupées à la fin, hors du texte à présenter.

## Minutage

Prévoir **25 minutes**, dont 3 pour la démonstration, puis 5 minutes de questions et de marge. Les durées incluent la lecture des schémas ; elles sont à ajuster après répétition.

| Slide | Sujet | Durée | Fin |
|---:|---|---:|---:|
| 1 | [Objectif DOOM](my_wine.html#slide-1) | 0:45 | 0:45 |
| 2 | [D’où vient ce projet ?](my_wine.html#slide-2) | 1:00 | 1:45 |
| 3 | [Les instructions savent déjà tourner.](my_wine.html#slide-3) | 1:00 | 2:45 |
| 4 | [ld.so nous a donné la méthode.](my_wine.html#slide-4) | 1:00 | 3:45 |
| 5 | [Ce qu’il faut mettre en mémoire pour DOOM.](my_wine.html#slide-5) | 1:15 | 5:00 |
| 6 | [Portable Executable (PE)](my_wine.html#slide-6) | 1:00 | 6:00 |
| 7 | [Construire une image cohérente.](my_wine.html#slide-7) | 1:00 | 7:00 |
| 8 | [Brancher les fonctions manquantes.](my_wine.html#slide-8) | 1:00 | 8:00 |
| 9 | [Où trouve-t-on les fonctions des DLL ?](my_wine.html#slide-9) | 1:30 | 9:30 |
| 10 | [L’image est prête. Le programme aussi ?](my_wine.html#slide-10) | 1:00 | 10:30 |
| 11 | [Les cases de travail du processeur.](my_wine.html#slide-11) | 1:00 | 11:30 |
| 12 | [Après le retour, l’argument reste.](my_wine.html#slide-12) | 1:30 | 13:00 |
| 13 | [Même fonction, même convention d’appel.](my_wine.html#slide-13) | 1:15 | 14:15 |
| 14 | [Le programme lit aussi son environnement en mémoire.](my_wine.html#slide-14) | 1:00 | 15:15 |
| 15 | [Zoom sur le PEB et le TEB](my_wine.html#slide-15) | 1:00 | 16:15 |
| 16 | [De WriteFile sous Windows à write sous Linux.](my_wine.html#slide-16) | 1:15 | 17:30 |
| 17 | [Deux Stacks: une Linux et une Windows.](my_wine.html#slide-17) | 1:15 | 18:45 |
| 18 | [Garder la Stack ou appeler une bibliothèque ?](my_wine.html#slide-18) | 1:15 | 20:00 |
| 19 | [Le relais multimédia côté Linux.](my_wine.html#slide-19) | 1:00 | 21:00 |
| 20 | [Le moment de vérité.](my_wine.html#slide-20) | 3:00 | 24:00 |
| 21 | [Faire tourner DOOM sous Linux.](my_wine.html#slide-21) | 1:00 | 25:00 |

Repères : chargement jusqu’à **9:30** ; état Windows et WriteFile jusqu’à **17:30** ; Stacks et multimédia jusqu’à **21:00** ; démonstration jusqu’à **24:00**, puis conclusion.

## Parcours principal

### 1 — [Objectif DOOM](my_wine.html#slide-1) · 0:45 · Temps cumulé : 0:45

Le point d’arrivée, c’est cette commande : lancer DOOM95, la version Windows de DOOM, avec my_wine sous Linux.

Le projet sert à comprendre ce qu’il faut construire entre le fichier et le jeu. On va suivre ses instructions, sa mémoire et ses appels aux fonctions Windows.

**À montrer :** la commande, puis le parcours sur la carte. Garder les détails pour les slides suivantes.

**Transition :** D’abord, pourquoi avoir commencé ce projet ?

### 2 — [D’où vient ce projet ?](my_wine.html#slide-2) · 1:00 · Temps cumulé : 1:45

L’essor de Wine et de Proton m’a donné envie de comprendre comment un programme Windows pouvait fonctionner sous Linux.

Wine fournit une couche de compatibilité avec Windows. Proton s’appuie notamment sur Wine pour les jeux. Avec my_wine, je reconstruis une petite partie de ces mécanismes, dans un programme assez simple pour pouvoir suivre ce qui se passe.

DOOM95 donne une cible concrète : charger un exécutable, afficher quelque chose et répondre aux touches.

**Transition :** Qu’est-ce qui empêche son code de tourner directement ?

### 3 — [Les instructions savent déjà tourner.](my_wine.html#slide-3) · 1:00 · Temps cumulé : 2:45

Le processeur comprend déjà ces instructions. Ici, on place 42 dans EAX, puis on ajoute 1. Sur une machine x86 compatible avec le 32 bits, ces opérations peuvent s’exécuter directement.

Ce qui manque, c’est ce que le programme attend autour : quelqu’un pour charger son fichier, préparer sa mémoire et fournir les fonctions Windows qu’il appelle.

C’est ce travail que my_wine prend en charge.

**À montrer :** les deux instructions, puis les trois besoins.

**Transition :** Pour la partie chargement, on a déjà des repères avec ld.so.

### 4 — [ld.so nous a donné la méthode.](my_wine.html#slide-4) · 1:00 · Temps cumulé : 3:45

On retrouve les étapes du chargeur dynamique Linux : lire le fichier, placer le code et les données en mémoire, corriger les adresses, trouver les fonctions demandées, puis donner le contrôle au programme.

Résoudre une fonction, c’est par exemple partir du nom puts, trouver son implémentation et brancher son adresse.

On reprend cette méthode pour un fichier Windows. Après le chargement, il restera à préparer ce que son code attend pendant l’exécution.

**À montrer :** parcourir les cinq opérations sans détailler leur implémentation.

**Transition :** Commençons par ce qui doit se retrouver en mémoire.

### 5 — [Ce qu’il faut mettre en mémoire pour DOOM.](my_wine.html#slide-5) · 1:15 · Temps cumulé : 5:00

Le fichier contient les instructions, des données et les informations nécessaires pour les charger. En mémoire, on place le code, on recopie les données initialisées et on met à zéro les zones qui doivent l’être.

Le programme a aussi besoin d’espace pendant son exécution. La Heap sert aux allocations dynamiques. La Stack contient notamment les arguments et les adresses de retour des appels de fonctions.

Toutes ces zones ne viennent donc pas d’une simple copie du fichier.

**À montrer :** code et données, puis Heap et Stack. Le dessin représente des rôles, pas un plan à l’échelle.

**Transition :** Comment le fichier décrit-il ce chargement ?

### 6 — [Portable Executable (PE)](my_wine.html#slide-6) · 1:00 · Temps cumulé : 6:00

Le format s’appelle Portable Executable, ou PE. DOOM95 utilise la variante PE32.

Les en-têtes indiquent notamment l’architecture, la base préférée et le point d’entrée. Les sections décrivent le code et les données. Les répertoires permettent de retrouver les tables d’imports et de relocalisation.

Avec ELF, le format Linux, on cherchait déjà des informations de ce genre. Les structures diffèrent, mais elles répondent à des questions proches.

**À montrer :** les trois groupes, puis une correspondance dans le tableau.

**Transition :** Il faut maintenant convertir ces indications en adresses dans le processus.

### 7 — [Construire une image cohérente.](my_wine.html#slide-7) · 1:00 · Temps cumulé : 7:00

Une RVA est un déplacement depuis le début de l’image en mémoire. Ici, la base est 0x00500000 et le déplacement vaut 0x2000 : on obtient 0x00502000.

Si l’image n’est pas placée à sa base préférée, il faut aussi corriger les pointeurs désignés par les relocalisations. Dans l’exemple du bas, le décalage vaut 0x00100000.

Enfin, les zones reçoivent leurs permissions de lecture, d’écriture et d’exécution.

**À montrer :** faire l’addition une fois. Distinguer le déplacement dans le fichier de l’adresse dans l’image.

**Transition :** Le programme est en mémoire. Où trouver les fonctions qui lui manquent ?

### 8 — [Brancher les fonctions manquantes.](my_wine.html#slide-8) · 1:00 · Temps cumulé : 8:00

Le programme demande WriteFile dans kernel32.dll. Une DLL est une bibliothèque liée dynamiquement. L’ILT décrit les fonctions attendues.

**[→ Afficher la résolution.]** Le chargeur cherche une implémentation correspondant à cette demande.

**[→ Afficher l’IAT.]** Il écrit l’adresse trouvée dans l’IAT, la table d’adresses utilisée par le programme. Quand celui-ci appelle WriteFile, il passe par ce pointeur.

Un nom est devenu une adresse appelable. Ici, « handler » désigne simplement la fonction qui traite la demande.

**Transition :** Dans my_wine, cette adresse peut venir de deux endroits.

### 9 — [Où trouve-t-on les fonctions des DLL ?](my_wine.html#slide-9) · 1:30 · Temps cumulé : 9:30

Avec ld.so, puts venait d’une bibliothèque partagée, libc.so. Pour simplifier my_wine, nos fonctions Windows sont compilées directement dans le runtime.

Une table interne associe kernel32.dll et WriteFile à notre fonction C. Pour cette fonction, il n’y a donc pas de fichier kernel32.dll de Windows à charger.

Le projet possède aussi un chargeur de DLL. Une DLL du programme peut être placée en mémoire avec ses dépendances ; sa table d’exports permet de retrouver ses fonctions.

Dans les deux cas, on obtient une adresse pour l’IAT.

**À montrer :** les deux origines possibles, puis le résultat commun.

**Transition :** Une fois ces adresses trouvées, peut-on simplement sauter au point d’entrée ?

### 10 — [L’image est prête. Le programme aussi ?](my_wine.html#slide-10) · 1:00 · Temps cumulé : 10:30

Nous savons où se trouvent les instructions et où commencer l’exécution. Mais le premier appel de fonction aura besoin d’une Stack valide.

Le programme peut aussi lire des informations sur son thread ou son processus. Et lorsqu’il appelle nos fonctions, il attend les mêmes règles que sous Windows pour les arguments et les résultats.

Le saut au point d’entrée ne prépare rien de tout cela. C’est au chargeur de le faire avant.

**À montrer :** laisser une pause entre la colonne du chargement et celle de l’exécution.

**Transition :** Pour expliquer ces règles, il nous faut trois registres.

### 11 — [Les cases de travail du processeur.](my_wine.html#slide-11) · 1:00 · Temps cumulé : 11:30

Un registre est une petite case de travail dans le processeur. EAX contient notamment des résultats, EIP l’adresse de l’instruction à exécuter, et ESP l’adresse du sommet de la Stack.

Dans mov eax, [esp], les crochets demandent de lire en mémoire. On prend l’adresse contenue dans ESP, on lit quatre octets à cet endroit, puis on place leur valeur dans EAX.

Nous suivons les conventions cdecl et stdcall en 32 bits : dans ces exemples, les arguments passent par la Stack.

**À montrer :** ESP, puis les crochets de l’instruction.

**Transition :** Regardons ce que fait un appel à cette Stack.

### 12 — [Après le retour, l’argument reste.](my_wine.html#slide-12) · 1:30 · Temps cumulé : 13:00

On empile 42 : ESP diminue de quatre et pointe sur cet argument.

**[→ Afficher l’appel.]** call empile l’adresse de l’instruction à reprendre après l’appel, puis saute dans la fonction. ESP pointe maintenant sur cette adresse de retour.

**[→ Afficher le retour.]** ret dépile cette adresse et reprend l’exécution à cet endroit. L’instruction ret est dans le code ; seule l’adresse était sur la Stack.

Mais 42 est encore là. Ce ret sans opérande a retiré l’adresse de retour, pas l’argument. Si personne ne retire l’argument, ou si les deux côtés le retirent, ESP finit au mauvais endroit.

**Transition :** Qui doit s’en charger ? C’est une règle de la convention d’appel.

### 13 — [Même fonction, même convention d’appel.](my_wine.html#slide-13) · 1:15 · Temps cumulé : 14:15

cdecl et stdcall sont deux conventions d’appel. Pour le nettoyage des arguments, cdecl donne la responsabilité à l’appelant ; stdcall à la fonction appelée.

DOOM95 est déjà compilé. Quand notre fonction remplace une fonction Windows, c’est à nous de conserver la convention qu’il attend.

WriteFile utilise stdcall en 32 bits. L’attribut affiché indique cette convention au compilateur de my_wine. Avec ses cinq arguments de quatre octets, il génère un retour qui retire aussi les vingt octets d’arguments.

Il faut faire correspondre la signature et la convention de chaque fonction remplacée.

**À montrer :** la définition des conventions en haut, puis l’attribut du snippet.

**Transition :** Le programme peut aussi obtenir des informations sans appeler de fonction.

### 14 — [Le programme lit aussi son environnement en mémoire.](my_wine.html#slide-14) · 1:00 · Temps cumulé : 15:15

Windows met à disposition des structures en mémoire : le TEB décrit le thread, et le PEB décrit le processus. Le TEB contient un pointeur vers le PEB.

En x86 32 bits, le code Windows peut accéder au TEB avec FS. FS contient un sélecteur de segment ; la base associée à ce segment conduit au TEB. Ce n’est pas ESP, et ce n’est pas directement l’adresse de la Stack.

my_wine prépare ces données et configure FS avant le lancement du code Windows.

**À montrer :** FS → TEB → PEB, puis les exemples d’informations.

**Transition :** Voyons le contenu sous une forme C simplifiée.

### 15 — [Zoom sur le PEB et le TEB](my_wine.html#slide-15) · 1:00 · Temps cumulé : 16:15

Dans le PEB, ImageBaseAddress pointe vers l’image du programme. ProcessHeap désigne l’objet qui gère sa Heap. ProcessParameters mène aux paramètres de lancement.

Dans le TEB, on retrouve le pointeur vers le PEB, une référence au TEB lui-même, les gestionnaires d’exceptions et, dans notre projet, les variables d’environnement sous forme de chaînes CLÉ=valeur.

À droite, CommandLine utilise un descripteur de chaîne : Buffer pointe sur les caractères, Length compte les octets de texte et MaximumLength donne la capacité du tampon. Pour DOOM95.EXE, dix caractères ASCII deviennent vingt octets en UTF-16 ; avec le zéro final, le tampon en contient vingt-deux.

**À montrer :** suivre les champs dans cet ordre. Les deux extraits .h sont pédagogiques et omettent des champs et des offsets.

**Transition :** L’environnement est préparé. Suivons maintenant un appel à WriteFile.

### 16 — [De WriteFile sous Windows à write sous Linux.](my_wine.html#slide-16) · 1:15 · Temps cumulé : 17:30

Le jeu appelle WriteFile avec un handle, un tampon et un nombre d’octets. Le handle est un identifiant de ressource Windows.

L’IAT amène l’appel dans notre fonction. Elle retrouve le descripteur de fichier Linux correspondant, puis effectue directement l’appel système write.

Elle doit aussi répondre comme la fonction Windows : renvoyer le succès ou l’échec et écrire le nombre d’octets effectivement transférés dans la variable fournie par le programme.

La réimplémentation traduit donc les paramètres et le résultat, pas seulement le nom de la fonction.

**À montrer :** les trois blocs, puis la réponse en dessous.

**Transition :** Pour l’affichage, nous réutilisons une bibliothèque Linux. Cela introduit un autre chemin.

### 17 — [Deux Stacks: une Linux et une Windows.](my_wine.html#slide-17) · 1:15 · Temps cumulé : 18:45

La Stack du jeu est une zone réservée avec mmap. Au lancement, on y prépare les arguments et une adresse de retour. Si le point d’entrée retourne, cette adresse mène au code qui termine le processus. ESP est positionné sur ce cadre avant le saut au programme.

Pour les appels aux bibliothèques Linux, my_wine prévoit une autre Stack. SDL2 sert notamment à afficher le jeu ; la libc fournit des fonctions comme malloc et getenv.

Pendant ces appels, la Stack du jeu reste en mémoire. Changer de Stack consiste à changer son pointeur, sans copier son contenu.

**Transition :** Quand est-ce qu’on garde la Stack du jeu, et quand est-ce qu’on bascule ?

### 18 — [Garder la Stack ou appeler une bibliothèque ?](my_wine.html#slide-18) · 1:15 · Temps cumulé : 20:00

Notre WriteFile reste sur la Stack du jeu. C’est une fonction C compilée avec la convention Windows, qui effectue un appel système direct. Elle ne passe pas par la fonction write de la libc.

Le chemin SDL2 utilise notre code de transition : sauvegarder ESP, passer sur la Stack Linux préparée, appeler la bibliothèque, puis restaurer l’état pour reprendre le jeu. Les appels malloc, free et getenv de ce backend peuvent suivre le même chemin.

Deux Stacks sont un choix du projet. Une autre Stack ne suffit pas, à elle seule, à rendre un appel correct : la convention d’appel, l’alignement et le contexte du thread comptent aussi.

**À montrer :** comparer les deux colonnes, puis les trois étapes à droite.

**Transition :** Qu’est-ce que ces bibliothèques nous permettent de faire pour DOOM ?

### 19 — [Le relais multimédia côté Linux.](my_wine.html#slide-19) · 1:00 · Temps cumulé : 21:00

SDL2 signifie Simple DirectMedia Layer, version 2. Elle nous donne les fonctions Linux pour l’affichage, les entrées et les effets sonores.

Les demandes DirectDraw deviennent des opérations sur les surfaces et la palette, jusqu’à la fenêtre. Pour le clavier et la souris, le trajet s’inverse : les événements SDL2 deviennent des messages Windows que le jeu peut traiter.

Les demandes DirectSound passent par le mélange audio et la sortie sonore. La musique a un chemin distinct, qui utilise notamment FluidSynth.

**À montrer :** suivre les flèches ; insister sur le sens inverse pour les entrées.

**Transition :** On lance le jeu.

### 20 — [Le moment de vérité.](my_wine.html#slide-20) · 3:00 · Temps cumulé : 24:00

« Voici le binaire Windows. Je le lance avec my_wine. »

**À faire :** lancer la commande préparée, ouvrir une partie, se déplacer et déclencher un effet sonore. Laisser le public regarder ; commenter seulement ce qui mérite une explication.

Les images de la slide illustrent le jeu. La démonstration se déroule dans la fenêtre lancée, pas dans ces images.

**Si le lancement bloque :** « Le lancement bloque ici. » Utiliser une capture de repli si elle a été préparée et vérifiée ; sinon revenir au schéma d’intégration. La préparation est détaillée en fin de notes.

**Transition :** Revenons aux trois choses que my_wine prend en charge.

### 21 — [Faire tourner DOOM sous Linux.](my_wine.html#slide-21) · 1:00 · Temps cumulé : 25:00

Charger les sections et les DLL, puis relier les imports. Préparer la Stack et les structures Windows. Réimplémenter les fonctions demandées par le jeu avec le noyau et les bibliothèques Linux.

On retrouve la méthode de ld.so, prolongée jusqu’aux besoins du programme pendant son exécution. Le processeur exécute le code du jeu ; my_wine fournit les fonctions Windows qu’il appelle.

Le projet couvre une partie de ces fonctions, autour de notre cible. Il reste beaucoup de travail pour prendre en charge d’autres programmes.

**À montrer :** les trois verbes, puis le parcours relié sur la carte de l’intro.

**Fin :** « Merci ! » Laisser la slide affichée pour les questions.

## Réserve

À ouvrir pendant les questions : **22**, comparaison 32/64 bits ; **23**, allocations via HeapAlloc.

### 22 — [Qu’est-ce qui change en 64 bits ?](my_wine.html#slide-22) · Réserve

Le lanceur choisit my_wine32 ou my_wine64 selon le format du programme. Les principes restent les mêmes ; la taille des pointeurs, les registres et les conventions changent.

En Microsoft x64, les quatre premiers arguments entiers ou pointeurs usuels passent par RCX, RDX, R8 et R9. La Stack existe toujours : elle reçoit notamment les adresses de retour et les arguments supplémentaires.

Pour l’état Windows du thread, on passe de FS en x86 à GS en x64. ESP/RSP sont les pointeurs de Stack ; EAX/RAX servent notamment au résultat entier d’une fonction.

**À montrer :** uniquement les lignes qui répondent à la question posée.

### 23 — [Comment le jeu alloue-t-il sa mémoire ?](my_wine.html#slide-23) · Réserve

GetProcessHeap renvoie l’objet de gestion de la Heap par défaut, aussi référencé dans le PEB. HeapAlloc lui demande ici un bloc de cent octets.

Notre implémentation vérifie la Heap puis utilise l’allocateur 32 bits. Celui-ci demande une zone à mmap, conserve sa capacité dans un en-tête de quatre octets et renvoie l’adresse juste après cet en-tête.

HeapFree retrouve la base en reculant de quatre octets, lit la capacité et appelle munmap pour libérer la zone.

C’est un choix simple : une zone distincte par allocation. Il consomme davantage de mémoire pour les petites demandes qu’un allocateur qui regroupe ses blocs.

**À montrer :** le snippet, les trois étapes, puis HeapFree. Cet exemple décrit le chemin HeapAlloc, pas toutes les allocations possibles du jeu.

## Précisions pour les questions

Ces repères complètent le texte oral ; ils n’ont pas besoin d’être présentés systématiquement.

| Slide | Point à garder exact |
|---|---|
| 7 | Une RVA se rapporte à l’image en mémoire, pas à la position des octets dans le fichier. Résolution et relocalisation peuvent être entrelacées dans le chargeur. |
| 9 | Le projet possède un chargeur de DLL et LoadLibraryA. Cela ne garantit pas que le démarrage PE32 charge automatiquement toutes les DLL absentes. Nos fonctions système sont internes ; SDL2 reste une bibliothèque Linux. |
| 11–13 | Les arguments passent par la Stack dans les conventions x86 étudiées. Ce n’est pas une conséquence universelle du « 32 bits ». Le ret sans opérande de la slide 12 ne nettoie pas les arguments. La mémoire dépilée n’est pas effacée. |
| 13 | La signature et la convention doivent correspondre des deux côtés. Le snippet développe l’attribut stdcall pour le rendre visible ; les macros du projet appliquent cet attribut à la déclaration et à la définition. Pour WriteFile, ret 20 dépile l’adresse de retour puis retire vingt octets d’arguments. |
| 14–15 | FS est un sélecteur de segment. En Windows x86, FS:[0x30] lit les quatre octets à l’offset 0x30 du TEB : ils contiennent l’adresse du PEB. Le changement de Stack ne change pas automatiquement FS. |
| 15 | Les noms .h et les déclarations sont illustratifs. Les écritures du projet sont partielles et certains offsets diffèrent du TEB Windows officiel. EnvironmentPointer désigne ici environ, un tableau de pointeurs vers des chaînes CLÉ=valeur terminé par un pointeur nul. |
| 15 | Le bloc des paramètres contient un descripteur de forme UNICODE_STRING. Length exclut le zéro final ; MaximumLength l’inclut dans notre exemple. Le code élargit les octets en unités de seize bits : l’exemple ASCII fonctionne, mais ce n’est pas une conversion Unicode générale. Cela ne démontre pas que GetCommandLineA ou DOOM lit ce descripteur. |
| 16 | WriteFile vérifie qu’un thunk existe, mais ne l’appelle pas : l’écriture utilise un appel système direct. Le paramètre d’opération asynchrone n’est pas implémenté. |
| 17–18 | Les deux zones décrites restent en espace utilisateur. Le noyau Linux possède sa propre Stack. my_wine peut aussi prévoir une Stack pour les signaux : « deux Stacks » décrit ici les deux chemins présentés. |
| 18 | Le helper SDL2 sauvegarde des registres, adapte l’alignement et gère le sélecteur de segment prévu par ce chemin. Il peut garder la Stack courante si la Stack Linux est absente. Ne pas généraliser ce helper à tous les appels libc, ni présenter FS comme le registre habituel des données de thread Linux i386. |
| 22 | La convention Microsoft x64 prévoit aussi un espace réservé par l’appelant et des règles d’alignement. Le tableau porte sur l’état Windows, pas sur les conventions Linux. |
| 23 | Le backend 32 bits arrondit la capacité au multiple de huit supérieur, avec un minimum de 4096 octets, puis demande quatre octets de plus à mmap. Le noyau arrondit le mapping aux pages : une demande de cent octets ne consomme ni seulement 104 octets, ni nécessairement une seule page. Le backend 64 bits est différent. |

## Démonstration : préparation

Depuis la racine du dépôt, la commande réelle est :

```bash
./my_wine samples/unpacked/doom95/DOOM95.EXE
```

La slide utilise la version courte, avec DOOM95.EXE dans le dossier courant. Garder une commande préparée dans le terminal pour éviter de chercher le fichier pendant la présentation.

Avant le talk, vérifier le lancement avec l’écran et la sortie audio prévus : ouvrir une partie, se déplacer, produire un son et fermer le jeu. Préparer une capture locale de repli si possible. Les deux images de la slide sont des illustrations, pas une preuve de fonctionnement de my_wine.

Si les fichiers du jeu sont absents, le script suivant les décompresse. Il peut remplacer des fichiers de destination ; cette préparation se fait avant la présentation.

```bash
./scripts/unpack_samples.sh doom95
```

Si le lancement bloque pendant une vingtaine de secondes, passer à la capture vérifiée ou revenir à la slide 19. Un exemple console peut aussi servir de repli après vérification sur la machine :

```bash
./my_wine samples/hello_world/hello_world.exe
```

**Repère pour la répétition :** un essai SDL en mode dummy peut montrer l’activité du jeu, mais ne valide pas l’affichage réel, les entrées ou le son. Vérifier ces trois points dans la fenêtre du jeu avant le talk.

## Navigation et temps

- Flèches, Espace ou Entrée : avancer. Les slides **8 et 12** demandent chacune deux avancées pour révéler tout le schéma.
- **A** ou **Réserve** : ouvrir les annexes et revenir à la slide quittée.
- **F** : plein écran ; **?** : aide ; **Échap** : fermer l’aide.
- **Début / Fin** : première ou dernière slide du parcours courant.
- Liens directs : `#slide-1` à `#slide-23`. L’impression inclut les deux réserves.

Si le temps manque, raccourcir les rappels de chargement. Garder la démo à **21:00**, revenir à la conclusion à **24:00** et terminer vers **25:00**. Les réserves restent pour les questions.

## Références de préparation

Le code du projet fait référence pour ce qui est réellement implémenté. Ces liens servent à préparer les réponses ; aucun fichier source n’est à ouvrir pendant le talk.

- [Imports et fonctions internes](../src/loader/import_lookup.c), [chargement des DLL](../src/loader/dll_loader.c).
- [Conventions d’appel](../include/wine_abi.h), [WriteFile](../src/msvcrt/kernel32_console.c).
- [Initialisation du TEB/PEB](../src/loader/teb_peb.c), [paramètres PE32](../src/loader/pe32_process.c), [préparation du lancement](../src/loader/pe32_guest_launch.c), [changement initial de Stack](../src/loader/pe32_run_guest.S).
- [Appels SDL2 et contexte Linux](../src/backend/sdl2/rb_sdl2_priv.h).
- [API Heap](../src/heap/wine_heap.c), [allocateur 32 bits](../src/heap/pe32_mmap_heap_backend.c).
- [Proton — Valve](https://github.com/ValveSoftware/Proton), [format PE — Microsoft](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), [stdcall — Microsoft](https://learn.microsoft.com/en-us/cpp/cpp/stdcall).

**Images de la slide 20 :** l’écran-titre est extrait de TITLEPIC avec PLAYPAL, dans DOOM1.WAD (id Software). La scène E1M1 provient de [Soulsphere](https://soulsphere.org/img/blog/doom-e1m1/E1M1-2.png). Les deux fichiers sont intégrés localement dans assets/.
