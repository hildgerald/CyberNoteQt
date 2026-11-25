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
  alignement en colonnes), dans le presse-papier. **Depuis la v1.3.0**, la largeur
  de l'image est celle du texte sélectionné le plus long + 16 px de marge (et non
  plus la largeur du terminal). `Ctrl+Shift+C`/`Ctrl+Shift+V`
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

## Version 1.3.0 — Ctrl+Shift+S : image ajustée au texte

- `TerminalWidget::copySelectionAsImage()` (terminal Linux **et** console
  netcat, qui partagent ce composant) : la largeur de l'image n'est plus
  `colonnes_du_terminal × largeur_cellule` mais la largeur du **texte le plus
  long** de la sélection (dernier caractère non blanc de chaque ligne) **+ 16 px
  de marge** (8 px de chaque côté). Les captures d'écran ne sont plus larges
  inutilement.
- Une sélection composée uniquement d'espaces ne produit plus d'image.
- Version incrémentée à 1.3.0 (titre de fenêtre, « À propos », `CMakeLists.txt`).

## Version 1.4.0 — Ctrl+Alt+S : image de la sélection insérée dans la note

- Nouveau raccourci **`Ctrl+Alt+S`** dans `TerminalWidget` (terminal Linux et console
  netcat) : génère la même image que `Ctrl+Shift+S` (largeur ajustée au texte le plus
  long + 16 px), mais l'**insère directement dans la note ouverte**, sur une nouvelle
  ligne située juste après la ligne du curseur (après le tableau si le curseur est dans
  un tableau). Aussi disponible dans le menu clic droit du terminal
  (« Copy Selection As Image Into Note »).
- L'image est enregistrée en PNG dans le dossier de la note (nom horodaté, suffixe
  `_1`, `_2`... si deux captures tombent dans la même seconde) et insérée comme un collage
  `Ctrl+V` (`![[nom.png]]` + image). Le curseur reste après l'image : des captures
  successives s'empilent. La note est marquée modifiée (sauvegarde auto habituelle).
- Si aucune note n'est ouverte, un message l'indique.
- Architecture : `TerminalWidget::renderSelectionImage()` (rendu commun) ->
  signal `selectionImageForNote` -> relais `imageForNoteReady` des deux fenêtres ->
  `MainWindow::onTerminalImageForNote` -> `MarkdownEdit::insertImageOnNextLine`.
- Version incrémentée à 1.4.0.

## Version 1.5.0 — Ctrl+Alt+C : texte de la sélection inséré dans la note

- Nouveau raccourci **`Ctrl+Alt+C`** dans `TerminalWidget` (terminal Linux et console
  netcat) : insère le **texte sélectionné** (même contenu que `Ctrl+Shift+C`, fins de
  ligne nettoyées) dans la note ouverte, sur de nouvelles lignes situées juste après la
  ligne du curseur (après le tableau si le curseur est dans un tableau). Le texte est
  inséré tel quel (une ligne du terminal = une ligne de la note) et **encadré par deux
  lignes ```** pour être pris comme un extrait de code ; les lignes vides en début/fin de
  sélection sont retirées. Le curseur reste sur la ``` de fermeture : les insertions
  successives s'empilent. Aussi dans le menu clic droit du terminal
  (« Copy Selection As Text Into Note »). Message si aucune note n'est ouverte.
  Limite connue : si le texte sélectionné contient lui-même une ligne commençant par
  ```, celle-ci fermera le bloc de code plus tôt (l'éditeur ne gère qu'un seul niveau de
  clôture).
- Architecture : signal `TerminalWidget::selectionTextForNote` -> relais `textForNoteReady`
  des deux fenêtres -> `MainWindow::onTerminalTextForNote` ->
  `MarkdownEdit::insertTextOnNextLine` (qui partage avec l'insertion d'image la routine
  `openNewLineAfterCursor`).
- Version incrémentée à 1.5.0.

## Version 1.7.0 — Ctrl+Alt+C dans la bibliothèque

- **Sur un fichier de l'arbre Libraries** : `Ctrl+Alt+C` ajoute son contenu à la **fin** de la
  note ouverte (équivalent du double-clic : `MainWindow::appendLibraryNoteFile`, avec
  substitution des paramètres `<...>`). Raccourci `QShortcut` limité au widget (`Qt::WidgetShortcut`).
- **Dans l'aperçu (onglet Preview)** : `Ctrl+Alt+C` insère le **texte sélectionné** sur de nouvelles
  lignes situées juste après la ligne du curseur de la note en cours d'édition
  (`MarkdownEdit::selectedMarkdownText` -> signal `copyToNoteRequested` ->
  `MainWindow::onLibraryPreviewCopyToNote` -> `MarkdownEdit::insertMarkdownOnNextLine`). Le texte est
  inséré comme du Markdown (pas de ```) : les tableaux touchés par la sélection sont repris en
  entier et recréés en vrais tableaux, et les paramètres `<...>` sont substitués.
- **Images** (aperçu *et* contenu d'un fichier ajouté, y compris par double-clic) : les images
  référencées par `![[x.png]]` sont **copiées dans le dossier de la note de destination**
  (`MarkdownEdit::copyReferencedImages`) au lieu de réécrire les liens : copier le dossier des
  notes ne fait perdre aucune image. La balise garde son nom relatif (sous-dossier conservé) ;
  si un fichier de même nom mais de contenu différent existe déjà, la copie est renommée
  (`x_1.png`...) et la balise ajustée ; un fichier identique déjà présent est réutilisé ; une
  image introuvable laisse la balise inchangée. Les balises absolues ou `~/` sont ramenées au
  seul nom de fichier.
- Message si aucune note n'est ouverte. Versions 1.6.x non utilisées : on passe de 1.5.0 à 1.7.0.

## Version 1.8.0 — Execute : commande longue repliée par l'éditeur

- Bug : l'icône / le menu **Execute** n'envoyait au terminal que le morceau de la commande affiché
  sur la ligne visuelle du curseur lorsque l'éditeur repliait (retour automatique à la ligne)
  une commande plus longue que sa largeur.
- Cause : `MarkdownEdit::getCurrentLineTextContent()` utilisait `QTextCursor::LineUnderCursor`
  (ligne **visuelle**). Il lit maintenant le **bloc** (paragraphe) du curseur, c'est-à-dire la vraie
  ligne du fichier : seuls les vrais retours à la ligne séparent des commandes, pas ceux imposés
  par l'affichage. Les images intégrées à la ligne ne sont pas incluses (seule leur balise l'est).

## Version 1.9.0 — « Make a copy » sur un dossier

- **Make a copy** (barre d'outils / menu contextuel de l'arbre Project) fonctionne maintenant aussi sur
  un **dossier** : il est dupliqué avec tout son contenu (sous-dossiers, notes, images, fichiers
  cachés) dans le **même dossier parent**, sous le nom saisi (proposé : « <nom> - copy »). Un nom déjà
  existant ou invalide (contenant `/`) est refusé et redemandé. Le dossier racine du projet ne peut pas
  être dupliqué (sa copie serait hors du projet). Une copie partielle (erreur) est supprimée.
- Réutilise `MainWindow::copyRecursively` (déjà utilisé par Copy/Paste de l'arbre). Le comportement
  sur une note est inchangé.

## Version 1.10.0 — extension .md des notes markdown

- Bug : les noms de notes saisis pour **New note**, **Make a copy** (note) et **Rename** n'étaient
  pas contrôlés : « note.md » devenait « note.md.md » (New note / Make a copy) et un renommage
  pouvait faire perdre l'extension (« autre » au lieu de « autre.md »).
- Nouveau `ensureMdExtension()` (`mainwindow.cpp`) : ajoute `.md` à la fin du nom s'il manque
  (insensible à la casse, sans le doubler). Utilisé par New note, Make a copy (note) et Rename
  d'une note `.md` (les dossiers, images et notes `.txt` ne sont pas modifiés par Rename).
  L'onglet de la note dupliquée affiche le nom réel du fichier.

## Version 1.11.0 — New note sans écrasement, Rename avec nom prérempli

- **New note** : si une note du même nom existe déjà, la création est **refusée** (message d'erreur) et
  le nom est **redemandé** (dernier nom saisi reproposé), au lieu d'écraser silencieusement la note.
  L'extension `.md` est prise en compte dans la vérification (« note » et « note.md » sont le même
  fichier).
- **Rename** : le champ de saisie affiche maintenant le **nom actuel** du fichier ou du dossier à
  renommer. Valider sans changement ne fait rien (au lieu d'une erreur « existe déjà »).
  Les messages d'erreur d'un fichier parlent désormais d'un « file » et non d'un « folder ».

## Version 2.0.0 — Préparation de rapport : insérer les notes d'un dossier

Objectif : partir d'un gabarit de rapport et y intégrer rapidement les notes de prise de notes, sans
reprendre les fausses pistes.

- **Convention d'exclusion** : une note ou un dossier dont le nom **commence par `_`** est une fausse
  piste. Menu contextuel de l'arbre Project : **Exclude from the report / Include in the report**
  ajoute ou retire ce préfixe (`MainWindow::onToggleReportExclusion`). Les éléments exclus sont
  affichés **en gris** dans l'arbre. Si la note ouverte (ou un de ses dossiers) est renommée, l'application
  suit son nouveau chemin (onglet, dossier des images, sauvegarde auto).
- **Insert the notes of this folder into the open note...** (menu contextuel d'un **dossier**) :
  nouvelle fenêtre `InsertNotesDialog` (`insertnotesdialog.h/.cpp`) qui liste en arbre les notes `.md`/`.txt`
  du dossier et de ses sous-dossiers (tri « naturel » : 2 avant 10 ; notes du dossier puis sous-dossiers ;
  dossiers sans note masqués ; la note ouverte n'est pas proposée). Les éléments `_...` sont décochés par
  défaut (cochables à la main) ; cocher un dossier coche tout son contenu ; **Up/Down** réordonne au sein
  d'un même niveau ; *Check all / Uncheck all*.
- Options : **titre avant chaque note** (chemin relatif sans extension, ex. `web/gobuster`) au niveau choisi
  (1 à 5, défaut 2) ; les titres propres à chaque note sont décalés d'autant (plafonnés à 6, lignes des blocs
  ``` ignorées) pour s'emboîter sous le titre ajouté.
- **Insertion** (`MarkdownEdit::insertMarkdownOnNextLine`) : toutes les notes cochées, séparées par une ligne
  vide, sont insérées à la ligne suivant le curseur de la note ouverte (le gabarit) ; les **images** sont
  copiées dans le dossier du gabarit (`copyReferencedImages`, cf. 1.7.0). Les notes vides sont ignorées.
  La note ouverte est enregistrée avant l'ouverture de la fenêtre.
- Pas de rapport « généré » automatiquement (directives `include`) pour l'instant : c'est la 2ᵉ étape
  envisagée.

## Version 2.1.0 — Export Word (.docx) direct (remplacé en 2.2.0)

- **File > Export the open note to Word (.docx)...** : exporte la note ouverte (le rapport) en `.docx`.
  La note est enregistrée avant l'export ; le nom proposé est celui de la note (dans son dossier) ; une
  fois l'export fait, on peut ouvrir le fichier directement.
- Moteur **autonome** `docxwriter.h/.cpp` : **C++17 standard uniquement** (ni Qt, ni pandoc, ni bibliothèque zip —
  il écrit lui-même l'archive ZIP et les XML WordprocessingML). `MainWindow::onExportReportDocx` ne fournit que le
  fichier Markdown, le chargeur d'images (`QImage`) et l'écriture du résultat (`QSaveFile`).
- Rendu : titres -> styles Word *Titre 1 à 6* (volet de navigation, sommaire) ; une ligne de la note = un paragraphe
  (comme dans l'éditeur) ; gras, italique, barré, souligné, surligné, `code`, liens cliquables ; `%%commentaire%%`
  omis ; listes à puces / numérotées imbriquées (chaque liste numérotée repart à 1) et cases `[ ]`/`[x]` ;
  citations ; séparateurs `---` ; blocs ``` (Consolas, fond gris, lignes vides conservées) ; tableaux GFM
  (alignements `:---:`, ligne d'en-tête répétée sur chaque page) ; images (largeur plafonnée à ~16 cm, proportions
  conservées, PNG/JPEG repris tels quels, autres formats convertis en PNG ; image introuvable = repère rouge).
- Mise en page : A4, marges 2,5 cm, Calibri 11. Les caractères de contrôle (séquences d'échappement de terminal
  collées dans une note) et l'UTF-8 invalide sont nettoyés pour garder un `.docx` toujours valide.
- **Tests** (hors application) : `tests/docx_test.cpp` + `tests/note.md`, compilables seuls :
  `g++ -std=c++17 -Isrc tests/docx_test.cpp src/docxwriter.cpp -o docx_test && ./docx_test tests/note.md out.docx`.
  Le moteur a été validé avec python-docx, rendu avec LibreOffice, et passé en fuzzing (AddressSanitizer/UBSan).
- Pas (encore) géré : page de garde, sommaire automatique, en-têtes/pieds de page, gabarit Word personnalisé
  (styles/logo d'entreprise).

## Version 2.2.0 — Rapport Word : fenêtre à trois volets avec éditeur Word intégré

Remplace l'export direct de la 2.1.0 (le moteur `docxwriter` est supprimé) par une vraie fenêtre de rédaction.

- **File > Export to a Word report...** (et menu contextuel d'un dossier du projet) ouvre `ReportWindow` pour le dossier
  sélectionné (ou le projet entier) : **arbre des notes** à gauche, **éditeur Markdown** (lecture) au centre, **éditeur
  Word** à droite.
- **report.docx** : si `<dossier>/report.docx` existe, il est ouvert. Sinon, une fenêtre demande un **modèle** (.docx ou
  .dotx) qui est copié à la racine du dossier sous le nom `report.docx` (en cas d'annulation : rapport vierge proposé).
- **Insertion** : `Ctrl+Alt+C` (ou le bouton) dans la fenêtre insère dans le Word la **sélection** du volet Markdown, ou
  **toute la note** s'il n'y a pas de sélection, **à la ligne suivant le curseur** du document Word (une seule action
  d'annulation). Titres, paragraphes, gras/italique/barré/souligné/surligné/code, liens, listes imbriquées, citations, blocs de
  code, tableaux et images (copiées dans le document) sont convertis.
- **Éditeur Word** (`DocxEdit`) : styles de paragraphe (titres 1-6, citation, code...), police, taille, couleur, surlignage,
  gras/italique/souligné/barré, alignement, listes à puces/numérotées (niveaux), **tableaux** (insertion, lignes/colonnes
  avant/après, suppression), **images** (fichier ou collage), liens, saut de page, couper/copier/coller, annuler/rétablir, Ctrl+S.
- **Le gabarit est conservé** (cœur C++ sans Qt, testé) : toutes les parties du .docx d'origine (styles, thème, en-têtes et pieds
  de page, numérotation, polices, propriétés...) sont recopiées ; seul le corps `word/document.xml` est régénéré. Les contenus
  non éditables (table des matières, champs, sauts de section/page, formes, tableaux à cellules fusionnées ou imbriqués, révisions)
  sont gardés comme **blocs bruts** (bandeau gris dans l'éditeur) et réécrits à l'identique. Les styles manquants du gabarit
  (code, citation, liste, texte de tableau, titres) sont ajoutés à l'enregistrement ; ceux du gabarit sont réutilisés.
- Architecture : `docx_zip` (ZIP + décompresseur Deflate maison), `docx_xml` (analyseur XML), `docx_model.h` (modèle),
  `docx_package` (lecture), `docx_save` (écriture en conservant le gabarit), `docx_markdown` (Markdown -> modèle) —
  tous en **C++17 standard** ; puis la couche Qt `docxconvert` (modèle <-> `QTextDocument`), `docxedit` (éditeur + barre
  d'outils) et `reportwindow`. Tests du cœur : `tests/LISEZMOI.txt`.
- **Limites** : l'éditeur est un éditeur riche Qt (pas de pagination, d'en-têtes/pieds de page ni de table des matières
  affichés ni modifiables — ils sont conservés dans le fichier) ; la mise en page affichée est approximative (celle de Word
  fait foi) ; des mises en forme très avancées (colonnes, cadres, formes, cellules fusionnées) ne sont pas éditables ; un
  changement du nombre de colonnes d'un tableau issu du gabarit recalcule ses largeurs.

### Corrections de la 2.2.0 (modèle de rapport OSCP réel : 37 pages, numérotation, sommaire)

Trois défauts constatés sur un vrai rapport OffSec, tous corrigés :

- **Seulement 6 pages visibles sur 37.** Tout le corps du rapport est dans un grand tableau de mise en page contenant des
  **tableaux imbriqués** et des images enveloppées dans `mc:AlternateContent`, que le lecteur ne gérait pas : le tableau entier
  devenait un bloc brut. Désormais : les cellules contiennent des **blocs** (paragraphes, tableaux imbriqués, blocs bruts) ;
  un élément non géré ne rend brut que **lui-même** (plus tout le tableau) ; les images `mc:AlternateContent` sont lues
  (`w:drawing` du premier `mc:Choice`). Les lignes plus courtes que les autres ne reçoivent plus de case vide à l'enregistrement.
- **Titres sans numéro.** Les numéros viennent de la numérotation **liée au style** (`numPr` dans `Heading 1..4`,
  `numbering.xml` à niveaux `%1`, `%1.%2`...), pas du paragraphe. `NumberingCounter` (cœur) les calcule comme Word (formats
  decimal, lettres, romains, séparateur tab/espace, remise à zéro des niveaux inférieurs) ; `DocxConverter::refreshNumbering`
  les affiche en début de titre, après un chargement, une insertion ou un changement de style. Le numéro n'est **jamais
  enregistré** dans le .docx. Validé : 33 titres sur 33 identiques au sommaire enregistré par Word.
- **Sommaire invisible.** Un champ de table des matières est un bloc brut : son **texte enregistré** (titres et pages) est
  maintenant affiché dans le bandeau gris, de même que le texte des tableaux non éditables. À l'enregistrement, un document
  contenant un sommaire reçoit `w:updateFields` : Word propose de mettre les champs à jour à l'ouverture.
- **Insertion dans une cellule.** `Ctrl+Alt+C` insère après le **paragraphe du curseur**, y compris dans une cellule (le
  contenu d'un tel modèle est entièrement dans un tableau), et non plus après le tableau entier. Un tableau peut aussi être
  inséré dans une cellule.
- Fidélité de l'aller-retour (document de 37 pages identique **au pixel** après lecture/écriture) : deux défauts trouvés et
  corrigés grâce à ce test — la police d'un run ne vient plus de `eastAsia`/`cs` (l'élément `rFonts` d'origine est conservé
  tel quel tant que la police ne change pas) ; les liens du gabarit gardent leur style de caractère (plus de bleu forcé).
- Bordures d'affichage : un tableau de mise en page à bordures blanches ou absentes n'est plus encadré dans l'éditeur.
- Tests : `tests/gabarit_complexe.docx` reproduit ces cas ; fuzzing de fichiers corrompus (AddressSanitizer/UBSan) sans plantage.
- Limites restantes : tableaux à **cellules fusionnées** (restent des blocs bruts, texte visible mais non éditable) ; images
  **flottantes** (`wp:anchor`) et formes (blocs bruts) ; les numéros ne se mettent à jour qu'après une insertion ou un changement
  de style, pas à chaque frappe.

## Version 2.3.0 — Serveur HTTP de distribution de fichiers sur un dossier

- **Menu contextuel d'un dossier (arbre Project)** : deux nouvelles entrées, **http server connect** (actif sur un dossier non
  servi) et **http server disconnect** (actif sur un dossier servi).
- **http server connect** : petite fenêtre de saisie du port d'écoute (**80 par défaut**), puis démarrage d'un serveur HTTP
  qui distribue les fichiers du dossier. **Une fois l'écoute réellement active**, le dossier prend l'icône `folder_http.svg`
  (dossier vert avec un globe) et son nom est suivi de `:<port>` (ex. `tools:8080`).
- **Écoute impossible** (port déjà utilisé par un autre programme ou par un autre serveur de CyberNote, droits insuffisants
  pour un port < 1024...) : message explicatif, puis choix « Choose another port » (nouvelle saisie, avec une proposition) ou
  « Cancel » (la demande est abandonnée).
- **Plusieurs serveurs** simultanés sur plusieurs dossiers (un port par dossier). Les serveurs sont suivis dans
  `MainWindow::httpServers` (clé = chemin du dossier).
- **Arrêt** : *http server disconnect*, ou automatiquement si le dossier est supprimé, renommé, déplacé ou n'est plus dans le
  projet ouvert (`refreshHttpServerItems`, appelé à chaque reconstruction de l'arbre), et **à la fermeture du programme**
  (`closeEvent`, destructeur et `aboutToQuit` : écoute et connexions en cours sont fermées).
- **Moteur** `httpfileserver.h/.cpp`, codé en interne (QTcpServer/QTcpSocket, sans thread ni bibliothèque externe) : GET/HEAD,
  index HTML des dossiers, envoi des fichiers **par blocs** (gros fichiers sans charger la mémoire), `Content-Type`,
  `Last-Modified`, **reprise de téléchargement** (`Range`, une plage), une requête par connexion. Les requêtes servies
  s'affichent dans la barre d'état.
- **Sécurité** : jamais en dehors du dossier (`..` refusé, liens symboliques sortants refusés) ; les éléments **cachés**
  (nom commençant par `.`, dont le fichier `.target` de la machine) ne sont ni listés ni servis ; délai de 15 s pour recevoir
  l'en-tête, en-tête limité à 16 Ko, 256 connexions simultanées par serveur. Le serveur écoute sur **toutes les interfaces**
  et sans authentification : à n'utiliser que le temps nécessaire, sur un réseau de confiance.
- Version portée à **2.3.0** (CMake, boîte « About »). Fichiers ajoutés : `src/httpfileserver.h/.cpp`,
  `resources/icons/folder_http.svg`.

## Version 2.4.0 — Onglet « tools » : serveur HTTP sur le dossier tools

- **Nouvel onglet « tools »**, juste après « Project » (même QTabWidget que Project/Outline). Il liste **récursivement**
  `<dossier de l'application>/tools/` (dossiers d'abord, icône `folder_plain.svg` pour les dossiers, `file_plain.svg` pour les
  fichiers ; éléments cachés non listés ; pas de descente dans les liens symboliques). L'arbre est reconstruit à chaque
  affichage de l'onglet, en conservant dossiers dépliés et sélection.
- **En haut de l'arbre** : bouton **connect** + champ **Port** (1-65535, 80 par défaut). *connect* démarre un `HttpFileServer`
  (moteur de la 2.3.0) qui distribue le **dossier sélectionné** dans l'arbre (fichier sélectionné : son dossier ; rien de
  sélectionné : racine de `tools`).
- **Connecté** : le bouton devient **unconnect** (icône `status_connected.svg`, verte), le champ port est verrouillé, et
  l'**onglet** prend la même icône et le nom `tools:<port>`. **Déconnecté** : onglet `tools`, icône `status_disconnected.svg`
  (grise). Info-bulle de l'onglet : port et dossier servi.
- **Port indisponible** (déjà utilisé par un autre programme ou un autre serveur de CyberNote, ou interdit sans droits
  administrateur) : **message d'erreur**, le bouton reste « connect ».
- Le serveur est fermé avec le programme (comme ceux de la 2.3.0) et s'arrête si le dossier servi disparaît.
- Refactorisation : `httpPortConflict`, `httpListenFailureReason` et `showHttpRequest` sont partagés avec « http server connect ».
- Version portée à **2.4.0** (CMake, boîte « About »). Icônes ajoutées : `file_plain.svg`, `status_connected.svg`,
  `status_disconnected.svg`.

## Correctif (v1.5.0) — texte copié : retours à la ligne automatiques

- Bug : une commande plus longue que la largeur du terminal est repliée à l'écran sur
  plusieurs lignes ; copier ce texte (Ctrl+Shift+C ou Ctrl+Alt+C) y laissait de faux
  sauts de ligne.
- `TermCell` reçoit un indicateur `wrapped`, posé par `TerminalScreen::putChar` sur la
  dernière cellule d'une ligne quand le retour à la ligne est **automatique** (et pas un vrai
  LF) ; il est conservé par `insertChars`/`deleteChars` (édition readline en milieu de ligne).
- `TerminalWidget::selectedText()` re-joint alors la ligne suivante sans saut de ligne (et
  sans rogner les espaces de fin de la ligne coupée). Valable pour tous les usages du texte
  sélectionné (copie, Ctrl+Alt+C). L'image Ctrl+Shift+S / Ctrl+Alt+S reste fidèle à l'affichage.
- Limite : après un redimensionnement de la fenêtre, le texte déjà affiché n'est pas
  re-découpé par l'émulateur, donc les anciens repliements ne sont pas modifiés.

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

## Aperçu éditable, menu contextuel Table / Format / bloc de code

- **Aperçu de la bibliothèque éditable** (onglet Preview) : la fiche peut être modifiée directement.
  Elle est **enregistrée automatiquement dès qu'on la quitte** si elle a été modifiée : perte du
  focus de l'aperçu (hors ouverture d'un menu contextuel), changement d'onglet, sélection d'une
  autre fiche (ou re-clic sur la même), fermeture de l'application (`MainWindow::savePreviewIfModified`,
  signal `MarkdownEdit::focusLeft`). Rien n'est écrit si le fichier a disparu entre-temps.
  `Ctrl+Alt+C` reste actif dans l'aperçu (`MarkdownEdit::setCopyToNoteShortcutEnabled`).
- **Menu contextuel > Table** (éditeur de note *et* aperçu) : `Insert Table...` (colonnes / lignes),
  puis, quand le clic droit est dans un tableau : `Insert Row Above/Below` (jamais au-dessus de
  l'en-tête), `Insert Column Left/Right`, `Delete Row`, `Delete Column` (+ `Duplicate Row/Column`).
  Les nouvelles lignes/colonnes reprennent l'alignement de leur voisine ; le curseur se place dans la
  nouvelle cellule.
- **Menu contextuel > Insert Code Block** : crée une zone ``` vide sur une nouvelle ligne, curseur à l'intérieur.
- **Menu contextuel > Format** (sur une sélection) : Gras `**`, Italique `*`, Souligné `__`, Barré `~~`,
  Surligné `==`, Code `` ` ``, Math `$` (bascule : réappliquer retire le format) ; Titre 1 à 5, Citation,
  Liste à puces / numérotée / de tâches (même format déjà présent -> retiré) ; `Clear Formatting`.
  Les zones de code sont ignorées. Ces formats sont des formats **Markdown** (le fichier reste du .md).
- **Nom de langage d'une zone de code** : pour ```` ```bash ````, quand le curseur quitte la zone les ```
  disparaissent et `bash` reste visible **en petit, gris, à droite** de la zone ; dans la zone, la ligne
  ```` ```bash ```` est affichée normalement à gauche (mécanisme corrigé en 2.6.1, voir ci-dessous).

## Version 2.6.0 — Résultat des commandes « Execute » ajouté à la fiche

- **Execute** (menu contextuel de la note) lance toujours la ligne courante dans le terminal choisi,
  mais le **résultat est maintenant récupéré et ajouté à la fiche**, sur de nouvelles lignes juste après
  la ligne de la commande. Si la commande est sur une ligne normale, le résultat est entouré de ```;
  si elle est déjà dans une zone ```, les lignes sont ajoutées telles quelles (pas de ``` imbriqués ;
  une ligne de résultat commençant par ``` reçoit une espace de largeur nulle).
- **Le résultat va dans la fiche d'où la commande a été lancée**, même si l'utilisateur change de fiche
  avant la fin (`MainWindow::PendingCommand` : chemin du fichier, texte de la ligne, rang parmi les
  lignes identiques, repère `QTextCursor` + `MarkdownEdit::loadSerial()`) :
  - fiche toujours affichée telle quelle : insertion dans le document (`insertCommandOutputAfter`) ;
    curseur, défilement et annulation de l'utilisateur sont conservés, le timer enregistre ;
  - fiche fermée, ou rechargée entre-temps : le résultat est écrit **directement dans le fichier**
    (`insertCommandOutputInFile`, la ligne est retrouvée par son texte, sinon résultat ajouté en fin
    de fichier) ; si la fiche est de nouveau ouverte, ses modifications sont d'abord enregistrées puis
    l'affichage est **rechargé** (défilement conservé).
- Capture (`TerminalWidget::sendCommandCapture`) : la commande reste exécutée dans le shell du
  terminal (`cd`, variables... persistent, la sortie s'affiche normalement). La ligne envoyée est
  `printf '<OSC 777 début>'; COMMANDE; printf '<OSC 777 fin + code retour>' $?` ; les octets reçus
  entre les deux marqueurs sont rejoués dans un `TerminalScreen` temporaire (couleurs, `\r`, retours à
  la ligne automatiques) pour en tirer le texte. Un commentaire shell final (`cmd # note`) est retiré
  de la ligne envoyée (il avalerait le marqueur de fin) ; `cmd &` est géré.
- **Ctrl+C** dans le terminal : le résultat partiel est ajouté. Fermer le terminal abandonne les
  commandes en attente. Sans sortie, seul un message apparaît dans la barre d'état.
- Limites : shell compatible POSIX/bash/zsh (`$?`) ; une ligne se terminant par `\`, `|`, `&&` est
  envoyée sans capture ; un programme plein écran (vim, top...) n'a pas de « résultat » exploitable ;
  sortie capturée plafonnée à 8 Mo (la fin est conservée).
- Version portée à **2.6.0** (CMake, boîte « About », titre de la fenêtre).

## Version 2.6.1 — Correctif : balise ``` + nom du langage d'une zone de code

- **Bug corrigé** : la zone « curseur dedans » d'un bloc ``` allait jusqu'au début de la ligne qui SUIT la
  zone (`closeBlock.position() + length()`). Un curseur placé juste sous la zone (flèche bas, clic sur la
  ligne suivante) laissait donc la balise ouvrante affichée. La plage s'arrête maintenant à la fin de la
  ligne ``` fermante (`MarkdownHighlighter::highlightBlock`).
- **Comportement** (comme un éditeur Markdown type Obsidian) :
  - curseur **dans** la zone : la ligne ouvrante s'affiche telle que tapée, `` ```bash `` à gauche, et la
    ligne fermante ``` est visible ;
  - curseur **hors** de la zone : les ``` disparaissent (ouvrante et fermante) et seul `bash` reste, en petit,
    gris, à droite de la zone.
- Mise en œuvre : hors zone, la ligne ouvrante est transparente (7 pt, pour garder une fine bande) et le nom du
  langage est dessiné par `MarkdownEdit::paintEvent` à droite de la zone. Plus aucun alignement de bloc
  n'est modifié (ce qui évitait de polluer l'historique d'annulation et d'afficher `` ```bash `` à droite
  quand le curseur était dedans).
- Version portée à **2.6.1** (CMake, boîte « About », titre de la fenêtre).
