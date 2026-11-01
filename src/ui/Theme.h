#pragma once
#include <QColor>
#include <QString>

namespace ponymail {

// Outlook.com-style palette. Four themes: light, dark, testudo, testudo-dark.
struct Theme {
    QString key;
    bool dark = false;
    QColor window, surface, surfaceAlt, border, text, textMuted, textFaint;
    QColor accent, accentHover, accentSoft, selection, hover, unreadBar;
    QColor danger, dangerSoft, success, warning;
    QColor railBg, railText, railSelected;
    QString emblemName;   // non-empty for testudo themes

    static Theme named(const QString& key);
    static Theme light();
    static Theme darkTheme();
    static Theme testudo();
    static Theme testudoDark();

    QString stylesheet() const;        // application-wide QSS
    QString webCss() const;            // CSS variables injected into reading pane / composer
    QString css(const QColor& c) const { return c.name(QColor::HexRgb); }
};

} // namespace ponymail
