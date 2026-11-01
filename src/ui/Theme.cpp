#include "Theme.h"

namespace ponymail {

Theme Theme::named(const QString& key)
{
    if (key == QLatin1String("dark")) return darkTheme();
    if (key == QLatin1String("testudo")) return testudo();
    if (key == QLatin1String("testudo-dark")) return testudoDark();
    return light();
}

Theme Theme::light()
{
    Theme t;
    t.key = QStringLiteral("light");
    t.window = QColor("#F5F5F5");
    t.surface = QColor("#FFFFFF");
    t.surfaceAlt = QColor("#FAFAFA");
    t.border = QColor("#E1DFDD");
    t.text = QColor("#242424");
    t.textMuted = QColor("#616161");
    t.textFaint = QColor("#8A8886");
    t.accent = QColor("#0F6CBD");
    t.accentHover = QColor("#115EA3");
    t.accentSoft = QColor("#EBF3FC");
    t.selection = QColor("#CFE4FA");   // light-blue multi-select tint
    t.hover = QColor("#F3F2F1");
    t.unreadBar = QColor("#0F6CBD");
    t.danger = QColor("#C4314B");
    t.dangerSoft = QColor("#FDE7E9");
    t.success = QColor("#107C10");
    t.warning = QColor("#F7630C");
    t.railBg = QColor("#FFFFFF");
    t.railText = QColor("#242424");
    t.railSelected = QColor("#EBF3FC");
    return t;
}

Theme Theme::darkTheme()
{
    Theme t = light();
    t.key = QStringLiteral("dark");
    t.dark = true;
    t.window = QColor("#1B1A19");
    t.surface = QColor("#292827");
    t.surfaceAlt = QColor("#252423");
    t.border = QColor("#3B3A39");
    t.text = QColor("#F3F2F1");
    t.textMuted = QColor("#C8C6C4");
    t.textFaint = QColor("#979593");
    t.accent = QColor("#479EF5");
    t.accentHover = QColor("#62ABF5");
    t.accentSoft = QColor("#1F2F40");
    t.selection = QColor("#1F3A55");
    t.hover = QColor("#323130");
    t.unreadBar = QColor("#479EF5");
    t.dangerSoft = QColor("#442726");
    t.railBg = QColor("#201F1E");
    t.railText = QColor("#F3F2F1");
    t.railSelected = QColor("#1F2F40");
    return t;
}

Theme Theme::testudo()
{
    Theme t = light();
    t.key = QStringLiteral("testudo");
    t.accent = QColor("#E21833");        // Maryland red
    t.accentHover = QColor("#B5122A");
    t.accentSoft = QColor("#FCE9EC");
    t.selection = QColor("#F9D3DA");
    t.unreadBar = QColor("#E21833");
    t.railSelected = QColor("#FCE9EC");
    t.warning = QColor("#FFD200");       // Maryland gold
    t.emblemName = QStringLiteral("testudo");
    return t;
}

Theme Theme::testudoDark()
{
    Theme t = darkTheme();
    t.key = QStringLiteral("testudo-dark");
    t.accent = QColor("#FF4D5E");
    t.accentHover = QColor("#FF7582");
    t.accentSoft = QColor("#3A1D22");
    t.selection = QColor("#4A2229");
    t.unreadBar = QColor("#FF4D5E");
    t.railSelected = QColor("#3A1D22");
    t.warning = QColor("#FFD200");
    t.emblemName = QStringLiteral("testudo");
    return t;
}

QString Theme::stylesheet() const
{
    return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: %window%; color: %text%; }
QWidget { color: %text%; font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; font-size: 13px; }
QToolTip { background: %surface%; color: %text%; border: 1px solid %border%; padding: 4px 6px; }
QSplitter::handle { background: %border%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:hover { background: %accent%; }

QToolBar { background: %surface%; border: none; border-bottom: 1px solid %border%; padding: 4px 8px; spacing: 4px; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 5px 9px; color: %text%; }
QToolBar QToolButton:hover { background: %hover%; border-color: %border%; }
QToolBar QToolButton:pressed, QToolBar QToolButton:checked { background: %accentSoft%; border-color: %accent%; }
QToolBar QToolButton:disabled { color: %textFaint%; }
QToolBar::separator { width: 1px; background: %border%; margin: 4px 6px; }

QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDateTimeEdit {
  background: %surface%; border: 1px solid %border%; border-radius: 4px; padding: 5px 8px; selection-background-color: %accent%; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus { border: 1px solid %accent%; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: %surface%; border: 1px solid %border%; selection-background-color: %accentSoft%; selection-color: %text%; outline: 0; }

QPushButton { background: %surface%; border: 1px solid %border%; border-radius: 4px; padding: 6px 14px; }
QPushButton:hover { background: %hover%; }
QPushButton:pressed { background: %selection%; }
QPushButton[primary="true"] { background: %accent%; color: white; border-color: %accent%; font-weight: 600; }
QPushButton[primary="true"]:hover { background: %accentHover%; }
QPushButton[danger="true"] { color: %danger%; }
QPushButton:disabled { color: %textFaint%; }

QTreeWidget#navRail { background: %railBg%; border: none; border-right: 1px solid %border%; outline: 0; padding: 6px 0; font-size: 13px; }
QTreeWidget#navRail::item { height: 30px; padding-left: 6px; border-radius: 4px; margin: 1px 6px; color: %railText%; }
QTreeWidget#navRail::item:hover { background: %hover%; }
QTreeWidget#navRail::item:selected { background: %railSelected%; color: %text%; border-left: 3px solid %accent%; }
QTreeWidget#navRail::branch { background: transparent; image: none; }

QListView#messageList { background: %surface%; border: none; outline: 0; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %border%; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %textFaint%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: %border%; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QMenu { background: %surface%; border: 1px solid %border%; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 3px; }
QMenu::item:selected { background: %accentSoft%; }
QMenu::separator { height: 1px; background: %border%; margin: 4px 8px; }

QStatusBar { background: %surface%; border-top: 1px solid %border%; color: %textMuted%; font-size: 12px; }
QLabel[muted="true"] { color: %textMuted%; }
QLabel[heading="true"] { font-size: 18px; font-weight: 600; }
QFrame[card="true"] { background: %surface%; border: 1px solid %border%; border-radius: 6px; }

QToolButton#unreadToggle:checked { background: %accentSoft%; border: 1px solid %accent%; }
QToolButton#chip { background: %surfaceAlt%; border: 1px solid %border%; border-radius: 10px; padding: 2px 9px; font-size: 11px; color: %textMuted%; }
QToolButton#chip:hover { border-color: %accent%; color: %accent%; }

QListWidget#attachList { background: %surfaceAlt%; border: 1px solid %border%; border-radius: 4px; }
QTableWidget { background: %surface%; border: 1px solid %border%; gridline-color: %border%; }
QHeaderView::section { background: %surfaceAlt%; border: none; border-bottom: 1px solid %border%; padding: 6px; font-weight: 600; }
)QSS")
        .replace(QStringLiteral("%window%"), css(window))
        .replace(QStringLiteral("%surfaceAlt%"), css(surfaceAlt))
        .replace(QStringLiteral("%surface%"), css(surface))
        .replace(QStringLiteral("%border%"), css(border))
        .replace(QStringLiteral("%textMuted%"), css(textMuted))
        .replace(QStringLiteral("%textFaint%"), css(textFaint))
        .replace(QStringLiteral("%text%"), css(text))
        .replace(QStringLiteral("%accentHover%"), css(accentHover))
        .replace(QStringLiteral("%accentSoft%"), css(accentSoft))
        .replace(QStringLiteral("%accent%"), css(accent))
        .replace(QStringLiteral("%selection%"), css(selection))
        .replace(QStringLiteral("%hover%"), css(hover))
        .replace(QStringLiteral("%danger%"), css(danger))
        .replace(QStringLiteral("%railBg%"), css(railBg))
        .replace(QStringLiteral("%railText%"), css(railText))
        .replace(QStringLiteral("%railSelected%"), css(railSelected));
}

QString Theme::webCss() const
{
    return QStringLiteral(
        ":root{--bg:%1;--surface:%2;--surface-alt:%3;--border:%4;--text:%5;--muted:%6;--faint:%7;"
        "--accent:%8;--accent-soft:%9;--danger:%10;--danger-soft:%11;--success:%12;--warning:%13;--selection:%14;}")
        .arg(css(window), css(surface), css(surfaceAlt), css(border), css(text), css(textMuted), css(textFaint),
             css(accent), css(accentSoft))
        .arg(css(danger), css(dangerSoft), css(success), css(warning), css(selection));
}

} // namespace ponymail
