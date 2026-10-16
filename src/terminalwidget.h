#pragma once

#include <QWidget>
#include <QFont>
#include <QPoint>
#include "terminalscreen.h"

class Pty;
class QScrollBar;

/**
 * TerminalWidget
 *
 * Widget de rendu d'un TerminalScreen adossé à un vrai pty (classe Pty).
 * Gère le clavier (traduction en séquences VT100/xterm), la sélection à la
 * souris, le défilement du scrollback (molette + ascenseur vertical), et le
 * raccourci Ctrl+Shift+S qui copie la sélection de texte courante sous forme
 * d'image dans le presse-papier.
 */
class TerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // ~ CreateShell : lance $SHELL (ou /bin/bash) sur le pty. workingDir : dossier de
    // démarrage du shell (vide = dossier courant du processus CyberNoteQt).
    void startShell(const QString &workingDir = QString());

    // Lance un programme arbitraire (ex: "nc") avec ses arguments sur le pty, plutôt
    // que le shell interactif. Utilisé par NetcatTermDialog pour piloter netcat en
    // écoute tout en profitant du même rendu de terminal (couleurs, etc.).
    // Peut être appelé plusieurs fois de suite sur le même TerminalWidget (ex: cycles
    // connecter/déconnecter successifs) : chaque appel redémarre un pty propre.
    void startProgram(const QString &program, const QStringList &args,
                       const QString &workingDir = QString());

    // Termine le programme en cours (shell ou programme lancé via startProgram) sans
    // détruire le widget, pour permettre un redémarrage ultérieur (ex: Déconnecter).
    void stopProgram();

    bool isRunning() const;

    // Réinitialise complètement l'écran (~ RIS, ESC c) : efface le contenu affiché et le
    // scrollback. Utile avant de redémarrer un nouveau programme pour ne pas mélanger la
    // sortie d'une session précédente avec la nouvelle.
    void resetTerminal();

    bool hasSelection() const;
    QString selectedText() const;

public slots:
    void copySelection();          // ~ MenuCopyClick
    void pasteFromClipboard();     // ~ MenuPasteClick
    void copySelectionAsImage();   // Ctrl+Shift+S ~ (nouveau) MenuCopyImageClick sur la sélection
    void clearSelection();         // ~ MenuDeselectClick
    void sendCommand(const QString &command); // envoie une commande + Entrée au shell (~ Menu_Execute de MainWindow)

signals:
    void titleChanged(const QString &title);
    void shellFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onScreenChanged();
    void onPtyFinished();
    void onCursorBlinkTimeout();
    void onScrollBarValueChanged(int value);

private:
    Pty *pty = nullptr;
    TerminalScreen *screen = nullptr;
    QScrollBar *m_scrollBar = nullptr; // ascenseur vertical (scrollback), à droite du widget

    QFont m_font;
    int m_cellW = 8, m_cellH = 16, m_ascent = 12;

    int m_scrollOffset = 0; // 0 = bas de l'écran (temps réel), N = décalé de N lignes dans le scrollback
    bool m_updatingScrollBar = false; // garde de ré-entrance (évite la boucle valueChanged <-> scrollViewport)

    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selStart, m_selEnd; // .y() = ligne "globale" (scrollback+écran), .x() = colonne

    bool m_cursorBlinkOn = true;

    void recomputeCellMetrics();
    void applySizeToScreenAndPty();
    void syncScrollBar(); // met à jour l'ascenseur (plage/valeur/activation) depuis l'état courant

    int firstVisibleGlobalLine() const;
    const TermLine &lineAtGlobal(int global) const;
    QPoint posToCell(const QPoint &pos) const;  // pixel -> (col, ligne globale)

    void sendToShell(const QByteArray &bytes);
    void scrollViewport(int deltaLines);
    void resetScrollToBottom();
};
