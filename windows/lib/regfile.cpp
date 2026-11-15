// Wine writes its registry as text: "[key path] time", then "name"=value
// lines (strings, dword:, str(2):, hex:, hex(n):). Strings escape
// backslashes and quotes, and characters past ASCII as \x followed by up to
// four hex digits. Long hex values continue on the next line after a "\".
#include "regfile.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>

#include <cctype>

namespace RayeWindows {

namespace {

constexpr qint64 kMaxRegFile = 256 * 1024 * 1024;

// unescapes text from *pos up to an unescaped end character (the closing
// quote of a string, the "]" of a key); *pos ends after it
QString unquote(const QString &s, int *pos, QChar end = QLatin1Char('"'))
{
    QString out;
    int i = *pos;
    while (i < s.size()) {
        const QChar c = s.at(i++);
        if (c == end)
            break;
        if (c != QLatin1Char('\\') || i >= s.size()) {
            out.append(c);
            continue;
        }
        const QChar e = s.at(i++);
        switch (e.unicode()) {
        case 'n': out.append(QLatin1Char('\n')); break;
        case 'r': out.append(QLatin1Char('\r')); break;
        case 't': out.append(QLatin1Char('\t')); break;
        case 'a': out.append(QChar(7)); break;
        case 'b': out.append(QChar(8)); break;
        case 'e': out.append(QChar(27)); break;
        case 'f': out.append(QChar(12)); break;
        case 'v': out.append(QChar(11)); break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            // octal, up to three digits
            int v = e.unicode() - '0', n = 1;
            while (n < 3 && i < s.size() && s.at(i) >= QLatin1Char('0') && s.at(i) <= QLatin1Char('7')) {
                v = v * 8 + (s.at(i++).unicode() - '0');
                ++n;
            }
            out.append(QChar(v));
            break;
        }
        case 'x': {
            int v = 0, n = 0;
            while (n < 4 && i < s.size() && isxdigit(s.at(i).toLatin1())) {
                v = v * 16 + QString(s.at(i++)).toInt(nullptr, 16);
                ++n;
            }
            out.append(n ? QChar(v) : QLatin1Char('x'));
            break;
        }
        default:
            out.append(e); // \\ and \" and anything else: the character itself
        }
    }
    *pos = i;
    return out;
}

QByteArray hexBytes(const QString &list)
{
    QByteArray out;
    for (const QString &part : list.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const uint v = part.trimmed().toUInt(&ok, 16);
        if (ok && v <= 0xff)
            out.append(char(v));
    }
    return out;
}

QString utf16FromBytes(const QByteArray &b)
{
    QString s;
    for (qsizetype i = 0; i + 1 < b.size(); i += 2) {
        const ushort c = ushort(quint8(b.at(i)) | (quint8(b.at(i + 1)) << 8));
        if (!c)
            break;
        s.append(QChar(c));
    }
    return s;
}

QString isoDate(const QString &yyyymmdd, qint64 fallbackSeconds)
{
    static const QRegularExpression digits(QStringLiteral("^(\\d{4})(\\d{2})(\\d{2})$"));
    const QRegularExpressionMatch m = digits.match(yyyymmdd.trimmed());
    if (m.hasMatch()) {
        const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (d.isValid())
            return d.toString(Qt::ISODate);
    }
    if (fallbackSeconds > 0)
        return QDateTime::fromSecsSinceEpoch(fallbackSeconds).date().toString(Qt::ISODate);
    return {};
}

} // namespace

QString RegKey::string(const QString &name) const
{
    const auto it = values.constFind(name.toLower());
    if (it == values.constEnd())
        return {};
    if (it->type == RegValue::Dword)
        return QString::number(it->dword);
    return it->text;
}

quint32 RegKey::dword(const QString &name, quint32 fallback) const
{
    const auto it = values.constFind(name.toLower());
    if (it == values.constEnd())
        return fallback;
    if (it->type == RegValue::Dword)
        return it->dword;
    bool ok = false;
    const quint32 v = it->text.trimmed().toUInt(&ok);
    return ok ? v : fallback;
}

bool RegFile::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxRegFile)
        return false;
    return parse(f.readAll());
}

bool RegFile::parse(const QByteArray &text)
{
    m_keys.clear();
    m_index.clear();
    m_is32 = false;
    const QStringList lines = QString::fromUtf8(text).split(QLatin1Char('\n'));
    if (lines.isEmpty() || !lines.first().startsWith(QLatin1String("WINE REGISTRY Version")))
        return false;
    RegKey *current = nullptr;
    for (int li = 1; li < lines.size(); ++li) {
        QString line = lines.at(li);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        // a value continued on the next lines
        while (line.endsWith(QLatin1Char('\\')) && li + 1 < lines.size() && line.contains(QLatin1String("=hex"))) {
            line.chop(1);
            QString next = lines.at(++li);
            if (next.endsWith(QLatin1Char('\r')))
                next.chop(1);
            line += next.trimmed();
        }
        if (line.startsWith(QLatin1String("#arch=")))
            m_is32 = line.mid(6).trimmed() == QLatin1String("win32");
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            // [path with \\ escapes] time
            int i = 1;
            const QString path = unquote(line, &i, QLatin1Char(']'));
            RegKey key;
            key.path = path;
            key.modified = line.mid(i).trimmed().section(QLatin1Char(' '), 0, 0).toLongLong();
            const QString lower = path.toLower();
            const auto existing = m_index.constFind(lower);
            if (existing != m_index.constEnd()) {
                current = &m_keys[*existing];
            } else {
                m_index.insert(lower, int(m_keys.size()));
                m_keys.append(key);
                current = &m_keys.last();
            }
            continue;
        }
        if (!current)
            continue;
        QString name;
        int pos = 0;
        if (line.startsWith(QLatin1Char('@'))) {
            pos = 1;
        } else if (line.startsWith(QLatin1Char('"'))) {
            pos = 1;
            name = unquote(line, &pos);
        } else {
            continue;
        }
        if (pos >= line.size() || line.at(pos) != QLatin1Char('='))
            continue;
        const QString v = line.mid(pos + 1);
        RegValue value;
        if (v.startsWith(QLatin1Char('"'))) {
            int p = 1;
            value.text = unquote(v, &p);
        } else if (v.startsWith(QLatin1String("str("))) {
            const int close = v.indexOf(QLatin1String("):\""));
            if (close < 0)
                continue;
            const int kind = v.mid(4, close - 4).toInt();
            int p = close + 3;
            value.text = unquote(v, &p);
            value.type = kind == 2 ? RegValue::ExpandString : kind == 7 ? RegValue::MultiString : RegValue::String;
        } else if (v.startsWith(QLatin1String("dword:"))) {
            bool ok = false;
            value.dword = v.mid(6).trimmed().toUInt(&ok, 16);
            if (!ok)
                continue;
            value.type = RegValue::Dword;
        } else if (v.startsWith(QLatin1String("hex"))) {
            const int colon = v.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            int kind = 3;
            if (v.at(3) == QLatin1Char('('))
                kind = v.mid(4, v.indexOf(QLatin1Char(')')) - 4).toInt(nullptr, 16);
            value.bytes = hexBytes(v.mid(colon + 1));
            if (kind == 1 || kind == 2) {
                value.type = kind == 2 ? RegValue::ExpandString : RegValue::String;
                value.text = utf16FromBytes(value.bytes);
            } else if (kind == 4 && value.bytes.size() >= 4) {
                value.type = RegValue::Dword;
                value.dword = quint32(quint8(value.bytes[0])) | quint32(quint8(value.bytes[1])) << 8
                    | quint32(quint8(value.bytes[2])) << 16 | quint32(quint8(value.bytes[3])) << 24;
            } else {
                value.type = kind == 7 ? RegValue::MultiString : RegValue::Binary;
            }
        } else {
            continue;
        }
        current->values.insert(name.toLower(), value);
    }
    return true;
}

const RegKey *RegFile::key(const QString &path) const
{
    const auto it = m_index.constFind(path.toLower());
    return it == m_index.constEnd() ? nullptr : &m_keys.at(*it);
}

QList<const RegKey *> RegFile::subkeys(const QString &path) const
{
    QList<const RegKey *> out;
    const QString prefix = path.toLower() + QLatin1Char('\\');
    for (const RegKey &k : m_keys) {
        const QString lower = k.path.toLower();
        if (lower.startsWith(prefix) && !lower.mid(prefix.size()).contains(QLatin1Char('\\'))
            && lower.size() > prefix.size())
            out.append(&k);
    }
    return out;
}

QList<UninstallEntry> uninstallEntries(const RegFile &system, const RegFile &user)
{
    static const QString uninstall = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const QString uninstall32 = QStringLiteral("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    QList<UninstallEntry> out;
    auto collect = [&](const RegFile &file, const QString &path, const QString &view) {
        for (const RegKey *k : file.subkeys(path)) {
            UninstallEntry e;
            e.key = k->path.mid(path.size() + 1);
            e.view = view;
            e.name = k->string(QStringLiteral("DisplayName")).trimmed();
            e.publisher = k->string(QStringLiteral("Publisher")).trimmed();
            e.version = k->string(QStringLiteral("DisplayVersion")).trimmed();
            e.uninstallString = k->string(QStringLiteral("UninstallString")).trimmed();
            e.quietUninstallString = k->string(QStringLiteral("QuietUninstallString")).trimmed();
            e.displayIcon = k->string(QStringLiteral("DisplayIcon")).trimmed();
            e.installLocation = k->string(QStringLiteral("InstallLocation")).trimmed();
            e.installDate = isoDate(k->string(QStringLiteral("InstallDate")), k->modified);
            e.estimatedSize = qint64(k->dword(QStringLiteral("EstimatedSize"))) * 1024;
            e.windowsInstaller = k->dword(QStringLiteral("WindowsInstaller")) == 1;
            e.noRemove = k->dword(QStringLiteral("NoRemove")) == 1;
            const QString release = k->string(QStringLiteral("ReleaseType"));
            e.visible = !e.name.isEmpty() && k->dword(QStringLiteral("SystemComponent")) != 1
                && !k->has(QStringLiteral("ParentKeyName"))
                && !release.contains(QLatin1String("Update"), Qt::CaseInsensitive)
                && release.compare(QLatin1String("Hotfix"), Qt::CaseInsensitive) != 0
                && (!e.uninstallString.isEmpty() || !e.quietUninstallString.isEmpty() || e.windowsInstaller);
            out.append(e);
        }
    };
    collect(system, uninstall, system.is32() ? QStringLiteral("32") : QStringLiteral("64"));
    if (!system.is32())
        collect(system, uninstall32, QStringLiteral("32"));
    collect(user, uninstall, QStringLiteral("user"));
    return out;
}

QString uninstallCommand(const UninstallEntry &e)
{
    if (!e.quietUninstallString.isEmpty())
        return e.quietUninstallString;
    static const QRegularExpression guid(QStringLiteral("^\\{[0-9A-Fa-f-]{36}\\}$"));
    if (e.windowsInstaller && guid.match(e.key).hasMatch())
        return QStringLiteral("MsiExec.exe /X%1 /qn").arg(e.key);
    return e.uninstallString;
}

} // namespace RayeWindows
