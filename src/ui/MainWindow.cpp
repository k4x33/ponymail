#include "MainWindow.h"
#include <memory>
#include <functional>
#include <algorithm>
#include "NavRail.h"
#include "MessageListModel.h"
#include "MessageListView.h"
#include "ReadingPane.h"
#include "ComposeWindow.h"
#include "AiDialog.h"
#include "SettingsDialogs.h"
#include "AccountSetupDialog.h"
#include "Icons.h"
#include "core/Settings.h"
#include "core/Paths.h"
#include "storage/Database.h"
#include "auth/OAuthManager.h"
#include "gmail/GmailClient.h"
#include "sync/SyncEngine.h"
#include "ai/ClaudeClient.h"
#include "ai/SummaryQueue.h"
#include "model/SenderResolver.h"
#include <QApplication>
#include <QNetworkAccessManager>
#include <QToolBar>
#include <QToolButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QShortcut>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

namespace ponymail {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    Paths::ensureDirs();
    m_settings = new Settings(this);
    m_settings->load();
    m_db = new Database(this);
    if (!m_db->open(Paths::databaseFile()))
        QMessageBox::critical(this, QStringLiteral("Database error"), QStringLiteral("Could not open ") + Paths::databaseFile());
    m_nam = new QNetworkAccessManager(this);
    m_auth = new OAuthManager(m_settings, m_nam, this);
    m_gmail = new GmailClient(m_auth, m_nam, this);
    m_store = new MailStore(this);
    m_sync = new SyncEngine(m_gmail, m_store, m_db, m_settings, this);
    m_ai = new ClaudeClient(m_settings, m_nam, this);
    m_queue = new SummaryQueue(m_ai, m_store, m_db, m_settings, this);

    setWindowTitle(QStringLiteral("PonyMail"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/ponymail.png")));
    buildUi();
    connectSignals();
    applyTheme(m_settings->theme());

    const QByteArray geo = m_settings->windowGeometry();
    if (!geo.isEmpty()) restoreGeometry(geo); else resize(1480, 920);
    m_lastFocusSync.start();
    QTimer::singleShot(0, this, &MainWindow::ensureSignedIn);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    auto* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto mk = [&](const QString& icon, const QString& text, const QString& tip) {
        QAction* a = tb->addAction(Icons::icon(icon, m_theme.textMuted), text);
        a->setToolTip(tip);
        a->setProperty("iconName", icon);
        return a;
    };
    QAction* newA = mk(QStringLiteral("compose"), QStringLiteral("New mail"), QStringLiteral("New mail (C)"));
    tb->addSeparator();
    m_replyA = mk(QStringLiteral("reply"), QStringLiteral("Reply"), QStringLiteral("Reply (Ctrl+R)"));
    m_replyAllA = mk(QStringLiteral("replyAll"), QStringLiteral("Reply all"), QStringLiteral("Reply all (Ctrl+Shift+R)"));
    m_forwardA = mk(QStringLiteral("forward"), QStringLiteral("Forward"), QStringLiteral("Forward (Ctrl+F)"));
    tb->addSeparator();
    m_deleteA = mk(QStringLiteral("trash"), QStringLiteral("Delete"), QStringLiteral("Delete (Del)"));
    m_archiveA = mk(QStringLiteral("archive"), QStringLiteral("Archive"), QStringLiteral("Archive (E)"));
    m_snoozeA = mk(QStringLiteral("snooze"), QStringLiteral("Snooze"), QStringLiteral("Snooze"));
    m_blockA = mk(QStringLiteral("block"), QStringLiteral("Block sender"), QStringLiteral("Block this sender"));
    tb->addSeparator();
    m_readA = mk(QStringLiteral("envelopeOpen"), QStringLiteral("Read / Unread"), QStringLiteral("Toggle read (Ctrl+Q)"));
    m_starA = mk(QStringLiteral("star"), QStringLiteral("Star"), QStringLiteral("Toggle star (S)"));
    tb->addSeparator();
    m_aiA = mk(QStringLiteral("wand"), QStringLiteral("Write with AI"), QStringLiteral("Write / Reply / Forward with AI"));
    auto* spacer = new QWidget(tb); spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    QAction* zoomOutA = tb->addAction(QStringLiteral("A\u2212"));
    zoomOutA->setToolTip(QStringLiteral("Smaller text in reading pane (Ctrl+-)"));
    zoomOutA->setShortcut(QKeySequence::ZoomOut);
    QAction* zoomInA = tb->addAction(QStringLiteral("A+"));
    zoomInA->setToolTip(QStringLiteral("Larger text in reading pane (Ctrl++)"));
    zoomInA->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    QAction* zoomResetA = new QAction(QStringLiteral("Reset text size"), this);
    zoomResetA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    addAction(zoomResetA);
    m_zoomLabel = new QLabel(tb);
    m_zoomLabel->setProperty("muted", true);
    m_zoomLabel->setMinimumWidth(42);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    tb->addWidget(m_zoomLabel);
    tb->addSeparator();
    m_syncLabel = new QLabel(tb);
    m_syncLabel->setProperty("muted", true);
    tb->addWidget(m_syncLabel);
    connect(zoomOutA, &QAction::triggered, this, [this]() { m_reading->zoomOut(); });
    connect(zoomInA, &QAction::triggered, this, [this]() { m_reading->zoomIn(); });
    connect(zoomResetA, &QAction::triggered, this, [this]() { m_reading->zoomReset(); });
    QAction* refreshA = mk(QStringLiteral("refresh"), QString(), QStringLiteral("Sync now (F5)"));
    refreshA->setShortcut(QKeySequence::Refresh);

    connect(newA, &QAction::triggered, this, &MainWindow::composeNew);
    connect(m_replyA, &QAction::triggered, this, [this]() { replyToSelected(false); });
    connect(m_replyAllA, &QAction::triggered, this, [this]() { replyToSelected(true); });
    connect(m_forwardA, &QAction::triggered, this, &MainWindow::forwardSelected);
    connect(m_deleteA, &QAction::triggered, this, &MainWindow::deleteSelected);
    connect(m_archiveA, &QAction::triggered, this, &MainWindow::archiveSelected);
    connect(m_blockA, &QAction::triggered, this, &MainWindow::blockSelectedSender);
    connect(m_readA, &QAction::triggered, this, &MainWindow::toggleReadSelected);
    connect(m_starA, &QAction::triggered, this, &MainWindow::toggleStarSelected);
    connect(m_aiA, &QAction::triggered, this, &MainWindow::writeWithAi);
    connect(refreshA, &QAction::triggered, this, [this]() { m_sync->pollNow(); m_sync->refreshActiveFolder(true); });
    m_replyA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_replyAllA->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_forwardA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    m_readA->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    // Snooze menu
    auto* snoozeMenu = new QMenu(this);
    auto addSnooze = [&](const QString& label, std::function<QDateTime()> when) {
        connect(snoozeMenu->addAction(label), &QAction::triggered, this, [this, when]() { snoozeSelected(when().toMSecsSinceEpoch()); });
    };
    addSnooze(QStringLiteral("Later today (+3 hours)"), []() { return QDateTime::currentDateTime().addSecs(3 * 3600); });
    addSnooze(QStringLiteral("Tomorrow 09:00"), []() { return QDateTime(QDate::currentDate().addDays(1), QTime(9, 0)); });
    addSnooze(QStringLiteral("This weekend (Sat 09:00)"), []() { QDate d = QDate::currentDate(); int add = (6 - d.dayOfWeek() + 7) % 7; if (add == 0) add = 7; return QDateTime(d.addDays(add), QTime(9, 0)); });
    addSnooze(QStringLiteral("Next week (Mon 09:00)"), []() { QDate d = QDate::currentDate(); int add = (8 - d.dayOfWeek()) % 7; if (add == 0) add = 7; return QDateTime(d.addDays(add), QTime(9, 0)); });
    snoozeMenu->addSeparator();
    connect(snoozeMenu->addAction(QStringLiteral("Pick date & time…")), &QAction::triggered, this, [this]() {
        QDialog dlg(this); dlg.setWindowTitle(QStringLiteral("Snooze until"));
        auto* l = new QVBoxLayout(&dlg);
        auto* edit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1), &dlg);
        edit->setCalendarPopup(true); edit->setDisplayFormat(QStringLiteral("dd MMM yyyy HH:mm"));
        l->addWidget(edit);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted) snoozeSelected(edit->dateTime().toMSecsSinceEpoch());
    });
    m_snoozeA->setMenu(snoozeMenu);
    m_snoozeMenu = snoozeMenu;
    if (auto* btn = qobject_cast<QToolButton*>(tb->widgetForAction(m_snoozeA))) btn->setPopupMode(QToolButton::InstantPopup);

    // --- three panes ---
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);

    m_nav = new NavRail(m_splitter);
    m_nav->setFolders(m_sync->systemFolders(), m_sync->labelFolders());
    auto* settingsMenu = new QMenu(this);
    connect(settingsMenu->addAction(QStringLiteral("Add account…")), &QAction::triggered, this, &MainWindow::openAddAccount);
    connect(settingsMenu->addAction(QStringLiteral("Account & AI…")), &QAction::triggered, this, &MainWindow::openAccountDialog);
    connect(settingsMenu->addAction(QStringLiteral("AI Writing Style…")), &QAction::triggered, this, [this]() { AiWritingStyleDialog d(m_settings, this); d.exec(); });
    connect(settingsMenu->addAction(QStringLiteral("Blocked Senders…")), &QAction::triggered, this, [this]() { BlockedSendersDialog d(m_db, this); d.exec(); });
    settingsMenu->addSeparator();
    auto* themeMenu = settingsMenu->addMenu(QStringLiteral("Theme"));
    for (const auto& t : {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("testudo"), QStringLiteral("testudo-dark")})
        connect(themeMenu->addAction(t), &QAction::triggered, this, [this, t]() { m_settings->setTheme(t); });
    auto* sortMenu = settingsMenu->addMenu(QStringLiteral("Sort"));
    connect(sortMenu->addAction(QStringLiteral("Newest first")), &QAction::triggered, this, [this]() { m_settings->setDefaultSort(QStringLiteral("date_desc")); m_model->setSort(QStringLiteral("date_desc")); });
    connect(sortMenu->addAction(QStringLiteral("Oldest first")), &QAction::triggered, this, [this]() { m_settings->setDefaultSort(QStringLiteral("date_asc")); m_model->setSort(QStringLiteral("date_asc")); });
    connect(sortMenu->addAction(QStringLiteral("Sender")), &QAction::triggered, this, [this]() { m_settings->setDefaultSort(QStringLiteral("sender")); m_model->setSort(QStringLiteral("sender")); });
    settingsMenu->addSeparator();
    connect(settingsMenu->addAction(QStringLiteral("About PonyMail")), &QAction::triggered, this, [this]() { AboutDialog d(this); d.exec(); });
    m_nav->settingsButton()->setMenu(settingsMenu);

    // Center column
    auto* center = new QWidget(m_splitter);
    auto* cl = new QVBoxLayout(center);
    cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(0);

    auto* listTb = new QWidget(center);
    listTb->setStyleSheet(QStringLiteral("background:%1;border-bottom:1px solid %2;").arg(m_theme.surface.name(), m_theme.border.name()));
    auto* ltl = new QVBoxLayout(listTb);
    ltl->setContentsMargins(10, 8, 10, 6); ltl->setSpacing(6);
    auto* row1 = new QHBoxLayout();
    m_listTitle = new QLabel(QStringLiteral("Inbox"), listTb);
    m_listTitle->setProperty("heading", true);
    row1->addWidget(m_listTitle, 1);
    m_unreadToggle = new QToolButton(listTb);
    m_unreadToggle->setObjectName(QStringLiteral("unreadToggle"));
    m_unreadToggle->setCheckable(true);
    m_unreadToggle->setToolTip(QStringLiteral("Show unread only (stacks with the filter)"));
    m_unreadToggle->setCursor(Qt::PointingHandCursor);
    row1->addWidget(m_unreadToggle);
    m_filter = new QComboBox(listTb);
    m_filter->addItems({QStringLiteral("All"), QStringLiteral("Action only"), QStringLiteral("Follow-up only"), QStringLiteral("Info only"),
                        QStringLiteral("Important"), QStringLiteral("Starred"), QStringLiteral("To me"), QStringLiteral("Has files"), QStringLiteral("Calendar invites")});
    m_filter->setMinimumWidth(150);
    row1->addWidget(m_filter);
    ltl->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    m_search = new QLineEdit(listTb);
    m_search->setPlaceholderText(QStringLiteral("Search mail  (Enter to search Gmail)"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(Icons::icon(QStringLiteral("search"), m_theme.textFaint), QLineEdit::LeadingPosition);
    row2->addWidget(m_search, 1);
    ltl->addLayout(row2);

    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    for (const auto& chip : {QStringLiteral("from:"), QStringLiteral("has:attachment"), QStringLiteral("is:starred"), QStringLiteral("to:me")}) {
        auto* b = new QToolButton(listTb);
        b->setObjectName(QStringLiteral("chip"));
        b->setText(chip);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, chip]() {
            QString t = m_search->text().trimmed();
            if (!t.isEmpty()) t += ' ';
            m_search->setText(t + chip + (chip.endsWith(':') ? QString() : QStringLiteral(" ")));
            m_search->setFocus();
            if (!chip.endsWith(':')) onSearchSubmitted();
        });
        chips->addWidget(b);
    }
    chips->addStretch();
    ltl->addLayout(chips);
    cl->addWidget(listTb);

    m_model = new MessageListModel(m_store, m_db, this);
    m_model->setSort(m_settings->defaultSort());
    m_model->setFolder(QStringLiteral("inbox"));
    m_list = new MessageListView(center);
    m_list->setModel(m_model);
    cl->addWidget(m_list, 1);

    m_reading = new ReadingPane(m_store, m_db, m_gmail, m_queue, m_ai, m_splitter);
    m_reading->setZoom(m_settings->readingZoom());
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_reading->zoom() * 100)));
    connect(m_reading, &ReadingPane::zoomChanged, this, [this](qreal z) {
        m_settings->setReadingZoom(z);
        m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(z * 100)));
    });

    m_splitter->addWidget(m_nav);
    m_splitter->addWidget(center);
    m_splitter->addWidget(m_reading);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);
    m_splitter->setSizes({220, m_settings->splitterListWidth(), 900});
    setCentralWidget(m_splitter);
    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::connectSignals()
{
    connect(m_settings, &Settings::themeChanged, this, &MainWindow::applyTheme);
    connect(m_nav, &NavRail::folderSelected, this, &MainWindow::onFolderSelected);
    connect(m_nav, &NavRail::composeRequested, this, &MainWindow::composeNew);
    connect(m_nav, &NavRail::emptyFolderRequested, this, &MainWindow::emptyFolder);
    connect(m_store, &MailStore::labelsChanged, this, [this]() { m_nav->setFolders(m_sync->systemFolders(), m_sync->labelFolders()); });
    connect(m_store, &MailStore::folderChanged, this, [this](const QString&) { updateCounts(); });
    connect(m_store, &MailStore::threadChanged, this, [this](const QString&) { updateCounts(); });

    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { onSelectionChanged(); });
    connect(m_list, &MessageListView::quickStar, this, [this](const QString& id) { if (const Thread* t = m_store->thread(id)) m_sync->setStarred(id, !t->isStarred()); });
    connect(m_list, &MessageListView::quickReadToggle, this, [this](const QString& id) { if (const Thread* t = m_store->thread(id)) m_sync->markRead({id}, t->isUnread()); });
    connect(m_list, &MessageListView::quickDelete, this, [this](const QString& id) {
        const int row = m_model->rowForThread(id);
        m_list->animateRemove(id, [this, id, row]() { m_sync->trashThreads({id}); m_list->selectRow(qMin(row, m_model->rowCount() - 1)); });
    });
    connect(m_list, &MessageListView::visibleRangeChanged, this, [this](int, int) { prefetchVisible(); });
    connect(m_list, &MessageListView::reachedBottom, this, [this]() { m_sync->loadMoreActiveFolder(); });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = m_list->indexAt(pos);
        if (!idx.isValid()) return;
        if (!m_list->selectionModel()->isSelected(idx)) m_list->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
        else m_list->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Current);
        const QStringList ids = m_list->selectedThreadIds();
        bool anyUnread = false, anyStarred = false;
        for (const auto& id : ids) if (const Thread* t = m_store->thread(id)) { anyUnread |= t->isUnread(); anyStarred |= t->isStarred(); }
        QMenu menu(this);
        menu.addAction(m_replyA); menu.addAction(m_replyAllA); menu.addAction(m_forwardA);
        menu.addSeparator();
        menu.addAction(m_deleteA); menu.addAction(m_archiveA);
        QAction* snooze = menu.addMenu(m_snoozeMenu)->menu()->menuAction();
        snooze->setText(QStringLiteral("Snooze"));
        snooze->setIcon(m_snoozeA->icon());
        menu.addAction(m_blockA);
        menu.addSeparator();
        m_readA->setText(anyUnread ? QStringLiteral("Mark as read") : QStringLiteral("Mark as unread"));
        m_starA->setText(anyStarred ? QStringLiteral("Remove star") : QStringLiteral("Star"));
        menu.addAction(m_readA); menu.addAction(m_starA);
        menu.addSeparator();
        menu.addAction(m_aiA);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        m_readA->setText(QStringLiteral("Read / Unread"));
        m_starA->setText(QStringLiteral("Star"));
    });
    connect(m_list, &MessageListView::rowActivated, this, [this](const QString& id) {
        if (const Thread* t = m_store->thread(id)) if (t->hasLabel(QStringLiteral("DRAFT")) && t->latest()) openDraft(id, t->latest()->id);
    });
    connect(m_model, &MessageListModel::rebuilt, this, [this]() { prefetchVisible(); updateCounts(); });

    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int i) { m_model->setFilter(static_cast<ListFilter>(i)); });
    connect(m_unreadToggle, &QToolButton::toggled, this, [this](bool on) { m_model->setUnreadOnly(on); });
    connect(m_search, &QLineEdit::returnPressed, this, &MainWindow::onSearchSubmitted);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (t.isEmpty() && m_inSearch) clearSearch();
        else if (!m_inSearch) m_model->setLocalSearch(t);   // instant local narrowing while typing
    });

    connect(m_reading, &ReadingPane::editDraftRequested, this, &MainWindow::openDraft);
    connect(m_reading, &ReadingPane::statusMessage, this, [this](const QString& m) { showStatus(m); });

    connect(m_sync, &SyncEngine::statusMessage, this, [this](const QString& m) { showStatus(m); });
    connect(m_sync, &SyncEngine::errorOccurred, this, [this](const QString& m) { showStatus(QStringLiteral("⚠ ") + m, 10000); });
    connect(m_sync, &SyncEngine::syncStateChanged, this, [this](bool syncing, bool limited) {
        m_syncState = limited ? QStringLiteral("Rate limited · 120 s polling") : syncing ? QStringLiteral("Syncing…") : QStringLiteral("Up to date · 30 s polling");
        m_syncLabel->setText(m_syncState);
    });
    connect(m_gmail, &GmailClient::quotaUsage, this, [this](int used, int budget) {
        if (used * 2 >= budget) m_syncLabel->setText(QStringLiteral("%1 · Gmail quota %2/%3 units").arg(m_syncState).arg(used).arg(budget));
        else m_syncLabel->setText(m_syncState);
    });
    connect(m_gmail, &GmailClient::quotaThrottled, this, [this](int used, int budget, int queued) {
        m_syncLabel->setText(QStringLiteral("Pacing Gmail requests · %1/%2 units · %3 queued").arg(used).arg(budget).arg(queued));
    });
    connect(m_sync, &SyncEngine::folderLoading, this, [this](const QString& key, bool loading) {
        if (key == m_model->folderKey() && loading && m_model->rowCount() == 0) m_listTitle->setText(m_sync->activeFolder().title + QStringLiteral("  …"));
        else if (key == m_model->folderKey()) m_listTitle->setText(m_sync->activeFolder().title);
    });
    connect(m_sync, &SyncEngine::newMessagesArrived, this, [this](const QStringList& ids) {
        showStatus(QStringLiteral("%1 new message%2").arg(ids.size()).arg(ids.size() == 1 ? QString() : QStringLiteral("s")));
        m_queue->enqueue(ids, true);
    });
    connect(m_sync, &SyncEngine::profileLoaded, this, [this](const QString& email) {
        m_nav->setAccountLabel(email);
        m_model->setIdentity(email, m_settings->userName());
    });

    connect(m_auth, &OAuthManager::authenticated, this, [this](Provider p) { showStatus(QStringLiteral("Signed in with ") + providerDisplayName(p) + QLatin1Char('.')); });
    connect(m_auth, &OAuthManager::authFailed, this, [this](const QString& e) { showStatus(QStringLiteral("Sign-in failed: ") + e, 10000); });
    connect(m_auth, &OAuthManager::signedOut, this, [this]() {
        m_sync->stop(); m_reading->clear(); m_nav->setAccountLabel(QString());
        m_model->setFolder(QString());
        showStatus(QStringLiteral("Account removed."));
        QTimer::singleShot(300, this, &MainWindow::ensureSignedIn);
    });
    connect(m_gmail, &GmailClient::authExpired, this, [this]() { QTimer::singleShot(0, this, &MainWindow::ensureSignedIn); });

    connect(m_queue, &SummaryQueue::summaryReady, this, [this](const QString&) { updateCounts(); });

    // Shortcuts that must not fire while typing
    auto* delS = new QShortcut(QKeySequence::Delete, m_list);
    delS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(delS, &QShortcut::activated, this, &MainWindow::deleteSelected);
    auto* starS = new QShortcut(QKeySequence(Qt::Key_S), m_list); starS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(starS, &QShortcut::activated, this, &MainWindow::toggleStarSelected);
    auto* archS = new QShortcut(QKeySequence(Qt::Key_E), m_list); archS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(archS, &QShortcut::activated, this, &MainWindow::archiveSelected);
    auto* openS = new QShortcut(QKeySequence(Qt::Key_Return), m_list); openS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(openS, &QShortcut::activated, this, [this]() { if (const Thread* t = selectedThread()) if (t->hasLabel(QStringLiteral("DRAFT")) && t->latest()) openDraft(t->id, t->latest()->id); });
    auto* escS = new QShortcut(QKeySequence(Qt::Key_Escape), m_search); escS->setContext(Qt::WidgetShortcut);
    connect(escS, &QShortcut::activated, this, [this]() { m_search->clear(); m_list->setFocus(); });
    auto* composeS = new QShortcut(QKeySequence(Qt::Key_C), m_list); composeS->setContext(Qt::WidgetWithChildrenShortcut);
    connect(composeS, &QShortcut::activated, this, &MainWindow::composeNew);
    auto* findS = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this);
    connect(findS, &QShortcut::activated, this, [this]() { m_search->setFocus(); m_search->selectAll(); });
    onSelectionChanged();
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    // "C" opens compose unless a text field has focus.
    QWidget* f = QApplication::focusWidget();
    const bool typing = qobject_cast<QLineEdit*>(f) || (f && f->inherits("QPlainTextEdit")) || (f && f->inherits("QWebEngineView")) || (f && f->parent() && f->parent()->inherits("QWebEngineView"));
    if (!typing && e->key() == Qt::Key_C && e->modifiers() == Qt::NoModifier) { composeNew(); return; }
    QMainWindow::keyPressEvent(e);
}

// ---------------------------------------------------------------------------
void MainWindow::applyTheme(const QString& key)
{
    m_theme = Theme::named(key);
    qApp->setStyleSheet(m_theme.stylesheet());
    m_list->setTheme(m_theme);
    m_nav->setTheme(m_theme);
    m_reading->setTheme(m_theme);
    m_unreadToggle->setIcon(Icons::icon(QStringLiteral("unreadToggle"), m_theme.textMuted, 20));
    for (QAction* a : findChildren<QAction*>()) {
        const QString ic = a->property("iconName").toString();
        if (ic.isEmpty()) continue;
        QColor c = m_theme.textMuted;
        if (ic == QLatin1String("wand")) c = QColor(0x7C, 0x3A, 0xED);
        if (ic == QLatin1String("compose")) c = m_theme.accent;
        if (ic == QLatin1String("trash") || ic == QLatin1String("block")) c = m_theme.danger;
        a->setIcon(Icons::icon(ic, c));
    }
    if (!m_theme.emblemName.isEmpty()) {
        m_listTitle->setPixmap(Icons::pixmap(QStringLiteral("turtle"), m_theme.accent, 26, devicePixelRatioF()));
        setWindowTitle(QStringLiteral("PonyMail · Fear the Turtle"));
    } else {
        m_listTitle->setText(m_sync->activeFolder().title);
        setWindowTitle(QStringLiteral("PonyMail"));
    }
}

void MainWindow::showStatus(const QString& msg, int ms) { statusBar()->showMessage(msg, ms); }

void MainWindow::ensureSignedIn()
{
    if (m_auth->isAuthenticated()) { startMailbox(); return; }
    openAddAccount();
}

void MainWindow::openAddAccount()
{
    if (findChild<AccountSetupDialog*>()) return;   // already open
    auto* dlg = new AccountSetupDialog(m_auth, m_theme, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &AccountSetupDialog::accountAdded, this, [this](Provider) { m_sync->stop(); m_reading->clear(); startMailbox(); });
    dlg->open();
}

void MainWindow::openAccountDialog()
{
    auto* dlg = new AccountDialog(m_settings, m_auth, m_ai, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &AccountDialog::addAccountRequested, this, [this, dlg]() { dlg->accept(); openAddAccount(); });
    connect(dlg, &AccountDialog::signOutRequested, this, [this]() { m_auth->signOut(); });
    dlg->open();
}

void MainWindow::startMailbox()
{
    const Provider p = m_auth->activeProvider();
    if (p == Provider::Google) { m_sync->start(); return; }
    if (p == Provider::Microsoft) {
        m_nav->setAccountLabel(m_settings->userEmail());
        m_model->setIdentity(m_settings->userEmail(), m_settings->userName());
        showStatus(QStringLiteral("Outlook account connected (%1). Mailbox sync for Microsoft Graph is not available in this build; mail features currently require a Google account.").arg(m_settings->userEmail()), 0);
        QMessageBox::information(this, QStringLiteral("Outlook account connected"),
            QStringLiteral("Signed in as %1.\n\nThis build syncs mail through the Gmail API only; Microsoft Graph mailbox sync is not yet implemented. "
                           "Your Outlook sign-in has been saved securely and will be used once Graph support ships. To read mail now, add a Google account.").arg(m_settings->userEmail()));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::onFolderSelected(const Folder& f)
{
    m_inSearch = false;
    m_search->blockSignals(true); m_search->clear(); m_search->blockSignals(false);
    m_model->setLocalSearch(QString());
    m_sync->setActiveFolder(f);
    m_model->setFolder(f.key);
    m_listTitle->setText(f.title);
    m_reading->clear();
    if (f.key == QLatin1String("drafts")) {
        m_gmail->listDrafts(100, QString(), [this](const QJsonObject& o, int status, const QString&) {
            if (status != 200) return;
            m_draftIdByMessage.clear();
            for (const auto v : o.value(QStringLiteral("drafts")).toArray()) {
                const auto d = v.toObject();
                m_draftIdByMessage.insert(d.value(QStringLiteral("message")).toObject().value(QStringLiteral("id")).toString(), d.value(QStringLiteral("id")).toString());
            }
        });
    }
}

void MainWindow::onSearchSubmitted()
{
    const QString text = m_search->text().trimmed();
    // Secret commands
    if (text.compare(QStringLiteral("testudo-on"), Qt::CaseInsensitive) == 0) {
        m_settings->setTheme(m_theme.dark ? QStringLiteral("testudo-dark") : QStringLiteral("testudo"));
        m_search->clear(); showStatus(QStringLiteral("Fear the Turtle. 🐢")); return;
    }
    if (text.compare(QStringLiteral("testudo-off"), Qt::CaseInsensitive) == 0) {
        m_settings->setTheme(m_theme.dark ? QStringLiteral("dark") : QStringLiteral("light"));
        m_search->clear(); showStatus(QStringLiteral("Theme restored.")); return;
    }
    if (text.isEmpty()) { clearSearch(); return; }
    if (!m_inSearch) { m_folderBeforeSearch = m_sync->activeFolder(); m_inSearch = true; }
    Folder f{QStringLiteral("search"), QStringLiteral("Search: ") + text, {}, text, false, true};
    m_model->setLocalSearch(QString());
    m_sync->setActiveFolder(f);
    m_model->setFolder(f.key);
    m_listTitle->setText(f.title);
}

void MainWindow::clearSearch()
{
    if (!m_inSearch) { m_model->setLocalSearch(QString()); return; }
    m_inSearch = false;
    onFolderSelected(m_folderBeforeSearch.key.isEmpty() ? m_sync->systemFolders().first() : m_folderBeforeSearch);
    m_nav->selectFolder(m_sync->activeFolder().key);
}

QString MainWindow::selectedThreadId() const
{
    const QModelIndex cur = m_list->currentIndex();
    return cur.isValid() ? cur.data(MessageListModel::ThreadIdRole).toString() : QString();
}

const Thread* MainWindow::selectedThread() const
{
    const QString id = selectedThreadId();
    return id.isEmpty() ? nullptr : m_store->thread(id);
}

void MainWindow::onSelectionChanged()
{
    const QStringList ids = m_list->selectedThreadIds();
    const bool any = !ids.isEmpty();
    for (QAction* a : {m_replyA, m_replyAllA, m_forwardA, m_aiA}) a->setEnabled(ids.size() == 1);
    for (QAction* a : {m_deleteA, m_archiveA, m_snoozeA, m_blockA, m_readA, m_starA}) a->setEnabled(any);
    if (ids.size() == 1) {
        const QString id = selectedThreadId().isEmpty() ? ids.first() : selectedThreadId();
        m_reading->showThread(id);
        m_queue->enqueueFront(id);
        // Selecting a thread never changes its read state; marking read is always an explicit user action.
    } else if (!any) {
        m_reading->clear();
    }
}

void MainWindow::prefetchVisible()
{
    const auto range = m_list->visibleRowRange();
    if (range.second < range.first) {
        // Nothing visible yet: pre-generate the top 50 of the active sort order.
        m_queue->enqueue(m_model->orderedThreadIds().mid(0, 50), false);
        return;
    }
    m_queue->setVisibleRows(m_model->orderedThreadIds(), range.first, range.second);
    if (range.first == 0) m_queue->enqueue(m_model->orderedThreadIds().mid(0, 50), false);
}

void MainWindow::updateCounts()
{
    int unread = 0;
    for (const Thread* t : m_store->threadsInFolder(QStringLiteral("inbox"))) if (t->isUnread()) ++unread;
    m_nav->setUnreadCount(QStringLiteral("inbox"), unread);
    int drafts = m_store->folderThreadIds(QStringLiteral("drafts")).size();
    m_nav->setUnreadCount(QStringLiteral("drafts"), drafts);
}

// ---------------------------------------------------------------------------
void MainWindow::openCompose(const ComposeContext& ctx)
{
    auto* w = new ComposeWindow(ctx, m_db, m_gmail, m_sync, m_settings, m_ai, m_store, m_theme, nullptr);
    connect(w, &ComposeWindow::statusMessage, this, [this](const QString& m) { showStatus(m); });
    connect(w, &ComposeWindow::sent, this, [this](const QString& tid) { if (!tid.isEmpty()) m_sync->fetchThread(tid); });
    w->show();
    w->raise();
    w->activateWindow();
}

void MainWindow::composeNew()
{
    ComposeContext c;
    c.mode = QStringLiteral("new");
    openCompose(c);
}

void MainWindow::replyToSelected(bool all)
{
    const Thread* t = selectedThread();
    if (!t || !t->latest()) return;
    // Reply to the latest message that isn't a draft.
    const Message* target = nullptr;
    for (int i = t->messages.size() - 1; i >= 0; --i) if (!t->messages[i].isDraft) { target = &t->messages[i]; break; }
    if (!target) return;
    SenderResolver who(m_settings->userEmail(), m_settings->userName());
    openCompose(ComposeWindow::contextForReply(*t, *target, all, who));
}

void MainWindow::forwardSelected()
{
    const Thread* t = selectedThread();
    if (!t || !t->latest()) return;
    SenderResolver who(m_settings->userEmail(), m_settings->userName());
    openCompose(ComposeWindow::contextForForward(*t, *t->latest(), who));
}

void MainWindow::deleteSelected()
{
    const QStringList ids = m_list->selectedThreadIds();
    if (ids.isEmpty()) return;
    const QModelIndex cur = m_list->currentIndex();
    const int row = cur.isValid() ? cur.row() : 0;
    const bool permanent = m_model->folderKey() == QLatin1String("trash") || m_model->folderKey() == QLatin1String("spam");
    if (permanent && QMessageBox::question(this, QStringLiteral("Delete permanently?"),
            QStringLiteral("Permanently delete %1 conversation%2? This cannot be undone.").arg(ids.size()).arg(ids.size() == 1 ? QString() : QStringLiteral("s"))) != QMessageBox::Yes) return;
    auto remaining = std::make_shared<int>(ids.size());
    for (const QString& id : ids) {
        m_list->animateRemove(id, [this, ids, remaining, row, permanent]() {
            if (--*remaining > 0) return;
            if (permanent) m_sync->deletePermanently(ids); else m_sync->trashThreads(ids);
            // Auto-select the next item.
            QTimer::singleShot(0, this, [this, row]() { m_list->selectRow(qMin(row, m_model->rowCount() - 1)); });
        });
    }
}

void MainWindow::archiveSelected()
{
    const QStringList ids = m_list->selectedThreadIds();
    if (ids.isEmpty()) return;
    const int row = m_list->currentIndex().row();
    m_sync->archiveThreads(ids);
    QTimer::singleShot(0, this, [this, row]() { m_list->selectRow(qMin(row, m_model->rowCount() - 1)); });
}

void MainWindow::toggleReadSelected()
{
    const QStringList ids = m_list->selectedThreadIds();
    if (ids.isEmpty()) return;
    bool anyUnread = false;
    for (const auto& id : ids) if (const Thread* t = m_store->thread(id)) if (t->isUnread()) anyUnread = true;
    m_sync->markRead(ids, anyUnread);
}

void MainWindow::toggleStarSelected()
{
    for (const auto& id : m_list->selectedThreadIds()) if (const Thread* t = m_store->thread(id)) m_sync->setStarred(id, !t->isStarred());
}

void MainWindow::blockSelectedSender()
{
    const Thread* t = selectedThread();
    if (!t) return;
    SenderResolver who(m_settings->userEmail(), m_settings->userName());
    const QString email = who.displaySenderEmail(*t);
    if (email.isEmpty()) { showStatus(QStringLiteral("Nothing to block: this conversation is only from you.")); return; }
    const QString domain = email.mid(email.indexOf('@'));
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Block sender"));
    box.setText(QStringLiteral("Block future mail from <b>%1</b>?<br><span style='color:#616161'>Matching mail is moved to Deleted Items and tagged Blocked.</span>").arg(email.toHtmlEscaped()));
    QPushButton* addr = box.addButton(QStringLiteral("Block address"), QMessageBox::AcceptRole);
    QPushButton* dom = box.addButton(QStringLiteral("Block domain %1").arg(domain), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton*>(addr)) m_sync->blockSender(t->id, email);
    else if (box.clickedButton() == static_cast<QAbstractButton*>(dom)) m_sync->blockSender(t->id, domain);
}

void MainWindow::snoozeSelected(qint64 untilMs)
{
    const QStringList ids = m_list->selectedThreadIds();
    const int row = m_list->currentIndex().row();
    for (const auto& id : ids) m_sync->snoozeThread(id, untilMs);
    showStatus(QStringLiteral("Snoozed until ") + util::formatTimestamp(QDateTime::fromMSecsSinceEpoch(untilMs)));
    QTimer::singleShot(0, this, [this, row]() { m_list->selectRow(qMin(row, m_model->rowCount() - 1)); });
}

void MainWindow::writeWithAi()
{
    const Thread* t = selectedThread();
    const QString mode = t ? QStringLiteral("reply") : QStringLiteral("new");
    const QString context = t ? ComposeWindow::threadPlainText(*t) : QString();
    const QString subject = t ? t->subject() : QString();
    const QString tid = t ? t->id : QString();
    auto* dlg = new AiDialog(m_ai, m_settings, mode, subject, context, this);
    connect(dlg, &AiDialog::insertRequested, this, [this, tid](const QString& insertMode, const ClaudeClient::DraftResult& r) {
        const Thread* th = tid.isEmpty() ? nullptr : m_store->thread(tid);
        SenderResolver who(m_settings->userEmail(), m_settings->userName());
        ComposeContext c;
        if (th && th->latest() && insertMode == QLatin1String("reply")) c = ComposeWindow::contextForReply(*th, *th->latest(), false, who);
        else if (th && th->latest() && insertMode == QLatin1String("reply_all")) c = ComposeWindow::contextForReply(*th, *th->latest(), true, who);
        else if (th && th->latest() && insertMode == QLatin1String("forward")) c = ComposeWindow::contextForForward(*th, *th->latest(), who);
        else { c.mode = QStringLiteral("new"); c.subject = r.subject; }
        if (c.subject.trimmed().isEmpty()) c.subject = r.subject;
        c.bodyHtml = r.bodyHtml;   // quoted thread stays below (quoteHtml)
        openCompose(c);
    });
    dlg->open();
}

void MainWindow::emptyFolder(const Folder& f)
{
    m_sync->countFolder(f.key, [this, f](int count) {
        const QString what = f.key == QLatin1String("trash") ? QStringLiteral("Deleted Items") : QStringLiteral("Junk Email");
        const QString n = count >= 0 ? QString::number(count) : QStringLiteral("all");
        if (QMessageBox::question(this, QStringLiteral("Empty ") + what,
                QStringLiteral("Permanently delete %1 message%2 in %3? This cannot be undone.").arg(n).arg(count == 1 ? QString() : QStringLiteral("s")).arg(what),
                QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;
        showStatus(QStringLiteral("Emptying ") + what + QStringLiteral("…"), 0);
        m_sync->emptyFolder(f.key, [this, what](int deleted, const QString& err) {
            if (!err.isEmpty()) showStatus(QStringLiteral("Empty folder failed after %1 messages: %2").arg(deleted).arg(err), 10000);
            else showStatus(QStringLiteral("%1 emptied (%2 messages).").arg(what).arg(deleted));
            m_sync->refreshActiveFolder(true);
        });
    });
}

void MainWindow::openDraft(const QString& threadId, const QString& messageId)
{
    const Thread* tp = m_store->thread(threadId);
    if (!tp) return;
    const Thread t = *tp;   // copy: the store may refresh while drafts.list is in flight
    Message m;
    bool found = false;
    for (const auto& msg : t.messages) if (msg.id == messageId) { m = msg; found = true; }
    if (!found) return;
    auto openWith = [this, t, m](const QString& draftId) {
        if (draftId.isEmpty()) { showStatus(QStringLiteral("Could not resolve the Gmail draft for this message.")); return; }
        openCompose(ComposeWindow::contextForDraft(t, m, draftId));
    };
    if (m_draftIdByMessage.contains(messageId)) { openWith(m_draftIdByMessage.value(messageId)); return; }
    m_gmail->listDrafts(100, QString(), [this, messageId, openWith](const QJsonObject& o, int status, const QString&) {
        if (status == 200) {
            for (const auto v : o.value(QStringLiteral("drafts")).toArray()) {
                const auto d = v.toObject();
                m_draftIdByMessage.insert(d.value(QStringLiteral("message")).toObject().value(QStringLiteral("id")).toString(), d.value(QStringLiteral("id")).toString());
            }
        }
        openWith(m_draftIdByMessage.value(messageId));
    });
}

// ---------------------------------------------------------------------------
bool MainWindow::event(QEvent* e)
{
    if (e->type() == QEvent::WindowActivate) {
        if (m_lastFocusSync.elapsed() > 10000) { m_lastFocusSync.restart(); m_sync->onAppFocus(); }
    }
    return QMainWindow::event(e);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    m_settings->setWindowGeometry(saveGeometry());
    const auto sizes = m_splitter->sizes();
    if (sizes.size() >= 2) m_settings->setSplitterListWidth(sizes[1]);
    QMainWindow::closeEvent(e);
}

} // namespace ponymail
