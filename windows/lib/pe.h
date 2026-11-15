// Reading Windows programs (.exe, .dll): version details, icons and the
// name of whoever signed them. Nothing here runs the program.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QList>
#include <QString>

namespace RayeWindows {

struct PeVersion {
    QString productName;
    QString fileDescription;
    QString companyName;
    QString fileVersion;
    QString productVersion;
    QString originalFilename;
};

class PeFile
{
public:
    PeFile() = default;
    PeFile(const PeFile &) = delete;
    PeFile &operator=(const PeFile &) = delete;

    // maps the file; false when it is not a Windows program
    bool load(const QString &path);
    bool loadData(const QByteArray &data);

    bool isValid() const { return m_valid; }
    bool is64() const { return m_is64; }

    // the English strings when there are several languages, else the first
    PeVersion version() const;

    int iconGroupCount() const { return int(m_groups.size()); }
    // Windows' rule for "file,index": index >= 0 is the n-th icon group,
    // below zero the group whose resource id is -index
    QList<QImage> icons(int index = 0) const;

    // the PKCS#7 block of the Authenticode signature, empty when unsigned
    QByteArray signature() const;
    // the common name of the signing certificate, not checked
    QString signerName() const;

private:
    struct Resource {
        quint32 id = 0;      // 0 for named resources
        quint32 rva = 0;
        quint32 size = 0;
    };

    bool parse();
    qint64 rvaToOffset(quint32 rva) const;
    QByteArray bytesAt(quint32 rva, quint32 size) const;
    void readResources();

    QFile m_file;
    QByteArray m_data;
    bool m_valid = false;
    bool m_is64 = false;
    struct Section { quint32 rva, size, raw, rawSize; };
    QList<Section> m_sections;
    quint32 m_resourceRva = 0, m_resourceSize = 0;
    quint32 m_certOffset = 0, m_certSize = 0;
    QList<Resource> m_groups;          // RT_GROUP_ICON, in directory order
    QList<Resource> m_icons;           // RT_ICON
    QList<Resource> m_versions;        // RT_VERSION
};

// the signer's common name (else organisation) from a PKCS#7 SignedData
QString pkcs7SignerName(const QByteArray &der);

// the picture closest to size x size: an exact one (most colours), else
// the next bigger one scaled down, else the biggest scaled up
QImage pickIcon(const QList<QImage> &images, int size);

// icons of a file named by an icon location: .exe/.dll (with index) or .ico
QList<QImage> iconsFromFile(const QString &path, int index = 0);

// Asks osslsigncode whether the signature holds and leads to a trusted
// root. False when it does not, or when osslsigncode is missing.
bool signatureVerified(const QString &path);

} // namespace RayeWindows
