# Crack des empreinte netntlm_ntlmv2
```
hashcat -m 5600 --force -a 0 netntlm_ntlmv2 /usr/share/wordlists/rockyou.txt --rules /opt/password_cracking_rules/OneRuleToRuleThemAll.rule -o result.hashcat
```

# Crack des empreinte kerberoasting
```
hashcat -m 13100 --force -a 0 kerberoasting.hashes /usr/share/wordlists/rockyou.txt --rules /opt/password_cracking_rules/OneRuleToRuleThemAll.rule -o result.hashcat --force
```

```
$ hashcat -m 13100 --force -a 0 hashes.krb  /usr/share/wordlists/rockyou.txt -o result.hashcat --force 

```
# Régles de hashcat :
The following functions are 100% compatible to John the Ripper and PasswordsPro. (However, note that while the *functions* are compatible, their “arguments” (the character sets that can be passed to them) may differ, which means that one cannot just take a list of complex John the Ripper rulesets and run them with hashcat.)

|Name|Function|Description|Example Rule|Input Word|Output Word|Note|
|---|---|---|---|---|---|---|
|Nothing|:|Do nothing (passthrough)|:|p@ssW0rd|p@ssW0rd||
|Lowercase|l|Lowercase all letters|l|p@ssW0rd|p@ssw0rd||
|Uppercase|u|Uppercase all letters|u|p@ssW0rd|P@SSW0RD||
|Capitalize|c|Capitalize the first character, lowercase the rest|c|p@ssW0rd|P@ssw0rd||
|Invert Capitalize|C|Lowercase the first character, uppercase the rest|C|p@ssW0rd|p@SSW0RD||
|Toggle Case|t|Toggle the case of all characters in word|t|p@ssW0rd|P@SSw0RD||
|Toggle @|TN|Toggle the case of characters at position N|T3|p@ssW0rd|p@sSW0rd|*|
|Reverse|r|Reverse the entire word|r|p@ssW0rd|dr0Wss@p||
|Duplicate|d|Duplicate entire word|d|p@ssW0rd|p@ssW0rdp@ssW0rd||
|Duplicate N|pN|Append duplicated word N times|p2|p@ssW0rd|p@ssW0rdp@ssW0rdp@ssW0rd|*|
|Reflect|f|Duplicate word reversed|f|p@ssW0rd|p@ssW0rddr0Wss@p||
|Rotate Left|{|Rotate the word left|{|p@ssW0rd|@ssW0rdp||
|Rotate Right|}|Rotate the word right|}|p@ssW0rd|dp@ssW0r||
|Append Character|$X|Append character X to end|$1$2|p@ssW0rd|p@ssW0rd12||
|Prepend Character|^X|Prepend character X to front|^2^1|p@ssW0rd|12p@ssW0rd||
|Truncate left|[|Delete first character|[|p@ssW0rd|@ssW0rd||
|Truncate right|]|Delete last character|]|p@ssW0rd|p@ssW0r||
|Delete @ N|DN|Delete character at position N|D3|p@ssW0rd|p@sW0rd|*|
|Extract range|xNM|Extract M characters, starting at position N|x04|p@ssW0rd|p@ss|* #|
|Omit range|ONM|Delete M characters, starting at position N|O12|p@ssW0rd|psW0rd|*|
|Insert @ N|iNX|Insert character X at position N|i4!|p@ssW0rd|p@ss!W0rd|*|
|Overwrite @ N|oNX|Overwrite character at position N with X|o3$|p@ssW0rd|p@s$W0rd|*|
|Truncate @ N|'N|Truncate word at position N|'6|p@ssW0rd|p@ssW0|*|
|Replace|sXY|Replace all instances of X with Y|ss$|p@ssW0rd|p@$$W0rd||
|Purge|@X|Purge all instances of X|@s|p@ssW0rd|p@W0rd||
|Duplicate first N|zN|Duplicate first character N times|z2|p@ssW0rd|ppp@ssW0rd|*|
|Duplicate last N|ZN|Duplicate last character N times|Z2|p@ssW0rd|p@ssW0rddd|*|
|Duplicate all|q|Duplicate every character|q|p@ssW0rd|pp@@ssssWW00rrdd||
|Extract memory|XNMI|Insert substring of length M starting from position N of word saved to memory at position I|lMX428|p@ssW0rd|p@ssw0rdw0|+|
|Append memory|4|Append the word saved to memory to current word|uMl4|p@ssW0rd|p@ssw0rdP@SSW0RD|+|
|Prepend memory|6|Prepend the word saved to memory to current word|rMr6|p@ssW0rd|dr0Wss@pp@ssW0rd|+|
|Memorize|M|Memorize current word|lMuX084|p@ssW0rd|P@SSp@ssw0rdW0RD|+|

- * Indicates that N starts at 0. For character positions other than 0-9 use A-Z (A=10)
    
- + Indicates that this rule is implemented in hashcat-legacy (non-OpenCL CPU) only.
    
- # Changed in oclHashcat v1.37 → v1.38 and hashcat v0.51 → v0.52
    

## Rules used to reject plains

|Name|Function|Description|Example Rule|Note|
|---|---|---|---|---|
|Reject less|<N|Reject plains if their length is greater than N|<G|*|
|Reject greater|>N|Reject plains if their length is less than N|>8|*|
|Reject equal|_N|Reject plains of length not equal to N|_7|*|
|Reject contain|!X|Reject plains which contain char X|!z||
|Reject not contain|/X|Reject plains which do not contain char X|/e||
|Reject equal first|(X|Reject plains which do not start with X|(h||
|Reject equal last|)X|Reject plains which do not end with X|)t||
|Reject equal at|=NX|Reject plains which do not have char X at position N|=1a|*|
|Reject contains|%NX|Reject plains which contain char X less than N times|%2a|*|
|Reject contains|Q|Reject plains where the memory saved matches current word|rMrQ|e.g. for palindrome|

Note: Reject rules only work either with hashcat-legacy, or when using “-j” or “-k” with hashcat. They will not work as regular rules (in a rule file) with hashcat.

- * Indicates that N starts at 0. For character positions other than 0-9 use A-Z (A=10)
    

## Implemented specific functions

The following functions are not available in John the Ripper and/or PasswordsPro:

|Name|Function|Description|Example Rule|Input Word|Output Word|Note|
|---|---|---|---|---|---|---|
|Swap front|k|Swap first two characters|k|p@ssW0rd|@pssW0rd||
|Swap back|K|Swap last two characters|K|p@ssW0rd|p@ssW0dr||
|Swap @ N|*NM|Swap character at position N with character at position M|*34|p@ssW0rd|p@sWs0rd|*|
|Bitwise shift left|LN|Bitwise shift left character @ N|L2|p@ssW0rd|p@æsW0rd|*|
|Bitwise shift right|RN|Bitwise shift right character @ N|R2|p@ssW0rd|p@9sW0rd|*|
|ASCII increment|+N|Increment character @ N by 1 ascii value|+2|p@ssW0rd|p@tsW0rd|*|
|ASCII decrement|-N|Decrement character @ N by 1 ascii value|-1|p@ssW0rd|p?ssW0rd|*|
|Replace N + 1|.N|Replace character @ N with value at @ N plus 1|.1|p@ssW0rd|psssW0rd|*|
|Replace N - 1|,N|Replace character @ N with value at @ N minus 1|,1|p@ssW0rd|ppssW0rd|*|
|Duplicate block front|yN|Duplicate first N characters|y2|p@ssW0rd|p@p@ssW0rd|*|
|Duplicate block back|YN|Duplicate last N characters|Y2|p@ssW0rd|p@ssW0rdrd|*|
|Title|E|Lower case the whole line, then upper case the first letter and every letter after a space|E|p@ssW0rd w0rld|P@ssw0rd W0rld|+|
|Title w/separator|eX|Lower case the whole line, then upper case the first letter and every letter after a custom separator character|e-|p@ssW0rd-w0rld|P@ssw0rd-W0rld|+|
|Toggle w/Nth separator|3NX|Toggle case the letter after the Nth instance of a separator char|30-|pass-word|pass-Word|*|

- * Indicates that N starts at 0. For character positions other than 0-9 use A-Z (A=10)
    
- + Only in JtR?
    
- # In beta or not yet released
    

## Using hex bytes in rules

You can use '\xNN' syntax to use hex bytes in rules.

$ echo "Penguin" | hashcat -j '$\x64' --stdout
Penguind

This is useful for non-printable ASCII characters, multibyte work, etc.

You may need to use double quotes for the rule with some shells (Windows Command Prompt, etc.)
# Crack des mot de passe avec rule :
```
$ hashcat -m0 md5-Q2.txt /usr/share/wordlists/rockyou.txt -r uppercase.rule --force
```
Fichier des régles standard:
```
/usr/share/hashcat/rules
```

# Crack mot de passe Keepass 
On doit d'abord construire le hash avec keepass2john et éliminer le terme Database: 
```
$ keepass2john Database.kdbx > keepass.hash

```
Le crack :
```
$ hashcat -m 13400 keepass.hash /usr/share/wordlists/rockyou.txt -r /usr/share/hashcat/rules/rockyou-30000.rule --force

```

```
```
