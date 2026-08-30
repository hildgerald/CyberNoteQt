#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("CyberNoteQt");
    app.setOrganizationName("CyberNote");
    app.setWindowIcon(QIcon(":/icons/app_icon.svg")); // icône utilisée pour toutes les fenêtres/barre des tâches

    MainWindow w;
    w.show();

    return app.exec();
}
