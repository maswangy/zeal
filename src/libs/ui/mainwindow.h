/****************************************************************************
**
** Copyright (C) 2015-2016 Oleg Shparber
** Copyright (C) 2013-2014 Jerzy Kozera
** Contact: https://go.zealdocs.org/l/contact
**
** This file is part of Zeal.
**
** Zeal is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** Zeal is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Zeal. If not, see <https://www.gnu.org/licenses/>.
**
****************************************************************************/

#ifndef ZEAL_WIDGETUI_MAINWINDOW_H
#define ZEAL_WIDGETUI_MAINWINDOW_H

#include <registry/searchquery.h>

#include <QMainWindow>

class QxtGlobalShortcut;

class QModelIndex;
class QSystemTrayIcon;

namespace Zeal {

namespace Core {
class Application;
class Settings;
} // namespace Core

namespace Registry {
class ListModel;
} //namespace Registry

namespace WidgetUi {

namespace Ui {
class MainWindow;
} // namespace Ui

class Tab;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Zeal::Core::Application *app, QWidget *parent = nullptr);
    ~MainWindow() override;

    void search(const Zeal::Registry::SearchQuery &query);
    void bringToFront();
    void createTab(int index = -1);

public slots:
    void toggleWindow();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private slots:
    void applySettings();
    void closeTab(int index = -1);
    void duplicateTab(int index);

private:
    void setupTabBar();

    Zeal::WidgetUi::Tab *currentTab() const;

    void createTrayIcon();
    void removeTrayIcon();

    Ui::MainWindow *ui = nullptr;
    Zeal::Core::Application *m_application = nullptr;
    Zeal::Core::Settings *m_settings = nullptr;

    QxtGlobalShortcut *m_globalShortcut = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
};

} // namespace WidgetUi
} // namespace Zeal

#endif // ZEAL_WIDGETUI_MAINWINDOW_H
