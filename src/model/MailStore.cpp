#include "MailStore.h"
#include <algorithm>
#include "Triage.h"

namespace ponymail {

MailStore::MailStore(QObject* parent) : QObject(parent) {}

const Thread* MailStore::thread(const QString& id) const
{
    const auto it = m_threads.constFind(id);
    return it == m_threads.constEnd() ? nullptr : &it.value();
}

Thread* MailStore::mutableThread(const QString& id)
{
    const auto it = m_threads.find(id);
    return it == m_threads.end() ? nullptr : &it.value();
}

void MailStore::upsertThread(const Thread& t)
{
    m_threads.insert(t.id, t);
    Q_EMIT threadChanged(t.id);
}

void MailStore::removeThread(const QString& id)
{
    m_threads.remove(id);
    for (auto it = m_folderIds.begin(); it != m_folderIds.end(); ++it) {
        if (it->removeAll(id) > 0) Q_EMIT folderChanged(it.key());
    }
    Q_EMIT threadRemoved(id);
}

QList<const Thread*> MailStore::threadsInFolder(const QString& folderKey) const
{
    QList<const Thread*> out;
    for (const auto& id : m_folderIds.value(folderKey)) {
        if (const Thread* t = thread(id)) out << t;
    }
    return out;
}

void MailStore::setFolderThreadIds(const QString& folderKey, const QStringList& ids, bool append)
{
    QStringList& list = m_folderIds[folderKey];
    if (!append) list.clear();
    for (const auto& id : ids) if (!list.contains(id)) list << id;
    Q_EMIT folderChanged(folderKey);
}

void MailStore::addThreadToFolder(const QString& folderKey, const QString& threadId)
{
    QStringList& list = m_folderIds[folderKey];
    if (!list.contains(threadId)) { list.prepend(threadId); Q_EMIT folderChanged(folderKey); }
}

void MailStore::removeThreadFromFolder(const QString& folderKey, const QString& threadId)
{
    if (m_folderIds[folderKey].removeAll(threadId) > 0) Q_EMIT folderChanged(folderKey);
}

void MailStore::setLabels(const QList<Label>& l)
{
    m_labels = l;
    snoozedLabelId = labelIdByName(QStringLiteral("Snoozed"));
    blockedLabelId = labelIdByName(QStringLiteral("Blocked"));
    Q_EMIT labelsChanged();
}

QString MailStore::labelIdByName(const QString& name) const
{
    for (const auto& l : m_labels) if (l.name.compare(name, Qt::CaseInsensitive) == 0) return l.id;
    return {};
}

QString MailStore::labelName(const QString& id) const
{
    for (const auto& l : m_labels) if (l.id == id) return l.name;
    return id;
}

void MailStore::setSummary(const QString& threadId, const ThreadSummary& s)
{
    m_summaries.insert(threadId, s);
    m_evaluating.remove(threadId);
    Q_EMIT triageChanged(threadId);
}

const ThreadSummary* MailStore::summary(const QString& threadId) const
{
    const auto it = m_summaries.constFind(threadId);
    return it == m_summaries.constEnd() ? nullptr : &it.value();
}

void MailStore::setStatuses(const QString& threadId, const QHash<QString, ActionStatusRecord>& st)
{
    m_statuses.insert(threadId, st);
    Q_EMIT triageChanged(threadId);
}

void MailStore::markEvaluating(const QString& threadId, bool on)
{
    if (on) m_evaluating.insert(threadId); else m_evaluating.remove(threadId);
    Q_EMIT triageChanged(threadId);
}

TriageResult MailStore::triage(const QString& threadId) const
{
    const Thread* t = thread(threadId);
    const bool calendar = t && t->hasCalendarInvite();
    const ThreadSummary* s = summary(threadId);
    // A cached summary for an older latest-message is still shown but re-evaluation is pending.
    return Triage::classifyThread(s, m_statuses.value(threadId), calendar);
}

} // namespace ponymail
