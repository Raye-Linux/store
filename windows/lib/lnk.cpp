// Layout from Microsoft's published [MS-SHLLINK] specification. The target
// is taken from LinkInfo when present, else from the environment block,
// else from the shell item list (My Computer, drive, then file entries).
#include "lnk.h"

#include <QFile>
#include <QtEndian>

namespace RayeWindows {

namespace {

constexpr qint64 kMaxLnk = 1024 * 1024;

enum : quint32 {
    HasLinkTargetIDList = 0x1,
    HasLinkInfo = 0x2,
    HasName = 0x4,
    HasRelativePath = 0x8,
    HasWorkingDir = 0x10,
    HasArguments = 0x20,
    HasIconLocation = 0x40,
    IsUnicode = 0x80,
    ForceNoLinkInfo = 0x100,
};

struct Bytes {
    const QByteArray &d;
    bool has(qint64 pos, qint64 n) const { return pos >= 0 && n >= 0 && pos <= d.size() && n <= d.size() - pos; }
    quint8 u8(qint64 p) const { return has(p, 1) ? quint8(d.at(p)) : 0; }
    quint16 u16(qint64 p) const { return has(p, 2) ? qFromLittleEndian<quint16>(d.constData() + p) : 0; }
    quint32 u32(qint64 p) const { return has(p, 4) ? qFromLittleEndian<quint32>(d.constData() + p) : 0; }
    // NUL-terminated strings, never past end
    QString ansi(qint64 p, qint64 end) const
    {
        QByteArray s;
        for (; p < end && has(p, 1) && d.at(p); ++p)
            s.append(d.at(p));
        // the shortcut's code page is unknown; Latin-1 keeps every byte
        return QString::fromLatin1(s);
    }
    QString utf16(qint64 p, qint64 end) const
    {
        QString s;
        for (; p + 1 < end && has(p, 2); p += 2) {
            const quint16 c = u16(p);
            if (!c)
                break;
            s.append(QChar(c));
        }
        return s;
    }
};

QString joinPath(const QString &a, const QString &b)
{
    if (b.isEmpty())
        return a;
    if (a.isEmpty() || a.endsWith(QLatin1Char('\\')))
        return a + b;
    return a + QLatin1Char('\\') + b;
}

// the path spelled by the shell items, when they are My Computer, a drive
// and file entries; empty for anything else (control panel, network...)
QString idListPath(const Bytes &b, qint64 pos, qint64 end)
{
    QString path;
    bool computer = false;
    int guard = 0;
    while (pos + 2 <= end && guard++ < 256) {
        const quint16 size = b.u16(pos);
        if (size == 0)
            break;
        if (size < 3 || pos + size > end)
            return {};
        const qint64 item = pos + 2, itemEnd = pos + size;
        const quint8 type = b.u8(item);
        if (type == 0x1f) {
            computer = true;
        } else if ((type & 0x70) == 0x20) { // a volume: "C:\"
            if (!computer)
                return {};
            path = b.ansi(item + 1, itemEnd).left(3);
        } else if ((type & 0x70) == 0x30) { // a file or folder
            if (path.isEmpty())
                return {};
            // the short name, then a version 3+ extension block with the long one
            qint64 p = item + 12;
            const QString shortName = b.ansi(p, itemEnd);
            p += shortName.size() + 1;
            if ((p - item) % 2)
                ++p;
            QString name = shortName;
            if (p + 8 <= itemEnd && b.u32(p + 4) == 0xbeef0004) {
                const quint16 extSize = b.u16(p), version = b.u16(p + 2);
                const qint64 extEnd = qMin<qint64>(itemEnd, p + extSize);
                qint64 at = p + 18;
                if (version >= 7)
                    at += 18;
                if (version >= 8)
                    at += 4;
                if (version >= 3 && at < extEnd) {
                    const QString longName = b.utf16(at, extEnd);
                    if (!longName.isEmpty())
                        name = longName;
                }
            }
            path = joinPath(path, name);
        } else {
            return {};
        }
        pos += size;
    }
    return path.size() > 3 ? path : QString();
}

} // namespace

Lnk readLnk(const QByteArray &data)
{
    Lnk out;
    const Bytes b{data};
    static const QByteArray clsid = QByteArray::fromHex("0114020000000000c000000000000046");
    if (!b.has(0, 0x4c) || b.u32(0) != 0x4c || data.mid(4, 16) != clsid)
        return out;
    const quint32 flags = b.u32(0x14);
    out.iconIndex = qint32(b.u32(0x38));
    qint64 pos = 0x4c;
    QString idPath;
    if (flags & HasLinkTargetIDList) {
        const quint16 size = b.u16(pos);
        if (!b.has(pos + 2, size))
            return out;
        idPath = idListPath(b, pos + 2, pos + 2 + size);
        pos += 2 + size;
    }
    if ((flags & HasLinkInfo) && !(flags & ForceNoLinkInfo)) {
        const quint32 size = b.u32(pos);
        if (size < 0x1c || !b.has(pos, size))
            return out;
        const qint64 end = pos + size;
        const quint32 header = b.u32(pos + 4), liFlags = b.u32(pos + 8);
        if (liFlags & 0x1) {
            QString base, suffix;
            if (header >= 0x24) {
                const quint32 bu = b.u32(pos + 0x1c), su = b.u32(pos + 0x20);
                if (bu)
                    base = b.utf16(pos + bu, end);
                if (su)
                    suffix = b.utf16(pos + su, end);
            }
            if (base.isEmpty()) {
                base = b.ansi(pos + b.u32(pos + 0x10), end);
                suffix = b.ansi(pos + b.u32(pos + 0x18), end);
            }
            out.target = joinPath(base, suffix);
        }
        pos = end;
    }
    const bool unicode = flags & IsUnicode;
    auto stringData = [&](QString *into) {
        const quint16 count = b.u16(pos);
        const qint64 bytes = unicode ? qint64(count) * 2 : count;
        if (!b.has(pos + 2, bytes))
            return false;
        if (unicode) {
            QString s;
            s.reserve(count);
            // a counted string can carry a NUL, and Windows reads up to it:
            // Roblox Studio's shortcut has arguments of one NUL
            for (int i = 0; i < count; ++i) {
                const quint16 c = b.u16(pos + 2 + 2 * i);
                if (c == 0)
                    break;
                s.append(QChar(c));
            }
            *into = s;
        } else {
            const QByteArray raw = data.mid(pos + 2, bytes);
            const int nul = raw.indexOf('\0');
            *into = QString::fromLatin1(nul >= 0 ? raw.left(nul) : raw);
        }
        pos += 2 + bytes;
        return true;
    };
    const struct { quint32 flag; QString *into; } strings[] = {
        {HasName, &out.description}, {HasRelativePath, &out.relativePath},
        {HasWorkingDir, &out.workingDir}, {HasArguments, &out.arguments},
        {HasIconLocation, &out.iconLocation},
    };
    for (const auto &s : strings) {
        if ((flags & s.flag) && !stringData(s.into))
            return out;
    }
    // extra data blocks: the environment-variable target
    QString envTarget;
    for (int guard = 0; guard < 64 && b.has(pos, 8); ++guard) {
        const quint32 size = b.u32(pos);
        if (size < 8 || !b.has(pos, size))
            break;
        const quint32 sig = b.u32(pos + 4);
        if ((sig == 0xa0000001 || sig == 0xa0000007) && size >= 0x314) {
            QString t = b.utf16(pos + 268, pos + 268 + 520);
            if (t.isEmpty())
                t = b.ansi(pos + 8, pos + 268);
            if (sig == 0xa0000001)
                envTarget = t;
            else if (out.iconLocation.isEmpty())
                out.iconLocation = t;
        }
        pos += size;
    }
    if (out.target.isEmpty())
        out.target = envTarget;
    if (out.target.isEmpty())
        out.target = idPath;
    out.valid = !out.target.isEmpty();
    return out;
}

Lnk readLnkFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxLnk)
        return {};
    return readLnk(f.readAll());
}

} // namespace RayeWindows
