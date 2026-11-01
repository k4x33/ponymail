#pragma once
#include "core/Util.h"
#include <QFrame>
#include <QList>

class QLineEdit;
class QCompleter;
class QStandardItemModel;
class QLayout;

namespace ponymail {

class Database;
class FlowLayout;

// A pill (chip) representing one recipient. Draggable between To / Cc / Bcc.
class RecipientPill : public QFrame {
    Q_OBJECT
public:
    RecipientPill(const EmailAddress& addr, QWidget* parent = nullptr);
    EmailAddress address() const { return m_addr; }
Q_SIGNALS:
    void removeRequested(RecipientPill* pill);
    void dragStarted(RecipientPill* pill);
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    EmailAddress m_addr;
    QPoint m_pressPos;
};

// To/Cc/Bcc field: pills + inline editor with contact autocomplete from SQLite.
class RecipientField : public QFrame {
    Q_OBJECT
public:
    RecipientField(const QString& label, Database* db, QWidget* parent = nullptr);
    QList<EmailAddress> addresses() const;
    void setAddresses(const QList<EmailAddress>& list);
    void addAddress(const EmailAddress& a);
    void removeAddress(const QString& email);
    void commitPendingText();
    QLineEdit* editor() const { return m_edit; }
    bool isEmpty() const;

Q_SIGNALS:
    void changed();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void rebuildCompleter(const QString& prefix);
    Database* m_db;
    QLineEdit* m_edit;
    QCompleter* m_completer;
    QStandardItemModel* m_model;
    FlowLayout* m_flow;
    QList<RecipientPill*> m_pills;
};

} // namespace ponymail
