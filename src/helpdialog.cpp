#include "helpdialog.h"

#include <QTextBrowser>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("CyberNotePas - Help"));
    resize(820, 640);

    auto *browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setHtml(helpHtml());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(browser);
    layout->addWidget(buttons);
}

QString HelpDialog::helpHtml()
{
    return QStringLiteral(R"HTML(
<html><body style="font-family: sans-serif;">

<h1>CyberNotePas</h1>
<p>Outil de prise de notes structuré par projet pour le pentest / la sécurité offensive :
arborescence de machines cibles, notes au format Markdown avec aperçu en direct,
fiches d'information par cible, bibliothèque de commandes réutilisables, et
terminaux Linux intégrés (vrais pseudo-terminaux, pas de simples pipes).</p>

<h2>1. Gestion de projet</h2>
<ul>
<li><b>File &gt; New Project</b> : choisit un dossier vide et y démarre un nouveau projet.</li>
<li><b>File &gt; Open Project</b> : ouvre un projet existant (charge son arborescence dans le panneau "Project").</li>
<li><b>File &gt; Export to a Word report...</b> (aussi dans le menu contextuel d'un dossier) : ouvre la fenêtre de rédaction du
rapport pour le dossier sélectionné (ou le projet entier). Trois volets : à gauche l'<b>arbre des notes</b> du dossier ; au centre
l'<b>éditeur Markdown</b> qui affiche la note choisie ; à droite l'<b>éditeur Word</b> du fichier <code>report.docx</code> situé à la
racine du dossier. S'il n'existe pas, une fenêtre vous demande le <b>modèle Word</b> (.docx ou .dotx), qui est copié à cet endroit
sous le nom <code>report.docx</code> (ou vous pouvez choisir un rapport vierge). Le modèle est conservé : styles, en-têtes et pieds
de page, numérotation, table des matières... Dans le volet Markdown, <b>Ctrl+Alt+C</b> (ou le bouton) insère dans le rapport Word le
texte sélectionné — ou toute la note sans sélection — à la ligne qui suit le curseur du document Word : titres, listes, tableaux, code,
images, liens et mise en forme sont convertis. L'éditeur Word permet styles de paragraphe (titres 1-6, citation, code...),
police/taille/couleur/surlignage, gras/italique/souligné/barré, alignement, listes, <b>tableaux</b> (insertion, lignes et colonnes),
<b>images</b> (fichier ou copier-coller), liens, saut de page, couper/copier/coller et annuler/rétablir. Ctrl+S enregistre le rapport.
Les titres numérotés par le style du modèle (1, 1.1, 1.1.1...) sont affichés avec leur numéro, calculé comme Word (affichage seulement : Word
le régénère) ; les tableaux de mise en page et leurs tableaux imbriqués sont modifiables. Les éléments que l'éditeur ne sait pas modifier
(table des matières, champs, tableaux à cellules fusionnées, formes...) sont affichés dans un bandeau gris — avec leur texte pour la table
des matières — et conservés à l'identique. Un rapport contenant une table des matières demande à Word de mettre les champs à jour à
l'ouverture (répondre « Oui » pour actualiser sommaire et numéros de page).</li>
<li>Le dernier projet et la dernière note ouverts sont mémorisés (<code>cybernotepas.conf</code>) et
rechargés automatiquement au démarrage.</li>
</ul>

<h3>Arborescence "Project"</h3>
<p>Clic simple sur un fichier <code>.md</code>/<code>.txt</code> : l'ouvre dans l'éditeur de note.
Barre d'outils et menu contextuel (clic droit) :</p>
<ul>
<li><b>Add Machine</b> : crée un sous-dossier "machine cible" avec un fichier <code>.target</code>
(voir §2) et la structure de sous-dossiers par défaut définie dans <code>cybernotepas.conf</code>
(section <code>&lt;AddNewMachine&gt;</code>, ex. <code>01-scanning</code>, <code>02-enumeration</code>...).</li>
<li><b>Add Folder</b> : crée un simple sous-dossier.</li>
<li><b>New Note</b> : crée une nouvelle note <code>.md</code> vide dans le dossier sélectionné (refusé, avec nouvelle saisie du nom,
si une note de ce nom existe déjà).</li>
<li><b>Copy</b> / <b>Paste</b> : copie un fichier <b>ou un dossier entier</b> (avec tout
son contenu, sous-dossiers compris) et le colle dans le dossier sélectionné. Si un
élément du même nom existe déjà à destination, un suffixe est ajouté automatiquement
(<code>nom - copy</code>, <code>nom - copy (2)</code>...) plutôt que d'écraser quoi que ce
soit. Impossible de coller un dossier dans lui-même ou dans un de ses propres
sous-dossiers.</li>
<li><b>Make a copy</b> : duplique la note sélectionnée — ou un <b>dossier avec tout son contenu</b> — sous un nouveau nom, dans le
même dossier que l'original (contrairement à Copy/Paste ci-dessus, qui place la copie
dans un dossier de destination choisi). Pour un dossier, la copie contient tous ses
sous-dossiers et fichiers ; le dossier racine du projet ne peut pas être dupliqué.</li>
<li><b>Move file into</b> : déplace la note sélectionnée vers un autre dossier.</li>
<li><b>Merge entire file with...</b> : ajoute le contenu de la note sélectionnée à la fin d'un
autre fichier choisi, puis supprime la note d'origine.</li>
<li><b>Rename</b> : renomme le fichier ou dossier sélectionné (le nom actuel est prérempli dans le champ ; aussi possible en tapant
directement sur le nom dans l'arbre après avoir appuyé sur <b>F2</b>).</li>
<li><b>Delete</b> : supprime définitivement (avec confirmation) le fichier ou dossier
sélectionné et tout son contenu.</li>
<li><b>Insert the notes of this folder into the open note...</b> (menu contextuel d'un dossier) : prépare un
rapport à partir d'un gabarit. Ouvrez d'abord le gabarit, puis choisissez le dossier (ex. une machine ou le
projet entier) : une fenêtre liste ses notes et celles de ses sous-dossiers, à cocher. Les notes et dossiers
dont le nom commence par <code>_</code> (fausses pistes) sont <b>décochés par défaut</b>. Les boutons
<i>Up</i>/<i>Down</i> changent l'ordre au sein d'un même niveau. Option : un titre (chemin de la note) est ajouté
avant chaque note, au niveau choisi, et les titres de la note sont décalés pour s'emboîter dessous. Les notes
cochées sont insérées à la ligne suivant le curseur du gabarit ; leurs images sont copiées dans le dossier
du gabarit.</li>
<li><b>http server connect</b> / <b>http server disconnect</b> (menu contextuel d'un dossier) : lance / arrête un petit
serveur HTTP interne qui distribue les fichiers du dossier (par exemple pour les télécharger depuis une machine cible avec
<code>wget</code> ou <code>curl</code>). Une petite fenêtre demande le port d'écoute (80 par défaut ; sous Linux, les ports
inférieurs à 1024 demandent les droits administrateur). Si le port est déjà utilisé, un message prévient et propose de
choisir un autre port ou d'annuler. Quand le serveur écoute réellement, le dossier prend une <b>icône verte (globe)</b> et son
nom est suivi de <code>:port</code> ; <i>http server disconnect</i> (disponible sur un dossier servi) le ferme. Plusieurs
dossiers peuvent être servis en même temps, sur des ports différents. Le serveur écoute sur <b>toutes les interfaces</b> ;
les fichiers et dossiers cachés (nom commençant par <code>.</code>, dont <code>.target</code>) ne sont jamais servis. Tous
les serveurs sont fermés à la fermeture du programme, ou si le dossier servi est supprimé, renommé ou déplacé.</li>
<li><b>Exclude from the report / Include in the report</b> : ajoute ou retire le préfixe <code>_</code> du nom
de la note ou du dossier sélectionné. Les éléments exclus apparaissent en gris dans l'arbre.</li>
</ul>

<h3>Onglet "tools"</h3>
<p>À côté de l'onglet <b>Project</b>, l'onglet <b>tools</b> liste (récursivement, avec des icônes de dossier et de fichier)
le dossier <code>tools</code> situé à côté de l'application. Au-dessus de l'arbre : un bouton <b>connect</b> et le
<b>port</b> d'écoute (80 par défaut). <b>connect</b> démarre un serveur HTTP interne qui distribue le <b>dossier
sélectionné</b> dans l'arbre (pour un fichier : son dossier ; sans sélection : tout le dossier <code>tools</code>).
Connecté, le bouton devient <b>unconnect</b>, et le bouton comme l'onglet affichent une icône verte ; l'onglet s'appelle alors
<code>tools:port</code> (il redevient <code>tools</code>, avec une icône grise, une fois déconnecté). Si le port est déjà
utilisé (ou interdit : ports &lt; 1024 sans droits administrateur), un message d'erreur s'affiche. Le serveur est fermé
à la fermeture du programme. Mêmes règles que <i>http server connect</i> : écoute sur toutes les interfaces, éléments
cachés (<code>.xxx</code>) jamais servis.</p>

<h2>2. Informations cible (fichier .target)</h2>
<p>Quand un dossier de la note ouverte (ou un de ses parents, jusqu'à la racine du
projet) contient un fichier <code>.target</code>, le panneau <b>Target</b> apparaît sous
l'arborescence "Project" avec une grille Nom/Valeur éditable directement
(IP, nom de la cible, commentaire...). Toute modification d'une cellule est
sauvegardée immédiatement dans le fichier <code>.target</code> correspondant.</p>

<h2>3. Éditeur de note (Markdown, mode "édition live")</h2>
<p>L'éditeur affiche le Markdown avec mise en forme immédiate, à la façon d'Obsidian/Typora :
les balises de mise en forme ne sont visibles que lorsque le curseur de texte
se trouve dessus ou dedans ; sinon seul le résultat stylé est affiché.</p>
<ul>
<li><b>Titres</b> : <code># Titre</code> à <code>##### Titre</code> (taille croissante). Le <code>#</code>
n'apparaît que si le curseur est sur la ligne du titre.</li>
<li><b>Styles inline</b> : <code>**gras**</code>, <code>*italique*</code>, <code>__souligné__</code>,
<code>~~barré~~</code>, <code>==surligné==</code>, <code>`code en ligne`</code>, <code>$math$</code>.
Les balises (<code>**</code>, <code>__</code>...) ne s'affichent que si le curseur est entre les deux.</li>
<li><b>Blocs de code</b> : <code>```langage</code> ... <code>```</code> (langages reconnus pour la
coloration : python, pascal, javascript/typescript, c/c++/c#, powershell). Le bloc entier
est mis en évidence par un fond coloré pleine largeur, même sur les lignes vides. Les
balises <code>```</code> elles-mêmes ne sont visibles que si le curseur se trouve n'importe
où à l'intérieur du bloc.</li>
<li><b>Images</b> : <code>![[nom_de_fichier.png]]</code> insère et affiche l'image correspondante
(chemin relatif au dossier de la note, ou <code>~/...</code> pour le dossier personnel).
Le texte de la balise reste toujours présent dans la note (donc éditable et sauvegardé
correctement) mais n'est affiché, juste au-dessus de l'image qui elle reste toujours
visible en dessous, que lorsque le curseur de texte se trouve dans ce texte ou sur
l'image elle-même.</li>
<li><b>Coller une image</b> (Ctrl+V) : si le presse-papier contient une image (capture
d'écran, copie depuis un autre logiciel...), elle est automatiquement enregistrée à
côté de la note et insérée comme ci-dessus.</li>
</ul>

<h3>Structures de bloc (listes, citations, tableaux...)</h3>
<p>Ces éléments restent <b>toujours visibles</b> (contrairement aux marqueurs d'emphase
ci-dessus) : ce sont des repères de structure, pas des décorations à masquer.</p>
<ul>
<li><b>Listes à puces</b> (<code>-</code>, <code>*</code>, <code>+</code>) et <b>numérotées</b>
(<code>1.</code>) : imbrication gérée par l'indentation (espaces en début de ligne). Le
marqueur est mis en couleur pour bien le distinguer du texte.</li>
<li><b>Cases à cocher GFM</b> : <code>- [ ] à faire</code> / <code>- [x] fait</code>. Une tâche
cochée est affichée barrée. (Remarque : la case n'est pas cliquable pour l'instant,
il faut éditer le texte <code>[ ]</code>/<code>[x]</code> directement.)</li>
<li><b>Citations</b> : <code>&gt; texte</code>, imbriquées avec <code>&gt; &gt; texte</code> etc.
Le contenu est affiché en italique atténué, avec une marge gauche qui augmente à
chaque niveau d'imbrication.</li>
<li><b>Règles horizontales</b> : <code>---</code>, <code>***</code> ou <code>___</code> (au moins 3
occurrences du même caractère) sur leur propre ligne.</li>
<li><b>Titres Setext</b> : un titre peut aussi s'écrire sur deux lignes, la seconde ne
contenant que des <code>=</code> (titre de niveau 1) ou des <code>-</code> (niveau 2) --
reconnu automatiquement, y compris la distinction avec une simple règle horizontale
<code>---</code> (qui elle n'est pas précédée d'un paragraphe).</li>
<li><b>Tableaux GFM</b> (<code>| colonne 1 | colonne 2 |</code> avec une ligne
<code>|---|---|</code> juste après l'en-tête) : convertis en <b>vrais tableaux
éditables</b> à l'ouverture de la note, exactement comme dans Obsidian :
  <ul>
  <li>Cliquer dans une cellule permet de taper directement dedans.</li>
  <li><b>Tab</b> / <b>Maj+Tab</b> passe à la cellule suivante/précédente ; Tab dans la
  toute dernière cellule ajoute automatiquement une nouvelle ligne.</li>
  <li><b>Clic droit</b> dans une cellule ouvre un sous-menu <b>Table</b> : Insert Row
  Above/Below, Duplicate Row, Delete Row, Insert Column Left/Right, Duplicate Column,
  Delete Column.</li>
  <li>L'alignement de colonne (<code>:---</code>, <code>:---:</code>, <code>---:</code>)
  est conservé et réappliqué à la sauvegarde, avec les colonnes ré-alignées visuellement
  dans le fichier <code>.md</code> (comme le fait Obsidian lui-même).</li>
  </ul>
</li>
</ul>

<p>Barre d'outils / menu contextuel de la note : <b>Copy</b>, <b>Cut</b>, <b>Paste</b>, et
<b>Execute</b> (voir §5).</p>
<p>La note est sauvegardée automatiquement dès qu'elle est modifiée (vérification
chaque seconde).</p>

<h2>4. Bibliothèque</h2>
<p>Le panneau "Libraries" (à droite) présente une arborescence de fichiers <code>.md</code>
de référence (commandes-types, aide-mémoires...) situé dans le dossier
<code>libraries/</code> à côté de l'exécutable. Double-cliquer sur une entrée
ajoute son contenu à la fin de la note actuellement ouverte.</p>
<ul>
<li><b>Ctrl+Alt+C</b> sur un fichier de l'arborescence : même effet que le double-clic (contenu ajouté à la
fin de la note ouverte).</li>
<li><b>Ctrl+Alt+C</b> dans l'aperçu (onglet <i>Preview</i>) : le <b>texte sélectionné</b> est inséré sur la
ligne qui suit la position du curseur de la note en cours d'édition. Les tableaux sont conservés et
les images référencées sont copiées dans le dossier de la note.</li>
</ul>
<p>Dans les deux cas, les paramètres <code>&lt;...&gt;</code> sont substitués (voir ci-dessous) et les images
référencées (<code>![[image.png]]</code>) sont <b>copiées dans le dossier de la note</b> : si vous copiez le dossier
des notes, aucune image n'est perdue. Si un fichier de même nom mais différent existe déjà, l'image copiée est
renommée (<code>image_1.png</code>...) et la balise ajustée.</p>

<h3>Substitution automatique des paramètres &lt;...&gt;</h3>
<p>Si la grille <b>Target</b> (§2) est affichée pour la note en cours, tout texte de
la forme <code>&lt;nomDuParametre&gt;</code> présent dans la note de bibliothèque ajoutée
est automatiquement remplacé par la valeur correspondante de la grille. Par exemple,
avec une grille contenant <code>targetIP = 10.0.0.5</code> et <code>targetName = web01</code>,
une note de bibliothèque contenant :</p>
<pre>nmap -sV -p- &lt;targetIP&gt;
# Cible : &lt;targetName&gt;</pre>
<p>devient, une fois ajoutée à la note :</p>
<pre>nmap -sV -p- 10.0.0.5
# Cible : web01</pre>
<p>Le paramètre spécial <code>&lt;attackIP&gt;</code> n'est pas lu depuis la grille : il est
remplacé par l'adresse IP de la machine locale <b>pertinente pour joindre la cible</b>
(utile pour générer directement des commandes de reverse shell, de transfert de
fichier, etc.). Cette IP est déterminée en interrogeant le système de routage
(<code>ip route get &lt;targetIP&gt;</code>), exactement comme le ferait une vraie connexion
réseau vers cette cible : si une interface VPN (<code>tun0</code> ou autre) est active et que
la route vers la cible passe par elle, c'est bien l'IP du VPN qui est utilisée, pas
celle de l'interface physique. Si aucune IP cible n'est disponible, l'IP source de la
route par défaut (vers Internet) est utilisée à la place.</p>
<p>Si la grille Target n'est pas affichée pour la note courante (aucun fichier
<code>.target</code> trouvé), aucune substitution n'est effectuée : le texte de la note de
bibliothèque est ajouté tel quel.</p>

<h2>5. Terminaux Linux</h2>
<p><b>Tools &gt; Start New Linux Term</b> ouvre une nouvelle fenêtre de terminal,
avec un nom optionnel à saisir (sinon numéroté automatiquement). Chaque terminal
ouvert apparaît aussi comme entrée dans le menu <b>Tools</b> pour lui redonner le focus.</p>
<p>Il s'agit d'un <b>vrai terminal</b> adossé à un pseudo-terminal (pty), pas de simples
tuyaux : le shell (bash) s'exécute exactement comme dans un terminal classique
(couleurs, historique de commandes, complétion, <code>Ctrl+C</code>/<code>Ctrl+Z</code>, saisie
de mot de passe <code>sudo</code> sans écho...), et les applications plein écran
(<code>vim</code>, <code>nano</code>, <code>htop</code>, <code>less</code>...) s'affichent normalement grâce à la
gestion de l'écran alternatif.</p>
<ul>
<li><b>Sélection à la souris</b> : cliquer-glisser sélectionne du texte (avec défilement
dans l'historique/scrollback quand on n'est pas dans une application plein écran).</li>
<li><b>Molette</b> : fait défiler l'historique ; relayée en flèches haut/bas pendant une
application plein écran (comme <code>less</code>, <code>vim</code>).</li>
<li><b>Menu contextuel</b> (clic droit) et raccourcis : <b>Copy</b> (Ctrl+Shift+C),
<b>Paste</b> (Ctrl+Shift+V), <b>Deselect</b>.</li>
<li><b>Copy Selection As Image</b> (<b>Ctrl+Shift+S</b>) : copie la sélection de texte en
cours sous forme d'image (fidèle aux couleurs/gras/police) dans le presse-papier. La largeur de
l'image correspond au texte le plus long de la sélection + 16 pixels de marge (et non à la
largeur du terminal).</li>
<li><b>Copy Selection As Image Into Note</b> (<b>Ctrl+Alt+S</b>) : même image, mais insérée
directement dans la note ouverte, sur la ligne qui suit celle du curseur (l'image est enregistrée
dans le dossier de la note, comme pour un collage Ctrl+V). Des captures successives
s'empilent les unes sous les autres.</li>
<li><b>Copy Selection As Text Into Note</b> (<b>Ctrl+Alt+C</b>) : insère le <i>texte</i> sélectionné
directement dans la note ouverte, sur les lignes qui suivent celle du curseur. Le texte est inséré
tel quel et encadré par deux lignes <code>```</code> : il est donc pris comme un extrait de code.</li>
</ul>

<h2>6. Envoyer une commande de la note vers un terminal</h2>
<p>Le menu <b>Execute</b> (barre d'outils ou menu contextuel de la note) envoie la
ligne de texte où se trouve le curseur, dans la note, à un terminal Linux ouvert :</p>
<ul>
<li>Un seul terminal ouvert : la commande y est envoyée directement.</li>
<li>Plusieurs terminaux ouverts : une boîte de dialogue liste leurs noms pour choisir
la destination.</li>
<li>Aucun terminal ouvert : un message invite à en ouvrir un d'abord.</li>
</ul>

<h2>7. Raccourcis clavier récapitulatifs</h2>
<table cellpadding="4">
<tr><td><b>F2</b></td><td>Renommer l'élément sélectionné dans l'arborescence Project</td></tr>
<tr><td><b>F1</b></td><td>Afficher cette aide</td></tr>
<tr><td><b>Ctrl+Shift+S</b></td><td>(dans un terminal) copier la sélection sous forme d'image</td></tr>
<tr><td><b>Ctrl+Alt+C</b></td><td>(dans un terminal) insérer le texte sélectionné dans la note ouverte, encadré par ``` (extrait de code)</td></tr>
<tr><td><b>Ctrl+Alt+S</b></td><td>(dans un terminal) insérer la sélection sous forme d'image dans la note ouverte</td></tr>
<tr><td><b>Ctrl+Shift+C / V</b></td><td>(dans un terminal) copier / coller</td></tr>
<tr><td><b>Ctrl+C/V/X</b></td><td>Copier/Coller/Couper (note et terminal, usage standard)</td></tr>
</table>

<h2>8. À propos</h2>
<p>Portage Qt6/C++ (Kali/Linux) du logiciel Lazarus/Free Pascal CyberNotePas d'origine.
Voir <b>Help &gt; About CyberNotePas</b> pour les informations de version.</p>

</body></html>
)HTML");
}
