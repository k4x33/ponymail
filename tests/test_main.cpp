#include <QtTest>
#include "model/Triage.h"
#include "model/SenderResolver.h"
#include "gmail/MimeMessage.h"
#include "gmail/MimeBuilder.h"
#include "storage/Database.h"
#include "ai/ClaudeClient.h"
#include "core/Util.h"
#include <QTemporaryDir>

using namespace ponymail;

class PonyMailTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---------------- classify_thread ----------------
    void triage_evaluating_without_summary()
    {
        auto r = Triage::classifyThread(nullptr, {}, false);
        QCOMPARE(r.tag, TriageTag::Evaluating);
    }
    void triage_calendar_invite_is_never_action()
    {
        ThreadSummary s; s.valid = true;
        ActionItem a; a.text = QStringLiteral("Mike to approve budget"); a.forUser = true;
        s.actions << a;
        auto r = Triage::classifyThread(&s, {}, true);
        QCOMPARE(r.tag, TriageTag::Info);
        QCOMPARE(r.openUserActions, 0);
        // even while evaluating
        QCOMPARE(Triage::classifyThread(nullptr, {}, true).tag, TriageTag::Info);
    }
    void triage_action_with_count()
    {
        ThreadSummary s; s.valid = true;
        for (int i = 0; i < 3; ++i) { ActionItem a; a.text = QStringLiteral("task %1").arg(i); a.forUser = true; s.actions << a; }
        QHash<QString, ActionStatusRecord> st;
        ActionStatusRecord done; done.status = ActionStatus::Completed; st.insert(s.actions[0].hash(), done);
        auto r = Triage::classifyThread(&s, st, false);
        QCOMPARE(r.tag, TriageTag::Action);
        QCOMPARE(r.openUserActions, 2);
    }
    void triage_done_when_all_completed_or_ignored()
    {
        ThreadSummary s; s.valid = true;
        ActionItem a; a.text = QStringLiteral("send deck"); a.forUser = true;
        ActionItem b; b.text = QStringLiteral("Arvid to pull numbers"); b.forUser = false;
        s.actions << a << b;
        QHash<QString, ActionStatusRecord> st;
        ActionStatusRecord c; c.status = ActionStatus::Completed; st.insert(a.hash(), c);
        ActionStatusRecord i; i.status = ActionStatus::Ignore; st.insert(b.hash(), i);   // Ignore == Completed
        QCOMPARE(Triage::classifyThread(&s, st, false).tag, TriageTag::Done);
    }
    void triage_waiting_when_delegated()
    {
        ThreadSummary s; s.valid = true;
        ActionItem a; a.text = QStringLiteral("review contract"); a.forUser = true;
        s.actions << a;
        QHash<QString, ActionStatusRecord> st;
        ActionStatusRecord w; w.status = ActionStatus::Awaiting; st.insert(a.hash(), w);
        QCOMPARE(Triage::classifyThread(&s, st, false).tag, TriageTag::Waiting);
        // Others' open actions → Waiting too
        ThreadSummary s2; s2.valid = true;
        ActionItem o; o.text = QStringLiteral("Derek to send forecast"); o.forUser = false; s2.actions << o;
        QCOMPARE(Triage::classifyThread(&s2, {}, false).tag, TriageTag::Waiting);
    }
    void triage_follow_up_beats_waiting_but_not_action()
    {
        ThreadSummary s; s.valid = true; s.isFollowUp = true;
        ActionItem o; o.text = QStringLiteral("Karl to confirm"); o.forUser = false; s.actions << o;
        QCOMPARE(Triage::classifyThread(&s, {}, false).tag, TriageTag::FollowUp);
        ActionItem a; a.text = QStringLiteral("Mike decide"); a.forUser = true; s.actions << a;
        QCOMPARE(Triage::classifyThread(&s, {}, false).tag, TriageTag::Action);
    }
    void triage_info_when_no_actions()
    {
        ThreadSummary s; s.valid = true; s.bullets << QStringLiteral("Newsletter");
        QCOMPARE(Triage::classifyThread(&s, {}, false).tag, TriageTag::Info);
    }
    void triage_on_hold_and_in_progress_are_open()
    {
        ThreadSummary s; s.valid = true;
        ActionItem a; a.text = QStringLiteral("x"); a.forUser = true; s.actions << a;
        for (auto st : {ActionStatus::OnHold, ActionStatus::InProgress, ActionStatus::NotStarted}) {
            QHash<QString, ActionStatusRecord> h; ActionStatusRecord r; r.status = st; h.insert(a.hash(), r);
            QCOMPARE(Triage::classifyThread(&s, h, false).tag, TriageTag::Action);
        }
    }

    // ---------------- sender resolver ----------------
    void sender_never_shows_user()
    {
        SenderResolver who(QStringLiteral("mike.brophy@neo4j.com"), QStringLiteral("Mike Brophy"));
        Thread t;
        Message m1; m1.from = util::parseAddress(QStringLiteral("Woody <woody@neo4j.com>")); m1.date = QDateTime::currentDateTimeUtc().addSecs(-100);
        Message m2; m2.from = util::parseAddress(QStringLiteral("Mike Brophy <mike.brophy@neo4j.com>")); m2.date = QDateTime::currentDateTimeUtc();
        t.messages << m1 << m2;
        QCOMPARE(who.displaySender(t), QStringLiteral("Woody"));
        QCOMPARE(who.displaySenderEmail(t), QStringLiteral("woody@neo4j.com"));
    }
    void sender_sent_by_me_when_only_self()
    {
        SenderResolver who(QStringLiteral("mike.brophy@neo4j.com"), QStringLiteral("Mike Brophy"));
        Thread t;
        Message m; m.from = util::parseAddress(QStringLiteral("Mike Brophy <mike.brophy@neo4j.com>"));
        t.messages << m;
        QCOMPARE(who.displaySender(t), QStringLiteral("Sent by me"));
        QVERIFY(who.displaySenderEmail(t).isEmpty());
    }
    void sender_matches_by_name_alias_address()
    {
        SenderResolver who(QStringLiteral("mike.brophy@neo4j.com"), QStringLiteral("Mike Brophy"));
        Thread t;
        Message m; m.from = util::parseAddress(QStringLiteral("Mike Brophy <mikebbrophy@gmail.com>"));   // personal alias, same display name
        t.messages << m;
        QCOMPARE(who.displaySender(t), QStringLiteral("Sent by me"));
    }
    void reply_to_self_sent_moves_cc_to_to()
    {
        SenderResolver who(QStringLiteral("mike.brophy@neo4j.com"), QStringLiteral("Mike Brophy"));
        Message m;
        m.from = util::parseAddress(QStringLiteral("Mike Brophy <mike.brophy@neo4j.com>"));
        m.to = util::parseAddressList(QStringLiteral("Mike Brophy <mike.brophy@neo4j.com>"));
        m.cc = util::parseAddressList(QStringLiteral("Arvid <arvid@neo4j.com>, Derek <derek@neo4j.com>"));
        QList<EmailAddress> to, cc;
        who.replyRecipients(m, false, to, cc);
        QCOMPARE(to.size(), 2);
        QCOMPARE(to[0].email, QStringLiteral("arvid@neo4j.com"));
        for (const auto& a : to) QVERIFY(!who.isSelf(a));
        QVERIFY(cc.isEmpty());
    }
    void reply_all_excludes_self_and_dedupes()
    {
        SenderResolver who(QStringLiteral("mike.brophy@neo4j.com"), QStringLiteral("Mike Brophy"));
        Message m;
        m.from = util::parseAddress(QStringLiteral("Woody <woody@neo4j.com>"));
        m.to = util::parseAddressList(QStringLiteral("mike.brophy@neo4j.com, woody@neo4j.com"));
        m.cc = util::parseAddressList(QStringLiteral("Sam <sam@neo4j.com>"));
        QList<EmailAddress> to, cc;
        who.replyRecipients(m, true, to, cc);
        QCOMPARE(to.size(), 1);
        QCOMPARE(to[0].email, QStringLiteral("woody@neo4j.com"));
        QCOMPARE(cc.size(), 1);
        QCOMPARE(cc[0].email, QStringLiteral("sam@neo4j.com"));
    }

    // ---------------- util ----------------
    void timestamp_format()
    {
        QDateTime dt(QDate(2026, 7, 28), QTime(15, 54), QTimeZone::systemTimeZone());
        QCOMPARE(util::formatTimestamp(dt), QStringLiteral("28 Jul 2026 15:54"));
        QDateTime dt2(QDate(2026, 1, 3), QTime(9, 5), QTimeZone::systemTimeZone());
        QCOMPARE(util::formatTimestamp(dt2), QStringLiteral("03 Jan 2026 09:05"));
    }
    void address_parsing()
    {
        auto list = util::parseAddressList(QStringLiteral("\"Brophy, Mike\" <mike@x.com>, plain@y.org, Bad Entry"));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].name, QStringLiteral("Brophy, Mike"));
        QCOMPARE(list[0].email, QStringLiteral("mike@x.com"));
        QCOMPARE(list[1].email, QStringLiteral("plain@y.org"));
    }
    void action_hash_stable_across_punctuation()
    {
        ActionItem a; a.text = QStringLiteral("Send the deck to Woody!");
        ActionItem b; b.text = QStringLiteral("send the deck  to woody");
        QCOMPARE(a.hash(), b.hash());
    }

    // ---------------- MIME ----------------
    void mime_parse_detects_calendar_and_attachments()
    {
        QJsonObject msg;
        msg[QStringLiteral("id")] = QStringLiteral("m1");
        msg[QStringLiteral("threadId")] = QStringLiteral("t1");
        msg[QStringLiteral("internalDate")] = QStringLiteral("1753714440000");
        msg[QStringLiteral("labelIds")] = QJsonArray{QStringLiteral("INBOX"), QStringLiteral("UNREAD")};
        QJsonObject payload;
        payload[QStringLiteral("mimeType")] = QStringLiteral("multipart/mixed");
        payload[QStringLiteral("headers")] = QJsonArray{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("From")}, {QStringLiteral("value"), QStringLiteral("Woody <woody@neo4j.com>")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Subject")}, {QStringLiteral("value"), QStringLiteral("Invitation: QBR")}}};
        QJsonObject text; text[QStringLiteral("mimeType")] = QStringLiteral("text/plain");
        text[QStringLiteral("body")] = QJsonObject{{QStringLiteral("data"), QString::fromLatin1(util::base64Url("hello world"))}};
        QJsonObject ics; ics[QStringLiteral("mimeType")] = QStringLiteral("application/ics"); ics[QStringLiteral("filename")] = QStringLiteral("invite.ics");
        ics[QStringLiteral("body")] = QJsonObject{{QStringLiteral("attachmentId"), QStringLiteral("att1")}, {QStringLiteral("size"), 1200}};
        QJsonObject pdf; pdf[QStringLiteral("mimeType")] = QStringLiteral("application/pdf"); pdf[QStringLiteral("filename")] = QStringLiteral("deck.pdf");
        pdf[QStringLiteral("body")] = QJsonObject{{QStringLiteral("attachmentId"), QStringLiteral("att2")}, {QStringLiteral("size"), 50000}};
        payload[QStringLiteral("parts")] = QJsonArray{text, ics, pdf};
        msg[QStringLiteral("payload")] = payload;

        Message m = MimeMessage::parseMessage(msg);
        QCOMPARE(m.textPlain, QStringLiteral("hello world"));
        QVERIFY(m.hasCalendarInvite());
        QCOMPARE(m.attachmentCount(), 2);
        QVERIFY(m.isUnread());
        QCOMPARE(m.from.email, QStringLiteral("woody@neo4j.com"));
    }
    void mime_quote_stripping()
    {
        const QString body = QStringLiteral("Sounds good, see you then.\n\nOn Tue, 28 Jul 2026 at 15:54, Woody <woody@neo4j.com> wrote:\n> Can you make Thursday?\n");
        QCOMPARE(MimeMessage::stripQuotedText(body), QStringLiteral("Sounds good, see you then."));
        QCOMPARE(MimeMessage::stripSignature(QStringLiteral("Body text\n-- \nMike\nVP")), QStringLiteral("Body text"));
    }
    void mime_builder_related_structure()
    {
        OutgoingMessage o;
        o.from = EmailAddress{QStringLiteral("Mike Brophy"), QStringLiteral("mike@x.com")};
        o.to << EmailAddress{QString(), QStringLiteral("woody@x.com")};
        o.subject = QStringLiteral("Résumé");
        o.html = QStringLiteral("<p>Hi <img src=\"cid:img1@ponymail\"></p>");
        OutgoingPart img; img.contentId = QStringLiteral("img1@ponymail"); img.mimeType = QStringLiteral("image/png"); img.filename = QStringLiteral("a.png"); img.data = QByteArray("PNGDATA");
        o.inlineImages << img;
        OutgoingPart att; att.mimeType = QStringLiteral("application/pdf"); att.filename = QStringLiteral("deck.pdf"); att.data = QByteArray("PDF");
        o.attachments << att;
        const QByteArray raw = MimeBuilder::build(o);
        QVERIFY(raw.contains("Content-Type: multipart/mixed"));
        QVERIFY(raw.contains("Content-Type: multipart/related"));
        QVERIFY(raw.contains("Content-Type: multipart/alternative"));
        QVERIFY(raw.contains("Content-ID: <img1@ponymail>"));
        QVERIFY(raw.contains("Subject: =?UTF-8?B?"));
        QVERIFY(raw.contains("Content-Disposition: attachment; filename=\"deck.pdf\""));
        QVERIFY(!raw.contains("X-Confirm-Reading-To"));   // no read receipts, ever
        QVERIFY(!raw.contains("Disposition-Notification-To"));
    }

    // ---------------- Claude JSON recovery ----------------
    void claude_json_recovery()
    {
        const QString fenced = QStringLiteral("Here you go:\n```json\n{\n  \"bullets\": [\"a\", \"b\",], // notes\n  \"actions\": [], // none\n  \"follow_up\": false,\n}\n```");
        QString err;
        const QJsonObject o = ClaudeClient::parseJsonObject(fenced, &err);
        QVERIFY2(!o.isEmpty(), qPrintable(err));
        QCOMPARE(o.value(QStringLiteral("bullets")).toArray().size(), 2);
        QCOMPARE(o.value(QStringLiteral("follow_up")).toBool(true), false);
        // Comment markers inside strings survive.
        const QJsonObject u = ClaudeClient::parseJsonObject(QStringLiteral("{\"bullets\":[\"see https://x.y/path // not a comment\"],\"actions\":[],\"follow_up\":false}"), &err);
        QCOMPARE(u.value(QStringLiteral("bullets")).toArray().first().toString(), QStringLiteral("see https://x.y/path // not a comment"));
        QVERIFY(ClaudeClient::parseJsonObject(QStringLiteral("I cannot summarise this."), &err).isEmpty());
    }

    // ---------------- SQLite storage ----------------
    void database_roundtrip()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QCOMPARE(db.schemaVersion(), 2);

        // blocked senders
        QVERIFY(db.addBlockedSender(QStringLiteral("Spam@Evil.com")));
        QVERIFY(db.addBlockedSender(QStringLiteral("@junk.biz")));
        QVERIFY(db.isBlocked(QStringLiteral("spam@evil.com")));
        QVERIFY(db.isBlocked(QStringLiteral("anyone@junk.biz")));
        QVERIFY(!db.isBlocked(QStringLiteral("ok@good.com")));
        QCOMPARE(db.blockedSenders().size(), 2);
        QCOMPARE(db.blockedSenders()[0].type == QStringLiteral("domain") || db.blockedSenders()[1].type == QStringLiteral("domain"), true);

        // action statuses
        ActionItem a; a.text = QStringLiteral("Approve budget");
        QVERIFY(db.upsertActionStatus(QStringLiteral("t1"), a, ActionStatus::InProgress));
        QVERIFY(db.upsertActionStatus(QStringLiteral("t1"), a, ActionStatus::Completed));   // upsert, not duplicate
        auto st = db.actionStatusesForThread(QStringLiteral("t1"));
        QCOMPARE(st.size(), 1);
        QCOMPARE(st.value(a.hash()).status, ActionStatus::Completed);

        // snooze
        QVERIFY(db.snoozeThread(QStringLiteral("t2"), 1000, {QStringLiteral("INBOX"), QStringLiteral("UNREAD")}));
        QCOMPARE(db.expiredSnoozes(2000).size(), 1);
        QCOMPARE(db.expiredSnoozes(500).size(), 0);
        QCOMPARE(db.snoozeFor(QStringLiteral("t2"))->originalLabels.size(), 2);
        QVERIFY(db.unsnoozeThread(QStringLiteral("t2")));
        QVERIFY(db.snoozedThreads().isEmpty());

        // contacts + FTS ranking by frequency DESC, last_seen DESC
        EmailAddress w{QStringLiteral("Mark Woodhams"), QStringLiteral("woody@neo4j.com")};
        EmailAddress wa{QStringLiteral("Woodrow Wilson"), QStringLiteral("woodrow@example.org")};
        db.touchContact(wa, 10);
        db.touchContact(w, 5); db.touchContact(w, 6); db.touchContact(w, 7);
        auto found = db.searchContacts(QStringLiteral("wood"));
        QVERIFY(found.size() >= 2);
        QCOMPARE(found[0].email, QStringLiteral("woody@neo4j.com"));
        QCOMPARE(found[0].frequency, 3);

        // summaries + prune
        for (int i = 0; i < 10; ++i) {
            ThreadSummary s; s.threadId = QStringLiteral("th%1").arg(i); s.latestMessageId = QStringLiteral("m%1").arg(i); s.bullets << QStringLiteral("b"); s.valid = true;
            QVERIFY(db.putSummary(s));
        }
        QCOMPARE(db.summaryCount(), 10);
        QVERIFY(db.getSummary(QStringLiteral("th3:m3")).has_value());
        QCOMPARE(db.getSummary(QStringLiteral("th3:m3"))->bullets.first(), QStringLiteral("b"));
        db.pruneSummaries(4);
        QCOMPARE(db.summaryCount(), 4);

        db.setKv(QStringLiteral("history_id"), QStringLiteral("123"));
        QCOMPARE(db.kv(QStringLiteral("history_id")), QStringLiteral("123"));
    }
};

QTEST_MAIN(PonyMailTests)
#include "test_main.moc"
