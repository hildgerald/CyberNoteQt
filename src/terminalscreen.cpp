#include "terminalscreen.h"

#include <algorithm>

TerminalScreen::TerminalScreen(int rows, int cols, QObject *parent)
    : QObject(parent)
    , m_rows(rows)
    , m_cols(cols)
{
    m_normalScreen.resize(m_rows);
    m_alternateScreen.resize(m_rows);
    for (int i = 0; i < m_rows; ++i) {
        m_normalScreen[i] = makeBlankLine();
        m_alternateScreen[i] = makeBlankLine();
    }
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
}

TermLine TerminalScreen::makeBlankLine() const
{
    TermLine line(m_cols);
    for (auto &c : line) {
        c.fg = m_currentAttrs.fg;
        c.bg = m_currentAttrs.bg;
    }
    return line;
}

void TerminalScreen::clearLine(TermLine &line)
{
    line.clear();
    line.resize(m_cols);
    for (auto &c : line) {
        c.fg = m_currentAttrs.fg;
        c.bg = m_currentAttrs.bg;
    }
}

const TermLine &TerminalScreen::lineAt(int row) const
{
    static const TermLine empty;
    const QVector<TermLine> &scr = m_usingAlternate ? m_alternateScreen : m_normalScreen;
    if (row < 0 || row >= scr.size())
        return empty;
    return scr.at(row);
}

void TerminalScreen::resizeScreen(int rows, int cols)
{
    if (rows == m_rows && cols == m_cols)
        return;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;

    auto resizeBuf = [&](QVector<TermLine> &buf) {
        // Ajuste le nombre de colonnes de chaque ligne existante
        for (auto &line : buf)
            line.resize(cols);
        // Ajuste le nombre de lignes
        if (buf.size() < rows) {
            while (buf.size() < rows) {
                TermLine l(cols);
                buf.append(l);
            }
        } else if (buf.size() > rows) {
            buf.resize(rows);
        }
    };
    resizeBuf(m_normalScreen);
    resizeBuf(m_alternateScreen);

    m_rows = rows;
    m_cols = cols;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_cursorRow = std::min(m_cursorRow, m_rows - 1);
    m_cursorCol = std::min(m_cursorCol, m_cols - 1);
    m_pendingWrap = false;

    emit screenChanged();
}

void TerminalScreen::feed(const QByteArray &data)
{
    const QString text = m_utf8Decoder(data);
    for (const QChar c : text)
        processChar(c);
    emit screenChanged();
}

void TerminalScreen::processChar(QChar c)
{
    const ushort u = c.unicode();

    switch (m_state) {
    case ParserState::Ground:
        if (u == 0x1B) { m_state = ParserState::Escape; return; }
        if (u == '\r') { carriageReturn(); return; }
        if (u == '\n') { newLine(); return; }
        if (u == '\b') { if (m_cursorCol > 0) { --m_cursorCol; m_pendingWrap = false; } return; }
        if (u == '\t') {
            int next = ((m_cursorCol / 8) + 1) * 8;
            m_cursorCol = std::min(next, m_cols - 1);
            return;
        }
        if (u == 0x07) { emit bell(); return; }
        if (u == 0x0E || u == 0x0F) { return; } // shift-out/in (jeux de caractères) : ignorés
        if (u < 0x20) { return; } // autres caractères de contrôle ignorés
        putChar(c);
        return;

    case ParserState::Escape:
        if (u == '[') { m_state = ParserState::CSI; m_csiParams.clear(); m_csiPrivate = false; return; }
        if (u == ']') { m_state = ParserState::OSC; m_oscBuffer.clear(); return; }
        if (u == '(' || u == ')' || u == '*' || u == '+') { m_state = ParserState::EscapeCharset; return; }
        executeEscape(c);
        m_state = ParserState::Ground;
        return;

    case ParserState::EscapeCharset:
        // Désignation de jeu de caractères (ESC ( B, ESC ( 0, ...) : non exploité, on
        // consomme simplement l'octet qui suit et on ignore (traité comme ASCII/latin1).
        m_state = ParserState::Ground;
        return;

    case ParserState::CSI:
        if (u == '?' && m_csiParams.isEmpty()) { m_csiPrivate = true; return; }
        if ((u >= '0' && u <= '9') || u == ';' || u == ':') { m_csiParams += c; return; }
        if (u >= 0x40 && u <= 0x7E) {
            executeCsi(c);
            m_state = ParserState::Ground;
            return;
        }
        // caractère intermédiaire (espace, !, etc.) : ignoré silencieusement
        return;

    case ParserState::OSC:
        if (u == 0x07) { executeOsc(); m_state = ParserState::Ground; return; }
        if (u == 0x1B) { executeOsc(); m_state = ParserState::Ground; return; } // ST simplifié
        m_oscBuffer += c;
        return;
    }
}

void TerminalScreen::putChar(QChar c)
{
    if (m_pendingWrap) {
        newLine();
        m_cursorCol = 0;
        m_pendingWrap = false;
    }

    QVector<TermLine> &scr = activeScreen();
    if (m_cursorRow >= 0 && m_cursorRow < scr.size()) {
        TermLine &line = scr[m_cursorRow];
        if (m_cursorCol >= 0 && m_cursorCol < line.size()) {
            TermCell cell = m_currentAttrs;
            cell.ch = c;
            line[m_cursorCol] = cell;
        }
    }

    if (m_cursorCol + 1 >= m_cols) {
        m_pendingWrap = true; // le retour à la ligne réel n'a lieu qu'au prochain caractère
    } else {
        ++m_cursorCol;
    }
}

void TerminalScreen::newLine()
{
    if (m_cursorRow == m_scrollBottom) {
        scrollRegionUp(1);
    } else if (m_cursorRow < m_rows - 1) {
        ++m_cursorRow;
    }
    m_pendingWrap = false;
}

void TerminalScreen::reverseIndex()
{
    if (m_cursorRow == m_scrollTop)
        scrollRegionDown(1);
    else if (m_cursorRow > 0)
        --m_cursorRow;
    m_pendingWrap = false;
}

void TerminalScreen::scrollRegionUp(int n)
{
    QVector<TermLine> &scr = activeScreen();
    for (int k = 0; k < n; ++k) {
        // La ligne qui sort par le haut de la région va dans le scrollback,
        // uniquement pour l'écran normal et seulement si la région couvre tout l'écran
        // (comportement usuel d'un terminal : le scrollback ne s'alimente pas derrière
        // les applications plein écran qui redéfinissent une sous-région).
        if (!m_usingAlternate && m_scrollTop == 0 && m_scrollTop < scr.size()) {
            m_scrollback.append(scr[m_scrollTop]);
            if (m_scrollback.size() > kMaxScrollback)
                m_scrollback.removeFirst();
        }
        for (int r = m_scrollTop; r < m_scrollBottom; ++r)
            scr[r] = scr[r + 1];
        scr[m_scrollBottom] = makeBlankLine();
    }
}

void TerminalScreen::scrollRegionDown(int n)
{
    QVector<TermLine> &scr = activeScreen();
    for (int k = 0; k < n; ++k) {
        for (int r = m_scrollBottom; r > m_scrollTop; --r)
            scr[r] = scr[r - 1];
        scr[m_scrollTop] = makeBlankLine();
    }
}

void TerminalScreen::eraseInDisplay(int mode)
{
    QVector<TermLine> &scr = activeScreen();
    if (mode == 0) { // curseur -> fin d'écran
        eraseInLine(0);
        for (int r = m_cursorRow + 1; r < m_rows; ++r)
            clearLine(scr[r]);
    } else if (mode == 1) { // début d'écran -> curseur
        eraseInLine(1);
        for (int r = 0; r < m_cursorRow; ++r)
            clearLine(scr[r]);
    } else { // 2 ou 3 : tout l'écran
        for (int r = 0; r < m_rows; ++r)
            clearLine(scr[r]);
    }
}

void TerminalScreen::eraseInLine(int mode)
{
    QVector<TermLine> &scr = activeScreen();
    if (m_cursorRow < 0 || m_cursorRow >= scr.size())
        return;
    TermLine &line = scr[m_cursorRow];
    TermCell blank; blank.fg = m_currentAttrs.fg; blank.bg = m_currentAttrs.bg;

    if (mode == 0) {
        for (int c = m_cursorCol; c < line.size(); ++c) line[c] = blank;
    } else if (mode == 1) {
        for (int c = 0; c <= m_cursorCol && c < line.size(); ++c) line[c] = blank;
    } else {
        for (int c = 0; c < line.size(); ++c) line[c] = blank;
    }
}

void TerminalScreen::eraseChars(int n)
{
    QVector<TermLine> &scr = activeScreen();
    if (m_cursorRow < 0 || m_cursorRow >= scr.size())
        return;
    TermLine &line = scr[m_cursorRow];
    TermCell blank; blank.fg = m_currentAttrs.fg; blank.bg = m_currentAttrs.bg;
    for (int c = m_cursorCol; c < std::min(m_cursorCol + n, (int)line.size()); ++c)
        line[c] = blank;
}

void TerminalScreen::deleteChars(int n)
{
    QVector<TermLine> &scr = activeScreen();
    if (m_cursorRow < 0 || m_cursorRow >= scr.size())
        return;
    TermLine &line = scr[m_cursorRow];
    for (int i = 0; i < n; ++i) {
        if (m_cursorCol < line.size())
            line.remove(m_cursorCol);
        TermCell blank; blank.fg = m_currentAttrs.fg; blank.bg = m_currentAttrs.bg;
        line.append(blank);
    }
}

void TerminalScreen::insertChars(int n)
{
    QVector<TermLine> &scr = activeScreen();
    if (m_cursorRow < 0 || m_cursorRow >= scr.size())
        return;
    TermLine &line = scr[m_cursorRow];
    for (int i = 0; i < n; ++i) {
        TermCell blank; blank.fg = m_currentAttrs.fg; blank.bg = m_currentAttrs.bg;
        if (m_cursorCol < line.size())
            line.insert(m_cursorCol, blank);
        if (line.size() > m_cols)
            line.resize(m_cols);
    }
}

void TerminalScreen::insertLines(int n)
{
    QVector<TermLine> &scr = activeScreen();
    for (int i = 0; i < n; ++i) {
        for (int r = m_scrollBottom; r > m_cursorRow; --r)
            scr[r] = scr[r - 1];
        if (m_cursorRow <= m_scrollBottom)
            scr[m_cursorRow] = makeBlankLine();
    }
}

void TerminalScreen::deleteLines(int n)
{
    QVector<TermLine> &scr = activeScreen();
    for (int i = 0; i < n; ++i) {
        for (int r = m_cursorRow; r < m_scrollBottom; ++r)
            scr[r] = scr[r + 1];
        if (m_scrollBottom < scr.size())
            scr[m_scrollBottom] = makeBlankLine();
    }
}

QList<int> TerminalScreen::parseParams(const QString &s)
{
    QList<int> result;
    if (s.isEmpty())
        return result;
    const QStringList parts = s.split(';');
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        result.append(ok ? v : 0);
    }
    return result;
}

void TerminalScreen::executeCsi(QChar finalByte)
{
    const QList<int> params = parseParams(m_csiParams);
    auto p = [&](int idx, int def) -> int {
        if (idx < params.size() && params.at(idx) != 0) return params.at(idx);
        if (idx < params.size()) return params.at(idx) == 0 ? 0 : params.at(idx);
        return def;
    };
    const int p0 = params.isEmpty() ? 0 : params.at(0);
    const int n = (p0 == 0) ? 1 : p0; // la plupart des séquences de mouvement : 0 <=> 1

    switch (finalByte.unicode()) {
    case 'A': m_cursorRow = std::max(m_scrollTop, m_cursorRow - n); m_pendingWrap = false; break;
    case 'B': m_cursorRow = std::min(m_scrollBottom, m_cursorRow + n); m_pendingWrap = false; break;
    case 'C': m_cursorCol = std::min(m_cols - 1, m_cursorCol + n); m_pendingWrap = false; break;
    case 'D': m_cursorCol = std::max(0, m_cursorCol - n); m_pendingWrap = false; break;
    case 'E': m_cursorCol = 0; m_cursorRow = std::min(m_rows - 1, m_cursorRow + n); break; // CNL
    case 'F': m_cursorCol = 0; m_cursorRow = std::max(0, m_cursorRow - n); break; // CPL
    case 'G': m_cursorCol = std::min(m_cols - 1, std::max(0, (p0 == 0 ? 1 : p0) - 1)); m_pendingWrap = false; break; // CHA
    case 'd': m_cursorRow = std::min(m_rows - 1, std::max(0, (p0 == 0 ? 1 : p0) - 1)); break; // VPA
    case 'H': case 'f': {
        const int row = (params.size() > 0 && params.at(0) != 0) ? params.at(0) : 1;
        const int col = (params.size() > 1 && params.at(1) != 0) ? params.at(1) : 1;
        m_cursorRow = std::min(m_rows - 1, std::max(0, row - 1));
        m_cursorCol = std::min(m_cols - 1, std::max(0, col - 1));
        m_pendingWrap = false;
        break;
    }
    case 'J': eraseInDisplay(p0); break;
    case 'K': eraseInLine(p0); break;
    case 'X': eraseChars(n); break;
    case 'P': deleteChars(n); break;
    case '@': insertChars(n); break;
    case 'L': insertLines(n); break;
    case 'M': deleteLines(n); break;
    case 'S': scrollRegionUp(n); break;
    case 'T': scrollRegionDown(n); break;
    case 'r': {
        int top = params.size() > 0 && params.at(0) != 0 ? params.at(0) - 1 : 0;
        int bottom = params.size() > 1 && params.at(1) != 0 ? params.at(1) - 1 : m_rows - 1;
        top = std::clamp(top, 0, m_rows - 1);
        bottom = std::clamp(bottom, 0, m_rows - 1);
        if (top < bottom) { m_scrollTop = top; m_scrollBottom = bottom; }
        m_cursorRow = m_originMode ? m_scrollTop : 0;
        m_cursorCol = 0;
        break;
    }
    case 'm': sgr(params); break;
    case 'h': setMode(params, m_csiPrivate, true); break;
    case 'l': setMode(params, m_csiPrivate, false); break;
    case 's': m_savedCursorRow = m_cursorRow; m_savedCursorCol = m_cursorCol; break; // SCP (ancien style)
    case 'u': m_cursorRow = m_savedCursorRow; m_cursorCol = m_savedCursorCol; break; // RCP
    case 'n':
        // DSR (Device Status Report). Certaines applications interactives (evil-winrm,
        // et plus généralement tout ce qui s'appuie sur Readline/Reline) envoient
        // ESC[6n pour demander la position du curseur et ATTENDENT la réponse du
        // terminal avant d'afficher leur prompt : sans réponse, l'appli reste
        // silencieusement bloquée (symptôme observé : texte de connexion affiché,
        // mais jamais le prompt). On répond ESC[row;colR (1-based), comme un vrai terminal.
        if (p0 == 6) {
            const QByteArray resp = QStringLiteral("\x1b[%1;%2R").arg(m_cursorRow + 1).arg(m_cursorCol + 1).toUtf8();
            emit hostResponse(resp);
        }
        break;
    case 'c':
        // DA (Device Attributes) côté CSI (à ne pas confondre avec ESC c = RIS, géré
        // dans executeEscape). Répond comme un VT100 générique ; nécessaire pour les
        // applications qui négocient les capacités du terminal avant de s'afficher.
        if (!m_csiPrivate && (params.isEmpty() || p0 == 0)) {
            emit hostResponse(QByteArrayLiteral("\x1b[?1;2c"));
        }
        break;
    default:
        break; // séquence non reconnue : ignorée (comportement standard "sois libéral en réception")
    }
    (void)p;
}

void TerminalScreen::executeEscape(QChar c)
{
    switch (c.unicode()) {
    case '7': // DECSC
        m_savedCursorRow = m_cursorRow;
        m_savedCursorCol = m_cursorCol;
        m_savedSgrCursor = m_currentAttrs;
        break;
    case '8': // DECRC
        m_cursorRow = m_savedCursorRow;
        m_cursorCol = m_savedCursorCol;
        m_currentAttrs = m_savedSgrCursor;
        m_pendingWrap = false;
        break;
    case 'c': resetTerminal(); break;      // RIS
    case 'D': newLine(); break;             // IND
    case 'E': carriageReturn(); newLine(); break; // NEL
    case 'M': reverseIndex(); break;        // RI
    default: break;
    }
}

void TerminalScreen::executeOsc()
{
    // Formats usuels : "0;titre" ou "2;titre" (titre de fenêtre/onglet). On extrait
    // le texte après le premier ';' s'il y en a un, sinon on ignore.
    const int idx = m_oscBuffer.indexOf(';');
    if (idx >= 0) {
        const QString codeStr = m_oscBuffer.left(idx);
        if (codeStr == "0" || codeStr == "1" || codeStr == "2") {
            m_title = m_oscBuffer.mid(idx + 1);
            emit titleChanged(m_title);
        }
    }
}

void TerminalScreen::setMode(const QList<int> &params, bool priv, bool enable)
{
    if (!priv) {
        // Modes ANSI standards (non privés) : IRM (4, insertion), etc. -- non exploités.
        return;
    }
    for (const int m : params) {
        switch (m) {
        case 1: // DECCKM : touches curseur en mode "application"
            m_appCursorKeys = enable;
            break;
        case 25: // DECTCEM : visibilité du curseur
            m_cursorVisible = enable;
            break;
        case 6: // DECOM : origine relative à la région de défilement
            m_originMode = enable;
            break;
        case 1049: // écran alternatif + sauvegarde/restauration curseur (xterm moderne)
        case 47:
        case 1047:
            if (enable) switchToAlternateScreen();
            else switchToNormalScreen();
            break;
        case 2004: // bracketed paste
            m_bracketedPaste = enable;
            break;
        default:
            break; // modes souris (1000/1002/1003/1006), etc. : non câblés, ignorés
        }
    }
}

void TerminalScreen::switchToAlternateScreen()
{
    if (m_usingAlternate)
        return;
    m_savedCursorRow = m_cursorRow;
    m_savedCursorCol = m_cursorCol;
    for (auto &line : m_alternateScreen)
        clearLine(line);
    m_usingAlternate = true;
    m_cursorRow = 0;
    m_cursorCol = 0;
    m_pendingWrap = false;
}

void TerminalScreen::switchToNormalScreen()
{
    if (!m_usingAlternate)
        return;
    m_usingAlternate = false;
    m_cursorRow = std::min(m_savedCursorRow, m_rows - 1);
    m_cursorCol = std::min(m_savedCursorCol, m_cols - 1);
    m_pendingWrap = false;
}

QRgb TerminalScreen::ansiColor(int index)
{
    static const QRgb table[16] = {
        qRgb(0, 0, 0),       qRgb(205, 49, 49),   qRgb(13, 188, 121),  qRgb(229, 229, 16),
        qRgb(36, 114, 200),  qRgb(188, 63, 188),  qRgb(17, 168, 205),  qRgb(229, 229, 229),
        qRgb(102, 102, 102), qRgb(241, 76, 76),   qRgb(35, 209, 139),  qRgb(245, 245, 67),
        qRgb(59, 142, 234),  qRgb(214, 112, 214), qRgb(41, 184, 219),  qRgb(255, 255, 255),
    };
    if (index >= 0 && index < 16)
        return table[index];
    return qRgb(229, 229, 229);
}

QRgb TerminalScreen::color256(int index)
{
    if (index < 16)
        return ansiColor(index);
    if (index < 232) {
        const int i = index - 16;
        const int r = i / 36;
        const int g = (i / 6) % 6;
        const int b = i % 6;
        auto lvl = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return qRgb(lvl(r), lvl(g), lvl(b));
    }
    const int gray = 8 + (index - 232) * 10;
    return qRgb(gray, gray, gray);
}

void TerminalScreen::sgr(const QList<int> &paramsIn)
{
    QList<int> params = paramsIn;
    if (params.isEmpty())
        params.append(0);

    for (int i = 0; i < params.size(); ++i) {
        const int code = params.at(i);
        if (code == 0) {
            TermCell def;
            m_currentAttrs.fg = def.fg;
            m_currentAttrs.bg = def.bg;
            m_currentAttrs.bold = m_currentAttrs.faint = m_currentAttrs.italic = false;
            m_currentAttrs.underline = m_currentAttrs.strike = m_currentAttrs.inverse = false;
        } else if (code == 1) { m_currentAttrs.bold = true; }
        else if (code == 2) { m_currentAttrs.faint = true; }
        else if (code == 3) { m_currentAttrs.italic = true; }
        else if (code == 4) { m_currentAttrs.underline = true; }
        else if (code == 7) { m_currentAttrs.inverse = true; }
        else if (code == 9) { m_currentAttrs.strike = true; }
        else if (code == 22) { m_currentAttrs.bold = m_currentAttrs.faint = false; }
        else if (code == 23) { m_currentAttrs.italic = false; }
        else if (code == 24) { m_currentAttrs.underline = false; }
        else if (code == 27) { m_currentAttrs.inverse = false; }
        else if (code == 29) { m_currentAttrs.strike = false; }
        else if (code >= 30 && code <= 37) { m_currentAttrs.fg = ansiColor(code - 30); }
        else if (code == 38) {
            if (i + 1 < params.size() && params.at(i + 1) == 5 && i + 2 < params.size()) {
                m_currentAttrs.fg = color256(params.at(i + 2));
                i += 2;
            } else if (i + 1 < params.size() && params.at(i + 1) == 2 && i + 4 < params.size()) {
                m_currentAttrs.fg = qRgb(params.at(i + 2), params.at(i + 3), params.at(i + 4));
                i += 4;
            }
        }
        else if (code == 39) { m_currentAttrs.fg = TermCell().fg; }
        else if (code >= 40 && code <= 47) { m_currentAttrs.bg = ansiColor(code - 40); }
        else if (code == 48) {
            if (i + 1 < params.size() && params.at(i + 1) == 5 && i + 2 < params.size()) {
                m_currentAttrs.bg = color256(params.at(i + 2));
                i += 2;
            } else if (i + 1 < params.size() && params.at(i + 1) == 2 && i + 4 < params.size()) {
                m_currentAttrs.bg = qRgb(params.at(i + 2), params.at(i + 3), params.at(i + 4));
                i += 4;
            }
        }
        else if (code == 49) { m_currentAttrs.bg = TermCell().bg; }
        else if (code >= 90 && code <= 97) { m_currentAttrs.fg = ansiColor(8 + (code - 90)); }
        else if (code >= 100 && code <= 107) { m_currentAttrs.bg = ansiColor(8 + (code - 100)); }
    }
}

void TerminalScreen::resetTerminal()
{
    for (auto &line : m_normalScreen) clearLine(line);
    for (auto &line : m_alternateScreen) clearLine(line);
    m_scrollback.clear();
    m_usingAlternate = false;
    m_cursorRow = m_cursorCol = 0;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_currentAttrs = TermCell();
    m_appCursorKeys = false;
    m_bracketedPaste = false;
    m_originMode = false;
    m_cursorVisible = true;
    m_pendingWrap = false;
}
