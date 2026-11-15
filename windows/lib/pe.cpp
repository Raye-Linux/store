// PE/COFF layout from Microsoft's published "PE Format" description; the
// resource tree, the icon groups and VS_VERSIONINFO as documented there.
// Every read is checked against the file size: programs come from anywhere.
#include "pe.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>

namespace RayeWindows {

namespace {

constexpr quint32 kRtIcon = 3;
constexpr quint32 kRtGroupIcon = 14;
constexpr quint32 kRtVersion = 16;
// bigger resources than these are not icons or version blocks
constexpr quint32 kMaxIconBytes = 8 * 1024 * 1024;
constexpr quint32 kMaxVersionBytes = 256 * 1024;
constexpr int kMaxEntries = 4096;

class Reader
{
public:
    explicit Reader(const QByteArray &data) : d(data) {}
    bool has(qint64 pos, qint64 n) const { return pos >= 0 && n >= 0 && pos <= d.size() && n <= d.size() - pos; }
    quint8 u8(qint64 pos) const { return has(pos, 1) ? quint8(d.at(pos)) : 0; }
    quint16 u16(qint64 pos) const { return has(pos, 2) ? qFromLittleEndian<quint16>(d.constData() + pos) : 0; }
    quint32 u32(qint64 pos) const { return has(pos, 4) ? qFromLittleEndian<quint32>(d.constData() + pos) : 0; }
    const QByteArray &d;
};

// --- VS_VERSIONINFO -------------------------------------------------------

struct VsNode {
    QString key;
    qint64 value = 0, valueLen = 0;  // in bytes
    bool text = false;
    qint64 children = 0, end = 0;
};

qint64 align4(qint64 v) { return (v + 3) & ~qint64(3); }

QString utf16At(const Reader &r, qint64 pos, qint64 end, qint64 *after = nullptr)
{
    QString s;
    while (pos + 2 <= end && r.has(pos, 2)) {
        const quint16 c = r.u16(pos);
        pos += 2;
        if (c == 0)
            break;
        s.append(QChar(c));
    }
    if (after)
        *after = pos;
    return s;
}

bool vsNode(const Reader &r, qint64 pos, qint64 limit, VsNode *n)
{
    if (!r.has(pos, 6))
        return false;
    const qint64 len = r.u16(pos);
    if (len < 6 || pos + len > limit)
        return false;
    n->end = pos + len;
    const quint16 vlen = r.u16(pos + 2);
    n->text = r.u16(pos + 4) == 1;
    qint64 after = 0;
    n->key = utf16At(r, pos + 6, n->end, &after);
    n->value = align4(after);
    n->valueLen = n->text ? qint64(vlen) * 2 : vlen;
    if (n->value + n->valueLen > n->end)
        n->valueLen = qMax<qint64>(0, n->end - n->value);
    n->children = align4(n->value + n->valueLen);
    return true;
}

template<typename F>
void vsChildren(const Reader &r, const VsNode &parent, F &&visit)
{
    qint64 pos = parent.children;
    int guard = 0;
    while (pos + 6 <= parent.end && guard++ < kMaxEntries) {
        VsNode child;
        if (!vsNode(r, pos, parent.end, &child))
            break;
        visit(child);
        pos = align4(child.end);
    }
}

// --- DER, just what a PKCS#7 signature needs ----------------------------

struct Tlv {
    quint8 tag = 0;
    qint64 start = 0;   // of the header
    qint64 body = 0;
    qint64 len = 0;
    qint64 end() const { return body + len; }
    bool valid = false;
};

Tlv tlvAt(const QByteArray &d, qint64 pos, qint64 limit)
{
    Tlv t;
    if (pos < 0 || pos + 2 > limit || limit > d.size())
        return t;
    t.tag = quint8(d.at(pos));
    t.start = pos;
    quint8 first = quint8(d.at(pos + 1));
    pos += 2;
    qint64 len = 0;
    if (first < 0x80) {
        len = first;
    } else {
        const int n = first & 0x7f;
        if (n == 0 || n > 4 || pos + n > limit)
            return t; // indefinite or absurd lengths: not DER
        for (int i = 0; i < n; ++i)
            len = (len << 8) | quint8(d.at(pos + i));
        pos += n;
    }
    if (len > limit - pos)
        return t;
    t.body = pos;
    t.len = len;
    t.valid = true;
    return t;
}

QList<Tlv> children(const QByteArray &d, const Tlv &parent)
{
    QList<Tlv> out;
    qint64 pos = parent.body;
    while (pos < parent.end() && out.size() < kMaxEntries) {
        const Tlv t = tlvAt(d, pos, parent.end());
        if (!t.valid)
            break;
        out.append(t);
        pos = t.end();
    }
    return out;
}

QByteArray raw(const QByteArray &d, const Tlv &t) { return d.mid(t.start, t.end() - t.start); }

QString derString(const QByteArray &d, const Tlv &t)
{
    const QByteArray b = d.mid(t.body, t.len);
    switch (t.tag) {
    case 0x0c: // UTF8String
        return QString::fromUtf8(b);
    case 0x1e: { // BMPString, UTF-16 big endian
        QString s;
        for (qsizetype i = 0; i + 1 < b.size(); i += 2)
            s.append(QChar(quint16((quint8(b.at(i)) << 8) | quint8(b.at(i + 1)))));
        return s;
    }
    case 0x13: case 0x16: case 0x14: case 0x1a: // Printable, IA5, T61, Visible
        return QString::fromLatin1(b);
    default:
        return {};
    }
}

QString nameAttribute(const QByteArray &d, const Tlv &name, const QByteArray &oidBody)
{
    QString found;
    for (const Tlv &rdn : children(d, name)) {
        for (const Tlv &atv : children(d, rdn)) {
            const QList<Tlv> parts = children(d, atv);
            if (parts.size() == 2 && parts[0].tag == 0x06 && d.mid(parts[0].body, parts[0].len) == oidBody)
                found = derString(d, parts[1]); // the last one is the most specific
        }
    }
    return found.trimmed();
}

QImage decodeIconData(const QByteArray &data, int width, int bitCount)
{
    if (data.startsWith("\x89PNG")) {
        QImage img;
        img.loadFromData(data, "PNG");
        return img;
    }
    // a bare DIB: wrap it in a one-entry .ico, which Qt reads
    QByteArray ico(22, '\0');
    qToLittleEndian<quint16>(1, ico.data() + 2);
    qToLittleEndian<quint16>(1, ico.data() + 4);
    ico[6] = char(width >= 256 ? 0 : width);
    ico[7] = char(width >= 256 ? 0 : width);
    ico[8] = char(bitCount > 0 && bitCount < 8 ? 1 << bitCount : 0); // palette size, 0 from 256 colours on
    qToLittleEndian<quint16>(1, ico.data() + 10);
    qToLittleEndian<quint16>(quint16(bitCount), ico.data() + 12);
    qToLittleEndian<quint32>(quint32(data.size()), ico.data() + 14);
    qToLittleEndian<quint32>(22, ico.data() + 18);
    ico += data;
    QBuffer buffer(&ico);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "ico");
    return reader.read();
}

} // namespace

bool PeFile::load(const QString &path)
{
    m_valid = false;
    m_data.clear();
    m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly) || m_file.size() < 64)
        return false;
    uchar *map = m_file.map(0, m_file.size());
    if (!map)
        return false;
    m_data = QByteArray::fromRawData(reinterpret_cast<const char *>(map), m_file.size());
    return parse();
}

bool PeFile::loadData(const QByteArray &data)
{
    m_valid = false;
    m_data = data;
    m_file.close();
    return parse();
}

bool PeFile::parse()
{
    m_sections.clear();
    m_groups.clear();
    m_icons.clear();
    m_versions.clear();
    m_resourceRva = m_resourceSize = m_certOffset = m_certSize = 0;
    const Reader r(m_data);
    if (!r.has(0, 64) || r.u16(0) != 0x5a4d) // "MZ"
        return false;
    const qint64 pe = r.u32(0x3c);
    if (!r.has(pe, 24) || r.u32(pe) != 0x00004550) // "PE\0\0"
        return false;
    const int sections = r.u16(pe + 6);
    const int optSize = r.u16(pe + 20);
    const qint64 opt = pe + 24;
    if (!r.has(opt, optSize) || optSize < 2)
        return false;
    const quint16 magic = r.u16(opt);
    if (magic != 0x10b && magic != 0x20b)
        return false;
    m_is64 = magic == 0x20b;
    const qint64 dirCountAt = opt + (m_is64 ? 108 : 92);
    const qint64 dirs = dirCountAt + 4;
    const quint32 dirCount = dirCountAt + 4 <= opt + optSize ? r.u32(dirCountAt) : 0;
    auto dir = [&](quint32 i, quint32 *a, quint32 *s) {
        if (i < dirCount && dirs + 8 * (i + 1) <= opt + optSize) {
            *a = r.u32(dirs + 8 * i);
            *s = r.u32(dirs + 8 * i + 4);
        }
    };
    dir(2, &m_resourceRva, &m_resourceSize);
    dir(4, &m_certOffset, &m_certSize);
    const qint64 table = opt + optSize;
    if (sections > 96 || !r.has(table, qint64(sections) * 40))
        return false;
    for (int i = 0; i < sections; ++i) {
        const qint64 s = table + 40 * i;
        m_sections.append({r.u32(s + 12), qMax(r.u32(s + 8), r.u32(s + 16)), r.u32(s + 20), r.u32(s + 16)});
    }
    m_valid = true;
    readResources();
    return true;
}

qint64 PeFile::rvaToOffset(quint32 rva) const
{
    for (const Section &s : m_sections) {
        if (rva >= s.rva && rva - s.rva < s.size) {
            const quint32 within = rva - s.rva;
            if (within >= s.rawSize)
                return -1;
            return qint64(s.raw) + within;
        }
    }
    return -1;
}

QByteArray PeFile::bytesAt(quint32 rva, quint32 size) const
{
    const qint64 off = rvaToOffset(rva);
    if (off < 0 || off > m_data.size() || size > m_data.size() - off)
        return {};
    return QByteArray(m_data.constData() + off, size);
}

void PeFile::readResources()
{
    if (!m_resourceRva)
        return;
    const qint64 base = rvaToOffset(m_resourceRva);
    if (base < 0)
        return;
    const Reader r(m_data);
    const qint64 limit = qMin<qint64>(m_data.size(), base + qMax<quint32>(m_resourceSize, 16));
    // entries of the directory at offset dir (from the section start)
    auto entries = [&](quint32 dir, auto &&visit) {
        const qint64 at = base + dir;
        if (at + 16 > limit || !r.has(at, 16))
            return;
        const int n = qMin(int(r.u16(at + 12)) + int(r.u16(at + 14)), kMaxEntries);
        for (int i = 0; i < n; ++i) {
            const qint64 e = at + 16 + 8 * i;
            if (!r.has(e, 8) || e + 8 > limit)
                return;
            const quint32 name = r.u32(e), target = r.u32(e + 4);
            visit((name & 0x80000000u) ? 0u : name, target);
        }
    };
    auto leaf = [&](quint32 target, quint32 id, QList<Resource> *out) {
        // name level -> language level -> data entry; first language wins
        if (!(target & 0x80000000u))
            return;
        bool done = false;
        entries(target & 0x7fffffffu, [&](quint32, quint32 lang) {
            if (done || (lang & 0x80000000u))
                return;
            const qint64 de = base + lang;
            if (!r.has(de, 16) || de + 16 > limit)
                return;
            out->append({id, r.u32(de), r.u32(de + 4)});
            done = true;
        });
    };
    entries(0, [&](quint32 type, quint32 typeTarget) {
        QList<Resource> *out = type == kRtGroupIcon ? &m_groups : type == kRtIcon ? &m_icons
            : type == kRtVersion ? &m_versions : nullptr;
        if (!out || !(typeTarget & 0x80000000u))
            return;
        entries(typeTarget & 0x7fffffffu, [&](quint32 id, quint32 nameTarget) {
            leaf(nameTarget, id, out);
        });
    });
}

PeVersion PeFile::version() const
{
    PeVersion v;
    if (m_versions.isEmpty() || m_versions.first().size > kMaxVersionBytes)
        return v;
    const QByteArray data = bytesAt(m_versions.first().rva, m_versions.first().size);
    const Reader r(data);
    VsNode root;
    if (!vsNode(r, 0, data.size(), &root) || root.key != QLatin1String("VS_VERSION_INFO"))
        return v;
    // the table of strings to use: US English first, then any English, then the first
    VsNode best;
    int bestScore = -1;
    vsChildren(r, root, [&](const VsNode &n) {
        if (n.key != QLatin1String("StringFileInfo"))
            return;
        vsChildren(r, n, [&](const VsNode &table) {
            const QString lang = table.key.left(4).toLower();
            const int score = lang == QLatin1String("0409") ? 3 : lang.endsWith(QLatin1String("09")) ? 2 : 1;
            if (score > bestScore) {
                best = table;
                bestScore = score;
            }
        });
    });
    if (bestScore >= 0) {
        vsChildren(r, best, [&](const VsNode &s) {
            const QString value = utf16At(r, s.value, s.value + s.valueLen).trimmed();
            if (s.key == QLatin1String("ProductName")) v.productName = value;
            else if (s.key == QLatin1String("FileDescription")) v.fileDescription = value;
            else if (s.key == QLatin1String("CompanyName")) v.companyName = value;
            else if (s.key == QLatin1String("FileVersion")) v.fileVersion = value;
            else if (s.key == QLatin1String("ProductVersion")) v.productVersion = value;
            else if (s.key == QLatin1String("OriginalFilename")) v.originalFilename = value;
        });
    }
    // VS_FIXEDFILEINFO when the strings say nothing
    if (v.fileVersion.isEmpty() && root.valueLen >= 52 && r.u32(root.value) == 0xfeef04bd) {
        const quint32 ms = r.u32(root.value + 8), ls = r.u32(root.value + 12);
        v.fileVersion = QStringLiteral("%1.%2.%3.%4").arg(ms >> 16).arg(ms & 0xffff).arg(ls >> 16).arg(ls & 0xffff);
    }
    return v;
}

QList<QImage> PeFile::icons(int index) const
{
    const Resource *group = nullptr;
    if (index >= 0) {
        if (index < m_groups.size())
            group = &m_groups.at(index);
    } else {
        for (const Resource &g : m_groups) {
            if (g.id == quint32(-qint64(index))) {
                group = &g;
                break;
            }
        }
    }
    QList<QImage> out;
    if (!group || group->size > 64 * 1024)
        return out;
    const QByteArray dir = bytesAt(group->rva, group->size);
    const Reader r(dir);
    if (r.u16(0) != 0 || r.u16(2) != 1)
        return out;
    const int n = qMin<int>(r.u16(4), 64); // real groups hold a handful
    for (int i = 0; i < n && r.has(6 + 14 * i, 14); ++i) {
        const qint64 e = 6 + 14 * i;
        const int width = r.u8(e) ? r.u8(e) : 256;
        const int bits = r.u16(e + 6);
        const quint32 id = r.u16(e + 12);
        for (const Resource &icon : m_icons) {
            if (icon.id != id)
                continue;
            if (icon.size > kMaxIconBytes)
                break;
            const QImage img = decodeIconData(bytesAt(icon.rva, icon.size), width, bits);
            if (!img.isNull()) {
                QImage argb = img.convertToFormat(QImage::Format_ARGB32);
                // a PNG entry is true colour whatever the group says
                const bool png = bytesAt(icon.rva, 4).startsWith("\x89PNG");
                argb.setText(QStringLiteral("bits"), QString::number(png ? 32 : (bits ? bits : img.depth())));
                out.append(argb);
            }
            break;
        }
    }
    return out;
}

QByteArray PeFile::signature() const
{
    // the security directory holds a file offset, not an address
    const Reader r(m_data);
    if (!m_certOffset || !r.has(m_certOffset, 8) || m_certSize < 8)
        return {};
    const quint32 len = r.u32(m_certOffset);
    const quint16 type = r.u16(m_certOffset + 6);
    if (type != 0x0002 || len < 8 || len > m_certSize || !r.has(m_certOffset, len))
        return {};
    return QByteArray(m_data.constData() + m_certOffset + 8, len - 8);
}

QString PeFile::signerName() const
{
    return pkcs7SignerName(signature());
}

QString pkcs7SignerName(const QByteArray &d)
{
    static const QByteArray signedData = QByteArray::fromHex("2a864886f70d010702");
    static const QByteArray cn = QByteArray::fromHex("550403");
    static const QByteArray org = QByteArray::fromHex("55040a");
    const Tlv info = tlvAt(d, 0, d.size());
    if (!info.valid || info.tag != 0x30)
        return {};
    const QList<Tlv> top = children(d, info);
    if (top.size() < 2 || top[0].tag != 0x06 || d.mid(top[0].body, top[0].len) != signedData || top[1].tag != 0xa0)
        return {};
    const QList<Tlv> wrapped = children(d, top[1]);
    if (wrapped.isEmpty() || wrapped[0].tag != 0x30)
        return {};
    const QList<Tlv> sd = children(d, wrapped[0]);
    QList<Tlv> certs;
    Tlv signers;
    for (const Tlv &t : sd) {
        if (t.tag == 0xa0)
            certs = children(d, t);
        else if (t.tag == 0x31)
            signers = t; // the last SET is signerInfos
    }
    if (!signers.valid)
        return {};
    const QList<Tlv> signerList = children(d, signers);
    if (signerList.isEmpty())
        return {};
    const QList<Tlv> si = children(d, signerList[0]);
    if (si.size() < 2 || si[1].tag != 0x30)
        return {};
    const QList<Tlv> sid = children(d, si[1]); // issuer and serial number
    if (sid.size() != 2)
        return {};
    const QByteArray issuer = raw(d, sid[0]), serial = raw(d, sid[1]);
    for (const Tlv &cert : certs) {
        const QList<Tlv> c = children(d, cert);
        if (c.isEmpty())
            continue;
        QList<Tlv> tbs = children(d, c[0]);
        if (!tbs.isEmpty() && tbs[0].tag == 0xa0)
            tbs.removeFirst(); // version
        if (tbs.size() < 5 || raw(d, tbs[0]) != serial || raw(d, tbs[2]) != issuer)
            continue;
        const QString name = nameAttribute(d, tbs[4], cn);
        return name.isEmpty() ? nameAttribute(d, tbs[4], org) : name;
    }
    return {};
}

// the colours an icon had before it became ARGB32 (set by the readers below)
static int iconBits(const QImage &img)
{
    bool ok = false;
    const int bits = img.text(QStringLiteral("bits")).toInt(&ok);
    return ok ? bits : img.depth();
}

QImage pickIcon(const QList<QImage> &images, int size)
{
    const QImage *exact = nullptr, *bigger = nullptr, *biggest = nullptr;
    for (const QImage &img : images) {
        const int w = img.width();
        if (w == size && (!exact || iconBits(img) > iconBits(*exact)))
            exact = &img;
        if (w > size && (!bigger || w < bigger->width()))
            bigger = &img;
        if (!biggest || w > biggest->width())
            biggest = &img;
    }
    if (exact)
        return *exact;
    const QImage *from = bigger ? bigger : biggest;
    if (!from)
        return {};
    return from->scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QList<QImage> iconsFromFile(const QString &path, int index)
{
    if (path.endsWith(QLatin1String(".ico"), Qt::CaseInsensitive)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxIconBytes)
            return {};
        QImageReader reader(&f, "ico");
        QList<QImage> out;
        for (int i = 0; i < reader.imageCount() && i < 64; ++i) {
            if (!reader.jumpToImage(i))
                break;
            const QImage img = reader.read();
            if (img.isNull())
                continue;
            QImage argb = img.convertToFormat(QImage::Format_ARGB32);
            argb.setText(QStringLiteral("bits"), QString::number(img.depth()));
            out.append(argb);
        }
        return out;
    }
    PeFile pe;
    if (!pe.load(path))
        return {};
    return pe.icons(index);
}

bool signatureVerified(const QString &path)
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("osslsigncode"));
    if (tool.isEmpty())
        return false;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // No revocation lists: fetching them would tell a server about every
    // program opened, and fail offline. The signature and the chain to a
    // trusted root are still checked. An absolute path, so a file named
    // "-something" is never an option.
    p.start(tool, {QStringLiteral("verify"), QStringLiteral("-ignore-crl"), QStringLiteral("-in"),
                   QFileInfo(path).absoluteFilePath()});
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished();
        return false;
    }
    const QByteArray out = p.readAll();
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && out.contains("Succeeded")
        && !out.contains("Failed");
}

} // namespace RayeWindows
