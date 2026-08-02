#pragma once

#include <QString>
#include <QStringList>

/**
 * ConfigFile
 *
 * Portage de uconfigfile.pas. L'unité Pascal d'origine est actuellement vide
 * (juste les uses, aucune fonction), donc ce squelette ne fait que réserver la
 * place : on y mettra la lecture/écriture de la config applicative (chemin du
 * dernier projet ouvert, préférences, etc.) au fur et à mesure des besoins
 * identifiés dans umain.pas (FSoftConfiguration).
 */
class ConfigFile
{
public:
    static QStringList load(const QString &path);
    static void save(const QString &path, const QStringList &lines);
};
