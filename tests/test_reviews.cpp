// SPDX-License-Identifier: GPL-3.0-or-later
// Reviews from the Open Desktop Ratings Service, against a small fake
// of the server: what is asked, what is shown, and that a dead network
// shows nothing rather than an error.
#include "reviews.h"

#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace
{

// Answers each path with canned JSON and keeps what it was sent.
class FakeOdrs : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QByteArray method;
        QString path;
        QJsonObject body;
    };
    QList<Request> requests;
    QHash<QString, QPair<int, QByteArray>> routes;
    // a fetch answers by the locale it asks for
    QHash<QString, QByteArray> fetchByLocale;

    FakeOdrs()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s] { read(s); });
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/1.0/reviews/api").arg(m_server.serverPort()); }
    QList<Request> to(const QString &path) const
    {
        QList<Request> out;
        for (const Request &r : requests)
            if (r.path.endsWith(path))
                out << r;
        return out;
    }

private:
    void read(QTcpSocket *s)
    {
        QByteArray &buf = m_buffers[s];
        buf += s->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> lines = buf.left(end).split('\n');
        int length = 0;
        for (const QByteArray &l : lines)
            if (l.toLower().startsWith("content-length:"))
                length = l.mid(15).trimmed().toInt();
        if (buf.size() < end + 4 + length)
            return;
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        Request r{first.value(0), QString::fromLatin1(first.value(1)),
                  QJsonDocument::fromJson(buf.mid(end + 4, length)).object()};
        requests << r;
        m_buffers.remove(s);

        QPair<int, QByteArray> answer{404, QByteArrayLiteral("{}")};
        const QString name = r.path.section(QLatin1Char('/'), 4);
        if (name == QLatin1String("fetch") && fetchByLocale.contains(r.body.value(QStringLiteral("locale")).toString()))
            answer = {200, fetchByLocale.value(r.body.value(QStringLiteral("locale")).toString())};
        else if (routes.contains(name))
            answer = routes.value(name);
        const QByteArray reply = "HTTP/1.1 " + QByteArray::number(answer.first) + (answer.first == 200 ? " OK" : " Bad Request")
            + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
            + QByteArray::number(answer.second.size()) + "\r\n\r\n" + answer.second;
        s->write(reply);
        s->disconnectFromHost();
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

const QString Me = QStringLiteral("a").repeated(40);

QByteArray review(qint64 id, const QString &locale, const QString &summary, int rating = 80,
                  const QString &author = QStringLiteral("Sam"), const QString &hash = QStringLiteral("b").repeated(40))
{
    QJsonObject o{{QStringLiteral("review_id"), id}, {QStringLiteral("app_id"), QStringLiteral("org.example.App.desktop")},
                  {QStringLiteral("summary"), summary}, {QStringLiteral("description"), QStringLiteral("Works well for me")},
                  {QStringLiteral("rating"), rating}, {QStringLiteral("locale"), locale},
                  {QStringLiteral("user_hash"), hash}, {QStringLiteral("user_skey"), QStringLiteral("k%1").arg(id).leftJustified(40, QLatin1Char('0'))},
                  {QStringLiteral("karma_up"), 3}, {QStringLiteral("karma_down"), 1}, {QStringLiteral("score"), 50},
                  {QStringLiteral("date_created"), 1767225600.0}, {QStringLiteral("version"), QStringLiteral("2.0")},
                  {QStringLiteral("distro"), QStringLiteral("Raye Linux")}};
    o.insert(QStringLiteral("user_display"), author.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(author));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray list(const QList<QByteArray> &items)
{
    return "[" + items.join(',') + "]";
}

Reviews *reviews(const FakeOdrs &server, const QString &cache, QObject *parent)
{
    auto *r = new Reviews(parent);
    r->setProperty("apiUrl", server.url());
    r->setProperty("cacheDir", cache);
    r->setProperty("locale", QStringLiteral("fr_FR"));
    r->setProperty("distro", QStringLiteral("Raye Linux"));
    r->setProperty("userHash", Me);
    r->setProperty("version", QStringLiteral("2.1"));
    return r;
}

bool settle(Reviews *r)
{
    return QTest::qWaitFor([r] { return !r->loading(); }, 5000);
}

} // namespace

class TestReviews : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-store");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void test_user_hash_matches_discover()
    {
        const QByteArray id = "0123456789abcdef0123456789abcdef\n";
        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray("gnome-software[alice:" + id + "]"), QCryptographicHash::Sha1).toHex());
        QCOMPARE(Odrs::userHash(QStringLiteral("alice"), id), expected);
        QCOMPARE(Odrs::userHash(QStringLiteral("alice"), id).size(), 40);
        QVERIFY(Odrs::userHash(QStringLiteral("alice"), {}).isEmpty());
        QVERIFY(Odrs::userHash(QStringLiteral("alice"), id) != Odrs::userHash(QStringLiteral("bob"), id));
    }

    void test_os_name()
    {
        QCOMPARE(Odrs::osName("ID=raye\nNAME=\"Raye Linux\"\nVERSION=\"1.0 Beta\"\n"), QStringLiteral("Raye Linux"));
        QCOMPARE(Odrs::osName("NAME=Plain\n"), QStringLiteral("Plain"));
        QVERIFY(Odrs::osName("ID=x\n").isEmpty());
    }

    void test_parse_fetch()
    {
        const auto f = Odrs::parseFetch(list({review(7, QStringLiteral("fr_FR"), QStringLiteral("Super"), 70),
                                              review(8, QStringLiteral("fr"), QStringLiteral("Bof"), 20, QString())}));
        QVERIFY(f.ok);
        QCOMPARE(f.reviews.size(), 2);
        const Odrs::Review &a = f.reviews.first();
        QCOMPARE(a.id, 7);
        QCOMPARE(a.summary, QStringLiteral("Super"));
        QCOMPARE(a.stars, 3.5);
        QCOMPARE(a.author, QStringLiteral("Sam"));
        QCOMPARE(a.appId, QStringLiteral("org.example.App.desktop"));
        QCOMPARE(a.up, 3);
        QCOMPARE(a.down, 1);
        QCOMPARE(a.date, 1767225600);
        QVERIFY(!a.voted);
        QVERIFY(f.reviews.at(1).author.isEmpty());
        QCOMPARE(f.reviews.at(1).stars, 1.0);
    }

    void test_parse_fetch_placeholder_and_votes()
    {
        // no reviews: the server sends one item with the key and nothing else
        const auto empty = Odrs::parseFetch(R"([{"app_id":"x","score":0,"user_hash":"h","user_skey":"sk"}])");
        QVERIFY(empty.ok);
        QVERIFY(empty.reviews.isEmpty());
        QCOMPARE(empty.skey, QStringLiteral("sk"));

        QJsonObject voted = QJsonDocument::fromJson(review(9, QStringLiteral("en"), QStringLiteral("Ok"))).object();
        voted.insert(QStringLiteral("vote_id"), 1);
        QVERIFY(Odrs::parseFetch(QJsonDocument(QJsonArray{voted}).toJson()).reviews.first().voted);

        QVERIFY(!Odrs::parseFetch("<html>").ok);
        QVERIFY(!Odrs::parseFetch(R"({"success":false})").ok);
        // a rating outside 1 to 100 is not a review the page can show
        QVERIFY(Odrs::parseFetch(list({review(3, QStringLiteral("en"), QStringLiteral("x"), 0)})).reviews.isEmpty());
    }

    void test_merge_puts_language_first()
    {
        const auto fr = Odrs::parseFetch(list({review(1, QStringLiteral("fr"), QStringLiteral("Un")),
                                               review(2, QStringLiteral("fr"), QStringLiteral("Deux"))})).reviews;
        const auto en = Odrs::parseFetch(list({review(3, QStringLiteral("en"), QStringLiteral("Three")),
                                               review(2, QStringLiteral("fr"), QStringLiteral("Deux"))})).reviews;
        const auto all = Odrs::merge(fr, en);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(0).id, 1);
        QCOMPARE(all.at(1).id, 2);
        QCOMPARE(all.at(2).id, 3);
    }

    void test_draft_rules()
    {
        const QString ok = QStringLiteral("Does what it says");
        QCOMPARE(Odrs::check(4, QStringLiteral("Nice"), ok), Odrs::Problem::None);
        QCOMPARE(Odrs::check(0, QStringLiteral("Nice"), ok), Odrs::Problem::Stars);
        QCOMPARE(Odrs::check(6, QStringLiteral("Nice"), ok), Odrs::Problem::Stars);
        // the server drops a final full stop before it counts
        QCOMPARE(Odrs::check(4, QStringLiteral(" a. "), ok), Odrs::Problem::SummaryShort);
        QCOMPARE(Odrs::check(4, QString(71, QLatin1Char('x')), ok), Odrs::Problem::SummaryLong);
        QCOMPARE(Odrs::check(4, QString(70, QLatin1Char('x')), ok), Odrs::Problem::None);
        QCOMPARE(Odrs::check(4, QStringLiteral("Nice"), QStringLiteral(" x ")), Odrs::Problem::TextShort);
        QCOMPARE(Odrs::check(4, QStringLiteral("Nice"), QString(3001, QLatin1Char('x'))), Odrs::Problem::TextLong);
        QCOMPARE(Odrs::check(4, QStringLiteral("a <b>"), ok), Odrs::Problem::Markup);
        QCOMPARE(Odrs::check(4, QStringLiteral("Nice"), QStringLiteral("x < y")), Odrs::Problem::Markup);
    }

    void test_submit_query()
    {
        const QJsonObject q = Odrs::submitQuery(QStringLiteral("org.example.App"), QStringLiteral("fr_FR"), QStringLiteral("Raye Linux"),
                                                QStringLiteral("2.1"), Me, 4, QStringLiteral("  Nice  "), QStringLiteral(" Good. "), QString());
        QCOMPARE(q.value(QStringLiteral("rating")).toInt(), 80);
        QCOMPARE(q.value(QStringLiteral("summary")).toString(), QStringLiteral("Nice"));
        QCOMPARE(q.value(QStringLiteral("description")).toString(), QStringLiteral("Good."));
        QCOMPARE(q.value(QStringLiteral("user_hash")).toString(), Me);
        QCOMPARE(q.value(QStringLiteral("distro")).toString(), QStringLiteral("Raye Linux"));
        QCOMPARE(q.value(QStringLiteral("locale")).toString(), QStringLiteral("fr_FR"));
        // no name: a word the server stores as none
        QCOMPARE(q.value(QStringLiteral("user_display")).toString(), QStringLiteral("Unknown"));
        const QJsonObject named = Odrs::submitQuery(QStringLiteral("a"), QStringLiteral("en"), QStringLiteral("d"), QString(), Me, 5,
                                                    QStringLiteral("s"), QStringLiteral("t"), QStringLiteral(" Ana "));
        QCOMPARE(named.value(QStringLiteral("user_display")).toString(), QStringLiteral("Ana"));
        // the server refuses an empty version
        QCOMPARE(named.value(QStringLiteral("version")).toString(), QStringLiteral("unknown"));
    }

    void test_refusal()
    {
        QCOMPARE(Odrs::refusal(R"({"success":false,"msg":"already reviewed this app"})"), Odrs::Refusal::AlreadyReviewed);
        QCOMPARE(Odrs::refusal(R"({"success":false,"msg":"review contains taboo word"})"), Odrs::Refusal::Taboo);
        QCOMPARE(Odrs::refusal(R"({"success":false,"msg":"account has been disabled due to abuse"})"), Odrs::Refusal::Banned);
        QCOMPARE(Odrs::refusal(R"({"success":false,"msg":"already voted on this app"})"), Odrs::Refusal::AlreadyVoted);
        QCOMPARE(Odrs::refusal("garbage"), Odrs::Refusal::Other);
    }

    void test_compat_ids()
    {
        QCOMPARE(Odrs::compatIds(QStringLiteral("org.kde.kate.desktop")), QStringList{QStringLiteral("org.kde.kate")});
        QCOMPARE(Odrs::compatIds(QStringLiteral("org.gimp.GIMP")), QStringList{QStringLiteral("org.gimp.GIMP.desktop")});
    }

    void test_load_shows_language_first_and_caches()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(1, QStringLiteral("fr"), QStringLiteral("Très bien")),
                                                                   review(5, QStringLiteral("fr"), QStringLiteral("Le mien"), 100, QStringLiteral("Moi"), Me)}));
        server.fetchByLocale.insert(QStringLiteral("en"), list({review(2, QStringLiteral("en"), QStringLiteral("Great"))}));
        server.routes.insert(QStringLiteral("ratings/org.example.App"), {200, R"({"star0":0,"star1":1,"star2":0,"star3":0,"star4":2,"star5":7,"total":10})"});
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));

        QVERIFY(r->available());
        const QVariantList shown = r->reviews();
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown.at(0).toMap().value(QStringLiteral("summary")).toString(), QStringLiteral("Très bien"));
        QCOMPARE(shown.at(1).toMap().value(QStringLiteral("summary")).toString(), QStringLiteral("Great"));
        // their own review is apart, and they can't write a second one
        QCOMPARE(r->mine().value(QStringLiteral("summary")).toString(), QStringLiteral("Le mien"));
        QVERIFY(!r->canWrite());
        QCOMPARE(r->rating().value(QStringLiteral("count")).toInt(), 10);
        QCOMPARE(r->rating().value(QStringLiteral("average")).toDouble(), 4.4);

        const auto fetches = server.to(QStringLiteral("/fetch"));
        QCOMPARE(fetches.size(), 2);
        const QJsonObject q = fetches.first().body;
        QCOMPARE(q.value(QStringLiteral("app_id")).toString(), QStringLiteral("org.example.App"));
        QCOMPARE(q.value(QStringLiteral("user_hash")).toString(), Me);
        QCOMPARE(q.value(QStringLiteral("version")).toString(), QStringLiteral("2.1"));
        QCOMPARE(q.value(QStringLiteral("distro")).toString(), QStringLiteral("Raye Linux"));
        QVERIFY(q.value(QStringLiteral("limit")).toInt() > 0);
        QCOMPARE(q.value(QStringLiteral("compat_ids")).toArray().first().toString(), QStringLiteral("org.example.App.desktop"));

        // next time, with the server gone, the cache answers
        server.requests.clear();
        auto *offline = reviews(server, cache.path(), this);
        offline->setProperty("apiUrl", QStringLiteral("http://127.0.0.1:9/none"));
        offline->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(offline));
        QVERIFY(offline->available());
        QCOMPARE(offline->reviews().size(), 2);
        QCOMPARE(offline->rating().value(QStringLiteral("count")).toInt(), 10);
        // asked to refresh, it fails and keeps what it had
        offline->reload(true);
        QVERIFY(settle(offline));
        QVERIFY(offline->available());
        QCOMPARE(offline->reviews().size(), 2);
    }

    void test_fresh_cache_skips_network()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(1, QStringLiteral("fr"), QStringLiteral("Bien"))}));
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        const int asked = server.requests.size();
        auto *again = reviews(server, cache.path(), this);
        again->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(again));
        QCOMPARE(server.requests.size(), asked);
        QCOMPARE(again->reviews().size(), 1);
    }

    void test_english_reader_asks_once()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("en_US"), list({review(2, QStringLiteral("en"), QStringLiteral("Great"))}));
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setProperty("locale", QStringLiteral("en_US"));
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QCOMPARE(server.to(QStringLiteral("/fetch")).size(), 1);
        QCOMPARE(r->reviews().size(), 1);
    }

    void test_dead_network_shows_nothing()
    {
        QTemporaryDir cache;
        auto *r = new Reviews(this);
        r->setProperty("apiUrl", QStringLiteral("http://127.0.0.1:9/none"));
        r->setProperty("cacheDir", cache.path());
        r->setProperty("userHash", Me);
        QSignalSpy sent(r, &Reviews::submitted);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QVERIFY(!r->available());
        QVERIFY(r->reviews().isEmpty());
        QVERIFY(r->rating().isEmpty());
        QCOMPARE(sent.count(), 0);
    }

    void test_garbled_answer_is_not_cached()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), "not json");
        server.fetchByLocale.insert(QStringLiteral("en"), "not json");
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QVERIFY(!r->available());
        QVERIFY(QDir(cache.path()).entryList(QDir::Files).isEmpty());
    }

    void test_submit_posts_review_and_shows_it()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), R"([{"app_id":"org.example.App","score":0,"user_skey":"sk"}])");
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        server.routes.insert(QStringLiteral("submit"), {200, R"({"success":true,"review_id":42})"});
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QVERIFY(r->canWrite());
        QSignalSpy sent(r, &Reviews::submitted);
        r->submit(5, QStringLiteral("Lovely"), QStringLiteral("Does the job."), QStringLiteral("Ana"));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(0).toBool(), true);
        const QJsonObject q = server.to(QStringLiteral("/submit")).first().body;
        QCOMPARE(q.value(QStringLiteral("rating")).toInt(), 100);
        QCOMPARE(q.value(QStringLiteral("app_id")).toString(), QStringLiteral("org.example.App"));
        QCOMPARE(q.value(QStringLiteral("user_display")).toString(), QStringLiteral("Ana"));
        QCOMPARE(r->mine().value(QStringLiteral("id")).toLongLong(), 42);
        QCOMPARE(r->mine().value(QStringLiteral("stars")).toDouble(), 5.0);
        QVERIFY(!r->canWrite());
    }

    void test_submit_refused_says_why()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), "[]");
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        server.routes.insert(QStringLiteral("submit"), {400, R"({"success":false,"msg":"already reviewed this app"})"});
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QSignalSpy sent(r, &Reviews::submitted);
        r->submit(3, QStringLiteral("Fine"), QStringLiteral("It works"), QString());
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(0).toBool(), false);
        QVERIFY(sent.first().at(1).toString().contains(QStringLiteral("already")));
        QVERIFY(r->mine().isEmpty());
    }

    void test_submit_checks_draft_first()
    {
        FakeOdrs server;
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        QSignalSpy sent(r, &Reviews::submitted);
        r->submit(0, QStringLiteral("Fine"), QStringLiteral("It works"), QString());
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.first().at(0).toBool(), false);
        QVERIFY(server.to(QStringLiteral("/submit")).isEmpty());
        QVERIFY(!r->problem(0, QStringLiteral("Fine"), QStringLiteral("It works")).isEmpty());
        QVERIFY(r->problem(4, QStringLiteral("Fine"), QStringLiteral("It works")).isEmpty());
    }

    void test_vote_uses_the_reviews_own_key()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(7, QStringLiteral("fr"), QStringLiteral("Bien"))}));
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        server.routes.insert(QStringLiteral("upvote"), {200, R"({"success":true})"});
        server.routes.insert(QStringLiteral("report"), {200, R"({"success":true})"});
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        r->vote(7, true);
        QVERIFY(QTest::qWaitFor([&] { return !server.to(QStringLiteral("/upvote")).isEmpty(); }, 5000));
        const QJsonObject q = server.to(QStringLiteral("/upvote")).first().body;
        QCOMPARE(q.value(QStringLiteral("review_id")).toInteger(), 7);
        QCOMPARE(q.value(QStringLiteral("app_id")).toString(), QStringLiteral("org.example.App.desktop"));
        QCOMPARE(q.value(QStringLiteral("user_skey")).toString().size(), 40);
        QCOMPARE(q.value(QStringLiteral("user_hash")).toString(), Me);
        const QVariantMap after = r->reviews().first().toMap();
        QVERIFY(after.value(QStringLiteral("voted")).toBool());
        QCOMPARE(after.value(QStringLiteral("up")).toInt(), 4);

        // a reported review leaves the list
        r->report(7);
        QVERIFY(QTest::qWaitFor([&] { return r->reviews().isEmpty(); }, 5000));
        QCOMPARE(server.to(QStringLiteral("/report")).size(), 1);
    }

    void test_vote_lost_offline_is_undone_quietly()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(7, QStringLiteral("fr"), QStringLiteral("Bien"))}));
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        r->setProperty("apiUrl", QStringLiteral("http://127.0.0.1:9/none"));
        r->vote(7, false);
        QVERIFY(QTest::qWaitFor([&] { return !r->reviews().first().toMap().value(QStringLiteral("voted")).toBool(); }, 5000));
        QCOMPARE(r->reviews().first().toMap().value(QStringLiteral("down")).toInt(), 1);
    }

    void test_remove_mine()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(5, QStringLiteral("fr"), QStringLiteral("Le mien"), 60, QStringLiteral("Moi"), Me)}));
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        server.routes.insert(QStringLiteral("remove"), {200, R"({"success":true})"});
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        QVERIFY(settle(r));
        QSignalSpy removed(r, &Reviews::removed);
        r->removeMine();
        QVERIFY(removed.wait(5000));
        QCOMPARE(removed.first().at(0).toBool(), true);
        QCOMPARE(server.to(QStringLiteral("/remove")).first().body.value(QStringLiteral("review_id")).toInteger(), 5);
        QVERIFY(r->mine().isEmpty());
        QVERIFY(r->canWrite());
    }

    void test_changing_app_drops_old_answers()
    {
        FakeOdrs server;
        server.fetchByLocale.insert(QStringLiteral("fr_FR"), list({review(1, QStringLiteral("fr"), QStringLiteral("Bien"))}));
        server.fetchByLocale.insert(QStringLiteral("en"), "[]");
        QTemporaryDir cache;
        auto *r = reviews(server, cache.path(), this);
        r->setAppId(QStringLiteral("org.example.App"));
        r->setAppId(QString());
        QVERIFY(settle(r));
        QTest::qWait(200);
        QVERIFY(r->reviews().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestReviews)
#include "test_reviews.moc"
