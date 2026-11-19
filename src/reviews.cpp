// SPDX-License-Identifier: GPL-3.0-or-later
#include "reviews.h"

#include "catalog.h"

#include <KLocalizedString>
#include <KUser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace
{

QString language(const QString &locale)
{
    const QString lang = locale.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('.'), 0, 0);
    return lang == QLatin1String("C") ? QStringLiteral("en") : lang;
}

// what the server does to a summary before it measures it
QString sanitised(QString s, bool summary)
{
    s = s.trimmed();
    s.replace(QLatin1String("!!!"), QLatin1String("!"));
    s.replace(QLatin1String(":)"), QString());
    s.replace(QLatin1String("  "), QLatin1String(" "));
    if (summary && s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s;
}

QString userAgent()
{
    return QCoreApplication::applicationName() + QLatin1Char('/') + QCoreApplication::applicationVersion();
}

// the server counts characters, not UTF-16 units
int length(const QString &s)
{
    return int(s.toUcs4().size());
}

QByteArray readFile(const QString &path, qint64 max = 64 * 1024)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(max) : QByteArray();
}

QJsonObject toJson(const Odrs::Review &r)
{
    return {{QStringLiteral("review_id"), r.id}, {QStringLiteral("app_id"), r.appId},
            {QStringLiteral("summary"), r.summary}, {QStringLiteral("description"), r.text},
            {QStringLiteral("user_display"), r.author}, {QStringLiteral("user_hash"), r.authorHash},
            {QStringLiteral("locale"), r.locale}, {QStringLiteral("version"), r.version},
            {QStringLiteral("user_skey"), r.skey}, {QStringLiteral("rating"), qRound(r.stars * 20)},
            {QStringLiteral("date_created"), double(r.date)}, {QStringLiteral("karma_up"), r.up},
            {QStringLiteral("karma_down"), r.down}, {QStringLiteral("score"), r.score},
            {QStringLiteral("vote_id"), r.voted ? 1 : 0}};
}

std::optional<Odrs::Review> fromJson(const QJsonObject &o)
{
    Odrs::Review r;
    r.id = o.value(QStringLiteral("review_id")).toInteger();
    const int rating = o.value(QStringLiteral("rating")).toInt();
    r.summary = o.value(QStringLiteral("summary")).toString();
    if (r.id <= 0 || rating < 1 || rating > 100 || r.summary.isEmpty())
        return std::nullopt;
    r.stars = rating / 20.0;
    r.appId = o.value(QStringLiteral("app_id")).toString();
    r.text = o.value(QStringLiteral("description")).toString();
    r.author = o.value(QStringLiteral("user_display")).toString().trimmed();
    r.authorHash = o.value(QStringLiteral("user_hash")).toString();
    r.locale = o.value(QStringLiteral("locale")).toString();
    r.version = o.value(QStringLiteral("version")).toString();
    r.skey = o.value(QStringLiteral("user_skey")).toString();
    r.date = qint64(o.value(QStringLiteral("date_created")).toDouble());
    r.up = qMax(0, o.value(QStringLiteral("karma_up")).toInt());
    r.down = qMax(0, o.value(QStringLiteral("karma_down")).toInt());
    r.score = o.value(QStringLiteral("score")).toInt();
    r.voted = o.value(QStringLiteral("vote_id")).toInt() > 0;
    return r;
}

QVariantMap ratingMap(const Rating &r)
{
    if (r.count <= 0)
        return {};
    QVariantList histogram;
    for (int n : r.histogram)
        histogram << n;
    return {{QStringLiteral("average"), r.average}, {QStringLiteral("count"), r.count},
            {QStringLiteral("histogram"), histogram}};
}

} // namespace

namespace Odrs
{

QString userHash(const QString &login, const QByteArray &machineId)
{
    if (machineId.isEmpty())
        return {};
    // the file as read, newline and all: that is what the others hash
    const QByteArray salted = "gnome-software[" + login.toUtf8() + ':' + machineId + ']';
    return QString::fromLatin1(QCryptographicHash::hash(salted, QCryptographicHash::Sha1).toHex());
}

QString osName(const QByteArray &osRelease)
{
    for (const QByteArray &line : osRelease.split('\n')) {
        if (!line.startsWith("NAME="))
            continue;
        QByteArray value = line.mid(5).trimmed();
        if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        return QString::fromUtf8(value);
    }
    return {};
}

Fetched parseFetch(const QByteArray &json)
{
    Fetched out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return out;
    out.ok = true;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (auto r = fromJson(o))
            out.reviews << *r;
        else if (out.skey.isEmpty())
            out.skey = o.value(QStringLiteral("user_skey")).toString();
    }
    return out;
}

static QString orUnknown(const QString &version)
{
    return version.trimmed().isEmpty() ? QStringLiteral("unknown") : version.trimmed();
}

QJsonObject fetchQuery(const QString &appId, const QString &locale, const QString &distro,
                       const QString &version, const QString &userHash, int limit)
{
    QJsonObject q{{QStringLiteral("app_id"), appId}, {QStringLiteral("locale"), locale},
                  {QStringLiteral("distro"), distro.isEmpty() ? QSysInfo::prettyProductName() : distro},
                  {QStringLiteral("version"), orUnknown(version)}, {QStringLiteral("limit"), qMax(1, limit)},
                  {QStringLiteral("compat_ids"), QJsonArray::fromStringList(compatIds(appId))}};
    // without one the server answers anonymously, with no keys to vote
    if (userHash.size() == 40)
        q.insert(QStringLiteral("user_hash"), userHash);
    return q;
}

QJsonObject submitQuery(const QString &appId, const QString &locale, const QString &distro,
                        const QString &version, const QString &userHash, int stars,
                        const QString &summary, const QString &text, const QString &name)
{
    const QString display = name.trimmed();
    return {{QStringLiteral("app_id"), appId}, {QStringLiteral("locale"), locale},
            {QStringLiteral("distro"), distro.isEmpty() ? QSysInfo::prettyProductName() : distro},
            {QStringLiteral("version"), orUnknown(version)}, {QStringLiteral("user_hash"), userHash},
            {QStringLiteral("rating"), qBound(1, stars, 5) * 20},
            {QStringLiteral("summary"), summary.trimmed()}, {QStringLiteral("description"), text.trimmed()},
            // the field may not be empty; the server keeps no name for this word
            {QStringLiteral("user_display"), display.isEmpty() ? QStringLiteral("Unknown") : display}};
}

QJsonObject voteQuery(const Review &review, const QString &userHash)
{
    return {{QStringLiteral("review_id"), review.id}, {QStringLiteral("app_id"), review.appId},
            {QStringLiteral("user_hash"), userHash}, {QStringLiteral("user_skey"), review.skey}};
}

QList<Review> merge(const QList<Review> &theirLanguage, const QList<Review> &others)
{
    QList<Review> out = theirLanguage;
    QSet<qint64> seen;
    for (const Review &r : theirLanguage)
        seen.insert(r.id);
    for (const Review &r : others)
        if (!seen.contains(r.id)) {
            seen.insert(r.id);
            out << r;
        }
    return out;
}

Problem check(int stars, const QString &summary, const QString &text)
{
    if (stars < 1 || stars > 5)
        return Problem::Stars;
    if (length(sanitised(summary, true)) < MinLength)
        return Problem::SummaryShort;
    if (length(summary.trimmed()) > SummaryMax)
        return Problem::SummaryLong;
    if (length(sanitised(text, false)) < MinLength)
        return Problem::TextShort;
    if (length(text.trimmed()) > TextMax)
        return Problem::TextLong;
    if (summary.contains(QLatin1Char('<')) || text.contains(QLatin1Char('<')))
        return Problem::Markup;
    return Problem::None;
}

Refusal refusal(const QByteArray &body)
{
    const QString msg = QJsonDocument::fromJson(body).object().value(QStringLiteral("msg")).toString();
    if (msg.contains(QLatin1String("already reviewed")))
        return Refusal::AlreadyReviewed;
    if (msg.contains(QLatin1String("taboo")))
        return Refusal::Taboo;
    if (msg.contains(QLatin1String("disabled")))
        return Refusal::Banned;
    if (msg.contains(QLatin1String("already voted")))
        return Refusal::AlreadyVoted;
    return Refusal::Other;
}

QStringList compatIds(const QString &appId)
{
    if (appId.isEmpty())
        return {};
    return {appId.endsWith(QLatin1String(".desktop")) ? appId.chopped(8) : appId + QStringLiteral(".desktop")};
}

} // namespace Odrs

Reviews::Reviews(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_locale(QLocale().name())
    , m_apiUrl(QString::fromLatin1(Odrs::Api))
    , m_cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/reviews"))
{
    m_nam->setTransferTimeout(15000);
    QByteArray osRelease = readFile(QStringLiteral("/etc/os-release"));
    if (osRelease.isEmpty())
        osRelease = readFile(QStringLiteral("/usr/lib/os-release"));
    m_distro = Odrs::osName(osRelease);
    m_userHash = Odrs::userHash(KUser().loginName(), readFile(QStringLiteral("/etc/machine-id"), 128));
}

void Reviews::setAppId(const QString &id)
{
    if (id == m_appId)
        return;
    m_appId = id;
    Q_EMIT appIdChanged();
    // the page sets the version after the id: ask once both are there
    ++m_generation;
    m_pending = 1;
    const int generation = m_generation;
    Q_EMIT changed();
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation)
            reload();
    });
}

QVariantList Reviews::reviews() const
{
    QVariantList out;
    const QString mine = language(m_locale);
    for (const Odrs::Review &r : m_reviews) {
        out << QVariantMap{{QStringLiteral("id"), r.id}, {QStringLiteral("summary"), r.summary},
                           {QStringLiteral("text"), r.text}, {QStringLiteral("author"), r.author},
                           {QStringLiteral("stars"), r.stars}, {QStringLiteral("date"), QDateTime::fromSecsSinceEpoch(r.date)},
                           {QStringLiteral("up"), r.up}, {QStringLiteral("down"), r.down},
                           {QStringLiteral("voted"), r.voted},
                           {QStringLiteral("otherLanguage"), !r.locale.isEmpty() && language(r.locale) != mine}};
    }
    return out;
}

QVariantMap Reviews::mine() const
{
    if (m_mine.id == 0)
        return {};
    return {{QStringLiteral("id"), m_mine.id}, {QStringLiteral("canDelete"), !m_mine.skey.isEmpty()},
            {QStringLiteral("summary"), m_mine.summary},
            {QStringLiteral("text"), m_mine.text}, {QStringLiteral("author"), m_mine.author},
            {QStringLiteral("stars"), m_mine.stars}, {QStringLiteral("date"), QDateTime::fromSecsSinceEpoch(m_mine.date)},
            {QStringLiteral("up"), m_mine.up}, {QStringLiteral("down"), m_mine.down}};
}

QNetworkReply *Reviews::post(const QString &path, const QJsonObject &query)
{
    QNetworkRequest request(QUrl(m_apiUrl + QLatin1Char('/') + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    return m_nam->post(request, QJsonDocument(query).toJson(QJsonDocument::Compact));
}

static bool succeeded(QNetworkReply *reply)
{
    return reply->error() == QNetworkReply::NoError
        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
}

// a refusal from the server, as opposed to no answer at all
static bool refused(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400;
}

QString Reviews::cacheFile() const
{
    const QByteArray key = QCryptographicHash::hash((m_appId + QLatin1Char('\n') + m_locale).toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_cacheDir + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".json");
}

bool Reviews::readCache(bool *fresh)
{
    *fresh = false;
    const QJsonObject o = QJsonDocument::fromJson(readFile(cacheFile(), 4 * 1024 * 1024)).object();
    if (o.value(QStringLiteral("app")).toString() != m_appId || !o.value(QStringLiteral("reviews")).isArray())
        return false;
    const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(o.value(QStringLiteral("time")).toDouble());
    *fresh = age >= 0 && age < FreshSeconds;
    m_fetchedAt = qint64(o.value(QStringLiteral("time")).toDouble());
    m_reviews.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("reviews")).toArray())
        if (auto r = fromJson(v.toObject()))
            m_reviews << *r;
    m_mine = fromJson(o.value(QStringLiteral("mine")).toObject()).value_or(Odrs::Review{});
    m_rating = ratingMap(parseRating(o.value(QStringLiteral("rating")).toObject()));
    m_ratingStars = o.value(QStringLiteral("rating")).toObject();
    m_appKey = o.value(QStringLiteral("key")).toString();
    m_available = true;
    return true;
}

void Reviews::writeCache()
{
    if (m_appId.isEmpty() || m_cacheDir.isEmpty() || !QDir().mkpath(m_cacheDir))
        return;
    QJsonArray list;
    for (const Odrs::Review &r : m_reviews)
        list << toJson(r);
    const QJsonObject o{{QStringLiteral("app"), m_appId}, {QStringLiteral("time"), double(m_fetchedAt)},
                        {QStringLiteral("reviews"), list},
                        {QStringLiteral("mine"), m_mine.id ? toJson(m_mine) : QJsonObject()},
                        {QStringLiteral("rating"), m_ratingStars}, {QStringLiteral("key"), m_appKey}};
    QSaveFile f(cacheFile());
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) > 0)
        f.commit();
}

void Reviews::reload(bool fresh)
{
    const int generation = ++m_generation;
    m_pending = 0;
    if (m_appId.isEmpty()) {
        m_reviews.clear();
        m_mine = {};
        m_rating.clear();
        m_available = false;
        Q_EMIT changed();
        return;
    }
    bool cacheFresh = false;
    if (!readCache(&cacheFresh)) {
        m_reviews.clear();
        m_mine = {};
        m_rating.clear();
        m_ratingStars = {};
        m_appKey.clear();
        m_available = false;
    }
    if (cacheFresh && !fresh) {
        Q_EMIT changed();
        return;
    }

    m_theirs = {};
    m_others = {};
    m_ratingAnswer = {};
    const bool english = language(m_locale) == QLatin1String("en");
    m_others.done = english;
    m_pending = english ? 2 : 3;
    Q_EMIT changed();

    const auto collect = [this, generation](QNetworkReply *reply, Answer *answer) {
        connect(reply, &QNetworkReply::finished, this, [this, generation, reply, answer] {
            reply->deleteLater();
            if (generation != m_generation)
                return;
            answer->done = true;
            answer->ok = succeeded(reply);
            answer->body = reply->readAll();
            if (--m_pending == 0)
                finishLoad();
        });
    };
    collect(post(QStringLiteral("fetch"), Odrs::fetchQuery(m_appId, m_locale, m_distro, m_version, m_userHash, 50)), &m_theirs);
    // reviews in other languages come after the reader's own
    if (!english)
        collect(post(QStringLiteral("fetch"), Odrs::fetchQuery(m_appId, QStringLiteral("en"), m_distro, m_version, m_userHash, 20)), &m_others);
    QNetworkRequest request(QUrl(m_apiUrl + QStringLiteral("/ratings/") + QString::fromLatin1(QUrl::toPercentEncoding(m_appId))));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    collect(m_nam->get(request), &m_ratingAnswer);
}

void Reviews::finishLoad()
{
    const Odrs::Fetched theirs = m_theirs.ok ? Odrs::parseFetch(m_theirs.body) : Odrs::Fetched{};
    // nothing new: what the cache showed stays, or nothing at all
    if (!theirs.ok) {
        Q_EMIT changed();
        return;
    }
    const Odrs::Fetched others = m_others.ok ? Odrs::parseFetch(m_others.body) : Odrs::Fetched{};
    QList<Odrs::Review> all = Odrs::merge(theirs.reviews, others.reviews);
    m_mine = {};
    m_reviews.clear();
    m_appKey = theirs.skey;
    for (const Odrs::Review &r : std::as_const(all)) {
        if (m_appKey.isEmpty() && r.appId == m_appId)
            m_appKey = r.skey;
        if (!m_userHash.isEmpty() && r.authorHash == m_userHash && m_mine.id == 0)
            m_mine = r;
        else
            m_reviews << r;
    }
    if (m_ratingAnswer.ok) {
        // "[]" for an app the server does not know
        const QJsonObject stars = QJsonDocument::fromJson(m_ratingAnswer.body).object();
        m_ratingStars = stars;
        m_rating = ratingMap(parseRating(stars));
    }
    m_fetchedAt = QDateTime::currentSecsSinceEpoch();
    m_available = true;
    writeCache();
    Q_EMIT changed();
}

Odrs::Review *Reviews::find(qint64 id)
{
    for (Odrs::Review &r : m_reviews)
        if (r.id == id)
            return &r;
    return nullptr;
}

void Reviews::vote(qint64 reviewId, bool useful)
{
    Odrs::Review *r = find(reviewId);
    if (!r || r->voted || m_userHash.isEmpty() || r->skey.isEmpty())
        return;
    r->voted = true;
    (useful ? r->up : r->down) += 1;
    Q_EMIT changed();
    const QString app = m_appId;
    QNetworkReply *reply = post(useful ? QStringLiteral("upvote") : QStringLiteral("downvote"), Odrs::voteQuery(*r, m_userHash));
    connect(reply, &QNetworkReply::finished, this, [this, reply, reviewId, useful, app] {
        reply->deleteLater();
        if (app != m_appId)
            return;
        Odrs::Review *r = find(reviewId);
        if (!r)
            return;
        // no answer: as if it never happened, the next visit can try again
        if (!succeeded(reply) && !(refused(reply) && Odrs::refusal(reply->readAll()) == Odrs::Refusal::AlreadyVoted)) {
            r->voted = false;
            (useful ? r->up : r->down) -= 1;
        }
        writeCache();
        Q_EMIT changed();
    });
}

void Reviews::report(qint64 reviewId)
{
    Odrs::Review *r = find(reviewId);
    if (!r || m_userHash.isEmpty() || r->skey.isEmpty())
        return;
    const QString app = m_appId;
    QNetworkReply *reply = post(QStringLiteral("report"), Odrs::voteQuery(*r, m_userHash));
    connect(reply, &QNetworkReply::finished, this, [this, reply, reviewId, app] {
        reply->deleteLater();
        if (app != m_appId || (!succeeded(reply) && !refused(reply)))
            return;
        // reported, or refused because they voted on it already: either
        // way they asked not to see it
        m_reviews.removeIf([reviewId](const Odrs::Review &r) { return r.id == reviewId; });
        writeCache();
        Q_EMIT changed();
    });
}

void Reviews::removeMine()
{
    if (m_mine.id == 0 || m_userHash.isEmpty()) {
        Q_EMIT removed(false);
        return;
    }
    const QString app = m_appId;
    QNetworkReply *reply = post(QStringLiteral("remove"), Odrs::voteQuery(m_mine, m_userHash));
    connect(reply, &QNetworkReply::finished, this, [this, reply, app] {
        reply->deleteLater();
        const bool ok = succeeded(reply);
        if (ok && app == m_appId) {
            m_mine = {};
            writeCache();
            Q_EMIT changed();
        }
        Q_EMIT removed(ok);
    });
}

QString Reviews::problem(int stars, const QString &summary, const QString &text) const
{
    switch (Odrs::check(stars, summary, text)) {
    case Odrs::Problem::None: return {};
    case Odrs::Problem::Stars: return i18n("Choose how many stars you give it.");
    case Odrs::Problem::SummaryShort: return i18n("Give your review a title.");
    case Odrs::Problem::SummaryLong: return i18np("The title can be one character at most.", "The title can be %1 characters at most.", Odrs::SummaryMax);
    case Odrs::Problem::TextShort: return i18n("Write a few words about the app.");
    case Odrs::Problem::TextLong: return i18np("Your review can be one character at most.", "Your review can be %1 characters at most.", Odrs::TextMax);
    case Odrs::Problem::Markup: return i18n("Leave out the < sign. Reviews are plain text.");
    }
    return {};
}

QString Reviews::defaultName() const
{
    return KUser().property(KUser::FullName).toString().trimmed();
}

void Reviews::submit(int stars, const QString &summary, const QString &text, const QString &name)
{
    const QString wrong = problem(stars, summary, text);
    if (!wrong.isEmpty() || m_userHash.isEmpty() || m_appId.isEmpty() || m_sending) {
        Q_EMIT submitted(false, !wrong.isEmpty() ? wrong : i18n("Reviews can't be written on this PC."));
        return;
    }
    m_sending = true;
    Q_EMIT changed();
    const QString app = m_appId;
    QNetworkReply *reply = post(QStringLiteral("submit"), Odrs::submitQuery(m_appId, m_locale, m_distro, m_version, m_userHash, stars,
                                                                              summary, text, name));
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater();
        m_sending = false;
        const QByteArray body = reply->readAll();
        if (!succeeded(reply)) {
            QString why;
            switch (refused(reply) ? Odrs::refusal(body) : Odrs::Refusal::Other) {
            case Odrs::Refusal::AlreadyReviewed: why = i18n("You've already reviewed this app."); break;
            case Odrs::Refusal::Taboo: why = i18n("Your review has a word that isn't allowed. Change it and try again."); break;
            case Odrs::Refusal::Banned: why = i18n("Reviews from this PC have been turned off by the Open Desktop Ratings Service."); break;
            default:
                why = refused(reply) ? i18n("The review service didn't accept your review. Try again later.")
                                     : i18n("We couldn't send your review. Check your internet connection and try again.");
            }
            Q_EMIT changed();
            Q_EMIT submitted(false, why);
            return;
        }
        if (app == m_appId) {
            Odrs::Review r;
            r.id = QJsonDocument::fromJson(body).object().value(QStringLiteral("review_id")).toInteger();
            r.appId = app;
            r.summary = summary.trimmed();
            r.text = text.trimmed();
            r.author = name.trimmed();
            r.authorHash = m_userHash;
            r.locale = m_locale;
            r.version = m_version;
            r.stars = qBound(1, stars, 5);
            r.date = QDateTime::currentSecsSinceEpoch();
            r.skey = m_appKey;
            m_mine = r;
            // the server's copy comes with the next visit
            m_fetchedAt = 0;
            writeCache();
        }
        Q_EMIT changed();
        Q_EMIT submitted(true, QString());
    });
}
