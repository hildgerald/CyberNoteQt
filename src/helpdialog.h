#pragma once

#include <QDialog>

/**
 * HelpDialog
 *
 * Fenêtre d'aide complète du logiciel, affichée via Help > Help Contents (F1).
 * Contenu statique au format HTML dans un QTextBrowser (non modale : reste
 * consultable pendant qu'on utilise le reste de l'application).
 */
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpDialog(QWidget *parent = nullptr);

private:
    static QString helpHtml();
};
