#pragma once
#include "Theme.h"
#include "model/MailStore.h"
#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>

class QNetworkAccessManager;
class QLineEdit;
class QComboBox;
class QToolButton;
class QSplitter;
class QLabel;
class QAction;
class QMenu;

namespace ponymail {

class Settings; class Database; class OAuthManager; class GmailClient; class SyncEngine;
class ClaudeClient; class SummaryQueue; class NavRail; class MessageListModel; class MessageListView;
class ReadingPane; class ComposeWindow;
struct ComposeContext;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void buildUi();
    void applyTheme(const QString& key);
    void connectSignals();
    void ensureSignedIn();
    void openAccountDialog();
    void openAddAccount();
    void startMailbox();
    void onFolderSelected(const Folder& f);
    void onSelectionChanged();
    void onSearchSubmitted();
    void clearSearch();
    void openCompose(const ComposeContext& ctx);
    void composeNew();
    void replyToSelected(bool all);
    void forwardSelected();
    void deleteSelected();
    void archiveSelected();
    void toggleReadSelected();
    void toggleStarSelected();
    void blockSelectedSender();
    void snoozeSelected(qint64 untilMs);
    void writeWithAi();
    void emptyFolder(const Folder& f);
    void openDraft(const QString& threadId, const QString& messageId);
    void updateCounts();
    void prefetchVisible();
    QString selectedThreadId() const;
    const Thread* selectedThread() const;
    void showStatus(const QString& msg, int ms = 6000);

    Settings* m_settings; Database* m_db; QNetworkAccessManager* m_nam; OAuthManager* m_auth; GmailClient* m_gmail;
    MailStore* m_store; SyncEngine* m_sync; ClaudeClient* m_ai; SummaryQueue* m_queue;

    NavRail* m_nav; MessageListModel* m_model; MessageListView* m_list; ReadingPane* m_reading; QSplitter* m_splitter;
    QLineEdit* m_search; QComboBox* m_filter; QToolButton* m_unreadToggle; QLabel* m_syncLabel; QLabel* m_listTitle; QLabel* m_zoomLabel;
    QMenu* m_snoozeMenu = nullptr;
    QAction *m_replyA, *m_replyAllA, *m_forwardA, *m_deleteA, *m_archiveA, *m_snoozeA, *m_blockA, *m_readA, *m_starA, *m_aiA;
    Theme m_theme = Theme::light();
    Folder m_folderBeforeSearch;
    bool m_inSearch = false;
    QString m_syncState;
    QElapsedTimer m_lastFocusSync;
    QHash<QString, QString> m_draftIdByMessage;
};

} // namespace ponymail
