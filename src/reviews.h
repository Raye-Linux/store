// SPDX-License-Identifier: GPL-3.0-or-later
// Reviews of one app from the Open Desktop Ratings Service, the one
// Discover and GNOME Software use: reading them, writing one, and the
// "was this helpful" votes. Raye runs no server of its own.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

namespace Odrs
{

inline constexpr char Api[] = "https://odrs.gnome.org/1.0/reviews/api";
// the server's limits
inline constexpr int SummaryMax = 70;
inline constexpr int TextMax = 3000;
inline constexpr int MinLength = 2;

// What Discover and GNOME Software send, so a person keeps the same
// reviews in all three: a SHA-1 of the login and the machine id. Empty
// without a machine id.
QString userHash(const QString &login, const QByteArray &machineId);
// NAME= of an os-release file.
QString osName(const QByteArray &osRelease);

struct Review {
    qint64 id = 0;
    QString appId;      // the server's, which may differ from ours by ".desktop"
    QString summary;
    QString text;
    QString author;     // empty: posted without a name
    QString authorHash;
    QString locale;
    QString version;
    QString skey;       // what a vote on it must carry
    double stars = 0;   // 0.5 to 5
    qint64 date = 0;    // seconds since 1970
    int up = 0;
    int down = 0;
    int score = 0;
    bool voted = false;
};

struct Fetched {
    bool ok = false;
    QList<Review> reviews;
    QString skey;       // from the placeholder the server sends when there are none
};
Fetched parseFetch(const QByteArray &json);

QJsonObject fetchQuery(const QString &appId, const QString &locale, const QString &distro,
                       const QString &version, const QString &userHash, int limit);
QJsonObject submitQuery(const QString &appId, const QString &locale, const QString &distro,
                        const QString &version, const QString &userHash, int stars,
                        const QString &summary, const QString &text, const QString &name);
QJsonObject voteQuery(const Review &review, const QString &userHash);

// The reviews in the reader's language first, then the others, each
// list in the server's order, never one review twice.
QList<Review> merge(const QList<Review> &theirLanguage, const QList<Review> &others);

enum class Problem { None, Stars, SummaryShort, SummaryLong, TextShort, TextLong, Markup };
Problem check(int stars, const QString &summary, const QString &text);

// what the server's error message means
enum class Refusal { Other, AlreadyReviewed, Taboo, Banned, AlreadyVoted };
Refusal refusal(const QByteArray &body);

// the Store's own app ids end in ".desktop" for some system packages
QStringList compatIds(const QString &appId);

} // namespace Odrs

class Reviews : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY appIdChanged)
    Q_PROPERTY(QString version MEMBER m_version NOTIFY settingsChanged)
    Q_PROPERTY(QString locale MEMBER m_locale NOTIFY settingsChanged)
    Q_PROPERTY(QString distro MEMBER m_distro NOTIFY settingsChanged)
    Q_PROPERTY(QString userHash MEMBER m_userHash NOTIFY settingsChanged)
    Q_PROPERTY(QString apiUrl MEMBER m_apiUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString cacheDir MEMBER m_cacheDir NOTIFY settingsChanged)
    // false: the server could not be reached and nothing was cached
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QVariantList reviews READ reviews NOTIFY changed)
    // this person's own review of the app, or empty
    Q_PROPERTY(QVariantMap mine READ mine NOTIFY changed)
    // {average, count, histogram} from the server, empty until known
    Q_PROPERTY(QVariantMap rating READ rating NOTIFY changed)
    Q_PROPERTY(bool canWrite READ canWrite NOTIFY changed)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)

public:
    explicit Reviews(QObject *parent = nullptr);

    QString appId() const { return m_appId; }
    void setAppId(const QString &id);
    bool available() const { return m_available; }
    bool loading() const { return m_pending > 0; }
    QVariantList reviews() const;
    QVariantMap mine() const;
    QVariantMap rating() const { return m_rating; }
    bool canWrite() const { return !m_userHash.isEmpty() && m_mine.id == 0; }
    bool sending() const { return m_sending; }

    // the cache is used when younger than this
    static constexpr int FreshSeconds = 6 * 3600;

    Q_INVOKABLE void reload(bool fresh = false);
    Q_INVOKABLE void submit(int stars, const QString &summary, const QString &text, const QString &name);
    Q_INVOKABLE void vote(qint64 reviewId, bool useful);
    Q_INVOKABLE void report(qint64 reviewId);
    Q_INVOKABLE void removeMine();
    // what is wrong with a draft, in words, or empty
    Q_INVOKABLE QString problem(int stars, const QString &summary, const QString &text) const;
    // the name a review is signed with, until the person changes it
    Q_INVOKABLE QString defaultName() const;

Q_SIGNALS:
    void appIdChanged();
    void settingsChanged();
    void changed();
    void submitted(bool ok, const QString &message);
    void removed(bool ok);

private:
    struct Answer {
        bool done = false;
        bool ok = false;
        QByteArray body;
    };
    QNetworkReply *post(const QString &path, const QJsonObject &query);
    QString cacheFile() const;
    bool readCache(bool *fresh);
    void writeCache();
    void finishLoad();
    Odrs::Review *find(qint64 id);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_appId;
    QString m_version;
    QString m_locale;
    QString m_distro;
    QString m_userHash;
    QString m_apiUrl;
    QString m_cacheDir;

    bool m_available = false;
    int m_pending = 0;
    int m_generation = 0;
    bool m_sending = false;
    QList<Odrs::Review> m_reviews;
    Odrs::Review m_mine;
    QVariantMap m_rating;
    QJsonObject m_ratingStars;   // the server's star counts, as cached
    qint64 m_fetchedAt = 0;
    // the key that goes with this person and this app id, for their own review
    QString m_appKey;
    Answer m_theirs, m_others, m_ratingAnswer;
};
