#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QRgb>
#include <QByteArray>
#include <QStringDecoder>

/**
 * Attributs et couleur d'une cellule de la grille de terminal.
 */
struct TermCell {
    QChar ch = QLatin1Char(' ');
    QRgb fg = qRgb(229, 229, 229);
    QRgb bg = qRgb(0, 0, 0);
    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool inverse = false;
    bool wide = false; // true pour la 1ère colonne d'un caractère "large" (CJK) -- simplifié, non exploité pour l'instant
};

using TermLine = QVector<TermCell>;

/**
 * TerminalScreen
 *
 * Émulateur d'écran VT100/xterm "complet" au sens fonctionnel : grille de
 * cellules avec attributs, écran alternatif (indispensable pour que vim/htop/
 * nano/less s'affichent correctement), région de défilement, scrollback,
 * couleurs 16/256/24-bit. Reçoit les octets bruts du pty via feed() et notifie
 * la vue (TerminalWidget) via screenChanged() pour repeindre.
 *
 * Ce n'est pas un portage du Pascal (ulinuxterm.pas ne faisait pas de vraie
 * émulation d'écran, juste un flux affiché dans un SynEdit) : c'est un
 * composant nouveau, du même esprit que le moteur de VTE/QTermWidget.
 */
class TerminalScreen : public QObject
{
    Q_OBJECT

public:
    explicit TerminalScreen(int rows, int cols, QObject *parent = nullptr);

    void resizeScreen(int rows, int cols);
    void feed(const QByteArray &data);

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    const TermLine &lineAt(int row) const; // ligne de l'écran actif (0 = haut de l'écran visible)
    const QVector<TermLine> &scrollback() const { return m_scrollback; }

    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool isAlternateScreen() const { return m_usingAlternate; }
    bool appCursorKeys() const { return m_appCursorKeys; }
    bool bracketedPaste() const { return m_bracketedPaste; }

    QString titleText() const { return m_title; }

signals:
    void screenChanged();
    void bell();
    void titleChanged(const QString &title);
    void hostResponse(const QByteArray &data); // réponse à renvoyer au shell (DSR, DA...) via le pty

private:
    int m_rows, m_cols;
    QVector<TermLine> m_normalScreen;
    QVector<TermLine> m_alternateScreen;
    QVector<TermLine> m_scrollback; // uniquement pour l'écran normal
    static constexpr int kMaxScrollback = 5000;
    bool m_usingAlternate = false;

    int m_cursorRow = 0, m_cursorCol = 0;
    int m_savedCursorRow = 0, m_savedCursorCol = 0;
    TermCell m_savedSgrCursor; // attributs SGR sauvegardés avec DECSC
    bool m_cursorVisible = true;
    bool m_pendingWrap = false; // sémantique "wrap différé" des vrais terminaux

    int m_scrollTop = 0, m_scrollBottom = 0; // région de défilement (0-based, incluse)

    bool m_appCursorKeys = false;   // DECCKM ?1
    bool m_bracketedPaste = false;  // ?2004
    bool m_originMode = false;      // DECOM ?6

    // Attributs SGR "courants" appliqués aux prochains caractères écrits
    TermCell m_currentAttrs;

    QString m_title;

    // --- État du parseur d'échappements ---
    enum class ParserState { Ground, Escape, EscapeCharset, CSI, OSC };
    ParserState m_state = ParserState::Ground;
    QString m_csiParams;
    QString m_oscBuffer;
    bool m_csiPrivate = false;

    QStringDecoder m_utf8Decoder{QStringDecoder::Utf8};

    void processChar(QChar c);
    void putChar(QChar c);
    void executeCsi(QChar finalByte);
    void executeEscape(QChar c);
    void executeOsc();

    void newLine();          // saut de ligne (avec défilement de la région courante)
    void reverseIndex();     // ESC M
    void carriageReturn() { m_cursorCol = 0; m_pendingWrap = false; }

    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void eraseChars(int n);
    void deleteChars(int n);
    void insertChars(int n);
    void insertLines(int n);
    void deleteLines(int n);
    void scrollRegionUp(int n);
    void scrollRegionDown(int n);

    void setMode(const QList<int> &params, bool priv, bool enable);
    void sgr(const QList<int> &params);
    void resetTerminal();

    void switchToAlternateScreen();
    void switchToNormalScreen();

    QVector<TermLine> &activeScreen() { return m_usingAlternate ? m_alternateScreen : m_normalScreen; }
    void clearLine(TermLine &line);
    TermLine makeBlankLine() const;

    static QList<int> parseParams(const QString &s);
    static QRgb ansiColor(int index); // 0-15
    static QRgb color256(int index);  // 0-255 (palette xterm)
};
