#include "RecipientField.h"
#include "storage/Database.h"
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QCompleter>
#include <QStandardItemModel>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QStyle>

namespace ponymail {

namespace { const char* kMime = "application/x-ponymail-recipient"; }

// ---------------------------------------------------------------------------
// Minimal flow layout (wraps children onto new lines).
// ---------------------------------------------------------------------------
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent) : QLayout(parent) { setContentsMargins(6, 4, 6, 4); setSpacing(4); }
    ~FlowLayout() override { while (QLayoutItem* it = takeAt(0)) delete it; }
    void addItem(QLayoutItem* item) override { m_items << item; }
    int count() const override { return m_items.size(); }
    QLayoutItem* itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt(int i) override { return (i >= 0 && i < m_items.size()) ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize s;
        for (auto* it : m_items) s = s.expandedTo(it->minimumSize());
        const auto m = contentsMargins();
        return s + QSize(m.left() + m.right(), m.top() + m.bottom());
    }
    void setGeometry(const QRect& r) override { QLayout::setGeometry(r); doLayout(r, false); }
private:
    int doLayout(const QRect& rect, bool testOnly) const
    {
        const auto m = contentsMargins();
        const QRect eff = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = eff.x(), y = eff.y(), lineH = 0;
        for (int i = 0; i < m_items.size(); ++i) {
            QLayoutItem* it = m_items[i];
            QSize sz = it->sizeHint();
            const bool last = i == m_items.size() - 1;
            if (last && it->widget() && qobject_cast<QLineEdit*>(it->widget())) sz.setWidth(qMax(140, eff.right() - x + 1));   // editor fills the rest
            if (x + sz.width() > eff.right() + 1 && lineH > 0) { x = eff.x(); y += lineH + spacing(); lineH = 0; if (last) sz.setWidth(eff.width()); }
            if (!testOnly) it->setGeometry(QRect(QPoint(x, y), sz));
            x += sz.width() + spacing();
            lineH = qMax(lineH, sz.height());
        }
        return y + lineH - rect.y() + m.bottom();
    }
    QList<QLayoutItem*> m_items;
};

// ---------------------------------------------------------------------------
RecipientPill::RecipientPill(const EmailAddress& addr, QWidget* parent) : QFrame(parent), m_addr(addr)
{
    setObjectName(QStringLiteral("pill"));
    setCursor(Qt::OpenHandCursor);
    setStyleSheet(QStringLiteral("QFrame#pill{background:#EBF3FC;border:1px solid #B4D0EE;border-radius:12px;} QLabel{color:#1B1B1B;font-size:12px;} QToolButton{border:none;color:#616161;font-weight:bold;padding:0 2px;} QToolButton:hover{color:#C4314B;}"));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(9, 2, 4, 2);
    l->setSpacing(4);
    auto* lbl = new QLabel(addr.display(), this);
    lbl->setToolTip(addr.email);
    auto* x = new QToolButton(this);
    x->setText(QStringLiteral("×"));
    x->setCursor(Qt::PointingHandCursor);
    x->setFocusPolicy(Qt::NoFocus);
    connect(x, &QToolButton::clicked, this, [this]() { Q_EMIT removeRequested(this); });
    l->addWidget(lbl);
    l->addWidget(x);
}

void RecipientPill::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) m_pressPos = e->pos();
    QFrame::mousePressEvent(e);
}

void RecipientPill::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    if ((e->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(kMime), m_addr.rfc822().toUtf8());
    mime->setText(m_addr.rfc822());
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    Q_EMIT dragStarted(this);
    if (drag->exec(Qt::MoveAction) == Qt::MoveAction) Q_EMIT removeRequested(this);
}

// ---------------------------------------------------------------------------
RecipientField::RecipientField(const QString& label, Database* db, QWidget* parent) : QFrame(parent), m_db(db)
{
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("RecipientField{border-bottom:1px solid #E1DFDD;} QLineEdit{border:none;background:transparent;padding:4px 2px;}"));
    m_flow = new FlowLayout(this);
    auto* lbl = new QLabel(label, this);
    lbl->setStyleSheet(QStringLiteral("color:#616161;min-width:34px;padding-top:4px;"));
    m_flow->addWidget(lbl);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QString());
    m_edit->installEventFilter(this);
    m_flow->addWidget(m_edit);

    m_model = new QStandardItemModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(8);
    m_edit->setCompleter(m_completer);
    connect(m_edit, &QLineEdit::textEdited, this, &RecipientField::rebuildCompleter);
    connect(m_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated), this, [this](const QModelIndex& idx) {
        EmailAddress a; a.email = idx.data(Qt::UserRole).toString(); a.name = idx.data(Qt::UserRole + 1).toString();
        m_edit->clear();
        addAddress(a);
    });
}

void RecipientField::rebuildCompleter(const QString& prefix)
{
    m_model->clear();
    if (prefix.trimmed().size() < 1) return;
    const auto contacts = m_db->searchContacts(prefix.trimmed(), 8);   // ORDER BY frequency DESC, last_seen DESC
    for (const auto& c : contacts) {
        auto* it = new QStandardItem(c.name.isEmpty() ? c.email : QStringLiteral("%1  <%2>").arg(c.name, c.email));
        it->setData(c.email, Qt::UserRole);
        it->setData(c.name, Qt::UserRole + 1);
        m_model->appendRow(it);
    }
    if (m_model->rowCount() > 0) { m_completer->complete(); }
}

bool RecipientField::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit) {
        if (e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            const bool popupVisible = m_completer->popup() && m_completer->popup()->isVisible();
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Comma || ke->key() == Qt::Key_Semicolon) && !popupVisible) {
                if (!m_edit->text().trimmed().isEmpty()) { commitPendingText(); return true; }
                if (ke->key() == Qt::Key_Comma || ke->key() == Qt::Key_Semicolon) return true;
            }
            if (ke->key() == Qt::Key_Backspace && m_edit->text().isEmpty() && !m_pills.isEmpty()) {
                auto* last = m_pills.last();
                removeAddress(last->address().email);
                return true;
            }
        } else if (e->type() == QEvent::FocusOut) {
            commitPendingText();
        }
    }
    return QFrame::eventFilter(o, e);
}

bool RecipientField::isEmpty() const { return m_pills.isEmpty() && m_edit->text().trimmed().isEmpty(); }

void RecipientField::commitPendingText()
{
    const QString txt = m_edit->text().trimmed();
    if (txt.isEmpty()) return;
    for (const auto& a : util::parseAddressList(txt)) addAddress(a);
    m_edit->clear();
}

QList<EmailAddress> RecipientField::addresses() const
{
    QList<EmailAddress> out;
    for (auto* p : m_pills) out << p->address();
    return out;
}

void RecipientField::setAddresses(const QList<EmailAddress>& list)
{
    for (auto* p : m_pills) { m_flow->removeWidget(p); p->deleteLater(); }
    m_pills.clear();
    for (const auto& a : list) addAddress(a);
}

void RecipientField::addAddress(const EmailAddress& a)
{
    if (!a.isValid()) return;
    for (auto* p : m_pills) if (p->address().email == a.email) return;
    auto* pill = new RecipientPill(a, this);
    connect(pill, &RecipientPill::removeRequested, this, [this](RecipientPill* p) { removeAddress(p->address().email); });
    m_pills << pill;
    // insert before the line edit (last item)
    m_flow->removeWidget(m_edit);
    m_flow->addWidget(pill);
    m_flow->addWidget(m_edit);
    pill->show();
    updateGeometry();
    Q_EMIT changed();
}

void RecipientField::removeAddress(const QString& email)
{
    for (int i = 0; i < m_pills.size(); ++i) {
        if (m_pills[i]->address().email == email) {
            auto* p = m_pills.takeAt(i);
            m_flow->removeWidget(p);
            p->deleteLater();
            updateGeometry();
            Q_EMIT changed();
            return;
        }
    }
}

void RecipientField::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat(QLatin1String(kMime))) { e->setDropAction(Qt::MoveAction); e->accept(); }
}

void RecipientField::dropEvent(QDropEvent* e)
{
    if (!e->mimeData()->hasFormat(QLatin1String(kMime))) return;
    const EmailAddress a = util::parseAddress(QString::fromUtf8(e->mimeData()->data(QLatin1String(kMime))));
    // If the pill came from this same field, keep it (no-op move).
    for (auto* p : m_pills) if (p->address().email == a.email) { e->setDropAction(Qt::IgnoreAction); e->accept(); return; }
    addAddress(a);
    e->setDropAction(Qt::MoveAction);
    e->accept();
}

} // namespace ponymail
