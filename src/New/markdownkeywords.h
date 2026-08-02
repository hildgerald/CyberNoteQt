#ifndef MARKDOWNKEYWORDS_H
#define MARKDOWNKEYWORDS_H

#include <QString>

/**
 * Portage de markdownedit.pas : IsKeyword / IsCmdLets / IsLangOperator / CommentPrefix.
 * Utilisé par MarkdownHighlighter pour colorer les blocs de code fencés (```lang ... ```).
 */
namespace MarkdownKeywords {

inline QString keywordsForLang(const QString &lang)
{
    const QString l = lang.toLower();
    if (l == "python" || l == "py")
        return " def class import from as return if elif else for while in not "
               "and or is none true false try except finally with pass break "
               "continue lambda yield global nonlocal assert raise del async await ";
    if (l == "pascal" || l == "pas" || l == "delphi" || l == "lazarus" ||
        l == "objectpascal" || l == "object pascal")
        return " begin end var const type procedure function if then else for "
               "while do repeat until case of record class interface "
               "implementation unit program uses array string integer boolean "
               "try except finally raise nil and or not div mod in is as ";
    if (l == "javascript" || l == "js" || l == "typescript" || l == "ts")
        return " function var let const if else for while return class extends "
               "new this typeof instanceof try catch finally throw switch case "
               "break continue import export default async await null undefined "
               "true false ";
    if (l == "c" || l == "cpp" || l == "c++" || l == "csharp" || l == "cs")
        return " int float double char void if else for while return class "
               "struct public private protected static const new delete using "
               "namespace try catch throw break continue switch case true false null ";
    if (l == "powershell")
        return " begin break catch class clean configuration continue data default do "
               "dynamicparam else elseif end enum exit filter finally for foreach "
               "from function hidden if import in module parallel param process "
               "return static switch throw trap try until using while workflow ";
    if (l == "ruby" || l == "rb")
        return " def end class module if elsif else unless while until for in do "
               "begin rescue ensure raise return yield self nil true false and or "
               "not then case when break next redo retry require require_relative "
               "attr_accessor attr_reader attr_writer public private protected super ";
    if (l == "bash" || l == "sh" || l == "shell" || l == "zsh")
        return " if then else elif fi for while until do done case esac function "
               "return break continue local export readonly declare shift exit "
               "echo eval exec source trap unset test in select true false ";
    if (l == "perl" || l == "pl")
        return " my our local sub if elsif else unless while until for foreach do "
               "return last next redo use no package require qw shift push pop "
               "splice defined undef print printf die warn ref bless eq ne lt gt "
               "le ge and or not ";
    if (l == "basic" || l == "vb" || l == "vbnet" || l == "freebasic" || l == "qbasic")
        return " dim let if then else elseif end for to step next while wend do "
               "loop until gosub goto return sub function call print input rem "
               "and or not true false data read restore as new public private "
               "static const type ";
    return QString();
}

inline QString cmdletsForLang(const QString &lang)
{
    // Seul 'powershell' a une table de cmdlets active côté Pascal (les autres
    // langages sont laissés en commentaire dans le code source d'origine).
    if (lang.toLower() == "powershell")
        return " get-help get-command get-member get-alias get-variable get-location set-location "
               "get-childitem get-item new-item remove-item copy-item move-item rename-item "
               "test-path resolve-path get-content set-content add-content clear-content "
               "out-file import-csv export-csv convertto-json convertfrom-json "
               "where-object foreach-object select-object sort-object group-object "
               "measure-object compare-object tee-object format-table format-list format-wide "
               "write-host write-output write-error write-warning write-verbose write-debug "
               "read-host start-process stop-process get-process wait-process "
               "get-service start-service stop-service restart-service set-service "
               "get-computerinfo get-date get-random get-history invoke-history clear-history "
               "invoke-command invoke-expression invoke-restmethod invoke-webrequest "
               "get-module import-module remove-module install-module update-module "
               "get-executionpolicy set-executionpolicy get-eventlog get-winevent ";
    return QString();
}

inline bool isKeyword(const QString &lang, const QString &word)
{
    const QString kw = keywordsForLang(lang);
    return !kw.isEmpty() && kw.contains(" " + word.toLower() + " ");
}

inline bool isCmdLet(const QString &lang, const QString &word)
{
    const QString kw = cmdletsForLang(lang);
    return !kw.isEmpty() && kw.contains(" " + word.toLower() + " ");
}

inline QString commentPrefix(const QString &lang)
{
    const QString l = lang.toLower();
    if (l == "python" || l == "py" || l == "bash" || l == "sh" || l == "shell" || l == "zsh" ||
        l == "ruby" || l == "rb" || l == "yaml" || l == "yml" || l == "r" ||
        l == "perl" || l == "pl")
        return "#";
    if (l == "basic" || l == "vb" || l == "vbnet" || l == "freebasic" || l == "qbasic")
        return "'"; // REM peut aussi être utilisé, mais l'apostrophe est le style dominant
    return "//"; // style C/Pascal par défaut
}

} // namespace MarkdownKeywords

#endif // MARKDOWNKEYWORDS_H
