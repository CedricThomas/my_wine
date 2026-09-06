# Objectif DOOM — récit et notes orales

Notes en français pour accompagner [la présentation](my_wine.html#slide-1), dans sa version actuelle : **20 slides principales et 4 slides de réserve**.

Créneau de 30 minutes : **21 minutes d’explications, 3 minutes de démonstration, 6 minutes de questions et de marge**. Les durées sont des cibles de répétition ; elles incluent les pauses pour regarder les schémas. Le texte oral peut être adapté à ta façon de parler.

## Le fil narratif

Le point de départ est personnel et simple : l’essor de Wine et Proton a suscité l’envie de comprendre comment un programme Windows peut tourner sous Linux. DOOM95 donne une cible concrète à cette curiosité.

Le personnage que l’on suit pendant l’explication, c’est le programme : ce que contient son fichier, les adresses qu’il utilise, ce qu’il trouve au premier saut, puis les fonctions qu’il appelle. Le projet apparaît à travers les réponses qu’il faut apporter à ces besoins.

Le public connaît déjà `ld.so`. On part de ce terrain connu, puis on découvre ce que le chargement seul ne fournit pas. Les registres et la Stack sont expliqués au moment où ils deviennent nécessaires pour suivre la suite.

### Le récit en quatre temps

| Temps | Slides | Question qui fait avancer le récit | Ce que le public doit comprendre |
|---|---|---|---|
| La curiosité et la cible | 1–4 | Si le processeur comprend déjà le code, que manque-t-il ? | L’environnement attendu par le programme ; le chargement reprend une méthode connue. |
| Du fichier à l’image | 5–8 | Comment placer les octets et brancher les fonctions ? | Le fichier décrit une image que le chargeur peut reconstruire. |
| Du chargement à l’exécution | 9–17 | Que trouve le programme quand il commence à travailler ? | Stack, registres, état Windows, conventions d’appel et traitement des demandes. |
| Des appels au jeu | 18–20 | Comment ces mécanismes produisent-ils une image, des entrées et du son ? | Les services s’assemblent ; la démonstration vérifie leur fonctionnement conjoint. |

**Le pivot est la slide 9.** Marquer une pause entre « le chargement est prêt » et « l’exécution reste à préparer ». C’est ce changement de question qui justifie la deuxième moitié du talk.

### Comment garder le public avec toi

Reprendre régulièrement une question concrète : « Qu’est-ce que le programme attend maintenant ? » Une explication technique doit répondre à ce besoin avant de nommer les structures qui y répondent.

La slide 4 annonce la méthode ; les slides 5 à 8 en détaillent l’application. La 5 sépare ce que le fichier décrit des zones à préparer pendant l’exécution. La 9 fait apparaître les besoins suivants. Les slides 10 et 11 donnent les outils pour comprendre les conventions et le changement de Stack.

Les slides 14 à 16 montrent deux chemins présents dans le projet : le handler direct de `WriteFile`, puis l’infrastructure de thunk et de dispatcher. Dire explicitement que ce sont des chemins distincts, pour ne pas faire croire que chaque appel du jeu traverse le même mécanisme.

L’arrivée du multimédia se prépare à la fin de la slide 17 : annoncer l’image, les touches et le son. La slide 18 présente alors la bibliothèque choisie pour y répondre. La 19 reprend exactement ces trois besoins comme critères de démonstration.

### Ton et repères visuels

Parler comme à des collègues : phrases courtes, vocabulaire concret, une raison avant chaque mécanisme. L’introduction donne le contexte du projet ; inutile de la jouer comme une citation. Ne pas inventer d’anecdote de débogage ou de résultat de démonstration.

Le parcours reste en **32 bits** jusqu’à la réserve. Utiliser **Stack**, **Heap** et **relocaliser**. Développer les termes complets avant les acronymes, y compris ceux déjà rencontrés avec `ld.so`. Pour les noms de registres, expliquer leur rôle.

L’ambre repère généralement les attentes Windows ; le turquoise, Linux ou les éléments acquis. Les libellés et les numéros donnent toujours le sens local du schéma. Le pied de page indique l’avancement du récit, pas l’état de tests exécutés en direct.

Les indications **[→]** accompagnent les trois schémas progressifs : slides 8, 11 et 16. Elles ne sont pas à prononcer.

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
| 9 | L’image est prête. Le programme aussi ? | 1:00 | 9:00 |
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

## Notes orales — parcours principal

Les paragraphes « À dire » sont une proposition de formulation, pas un texte à réciter mot pour mot. Les indications « À montrer » restent pour toi. Les transitions peuvent être dites avant d’avancer à la slide suivante.

### 01 — [Objectif DOOM](my_wine.html#slide-1) · 0:45

**Idée à faire passer :** donner tout de suite un objectif concret au public.

**À dire**

Aujourd’hui, le point d’arrivée est assez simple : lancer la version Windows de DOOM sur Linux, avec un petit programme que j’ai écrit pour comprendre ce qui se passe entre les deux.

La cible, c’est DOOM95, un exécutable Windows en 32 bits. On va suivre le chemin depuis ce fichier jusqu’au jeu : comment charger ses instructions, préparer ce qu’elles attendent, puis leur permettre d’utiliser Linux.

La carte donne ce parcours. On va construire les pièces une par une.

**À montrer :** le nom du jeu, la commande, puis le chemin sur la carte. Laisser une courte pause sur l’objectif, sans détailler les cinq étapes.

**Transition :** Avant de regarder le code, je vous explique rapidement d’où vient ce projet.

### 02 — [D’où vient ce projet ?](my_wine.html#slide-2) · 1:00

**Idée à faire passer :** une curiosité personnelle a donné une direction au projet.

**À dire**

Avec la montée en popularité de Wine et Proton, j’ai eu envie de regarder comment un programme Windows pouvait tourner sous Linux.

Wine, dont le nom signifie « Wine Is Not an Emulator », fournit une couche de compatibilité avec Windows. Proton est l’outil de Valve qui s’appuie sur Wine et d’autres composants pour les jeux.

Je voulais comprendre les mécanismes derrière ça. J’ai donc commencé à en reconstruire une petite partie, dans un projet assez limité pour pouvoir suivre les appels.

DOOM95 donne un objectif concret à cette exploration. Il faut que plusieurs choses fonctionnent ensemble : charger le programme, afficher une image et réagir aux touches.

**À montrer :** Wine → Proton → projet personnel. Garder un ton de conversation ; la slide raconte le contexte, sans effet de citation.

**Transition :** La première question, c’est donc : qu’est-ce qui empêche ce programme Windows de tourner directement ?

### 03 — [Les instructions savent déjà tourner](my_wine.html#slide-3) · 1:00

**Idée à faire passer :** situer la difficulté dans l’environnement attendu par le programme.

**À dire**

Sur une machine x86 compatible avec le 32 bits, le processeur comprend déjà les instructions de DOOM95.

L’assembleur, à gauche, est une écriture lisible du code machine. Ici, on place 42 dans une case de travail du processeur, appelée EAX, puis on ajoute 1. On reviendra sur ces cases, les registres, un peu plus loin.

Ce calcul ne dépend pas d’une fonction Windows. En revanche, le programme dépend de Windows pour être chargé et pour obtenir certains services.

Il nous reste donc trois choses à construire : charger son fichier, préparer sa mémoire et fournir les fonctions Windows qu’il utilise. C’est là que commence le travail de compatibilité.

**À montrer :** lire les deux instructions, puis les trois besoins à droite. Ne pas généraliser l’exécution native à une machine d’une autre architecture.

**Transition :** Pour charger le fichier, on a déjà rencontré une bonne partie de la méthode avec `ld.so`.

### 04 — [ld.so nous a donné la méthode](my_wine.html#slide-4) · 1:00

**Idée à faire passer :** réutiliser les connaissances du précédent talk.

**À dire**

On repart du parcours de `ld.so`, le chargeur dynamique Linux.

Lire, c’est repérer les informations du fichier. Mapper, c’est rendre le code et les données accessibles en mémoire. Relocaliser, c’est corriger les pointeurs concernés lorsqu’on place l’image à une autre adresse.

Résoudre mérite une précision : le programme demande une fonction par son nom, par exemple `puts`. Le chargeur trouve où se trouve cette fonction et branche son adresse.

Enfin, on donne le contrôle au point d’entrée du programme.

On va réutiliser cette méthode. Mais pour DOOM95, il faudra aussi fournir l’environnement Windows que le programme s’attend à trouver après ce premier saut.

**À montrer :** les cinq étapes numérotées, puis l’exemple `puts`.

**Repère technique :** le parcours est simplifié. Certaines relocalisations utilisent le résultat de la résolution d’un symbole ; les deux opérations peuvent être entrelacées dans le code.

**Transition :** Arrêtons-nous sur la mémoire : qu’est-ce que nous devons y construire pour DOOM ?

### 05 — [Ce qu’il faut mettre en mémoire pour DOOM](my_wine.html#slide-5) · 1:15

**Idée à faire passer :** distinguer ce qu’on charge selon le fichier de ce qu’on prépare pour l’exécution.

**À dire**

À gauche, on a le fichier sur disque. À droite, les différentes zones que le programme va utiliser dans son processus.

Le premier groupe vient des indications du fichier : les instructions du jeu et ses données. Certaines données ont une valeur initiale dans le fichier ; d’autres doivent simplement être mises à zéro.

Le deuxième groupe sert pendant l’exécution. Le Heap permet au programme de demander de la mémoire au fil de ses besoins. La Stack sert notamment à organiser les appels de fonctions et leurs retours.

Charger les instructions ne crée donc pas, à lui seul, tout l’espace de travail du jeu.

Pour l’instant, on s’occupe du code et des données. On reviendra sur la Stack au moment de donner le contrôle au programme.

**À montrer :** fichier → groupe ambre → groupe turquoise. Les groupes représentent des rôles, pas des adresses réelles ni des zones nécessairement contiguës.

**Repère technique :** le Heap peut obtenir de nouvelles zones pendant l’exécution. La distinction mémoire virtuelle/physique reste en réserve pour ne pas ajouter un second sujet ici.

**Transition :** Où le fichier indique-t-il ce qu’on doit charger, et à quel endroit ?

### 06 — [Portable Executable (PE)](my_wine.html#slide-6) · 1:00

**Idée à faire passer :** le nouveau format fournit une carte dont le rôle est déjà familier.

**À dire**

Ces indications sont dans le format Portable Executable, qu’on abrège PE. DOOM95 utilise sa variante 32 bits, PE32.

Dans le talk précédent, on avait l’Executable and Linkable Format, ou ELF. Les structures changent, mais les questions du chargeur restent proches.

Les en-têtes décrivent notamment l’architecture, la base préférée et le point d’entrée. Les sections décrivent le code et les données à placer. Les répertoires de données permettent de retrouver des tables utiles, comme les imports ou les corrections d’adresses.

Le tableau donne des correspondances de rôle. Il aide à retrouver nos repères dans cette nouvelle carte.

**À montrer :** les trois éléments à gauche, puis une seule comparaison à droite. Développer ELF oralement avant de le nommer dans le tableau.

**Transition :** Une fois cette carte lue, il faut transformer ses indications en vraies adresses.

### 07 — [Construire une image cohérente](my_wine.html#slide-7) · 1:00

**Idée à faire passer :** relier la base de l’image aux adresses utilisées par le programme.

**À dire**

Une notion revient partout : Relative Virtual Address, ou RVA. C’est un déplacement depuis le début de l’image en mémoire.

Dans l’exemple, on place l’image à la base 0x00500000. La section est à un déplacement de 0x2000. Son adresse devient donc 0x00502000. Le préfixe 0x indique que les nombres sont écrits en base 16.

Si la base réelle diffère de celle prévue, certains pointeurs doivent être corrigés. Les informations de relocalisation indiquent lesquels.

Le chargeur réserve l’espace, place les sections, corrige les adresses concernées et configure les permissions de lecture, d’écriture et d’exécution.

**À montrer :** faire une seule addition. Pointer ensuite les opérations à droite.

**Repère technique :** un offset dans le fichier et une RVA ne sont pas interchangeables. Le schéma regroupe les responsabilités ; le code peut effectuer plusieurs passes.

**Transition :** Le code du jeu est placé. Mais certaines fonctions qu’il appelle ne sont pas dans ce fichier.

### 08 — [Brancher les fonctions manquantes](my_wine.html#slide-8) · 1:00

**Idée à faire passer :** rendre concrète la résolution d’un nom en adresse.

**À dire**

Le programme peut demander une fonction dans une Dynamic-Link Library, ou DLL : une bibliothèque liée dynamiquement. Ici, il demande `WriteFile` dans `kernel32.dll`.

L’Import Lookup Table, ou ILT, décrit cette demande.

**[→ Faire apparaître la résolution.]**

Le chargeur cherche une implémentation. Dans `my_wine`, elle peut être une fonction que j’ai écrite, ou une fonction d’une bibliothèque chargée.

**[→ Faire apparaître l’adresse branchée.]**

L’Import Address Table, ou IAT, reçoit l’adresse trouvée. Le programme peut maintenant appeler cette adresse.

C’est le principe que vous avez vu avec `puts`. On appelle « handler » la fonction qui traite la demande. On suivra tout à l’heure ce que fait le handler de `WriteFile`.

**À montrer :** trois états, deux pressions sur →. Ne pas expliquer les stubs ici ; ils sont en réserve.

**Transition :** On a chargé l’image et branché les fonctions. Est-ce que cela suffit pour démarrer ?

### 09 — [L’image est prête. Le programme aussi ?](my_wine.html#slide-9) · 1:00

**Idée à faire passer :** marquer le passage du chargement à la préparation de l’exécution.

**À dire**

À gauche, le travail de chargement est prêt : les instructions sont en mémoire, les adresses sont corrigées et on connaît le point d’entrée.

On pourrait être tenté de sauter directement à cette adresse.

Mais regardons ce que le programme va faire ensuite. Dès qu’il appelle une fonction, il lui faut une Stack valide. Il peut aussi lire des informations sur son thread ou son processus. Et ses appels doivent suivre les règles qu’attendent les fonctions Windows.

Ce sont les trois éléments de droite.

Donner le contrôle au programme ne crée pas automatiquement tout cela. Il faut préparer cet état avant de le laisser s’exécuter.

**À montrer :** colonne gauche, courte pause, puis colonne droite. C’est le pivot du récit ; laisser le public voir ce qui manque encore.

**Transition :** Pour comprendre cet état, il nous faut d’abord quelques repères sur les registres du processeur.

### 10 — [Les cases de travail du processeur](my_wine.html#slide-10) · 1:00

**Idée à faire passer :** distinguer une valeur dans un registre de la mémoire qu’elle peut désigner.

**À dire**

Un registre est une petite case de travail directement dans le processeur. Il peut contenir une valeur ou une adresse.

On en utilise trois ici. EAX peut contenir une valeur de travail ou le résultat d’un appel. EIP suit les instructions à exécuter. ESP contient l’adresse du sommet de la Stack.

Regardons `mov eax, [esp]`. Les crochets veulent dire qu’on accède à la mémoire. On prend l’adresse contenue dans ESP, on lit quatre octets à cette adresse, puis on place la valeur lue dans EAX.

ESP contient donc une adresse. La Stack, elle, est une zone de mémoire.

**À montrer :** chaque registre, puis les trois étapes de lecture à droite. Prononcer les noms comme des identifiants ; ne pas inventer de développement de sigle.

**Transition :** Voyons maintenant comment ce pointeur bouge lorsqu’on appelle une fonction.

### 11 — [Appeler, c’est aussi préparer le retour](my_wine.html#slide-11) · 1:30

**Idée à faire passer :** un appel doit conserver l’adresse à laquelle revenir.

**À dire**

La Stack est une zone mémoire utilisée pendant les appels. Sur x86, lorsqu’on empile une valeur de quatre octets, son pointeur diminue de quatre.

Dans le premier dessin, on vient d’empiler 42. ESP pointe sur cette valeur, à l’adresse 0x0FFC.

**[→ Montrer l’appel.]**

L’instruction `call` fait deux choses : elle empile l’adresse de l’instruction à reprendre après l’appel, puis elle transfère l’exécution dans la fonction. ESP est maintenant à 0x0FF8.

**[→ Montrer le retour.]**

À la fin, `ret` récupère cette adresse et permet de reprendre l’exécution de l’appelant. ESP remonte à 0x0FFC.

Remarquez que 42 est encore sur la Stack. Le retour a retiré son adresse, mais pas cet argument. Quelqu’un doit encore s’en occuper.

**À montrer :** suivre ESP dans les trois dessins. Les adresses diminuent vers le bas. Laisser une pause à chaque apparition.

**Repère technique :** il s’agit de `ret` sans opérande. L’exemple omet le prologue et les variables locales. « Libérée » signifie disponible pour réutilisation, pas effacée ni rendue au système.

**Transition :** Qui retire cet argument ? Cela dépend d’une convention que les deux côtés doivent respecter.

### 12 — [Une bonne adresse ne suffit pas](my_wine.html#slide-12) · 1:15

**Idée à faire passer :** appeler la bonne fonction suppose aussi de lui transmettre correctement les données.

**À dire**

L’Application Binary Interface, ou ABI, désigne le contrat binaire entre les morceaux de code. Une partie de ce contrat concerne les appels de fonctions.

Où sont les arguments ? Où revient le résultat ? Quels registres doivent être conservés ? Qui remet la Stack en ordre ?

Dans cet exemple Windows 32 bits, l’adresse de retour est au sommet de la Stack, puis viennent les arguments.

Avec la convention `__stdcall`, la fonction appelée retire ses arguments. Avec `__cdecl`, c’est l’appelant qui le fait.

Si les deux côtés n’appliquent pas les mêmes règles, on peut atteindre la bonne adresse et quand même mal interpréter les arguments, ou revenir avec une Stack incorrecte.

**À montrer :** adresse de retour → arguments → règles à droite.

**Repère technique :** le dessin concerne un appel de fonction, pas toute la préparation du point d’entrée. Les arguments de ces conventions sont empilés de droite à gauche ; un résultat entier simple revient généralement dans EAX.

**Transition :** Les appels ont maintenant leurs règles. Le programme attend aussi de retrouver des informations Windows dans sa mémoire.

### 13 — [Windows existe aussi dans la mémoire](my_wine.html#slide-13) · 1:00

**Idée à faire passer :** certaines attentes Windows sont de simples lectures mémoire, avant tout appel de fonction.

**À dire**

Un thread est un fil d’exécution dans un processus. Il a notamment sa Stack et son état de registres.

Windows donne accès à des informations sur ce thread grâce à une structure appelée Thread Environment Block, ou TEB. Une autre structure, le Process Environment Block, ou PEB, décrit l’état du processus.

En 32 bits, le registre de segment FS permet de retrouver l’état du thread. Dans l’exemple, `FS:[0x30]` lit le pointeur vers le PEB.

Le programme peut faire cette lecture directement. Il faut donc que `my_wine` ait préparé ces structures et le mécanisme d’adressage qu’il attend.

**À montrer :** base du segment → état du thread → état du processus.

**Repère technique :** FS est un registre de segment ; sa base participe au calcul de l’adresse. Ce n’est pas un registre général contenant directement le pointeur vers le PEB.

**Transition :** Nous avons préparé l’état du programme. Suivons maintenant une vraie demande, avec `WriteFile`.

### 14 — [Écrire des octets : deux contrats](my_wine.html#slide-14) · 1:15

**Idée à faire passer :** traduire un appel comprend ses paramètres, ses objets et son résultat.

**À dire**

Une Application Programming Interface, ou API, est un ensemble de fonctions proposées au programme. `WriteFile` fait partie des fonctions Windows.

Le programme Windows, qu’on appelle ici l’invité, lui transmet notamment un handle. C’est un identifiant de ressource Windows. Il fournit aussi les octets à écrire et leur nombre.

Dans `my_wine`, le handler retrouve le descripteur Linux correspondant à ce handle. Il demande ensuite au noyau Linux d’écrire les octets.

Au retour, il faut encore répondre au format Windows : signaler si l’opération a réussi et renseigner le nombre d’octets écrits.

Ce chemin est concret : le handler utilisé ici appelle directement Linux. On adapte la demande, puis la réponse.

**À montrer :** le trajet de gauche à droite, puis le bandeau de retour. Définir « hôte » comme le côté Linux.

**Repère technique, hors texte oral :** ce handler vérifie l’existence d’un thunk mais ne l’exécute pas ; il émet un appel système Linux direct. Le paramètre d’opération asynchrone `lpOverlapped` n’y est pas implémenté.

**Transition :** Le projet contient aussi un autre chemin, qui passe par un répartiteur commun. Regardons sa porte d’entrée.

### 15 — [Un thunk : 15 octets pour passer la main](my_wine.html#slide-15) · 1:30

**Idée à faire passer :** séparer le petit adaptateur d’entrée du traitement réel du service.

**À dire**

Cette porte d’entrée s’appelle un thunk : un petit morceau de code généré qui transmet un identifiant à un répartiteur commun, le dispatcher.

Dans le projet, la version 32 bits montrée ici tient en quinze octets.

On préserve d’abord un registre, EBP. On place l’adresse du dispatcher dans EAX. Puis on met dans EDX le numéro du service demandé. On appelle le dispatcher, on restaure le registre sauvegardé et on revient à l’appelant.

Les services concernés appartiennent à la famille Windows New Technology, ou NT. Leur numéro sert ici à choisir un traitement interne. Il n’est pas envoyé tel quel comme numéro d’appel système Linux.

Le thunk sait donc où transmettre la demande et comment l’identifier. C’est le handler sélectionné qui réalisera l’adaptation du service.

**À montrer :** les six instructions dans l’ordre. Les largeurs de la barre correspondent à 1 + 5 + 5 + 2 + 1 + 1 octets.

**Repère technique :** beaucoup d’imports courants rejoignent directement leur handler. Ne pas laisser entendre que chaque appel de DOOM passe par ce thunk, ni qu’il intercepte toutes les instructions d’appel système.

**Transition :** Une fois dans le dispatcher, comment peut-on travailler côté Linux puis retrouver exactement l’appelant ?

### 16 — [Un appel. Deux Stacks. Un retour.](my_wine.html#slide-16) · 1:30

**Idée à faire passer :** suivre l’aller-retour dans un seul processus, avec deux Stacks.

**À dire**

Le temps se lit de gauche à droite. La zone du haut correspond au contexte du programme Windows ; celle du bas au travail côté Linux.

On entre depuis le thunk. Le dispatcher sauvegarde les registres nécessaires et la valeur d’ESP, notre pointeur de Stack.

**[→ Faire apparaître la descente et les étapes 3–4.]**

Il charge ensuite un autre pointeur de Stack, préparé pour le travail côté Linux. Sur cette Stack, il peut décoder la demande et appeler le traitement approprié.

**[→ Faire apparaître le retour, étape 5.]**

Une fois le traitement terminé, il reprend le pointeur sauvegardé, restaure l’état nécessaire et rend le résultat dans EAX.

Les deux Stacks sont restées en place. On a changé le pointeur utilisé, sans recopier leur contenu et sans créer un deuxième processus. Le thunk termine ensuite le retour vers l’appelant.

**À montrer :** suivre la ligne continue, les deux changements de niveau, puis l’étape 5. Les bandes restent visibles pendant les trois états.

**Repère technique :** cette slide représente le dispatcher 32 bits du projet. Les ponts vers les bibliothèques multimédias disposent aussi de leur propre gestion de contexte.

**Transition :** Le chemin aller-retour est prévu. Mais chaque passage impose encore quelques conditions.

### 17 — [Chaque passage a ses conditions](my_wine.html#slide-17) · 1:00

**Idée à faire passer :** poser les limites utiles avant l’intégration multimédia.

**À dire**

D’abord, une demande Windows ne peut pas être envoyée brute à Linux : les numéros, les arguments et les objets n’y ont pas le même sens.

Ensuite, les bibliothèques Linux ont besoin d’un contexte adapté. Les ponts du projet gèrent notamment la Stack et les segments selon l’architecture. Cela concerne aussi la bibliothèque standard C, appelée `libc`.

Enfin, si une instruction provoque une faute, c’est Linux qui la reçoit d’abord. Le projet permet le diagnostic, mais ne reconstruit pas tout le mécanisme d’exceptions Windows.

On sait donc mieux ce que chaque passage doit assurer, et ce qui reste incomplet.

**À montrer :** une phrase par colonne. Ne pas détailler les exceptions ici.

**Transition :** DOOM a maintenant besoin d’une image, des touches et du son. Quelle bibliothèque peut nous fournir ces services côté Linux ?

### 18 — [Le relais multimédia côté Linux](my_wine.html#slide-18) · 1:00

**Idée à faire passer :** SDL2 répond aux besoins annoncés juste avant.

**À dire**

Pour ces besoins, le projet utilise Simple DirectMedia Layer, version 2, abrégé SDL2. C’est le relais multimédia côté Linux.

Pour l’image, `my_wine` adapte les demandes DirectDraw du jeu en opérations sur des surfaces et une palette, puis les transmet à cette bibliothèque.

Pour les entrées, le trajet s’inverse : le clavier ou la souris produisent des événements côté Linux, qui deviennent des messages compréhensibles par le programme Windows.

Pour les effets sonores, les demandes DirectSound rejoignent le mélange audio et la sortie sonore.

La musique suit un chemin distinct, qui utilise notamment FluidSynth. Ces services doivent fonctionner avec le chargement et l’état du programme que nous avons préparés.

**À montrer :** image vers la droite, entrées vers la gauche, effets sonores vers la droite.

**Transition :** On peut maintenant vérifier trois choses concrètes : voir une image, déplacer le joueur et entendre un effet.

### 19 — [Le moment de vérité](my_wine.html#slide-19) · 3:00

**Idée à faire passer :** relier chaque observation de la démonstration à un mécanisme expliqué.

**À dire avant le lancement**

On va lancer le programme Windows avec `my_wine`. Le but est de vérifier ensemble les pièces qu’on vient de parcourir.

**À faire et à dire selon le résultat**

- Lancer la commande préparée. Dès qu’une image apparaît : « L’image passe par la chaîne d’affichage que nous venons de voir. »
- Entrer dans une partie et déplacer le joueur : « Les événements reviennent maintenant jusqu’au code du jeu. »
- Produire un effet sonore, si la sortie audio fonctionne : « Ici, c’est le chemin du son. »

Laisser le public observer ; ne pas remplir les trois minutes de commentaire.

**Si le lancement échoue**

Le lancement bloque ici. Je passe à la capture préparée pour montrer le résultat attendu, puis on revient au schéma.

Utiliser cette phrase seulement si une capture locale vérifiée existe. Sinon : « Je reviens au schéma : il montre les frontières à examiner pour diagnostiquer ce blocage. » Le petit exécutable de repli est détaillé plus bas.

**Transition si le jeu fonctionne :** Ce qu’on vient de voir, c’est le résultat de tous ces contrats qui fonctionnent ensemble.

**Transition sinon :** Nous avons maintenant une méthode pour situer ce qui manque entre le fichier et l’exécution.

**Repère pratique :** les slides affichent la commande courte `./my_wine ./DOOM95.EXE`. À la racine du dépôt, utiliser le chemin réel indiqué dans la section de préparation. Ne pas déplacer les fichiers ou improviser leur configuration en direct.

### 20 — [Charger les octets. Reconstruire le monde qu’ils attendent.](my_wine.html#slide-20) · 1:00

**Idée à faire passer :** refermer le récit sur la curiosité de départ et les mécanismes désormais visibles.

**À dire**

Au départ, je voulais comprendre comment un programme Windows pouvait tourner sous Linux.

On a suivi le fichier, reconstruit son image en mémoire, préparé sa Stack et son état Windows, puis regardé comment ses demandes pouvaient être traitées côté Linux.

`ld.so` nous avait donné la méthode pour charger et relier le programme. Ce projet prolonge cette méthode vers les attentes du programme pendant son exécution.

DOOM95 nous donne une cible pour vérifier que ces pièces tiennent ensemble. La couverture reste limitée, et cela ne signifie pas que n’importe quel exécutable Windows fonctionnera.

Mais on peut maintenant suivre les mécanismes derrière un appel, un retour ou une image affichée.

**À montrer :** parcourir une dernière fois le schéma complet, puis regarder le public.

**Sortie :** Merci. On peut revenir sur une étape, ou regarder l’un des détails en réserve.

## Notes orales — les quatre slides de réserve

Ouvrir ces slides en réponse à une question, avec A ou le bouton Réserve. Elles sont hors des 24 minutes du parcours principal.

### 21 — [DOOM95 suit la colonne 32 bits](my_wine.html#slide-21) · Réserve A

**Question à laquelle répondre :** pourquoi deux exécutables Linux, et que change le 64 bits ?

**À dire**

Le lanceur lit le type du fichier et choisit `my_wine32` ou `my_wine64`. Le programme Windows s’exécute ainsi dans un processus de la bonne largeur.

Le principe reste le même, mais les détails changent : taille des pointeurs, registres de Stack et de résultat, accès à l’état du thread et règles d’appel.

Pour les appels Microsoft x64 usuels, les quatre premiers arguments entiers ou pointeurs passent par RCX, RDX, R8 et R9. Le parcours 32 bits de notre présentation utilisait la Stack.

La séquence de thunk est elle aussi différente : quinze octets dans notre version 32 bits, vingt-trois dans la version 64 bits.

**À montrer :** comparer uniquement les lignes liées à la question. Garder DOOM95 dans la colonne 32 bits.

**Repère technique :** la convention x64 a aussi des règles d’alignement, un espace réservé par l’appelant et des règles pour les autres types d’arguments ; le tableau n’est pas une spécification complète.

**Retour au récit :** Les détails changent, mais la question reste la même : quel état le programme s’attend-il à trouver ?

### 22 — [Une faute arrive d’abord à Linux](my_wine.html#slide-22) · Réserve B

**Question à laquelle répondre :** que se passe-t-il quand le code Windows provoque une faute ?

**À dire**

Le code Windows s’exécute dans un processus Linux. S’il accède à une adresse interdite, le processeur déclenche une faute, puis Linux transmet un signal au processus, par exemple SIGSEGV pour une violation mémoire.

Une Stack alternative permet au gestionnaire de signal de travailler même si la Stack du programme est compromise.

Windows attend un autre mécanisme : Structured Exception Handling, ou SEH, les exceptions structurées. Le projet ne fournit pas toute la traduction entre ces mécanismes.

Il permet donc de diagnostiquer certaines erreurs, sans garantir que tous les programmes pourront les traiter comme ils le feraient sous Windows.

**À montrer :** accès invalide → signal Linux → diagnostic. Définir SEH avant d’utiliser son sigle.

**Retour au récit :** Le signal donne un point de diagnostic ; reconstruire le comportement Windows demande encore du travail.

### 23 — [Le vocabulaire du passage](my_wine.html#slide-23) · Réserve C

**Question à laquelle répondre :** que signifie un terme rencontré pendant la présentation ?

**À dire**

Cette slide sert de repère. On peut prendre le terme qui pose problème et le raccrocher à l’étape où il intervient.

Pour les imports, par exemple, une table décrit les fonctions demandées ; l’autre reçoit leurs adresses une fois qu’on les a trouvées.

**Si la question porte sur la mémoire**

Random Access Memory, ou RAM, désigne la mémoire vive physique. Le programme utilise, lui, des adresses virtuelles. Linux et le matériel gèrent la correspondance entre ces adresses et les pages physiques.

Réserver un espace d’adresses ne veut pas dire que toutes ses pages sont déjà présentes en mémoire physique. Heap et Stack sont des usages de la mémoire du processus, pas deux composants matériels distincts.

**À montrer :** une ligne à la fois. Développer le terme complet avant de réutiliser le sigle ; ne pas lire tout le tableau.

**Retour au récit :** Avec cette définition, on peut revenir à l’étape où ce terme intervient.

### 24 — [Des mécanismes que l’on peut inspecter](my_wine.html#slide-24) · Réserve D

**Question à laquelle répondre :** comment distinguer handler, stub, thunk et dispatcher ?

**À dire**

Un handler est une fonction qui traite une demande.

Un stub est une implémentation minimale : il peut ne donner qu’une réponse partielle, parfois fixe. Il faut donc regarder ce qu’il fait réellement avant de conclure qu’une fonction Windows est prise en charge.

Le thunk est le petit adaptateur d’entrée que nous avons vu en assembleur. Le dispatcher sélectionne le traitement à effectuer.

Ces mots décrivent des rôles différents. Dans le dépôt, on peut suivre un import jusqu’à son adresse, puis regarder si le traitement est direct ou passe par le répartiteur.

**À montrer :** les rôles à gauche, puis les références à droite si la personne veut poursuivre dans le code.

**Retour au récit :** On peut maintenant revenir au schéma complet, ou suivre un appel précis.


## Démonstration : préparation et repli

La commande courte affichée sur les slides suppose que le lanceur et le jeu sont accessibles depuis le dossier courant. Pour la démonstration depuis la racine du dépôt, préparer la commande avec le chemin réel ci-dessous. Garder les fichiers du jeu et les deux runtimes à leurs emplacements prévus.

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
