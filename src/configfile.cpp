#include "configfile.h"

#include <QFile>
#include <QTextStream>

QStringList ConfigFile::load(const QString &path)
{
    QStringList result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd())
        result << in.readLine();
    return result;
}

void ConfigFile::save(const QString &path, const QStringList &lines)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString &line : lines)
        out << line << '\n';
}
