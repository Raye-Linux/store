// SPDX-License-Identifier: GPL-3.0-or-later
#include "softwaresources.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace
{
struct Source {
    QString name;
    QString kind;
    QString address;
    QStringList components;
    bool enabled = true;
};

Source named(const QString &uri, const QStringList &components, bool enabled)
{
    const QString host = QUrl(uri).host();
    Source s;
    s.address = uri;
    s.components = components;
    s.enabled = enabled;
    if (host.endsWith(QLatin1String("rayelinux.com")) || uri.contains(QLatin1String("raye-linux.github.io"))) {
        s.name = QStringLiteral("Raye Linux");
        s.kind = QStringLiteral("raye");
    } else if (host.endsWith(QLatin1String("ubuntu.com"))) {
        s.name = QStringLiteral("Ubuntu");
        s.kind = QStringLiteral("ubuntu");
    } else {
        s.name = host.isEmpty() ? uri : host;
        s.kind = QStringLiteral("other");
    }
    return s;
}

QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// deb822: stanzas apart by blank lines, "Key: value"
void readDeb822(const QString &text, QList<Source> &out)
{
    for (const QString &stanza : text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts)) {
        QHash<QString, QString> keys;
        for (const QString &line : stanza.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(' ')))
                continue;
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0)
                keys.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        if (!keys.value(QStringLiteral("types")).split(QLatin1Char(' ')).contains(QStringLiteral("deb")))
            continue;
        const QString enabled = keys.value(QStringLiteral("enabled"), QStringLiteral("yes")).toLower();
        const QStringList components = keys.value(QStringLiteral("components")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &uri : keys.value(QStringLiteral("uris")).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            out << named(uri, components, enabled != QLatin1String("no") && enabled != QLatin1String("false"));
    }
}

// one-line: deb [options] uri suite components...
void readList(const QString &text, QList<Source> &out)
{
    static const QRegularExpression options(QStringLiteral("\\[[^\\]]*\\]"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        bool enabled = true;
        if (line.startsWith(QLatin1Char('#'))) {
            line = line.mid(1).trimmed();
            enabled = false;
            if (!line.startsWith(QLatin1String("deb ")))
                continue;
        }
        QStringList words = line.remove(options).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() < 3 || words.first() != QLatin1String("deb"))
            continue;
        out << named(words.at(1), words.mid(3), enabled);
    }
}

void readFlatpak(const QString &text, QList<Source> &out)
{
    static const QRegularExpression remote(QStringLiteral("^\\[remote \"([^\"]+)\"\\]$"));
    QString name;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        const auto m = remote.match(line);
        if (m.hasMatch()) {
            name = m.captured(1);
            continue;
        }
        if (line.startsWith(QLatin1Char('[')))
            name.clear();
        if (!name.isEmpty() && line.startsWith(QLatin1String("url="))) {
            Source s;
            s.address = line.mid(4);
            s.kind = name == QLatin1String("flathub") ? QStringLiteral("flathub") : QStringLiteral("other");
            s.name = name == QLatin1String("flathub") ? QStringLiteral("Flathub") : name;
            out << s;
        }
    }
}

int rank(const QString &kind)
{
    return kind == QLatin1String("raye") ? 0 : kind == QLatin1String("ubuntu") ? 1 : kind == QLatin1String("flathub") ? 2 : 3;
}
}

QVariantList SoftwareSources::read(const QString &root)
{
    QList<Source> all;
    const QDir dir(root + QStringLiteral("/etc/apt/sources.list.d"));
    readList(fileText(root + QStringLiteral("/etc/apt/sources.list")), all);
    for (const QString &file : dir.entryList({QStringLiteral("*.list"), QStringLiteral("*.sources")}, QDir::Files, QDir::Name)) {
        const QString text = fileText(dir.filePath(file));
        if (file.endsWith(QLatin1String(".sources")))
            readDeb822(text, all);
        else
            readList(text, all);
    }
    readFlatpak(fileText(root + QStringLiteral("/var/lib/flatpak/repo/config")), all);

    // one line per name: the archive, its updates and its security fixes
    QList<Source> merged;
    for (const Source &s : std::as_const(all)) {
        auto it = std::find_if(merged.begin(), merged.end(), [&s](const Source &m) { return m.name == s.name; });
        if (it == merged.end()) {
            merged << s;
            continue;
        }
        it->enabled = it->enabled || s.enabled;
        for (const QString &c : s.components) {
            if (!it->components.contains(c))
                it->components << c;
        }
    }
    std::stable_sort(merged.begin(), merged.end(), [](const Source &a, const Source &b) {
        return rank(a.kind) != rank(b.kind) ? rank(a.kind) < rank(b.kind) : a.name.localeAwareCompare(b.name) < 0;
    });
    QVariantList out;
    for (const Source &s : std::as_const(merged)) {
        out << QVariantMap{{QStringLiteral("name"), s.name},
                           {QStringLiteral("kind"), s.kind},
                           {QStringLiteral("address"), s.address},
                           {QStringLiteral("components"), s.components},
                           {QStringLiteral("enabled"), s.enabled}};
    }
    return out;
}
