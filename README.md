# CyberNotePasQt — portage Qt6/C++ du projet Lazarus CyberNotePas

Compilé et testé avec succès sur Kali/Debian (Qt 6.4, CMake 3.28, g++ 13) :
`cmake .. && cmake --build .` produit un binaire qui se lance sans erreur.

## Ce qui est fait

**Phase 1 — squelette**
- Projet CMake + Qt6 complet et **fonctionnel** (`qt_add_executable`, AUTOMOC/AUTOUIC/AUTORCC).
- `src/mainwindow.ui` : reproduction de la disposition de `umain.frm`
  (splitter Projet / Note / Bibliothèques, groupbox Target, menus File/Tools/Help,
  toolbars). **Ouvrable et éditable dans Qt Designer** (`/usr/lib/qt6/bin/designer`
  ou directement depuis Qt Creator).
- `src/linuxtermdialog.ui` : reproduction de `ulinuxterm.frm` (zone d'édition + popup menu).

**Phase 2 — module MainWindow (terminé)**
- Gestion de projet complète : `New/Open Project`, scan disque récursif de
  l'arborescence Projet et Bibliothèques (`scanProjectDisk`/`scanLibraryDisk`),
  création de dossier/machine/note, renommage (inline via F2 + bouton toolbar),
  suppression, duplication, fusion ("merge") et déplacement de notes.
- Fichiers `.target` : recherche en remontant les dossiers parents, affichage/
  édition dans `tableTarget`, sauvegarde automatique à la modification d'une cellule.
- Configuration logicielle (`cybernotepas.conf`) : chargement au démarrage,
  création d'une config par défaut si absente, sauvegarde du dernier projet/note
  ouverts à la fermeture.
- Terminaux Linux dynamiques : chaque "Start New Linux Term" ajoute une entrée
  dans le menu Tools pour redonner le focus à l'instance correspondante.
- Deux corrections de bugs du code Pascal d'origine (documentées en commentaire
  dans `mainwindow.cpp`) : `scan_Library_disk` qui peuplait par erreur l'arbre
  Projet au lieu de Bibliothèques, et `TreeView2DblClick` qui relisait la
  sélection de l'arbre Projet au lieu de Bibliothèques.
- **Compilé et testé** : build propre (0 erreur/warning) + lancement headless
  (Xvfb) sans crash sur cet environnement.

**Phase 3 — éditeur Markdown (terminé)**
- `MarkdownHighlighter` (`QSyntaxHighlighter`) : titres `#`…`#####` (taille croissante),
  **gras**, *italique*, __souligné__, ~~barré~~, ==surlignage==, `code en ligne`, $math$,
  blocs de code fencés ```` ```lang ``` ```` avec coloration des mots-clés/cmdlets/chaînes/
  commentaires par langage (`markdownkeywords.h`, portage fidèle de `IsKeyword`/`IsCmdLets`/
  `CommentPrefix` de `markdownedit.pas`).
- Différence assumée avec le rendu Lazarus d'origine (validée avec vous) : les marqueurs
  (`**`, `__`, etc.) restent visibles à l'écran au lieu d'être masqués par un moteur de
  peinture custom.
- **Images `![[chemin]]`** : round-trip fidèle. Au chargement, chaque balise est remplacée
  par une image réellement intégrée au document (`QTextDocument::addResource` +
  `QTextCursor::insertImage`) ; à la sauvegarde, les images intégrées sont re-sérialisées
  en balises `![[chemin]]` identiques à l'original. Testé bit-à-bit identique sur un fichier
  contenant titre, tous les styles inline, un bloc de code Python et une image PNG.
- Coller une image depuis le presse-papier (`insertFromMimeData`) l'enregistre sur disque
  dans le dossier de la note (nom horodaté `yyyyMMddhhmmss.png`, ~ `GenerateImageFileName`)
  puis l'insère à la position du curseur.
- **Compilé et testé** : build propre (0 erreur/warning) + test dédié de chargement/
  sauvegarde vérifiant l'égalité bit-à-bit du fichier `.md` avant/après.

**Phase 4 — terminal Linux (terminé, refonte PTY)**
- Sur votre demande, le terminal n'est **plus** basé sur `QProcess` (simples pipes)
  mais sur un **vrai pseudo-terminal POSIX** (`forkpty`, classe `Pty` /
  `ptyprocess.h`/`.cpp`) : le shell lancé voit un vrai terminal (`isatty()` vrai),
  donc couleurs, prompt interactif, job control (`Ctrl+C`/`Ctrl+Z`), saisie
  `sudo` sans echo et applications plein écran fonctionnent nativement.
- `TerminalScreen` (`terminalscreen.h`/`.cpp`) : émulateur d'écran VT100/xterm
  "complet" au sens fonctionnel — grille de cellules avec attributs (gras,
  italique, souligné, barré, inverse), **écran alternatif** (indispensable pour
  `vim`/`htop`/`nano`/`less`), région de défilement, scrollback (5000 lignes),
  couleurs 16/256/24-bit (SGR `38;5;n` et `38;2;r;g;b`), insertion/suppression
  de caractères et de lignes, sémantique de retour à la ligne "différé" comme
  un vrai terminal.
- `TerminalWidget` (`terminalwidget.h`/`.cpp`) : rendu par `QPainter`, clavier
  (flèches, F1-F12, Home/End/PageUp/PageDown, Ctrl+lettre, mode "touches
  curseur application"), sélection à la souris avec scrollback, molette
  (défilement du scrollback normalement, relayée en flèches ↑/↓ pendant une
  appli plein écran comme `less`/`vim`).
- **Raccourci demandé `Ctrl+Shift+S`** : copie la **sélection de texte en
  cours**, rendue fidèlement en image (police, couleurs, gras/italique,
  alignement en colonnes), dans le presse-papier. `Ctrl+Shift+C`/`Ctrl+Shift+V`
  disponibles en complément (copie/collage texte classique), ainsi que le menu
  contextuel (clic droit) équivalent au `PopupMenu1` du Pascal d'origine
  (Copy / Paste / Copy Selection As Image / Deselect).
- Conséquence de l'architecture pty : l'essentiel de la logique métier de
  `ulinuxterm.pas` (`PrintPrompt`, `FriendlyPath`, `NavigateHistory`,
  `RewriteSudoCommand`, `RewriteGuiCommand`, `ClearScreenBuiltin`...) devient
  **inutile** — c'est le vrai `bash` (readline, `PS1`, `sudo`...) qui s'en charge
  nativement, ce qui simplifie et fiabilise grandement le résultat par rapport
  à l'original.
- **Testé réellement** (pas seulement compilé) via des programmes de test
  dédiés lançant un vrai `bash` sur le pty : sortie colorée (`\033[31m`...)
  correctement interprétée, `htop` affiche ses barres CPU/Mem/Swap en couleur
  dans l'écran alternatif et rend la main proprement en quittant, **`vi`
  affiche le vrai contenu du fichier édité et bascule bien en écran
  alternatif puis restaure l'écran normal (avec l'historique intact) après
  `:q`**. Application complète testée en lancement headless (Xvfb) sans crash.

## Statut global : les 4 modules prévus sont terminés et compilent/s'exécutent réellement sur cet environnement (Kali/Debian, Qt 6.4).

## Correctifs post-livraison

- **Fenêtre de terminal intégrée au lieu d'être séparée** : corrigé. En Qt, un
  `QWidget` construit avec un parent (`new LinuxTermDialog(this)`) est traité
  par défaut comme un **widget enfant** dans la fenêtre du parent, et non comme
  une fenêtre à part, sauf si on lui donne explicitement le flag `Qt::Window`.
  `LinuxTermDialog` appelle maintenant `setWindowFlags(Qt::Window)` dans son
  propre constructeur (donc correct quel que soit l'endroit où on l'instancie),
  et s'ouvre avec une taille par défaut de 900x600. Vérifié avec un test dédié :
  `term->isWindow()` vaut bien `true` et les deux fenêtres (principale +
  terminal) apparaissent comme deux entrées top-level distinctes avec leurs
  propres géométries.

- **Éditeur Markdown : mode "édition live"** (balises visibles seulement autour
  du curseur, à la façon d'Obsidian/Typora) :
  - Les marqueurs inline (`**`, `__`, `~~`, `==`, `` ` ``, `$`, `%%`) ne sont
    affichés que lorsque le curseur de texte se trouve entre les deux
    délimiteurs (bornes incluses) ; sinon ils sont masqués et seul le texte
    stylé (gras/italique/...) reste visible.
  - Le préfixe `#`/`##`/... d'un titre n'est visible que si le curseur est sur
    la ligne de ce titre ; sinon seul le texte du titre (en grand/gras) reste
    affiché.
  - Implémentation : `MarkdownHighlighter::setCursorPosition()` reçoit la
    position du curseur à chaque déplacement (`MarkdownEdit::onCursorPositionChanged`,
    connecté à `QTextEdit::cursorPositionChanged`), qui déclenche un
    **ré-highlight ciblé** (`rehighlightBlock()` sur le bloc quitté et le bloc
    atteint seulement — pas tout le document, pour rester performant). Techniquement,
    un marqueur "masqué" n'est pas supprimé du document (le texte réel et la
    sauvegarde `.md` restent inchangés) : il est rendu en taille quasi nulle et
    couleur transparente (`QTextCharFormat::setFontPointSize(0.5)` +
    `setForeground(Qt::transparent)`), ce qui est la technique standard pour ce
    genre d'effet avec `QSyntaxHighlighter` (qui ne peut pas faire disparaître
    du texte de la mise en page, seulement changer son apparence).
  - **Testé fonctionnellement** (pas seulement compilé) avec un programme dédié
    qui déplace le curseur programmatiquement et inspecte les formats réellement
    appliqués par le highlighter (`QTextBlock::layout()->formats()`, la seule
    façon fiable de vérifier — `QTextCursor::charFormat()` ne reflète PAS les
    formats de surlignage syntaxique, piège classique) : masquage/révélation
    du `#` de titre et du `**` de gras confirmés dans les deux sens.

- **Éditeur Markdown : bulle de lien au-dessus d'une image** : quand le curseur
  de texte est juste avant/après une image intégrée, une bulle flottante
  (`QLabel` positionné dynamiquement, enfant du viewport) affiche `![[chemin]]`
  juste au-dessus de l'image ; elle disparaît dès que le curseur s'en éloigne.
  Se met à jour aussi au défilement/redimensionnement. **Testé** : bulle bien
  cachée par défaut, apparaît avec le bon texte quand le curseur touche
  l'image, disparaît quand il repart.

- **Éditeur Markdown : image révélée en texte réel (pas une bulle)** : corrigé.
  L'ancienne bulle flottante (`QLabel`) non éditable a été remplacée par un vrai
  mécanisme d'édition live cohérent avec le reste : quand le curseur se trouve
  juste avant/après une image, celle-ci est **remplacée en place par le texte
  réel et éditable `![[chemin]]`** dans le document (`MarkdownEdit::maybeRevealImageAtCursor`) ;
  dès que le curseur en sort, ce texte est ré-analysé et ré-encapsulé en image
  (`maybeCollapseRevealedImage`), avec le chemin éventuellement modifié entre-
  temps pris en compte (si le nouveau chemin ne pointe vers aucun fichier
  valide, le texte brut reste affiché tel quel plutôt que de le perdre
  silencieusement). Un `QTextCursor` "vivant" conservé en membre suit
  automatiquement les éditions du champ (pas besoin de recalculer ses bornes à
  chaque frappe). **Testé** : image → texte au survol, texte réellement présent
  et modifiable dans le document (vérifié via `toPlainText()`), le chemin
  modifié est bien pris en compte à la fermeture du champ, aucune régression
  sur le round-trip fichier (chargement/sauvegarde toujours bit-à-bit
  identiques sur un fichier de test avec titre, bloc de code et image).

- **Éditeur Markdown : blocs de code — masquage des balises \`\`\` et fond pleine
  largeur** : deux corrections.
  1. Les lignes de balise `\`\`\`` (ouvrante et fermante) suivent maintenant la
     même règle d'édition live que les autres marqueurs, mais à l'échelle du
     bloc entier : elles ne sont visibles que si le curseur se trouve
     **n'importe où dans le bloc de code** (pas seulement sur la ligne de
     balise elle-même) — y compris sur une ligne vide à l'intérieur du bloc.
     Cela nécessitait un rehighlight plus large qu'un simple bloc quitté/atteint
     (`MarkdownEdit::onCursorPositionChanged` détecte désormais qu'un
     déplacement touche un bloc de code et déclenche un rehighlight complet
     dans ce cas précis, seule façon fiable de mettre à jour l'apparence des
     deux bornes ouvrante/fermante qui peuvent être loin l'une de l'autre).
  2. Le fond coloré des blocs de code couvre maintenant **toute la largeur de
     l'éditeur**, y compris les lignes sans texte — un simple fond de
     caractères (`QSyntaxHighlighter::setFormat`) ne peut pas faire ça (il ne
     couvre que la largeur du texte présent) ; la coloration est donc appliquée
     séparément via `QTextBlockFormat::setBackground()` sur chaque bloc de la
     zone (`MarkdownEdit::updateCodeBlockBackgrounds`, recalculée à chaque
     frappe, avec garde anti-réentrance).
  **Testé** : balises masquées hors du bloc, révélées sur une ligne de contenu
  ET sur une ligne vide à l'intérieur du bloc ; fond coloré présent sur les
  lignes de contenu ET les lignes vides du bloc, absent en dehors.

## Correctifs post-livraison (round 3)

- **Couleur de surlignage `==...==` trop claire** : corrigée. Le jaune pâle
  (`255,245,120`) est remplacé par un jaune/ambre assombri (`150,120,0`) avec
  couleur de texte forcée en blanc, lisible même sur fond clair de l'éditeur.

- **Fichier `.target` jamais sauvegardé** : bug identifié et corrigé. Le code
  traitait par erreur la ligne 0 de la grille comme une ligne d'en-tête à
  ignorer (`if (row <= 0) return;` en lecture ET boucle d'écriture démarrant à
  `i = 1`), alors que cette grille n'a pas de ligne d'en-tête : la première
  (souvent unique) information du `.target` n'était donc jamais lue ni
  réécrite. Testé : modification de la ligne 0 maintenant bien répercutée dans
  le fichier après édition.

- **Terminal : prompt manquant après connexion `evil-winrm`** : cause
  identifiée -- ce type d'outil (basé sur Readline/Reline) envoie une requête
  `ESC[6n` (Device Status Report, demande de la position du curseur) et
  **attend la réponse du terminal** avant d'afficher son prompt ; comme
  l'émulateur ne répondait jamais, l'application restait bloquée en silence
  (le texte de connexion s'affiche, mais jamais le prompt interactif). Corrigé
  en implémentant la réponse `ESC[row;colR` (et, en bonus, la réponse `Device
  Attributes` `ESC[?1;2c` pour les négociations de capacités similaires),
  relayée jusqu'au pty via un nouveau signal `TerminalScreen::hostResponse`.
  **Testé** avec un script bash reproduisant exactement ce probleme
  (`read` bloquant en attente de la réponse `...R`) : la réponse est bien
  générée et reçue par le shell.

- **Menu "Execute" du Markdown : envoi vers un terminal** : implémenté (jusque là
  un simple message de type "placeholder"). La ligne courante de la note est
  envoyée directement au terminal ouvert s'il n'y en a qu'un seul ; s'il y en a
  plusieurs, une boîte de sélection liste leurs titres pour que l'utilisateur
  choisisse la destination ; si aucun n'est ouvert, un message invite à en
  ouvrir un. Ajout de `TerminalWidget::sendCommand()` /
  `LinuxTermDialog::sendCommand()`, et nettoyage correct de la liste des
  terminaux ouverts à leur fermeture (`openLinuxTerms`).

## Correctifs post-livraison (round 4)

- **Nom du terminal écrasé** : bug identifié et corrigé. `bash` envoie sa
  propre séquence de titre (OSC, ex. `\e]0;user@host: /path\a`) dès qu'il
  démarre, ce qui écrasait silencieusement le titre choisi dans
  `MainWindow::onNewLinuxTerm` juste après la création de la fenêtre. Ajout
  d'un verrou (`LinuxTermDialog::setFixedTitle()`) : une fois un titre fixé
  explicitement, les mises à jour de titre venant du shell sont ignorées.
  **Testé** : titre saisi toujours présent après 1,5s de fonctionnement du
  shell (le temps qu'il émette sa séquence de titre).

- **Aide complète du logiciel** : ajout d'une fenêtre d'aide (`HelpDialog`,
  `Help > Help Contents` ou <b>F1</b>) couvrant l'ensemble des fonctionnalités :
  gestion de projet, fichier `.target`, éditeur Markdown en mode édition live
  (styles, blocs de code, images), bibliothèque, terminaux Linux (pty réel,
  raccourcis, `Ctrl+Shift+S`), envoi de commande vers un terminal, et un
  récapitulatif des raccourcis clavier. `Help > About CyberNotePas` affiche un
  résumé court. **Testé** : la fenêtre s'ouvre et affiche bien son contenu.

## Correctifs post-livraison (round 5) — nouvelle fonctionnalité

- **Substitution des paramètres `<nom>` depuis la grille Target à l'ajout d'une
  note de bibliothèque** : au double-clic sur une note de bibliothèque, si la
  grille Target est affichée pour la note courante, tout texte `<nomDuParametre>`
  présent dans la note ajoutée est remplacé par la valeur correspondante de la
  grille (ex. `<targetIP>`, `<targetName>`, `<targetComment>`, ou tout autre nom
  de paramètre présent dans le fichier `.target`).
- **`<attackIP>`** est traité spécialement : remplacé par l'adresse IP de la
  machine locale **pertinente pour joindre la cible**, déterminée via
  `ip route get <targetIP>` (interroge directement le noyau, comme le ferait
  une vraie connexion réseau) plutôt que de deviner une interface "par défaut".
  Cela gère correctement le cas d'un VPN actif (ex. `tun0`) : si la route vers
  la cible passe par le VPN, c'est l'IP du VPN qui est utilisée, pas celle de
  l'interface physique. Repli sur la route par défaut (vers Internet) si l'IP
  cible n'est pas connue, puis sur la première interface active non-loopback
  en dernier recours.
- **Testé** avec un scénario reproduisant exactement le cas redouté : une
  interface `tun0` avec une route spécifique vers un réseau simulé, en plus de
  l'interface physique normale -- vérifié que l'IP renvoyée est bien celle
  d'`eth0` pour une cible normale, et bien celle de `tun0` pour une cible
  joignable via le VPN (donc pas une simple IP "par défaut" figée).
- Nécessite la commande `ip` (paquet `iproute2`, présent par défaut sur Kali).

## Correctifs post-livraison (round 6) — nouvelle fonctionnalité

- **Structures de bloc Markdown supplémentaires** dans l'éditeur de note, en
  complément des titres/emphase/blocs de code déjà gérés :
  - Listes à puces (`-`, `*`, `+`) et numérotées (`1.`), imbrication gérée par
    l'indentation existante du texte.
  - Cases à cocher GFM (`- [ ]` / `- [x]`) : le texte d'une tâche cochée est
    affiché barré.
  - Citations `>` imbriquées (`>>`, `> >`...) : contenu en italique atténué,
    marge gauche croissante par niveau d'imbrication (`QTextBlockFormat`).
  - Règles horizontales `---`, `***`, `___`.
  - Titres Setext (`Titre` suivi d'une ligne `===` ou `---`), avec la
    désambiguïsation correcte vis-à-vis d'une simple règle horizontale
    (priorité au Setext uniquement si la ligne précédente est un paragraphe
    valide, conformément à CommonMark).
  - Tableaux GFM (`| a | b |` + séparateur `|---|---|`) : détectés et mis en
    forme (police à chair fixe, bordures colorées, en-tête en gras) --
    **choix assumé** : pas de conversion en grille éditable façon tableur (qui
    aurait exigé de réécrire la sérialisation avec un risque de corruption du
    fichier) ; le tableau reste du Markdown normal, donc le round-trip
    fichier est garanti sans risque.
  - Tous ces marqueurs de structure restent **toujours visibles** (contrairement
    aux marqueurs d'emphase `**`/`__`/etc. qui eux se masquent/révèlent selon
    le curseur) : ce sont des repères structurels, pas des décorations.
  - **Testé** : 21 vérifications automatisées couvrant chaque structure (y
    compris les cas ambigus : `---` isolé traité comme règle horizontale mais
    PAS comme Setext, un `|` isolé dans une phrase normale non confondu avec
    un tableau, imbrication de listes et de citations, tâche cochée/non
    cochée) + non-régression du round-trip fichier (chargement/sauvegarde
    bit-à-bit identiques) sur un document combinant toutes ces structures.

## Correctifs post-livraison (round 7) — nouvelle fonctionnalité

- **Icônes colorées pour tous les menus/toolbars**, intégrées à l'exécutable
  sous forme de ressources Qt (`resources/resources.qrc`, traité automatiquement
  par `CMAKE_AUTORCC` -- pas de fichiers externes à déployer, tout est compilé
  dans le binaire) :
  - 24 icônes SVG vectorielles faites main (`resources/icons/*.svg`), une par
    action (Nouveau projet, Ouvrir, Quitter, Terminal, Ajouter machine/dossier,
    Nouvelle note, Copier/Couper/Coller/Dupliquer/Déplacer/Fusionner/Renommer/
    Supprimer, Exécuter, Aide/À propos, ainsi que Copier/Coller/Copier en
    image/Désélectionner dans le terminal), plus trois icônes par type
    d'élément d'arborescence (dossier/image/note Markdown, cf. round 8
    ci-dessous).
  - Nécessite le module **Qt6 SVG** (`qt6-svg-dev`/`libqt6svg6`, ajouté aux
    dépendances de compilation) : `QIcon` a besoin de ce module pour savoir
    charger des icônes au format SVG.
  - **Icône d'application** (`resources/icons/app_icon.svg`) sur le thème
    demandé -- pentest (loupe orange façon audit/investigation), bureau
    (format d'icône d'application classique, coins arrondis), prise de notes
    (bloc-notes avec pages et lignes de texte) -- posée via
    `QApplication::setWindowIcon()` dans `main.cpp`, donc appliquée par défaut
    à toutes les fenêtres de l'application (fenêtre principale, terminaux,
    aide) sans avoir à la répéter partout.
- **Piège rencontré et corrigé en cours de route** : un fichier de ressources
  Qt avec `<qresource prefix="/icons">` et des entrées `<file>icons/x.svg</file>`
  produit des chemins d'accès de la forme `:/icons/icons/x.svg` (le préfixe
  s'ajoute au chemin du fichier, il ne le remplace pas) -- corrigé en utilisant
  `prefix="/"` pour obtenir les chemins `:/icons/x.svg` réellement utilisés
  dans le code.
- **Testé** : rendu de chaque SVG vérifié individuellement (`rsvg-convert`,
  aucun échec), absence d'erreurs `qt.svg: Cannot open file` au lancement une
  fois le bug de préfixe corrigé, et capture d'écran réelle de l'application
  (Xvfb) confirmant que les icônes s'affichent bien en couleur à côté du texte
  des actions dans les menus déroulés.

## Correctifs post-livraison (round 8) — nouvelle fonctionnalité

- **Icônes différenciées par type d'élément dans les arborescences Project et
  Libraries** : dossier, image, note Markdown ont chacun leur icône propre
  (`folder_plain.svg`, `image_file.svg`, `markdown_file.svg`, appliquées dans
  `MainWindow::addTreeChild` selon `RoleType`, y compris sur les éléments
  racine des deux arborescences). Trois nouvelles icônes SVG ajoutées au même
  jeu de ressources que le round 7.
- **Testé** : projet de test avec un dossier, une image et deux notes
  Markdown, chargé au démarrage, capture d'écran réelle (Xvfb) puis analyse
  pixel par pixel de la colonne icône de chaque ligne de l'arbre -- confirmé
  que chaque ligne affiche la couleur caractéristique de l'icône attendue pour
  son type (bleu du dossier, cyan/sombre du document Markdown, jaune/bleu de
  l'image), dans le bon ordre alphabétique des éléments.

## Correctifs post-livraison (round 9)

- **Ordre des boutons de la toolbar "Project"** : corrigé pour correspondre à
  l'ordre du Pascal d'origine (identique à celui du menu contextuel) : New
  Project, Open Project, séparateur, Add Machine, Add Folder, New Note,
  séparateur, Rename, Make a Copy, Move file into, Merge entire file with,
  séparateur, Delete. Le menu contextuel (clic droit) avait lui aussi deux
  écarts avec cet ordre (Add Folder/Add Machine inversés, Rename positionné
  après le groupe Copy/Move/Merge au lieu d'avant) : corrigé au passage pour
  que les deux soient rigoureusement identiques, comme demandé.

## Correctifs post-livraison (round 10)

- **Renommer un terminal déjà ouvert** : ajouté. Clic droit dans le terminal >
  <b>Rename Terminal...</b> (nouvelle entrée `actionRenameTerminal`, icône
  crayon partagée avec Rename dans l'arborescence Project). Utilise le même
  mécanisme de verrou de titre que le nom saisi à la création
  (`LinuxTermDialog::setFixedTitle`), donc le shell ne peut pas l'écraser. Le
  libellé correspondant dans le menu <b>Tools</b> se met aussi à jour
  automatiquement (`QWidget::windowTitleChanged` connecté à `QAction::setText`).
  **Testé** : renommage vérifié effectif par lecture directe de `windowTitle()`
  après l'opération.
- **Sens de la molette inversé dans le terminal** : bug identifié et corrigé
  (`TerminalWidget::wheelEvent` appliquait le signe opposé à la convention
  standard). Molette vers le haut = recule dans l'historique (scrollback),
  molette vers le bas = revient au direct. **Testé** : simulation d'événements
  molette réels, vérifié que la valeur de l'ascenseur évolue dans le bon sens
  dans les deux directions.
- **Ascenseur vertical manquant dans le terminal** : ajouté (`QScrollBar` à
  droite du widget, largeur réservée en permanence pour éviter tout
  redimensionnement du texte affiché quand il apparaît/disparaît). Synchronisé
  avec le scrollback dans les deux sens (molette, `Shift+PageUp/PageDown`, et
  glisser l'ascenseur lui-même font tous avancer le même état). Désactivé
  (mais toujours visible, sans reflow) pendant une application plein écran
  (vim/htop/less...), où le scrollback ne s'applique pas. **Testé** :
  présence et géométrie de l'ascenseur vérifiées, ainsi que la mise à jour de
  sa plage/valeur après génération de contenu (`seq 1 200`).
- **Dossier de démarrage d'un nouveau terminal** : le shell démarre désormais
  dans le dossier actuellement sélectionné dans l'arborescence Project, sinon
  la racine du projet ouvert, sinon le dossier de l'application CyberNotePasQt
  (ordre de priorité demandé). Implémenté via un nouveau paramètre `workingDir`
  transmis de `MainWindow` à `LinuxTermDialog` puis `TerminalWidget::startShell`
  jusqu'à `Pty::start`, qui fait un `chdir()` dans le processus enfant juste
  avant `execvp` (donc avant même que le shell ne démarre, comme un vrai
  terminal qui s'ouvrirait "dans" un dossier). **Testé** : `pwd` exécuté juste
  après le démarrage confirme le bon dossier courant.

## Correctifs post-livraison (round 11) — nouvelle fonctionnalité

- **Copier/coller un dossier entier dans l'arborescence Project** : ajouté
  (`actionTreeCopy`/`actionTreePaste`, toolbar + menu contextuel, entre "New
  Note" et "Rename"). Contrairement à "Make a copy" (qui ne duplique qu'une
  note, sur place), Copy/Paste fonctionne sur un fichier **ou un dossier
  entier**, avec copie récursive de tout son contenu (sous-dossiers compris) --
  `QDir`/`QFile` n'offrent pas cette fonction telle quelle, implémentée à la
  main (`MainWindow::copyRecursively`).
  - Pas d'écrasement silencieux : en cas de collision de nom à destination, un
    suffixe est ajouté automatiquement (`nom - copy`, `nom - copy (2)`...),
    l'extension d'un fichier étant préservée.
  - Protection contre le collage d'un dossier dans lui-même ou l'un de ses
    propres sous-dossiers (qui corromprait les données par récursion infinie).
  - **Testé** : copie récursive d'un dossier avec sous-dossier vérifiée
    (contenu imbriqué bien présent à destination), génération de noms uniques
    testée pour dossiers et fichiers (extension préservée), protection
    anti-boucle vérifiée sur les trois cas (soi-même, sous-dossier, dossier
    normal).

## Correctifs post-livraison (round 12) — refonte majeure des tableaux

- **Vrais tableaux éditables façon Obsidian** : changement d'architecture assumé
  sur demande explicite, revenant sur le choix "détection + style texte" du
  round 6 (documenté comme un compromis à l'époque). Un tableau GFM détecté au
  chargement (ou à l'ajout depuis la bibliothèque) est maintenant converti en
  un **vrai `QTextTable`** (grille avec bordures réellement dessinées,
  éditable cellule par cellule), et non plus un texte à chair fixe simplement
  coloré :
  - **Tab / Maj+Tab** navigue de cellule en cellule ; Tab dans la toute
    dernière cellule ajoute automatiquement une nouvelle ligne (comme Obsidian).
  - **Clic droit** dans une cellule ouvre un sous-menu "Table" : Insert Row
    Above/Below, Duplicate Row, Delete Row, Insert Column Left/Right,
    Duplicate Column, Delete Column.
  - L'alignement de colonne GFM (`:---`, `:---:`, `---:`) est conservé (stocké
    comme alignement de paragraphe sur chaque cellule) et correctement
    resérialisé à la sauvegarde.
  - **Sérialisation (round-trip)** : un `QTextTable` est reconstruit en
    syntaxe GFM avec colonnes ré-alignées visuellement dans le fichier
    (comme le ferait Obsidian lui-même en sauvegardant), via un parcours du
    document par `QTextFrame::iterator` (qui traite un tableau comme une seule
    entité, pas cellule par cellule) plutôt que le simple parcours par bloc
    utilisé pour le reste du texte.
  - **Deux bugs de structure trouvés et corrigés pendant les tests**, tous les
    deux liés à des lignes vides parasites introduites par le mécanisme
    interne de Qt pour les tableaux :
    1. `QTextCursor::insertTable()` laisse le curseur **à l'intérieur** de la
       première cellule après l'insertion (pas après le tableau) : sans
       repositionnement explicite (`cursor.setPosition(table->lastPosition()+1)`),
       le contenu suivant se serait retrouvé fusionné dans la première cellule.
    2. Pré-créer un bloc séparateur juste avant/après un tableau (comme on le
       fait entre deux lignes de texte normales) laisse un paragraphe vide
       parasite, car `insertTable()` gère déjà sa propre séparation ; corrigé
       en ne pré-créant ce séparateur que lorsque l'élément suivant n'est pas
       lui-même un début de tableau. Cas particulier additionnel : un tableau
       en toute première ligne d'un fichier laisse le paragraphe vide par
       défaut que Qt crée dans tout document neuf -- une tentative de le
       supprimer directement du document a échoué (Qt étend toute sélection
       touchant la limite d'un cadre à la totalité de ce cadre : ça a
       supprimé le tableau entier pendant les tests !) -- corrigé plus
       prudemment en ignorant ce paragraphe fantôme au moment de la
       sérialisation plutôt qu'en modifiant le document lui-même.
  - **Testé** : 17 vérifications automatisées (structure de la grille,
    alignements, en-tête en gras, round-trip fichier, Tab en dernière
    cellule) + cas limites dédiés (tableau en tout début de fichier, tableau
    en toute fin de fichier) + capture d'écran réelle de l'application avec
    détection programmatique des bordures horizontales ET verticales aux
    positions pixel exactement attendues, confirmant un vrai rendu de grille
    (pas juste du texte à chair fixe avec des `|` colorés).

## Correctifs post-livraison (round 13) — nouvelle fonctionnalité : explorateur de titres (onglet « Outline »)

À gauche, à côté de l'onglet **Project**, un nouvel onglet **Outline** affiche les titres de la note ouverte au centre, sous forme d'arbre hiérarchique (un `###` se range sous le `##`/`#` précédent).

- **Clic (ou Entrée) sur un titre** : le curseur se place sur le titre et celui-ci est remonté tout en haut de la zone visible de l'éditeur. Si le titre est trop proche de la fin du document pour pouvoir être placé en haut sans laisser du vide en bas, le défilement s'arrête au maximum : la fin du document reste alors calée tout en bas de la vue.
- **Mise à jour pendant l'édition** : ajouter, modifier, supprimer un titre (ou charger une autre note, ou ajouter un modèle depuis la bibliothèque) met l'arbre à jour automatiquement, avec un léger différé (250 ms) pour ne pas recalculer à chaque frappe. L'arbre n'est reconstruit que si les titres ont réellement changé ; les nœuds repliés par l'utilisateur et le défilement de l'arbre sont conservés.
- **Titres reconnus** : exactement ceux que l'éditeur colore comme tels (`#` à `#####`, plafond de 5 niveaux comme `MarkdownHighlighter`, et titres Setext `===` / `---`). Les lignes `# ...` situées dans un bloc de code ``` ou dans une cellule de tableau ne sont **pas** prises pour des titres. Les marqueurs `**`, `__`, `~~`, `==` et `` ` `` sont retirés de l'affichage.

Fichiers touchés : `mainwindow.ui` (onglet + `treeOutline`), `mainwindow.h/.cpp` (`refreshOutline`, `onOutlineItemClicked`), `markdownedit.h/.cpp` (`headings()`, `revealHeading()`), `markdownhighlighter.h` (trois fonctions de détection de titres passées en `public static`).

## Pistes d'amélioration possibles (non demandées, notées pour information)

- `ConfigFile` : le format actuel (une ligne = une entrée façon `.ini` maison)
  reprend celui utilisé par `umain.pas`/`FSoftConfiguration` ; à faire évoluer
  si besoin (l'unité Pascal d'origine était vide, donc pas de contrainte de
  compatibilité stricte ici).
- Rendu du terminal actuellement cellule par cellule (simple, correct, mais
  pas optimisé façon VTE pour des flux très volumineux/rapides) ; un profilage
  pourrait motiver un regroupement par "runs" de même style si besoin.
- Le compte-rendu souris (xterm mouse reporting, modes 1000/1002/1003/1006)
  n'est pas câblé : les applications qui en dépendent pour la souris (ex.
  certains modes de `vim`/`tmux`) ne recevront pas les clics, seul le clavier
  et le défilement fonctionnent avec elles.
- Compatibilité Markdown standard (CommonMark/GFM) encore incomplète malgré le
  round 6 : liens `[texte](url)`, images standard `![alt](chemin)` (seul
  `![[chemin]]` façon Obsidian est géré), échappement `\*`, émphase imbriquée/
  combinée (`***gras+italique***`), HTML brut incorporé. `__texte__` est traité
  ici comme "souligné" (extension maison) plutôt que comme synonyme strict de
  gras (CommonMark) -- choix assumé mais à connaître si des fichiers `.md`
  doivent être ouverts ailleurs.

## Compiler sur Kali

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-svg-dev cmake build-essential
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
./CyberNotePasQt
```

## Ouvrir dans Qt Designer / Qt Creator

- Qt Creator : `File > Open File or Project...` → sélectionner `CMakeLists.txt`.
  Double-cliquer sur un `.ui` dans l'arborescence ouvre l'éditeur de formulaire
  intégré (= Qt Designer).
- Qt Designer autonome : `/usr/lib/qt6/bin/designer src/mainwindow.ui`

## Crédits et licences tierces

Par souci de transparence et de respect de la propriété intellectuelle, voici
l'origine de chaque élément de ce projet.

**Code source** : l'intégralité du code C++ de ce portage (émulateur de
terminal VT100/xterm, éditeur Markdown et son highlighter, gestion de projet,
intégration pty...) a été écrite spécifiquement pour ce projet, à partir de
connaissances générales de Qt/C++ et des spécifications techniques publiques
(protocole VT100/xterm, format CommonMark/GFM) -- aucun code n'a été copié
depuis un autre émulateur de terminal (xterm, VTE, QTermWidget/Konsole...) ni
depuis un autre projet tiers.

**Icônes** (`resources/icons/*.svg`, y compris l'icône d'application) :
dessinées à la main pour ce projet (formes géométriques simples : rectangles,
cercles, chemins vectoriels), et non issues d'une bibliothèque d'icônes tierce
(Font Awesome, Material Design Icons, Feather Icons, etc.).

**Projet Lazarus/Free Pascal d'origine** (`umain.pas`, `markdownedit.pas`,
`ulinuxterm.pas`, `uconfigfile.pas`, fichiers `.frm`) : fourni par l'utilisateur
ayant commandé ce portage, sur lequel se base l'architecture fonctionnelle de
l'application (mentionné ici pour la traçabilité, il ne s'agit pas d'une
source tierce externe).

**Bibliothèques et outils tiers utilisés** (liés dynamiquement / invoqués en
tant qu'outils externes, aucun de leur code source n'est copié dans ce dépôt) :

| Composant | Rôle | Licence | Site |
|---|---|---|---|
| [Qt 6](https://www.qt.io/) (Widgets, Core, Gui, Network, Svg) | Framework applicatif (interface graphique, réseau, rendu SVG) | LGPL v3 (ou GPL v3/licence commerciale selon build) | qt.io |
| glibc (`forkpty`, `<pty.h>`) | Création du pseudo-terminal (pty) | LGPL v2.1+ | gnu.org/software/libc |
| `iproute2` (commande `ip`) | Détermination de l'IP "attaquant" via `ip route get` (voir §"Substitution des paramètres") | GPL v2 | kernel.org/pub/software/network/iproute2 |
| `/bin/bash` (ou `$SHELL`) | Shell lancé dans les fenêtres de terminal | GPL v3 | gnu.org/software/bash |

Ces composants ne sont pas redistribués avec le code source de ce projet : ils
sont soit liés dynamiquement au moment de la compilation (Qt, glibc), soit
invoqués comme outils externes déjà présents sur le système au moment de
l'exécution (`ip`, `bash`). Si ce logiciel est redistribué sous forme de binaire
compilé, les obligations de la licence LGPL v3 de Qt (notamment la possibilité
pour l'utilisateur final de relier l'application à une version modifiée de Qt)
s'appliquent normalement à la liaison dynamique utilisée ici.
