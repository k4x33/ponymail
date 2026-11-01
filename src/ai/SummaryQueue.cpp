#include "SummaryQueue.h"
#include "ClaudeClient.h"
#include "model/MailStore.h"
#include "storage/Database.h"
#include "core/Settings.h"

namespace ponymail {

SummaryQueue::SummaryQueue(ClaudeClient* ai, MailStore* store, Database* db, Settings* settings, QObject* parent)
    : QObject(parent), m_ai(ai), m_store(store), m_db(db), m_settings(settings) {}

bool SummaryQueue::loadFromCache(const QString& threadId)
{
    const Thread* t = m_store->thread(threadId);
    if (!t) return false;
    const QString key = threadId + QLatin1Char(':') + t->latestMessageId();
    const ThreadSummary* current = m_store->summary(threadId);
    if (current && current->valid && current->latestMessageId == t->latestMessageId()) return true;
    if (auto cached = m_db->getSummary(key)) {
        m_store->setStatuses(threadId, m_db->actionStatusesForThread(threadId));
        m_store->setSummary(threadId, *cached);
        Q_EMIT summaryReady(threadId);
        return true;
    }
    return false;
}

void SummaryQueue::enqueue(const QStringList& threadIds, bool highPriority)
{
    for (const auto& id : threadIds) {
        if (m_inflight.contains(id)) continue;
        if (loadFromCache(id)) continue;
        if (m_failures.value(id) >= 2) continue;
        m_queue.removeAll(id);
        if (highPriority) m_queue.prepend(id); else m_queue.append(id);
    }
    pump();
}

void SummaryQueue::enqueueFront(const QString& threadId)
{
    if (m_inflight.contains(threadId)) return;
    if (loadFromCache(threadId)) return;
    m_failures.remove(threadId);
    m_queue.removeAll(threadId);
    m_queue.prepend(threadId);
    pump();
}

void SummaryQueue::setVisibleRows(const QStringList& ordered, int firstVisible, int lastVisible)
{
    // Visible rows first, then the next 50 below the viewport.
    QStringList visible, ahead;
    for (int i = qMax(0, firstVisible); i <= lastVisible && i < ordered.size(); ++i) visible << ordered[i];
    for (int i = lastVisible + 1; i < qMin(ordered.size(), lastVisible + 51); ++i) ahead << ordered[i];
    enqueue(visible, true);
    enqueue(ahead, false);
}

void SummaryQueue::clear() { m_queue.clear(); }

void SummaryQueue::invalidate(const QString& threadId)
{
    m_failures.remove(threadId);
    m_queue.removeAll(threadId);
}

void SummaryQueue::pump()
{
    if (!m_ai->hasApiKey()) return;
    while (m_inflight.size() < m_workers && !m_queue.isEmpty()) {
        const QString id = m_queue.takeFirst();
        const Thread* t = m_store->thread(id);
        if (!t) continue;
        // Calendar invites are always informational; skip the model entirely.
        if (t->hasCalendarInvite()) {
            ThreadSummary s;
            s.threadId = id; s.latestMessageId = t->latestMessageId(); s.valid = true;
            s.bullets << QStringLiteral("Calendar invitation: ") + t->subject();
            m_db->putSummary(s);
            m_store->setSummary(id, s);
            Q_EMIT summaryReady(id);
            continue;
        }
        m_inflight.insert(id);
        m_store->markEvaluating(id, true);
        m_ai->summarizeThread(*t, m_settings->userName(), m_settings->userEmail(),
            [this, id](const ThreadSummary& s, const QString& err) {
                m_inflight.remove(id);
                m_store->markEvaluating(id, false);
                if (!err.isEmpty()) {
                    m_failures[id] = m_failures.value(id) + 1;
                    Q_EMIT summaryFailed(id, err);
                } else {
                    m_db->putSummary(s);
                    m_db->pruneSummaries(2000);
                    m_store->setStatuses(id, m_db->actionStatusesForThread(id));
                    m_store->setSummary(id, s);
                    Q_EMIT summaryReady(id);
                }
                pump();
            });
    }
}

} // namespace ponymail
