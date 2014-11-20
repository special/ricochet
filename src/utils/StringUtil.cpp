/* Ricochet - https://ricochet.im/
 * Copyright (C) 2014, John Brooks <john.brooks@dereferenced.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 *    * Neither the names of the copyright owners nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "StringUtil.h"
#include <QVector>

QByteArray quotedString(const QByteArray &string)
{
    QByteArray out;
    out.reserve(string.size() * 2);

    out.append('"');

    for (int i = 0; i < string.size(); ++i)
    {
        switch (string[i])
        {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        default:
            out.append(string[i]);
            break;
        }
    }

    out.append('"');
    return out;
}

QByteArray unquotedString(const QByteArray &string)
{
    if (string.size() < 2 || string[0] != '"')
        return string;

    QByteArray out;
    out.reserve(string.size() - 2);

    for (int i = 1; i < string.size(); ++i)
    {
        switch (string[i])
        {
        case '\\':
            if (++i < string.size())
                out.append(string[i]);
            break;
        case '"':
            return out;
        default:
            out.append(string[i]);
        }
    }

    return out;
}

QList<QByteArray> splitQuotedStrings(const QByteArray &input, char separator)
{
    QList<QByteArray> out;
    bool inquote = false;
    int start = 0;

    for (int i = 0; i < input.size(); ++i)
    {
        switch (input[i])
        {
        case '"':
            inquote = !inquote;
            break;
        case '\\':
            if (inquote)
                ++i;
            break;
        }

        if (!inquote && input[i] == separator)
        {
            out.append(input.mid(start, i - start));
            start = i+1;
        }
    }

    if (start < input.size())
        out.append(input.mid(start));

    return out;
}

/* Sanitize an input for use as a file name, removing dangerous,
 * meaningful, or unprintable characters as well as extensions and
 * sequences that confuse some operating systems.
 *
 * Based in part on logic from Chromium filename_util.cc and
 * file_util_icu.cc, Copyright 2014 The Chromium Authors.
 */
QString sanitizedFileName(const QString &rawInput)
{
    QString blacklist = QStringLiteral("\"*/:<>?\\|");
    QChar replacement = QLatin1Char('-');
    QVector<uint> input = rawInput.trimmed().toUcs4();
    QString re;

    foreach (uint value, input) {
        QChar c = QChar(value);
        // Strip leading '.'
        if (re.isEmpty() && c == QLatin1Char('.'))
            continue;

        // Replace blacklisted characters
        if (c.isNonCharacter() ||
            c.category() == QChar::Other_Control ||
            c.category() == QChar::Other_Format ||
            blacklist.contains(c))
        {
            re.append(replacement);
        } else {
            re.append(c);
        }
    }

    // Remove trailing .
    while (re.endsWith(QLatin1Char('.')))
        re.chop(1);

#ifdef Q_OS_WIN
    // Find extension
    int dot = re.lastIndexOf(QLatin1Char('.'));
    QString extension = (dot < 0) ? QString() : re.mid(dot+1).toLower();

    // Windows shell has special behavior for extensions .lnk, .local, and CLSIDs
    if (extension == QStringLiteral("lnk") ||
        extension == QStringLiteral("local") ||
        (extension.startsWith(QLatin1Char('{')) && extension.endsWith(QLatin1Char('}'))))
    {
        re.append(QStringLiteral(".download"));
    }

    // Windows forbids device filenames, and has special behavior for
    // desktop.ini and thumbs.db
    static const char* const forbidden[] = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5",
        "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4",
        "lpt5", "lpt6", "lpt7", "lpt8", "lpt9", "clock$",
        "desktop.ini", "thumbs.db"
    };

    QString lower = re.toLower();
    for (unsigned i = 0; i < sizeof(forbidden)/sizeof(*forbidden); i++) {
        if (lower == QLatin1String(forbidden[i]) ||
            lower.startsWith(QLatin1String(forbidden[i]) + QLatin1Char('.')))
        {
            re.prepend(QLatin1Char('_'));
            break;
        }
    }
#endif

    return re;
}

