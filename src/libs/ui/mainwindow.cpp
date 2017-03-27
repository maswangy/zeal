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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"
#include "docsetsdialog.h"
#include "settingsdialog.h"
#include "qxtglobalshortcut/qxtglobalshortcut.h"
#include "widgets/tab.h"

#include <core/application.h>
#include <core/settings.h>
#include <registry/docsetregistry.h>
#include <registry/itemdatarole.h>
#include <registry/listmodel.h>
#include <registry/searchmodel.h>

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QWebSettings>

using namespace Zeal;
using namespace Zeal::WidgetUi;

namespace {
const char DarkModeCssUrl[] = ":/browser/darkmode.css";
const char HighlightOnNavigateCssUrl[] = ":/browser/highlight.css";;
}

MainWindow::MainWindow(Core::Application *app, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_application(app),
    m_settings(app->settings()),
    m_globalShortcut(new QxtGlobalShortcut(m_settings->showShortcut, this))
{
    ui->setupUi(this);

    // initialise key grabber
    connect(m_globalShortcut, &QxtGlobalShortcut::activated, this, &MainWindow::toggleWindow);

    setupTabBar();

    QShortcut *focusSearch = new QShortcut(QStringLiteral("Ctrl+K"), this);
    connect(focusSearch, &QShortcut::activated, this, [this] {
        currentTab()->focusSearchEdit();
    });

    QShortcut *duplicate = new QShortcut(QStringLiteral("Ctrl+Alt+T"), this);
    connect(duplicate, &QShortcut::activated, this, [this] {
        duplicateTab(ui->tabWidget->currentIndex());
    });

    restoreGeometry(m_settings->windowGeometry);
    //XXX: ui->splitter->restoreState(m_settings->verticalSplitterGeometry);

    // Menu
    // File
    // Some platform plugins do not define QKeySequence::Quit.
    if (QKeySequence(QKeySequence::Quit).isEmpty())
        ui->actionQuit->setShortcut(QStringLiteral("Ctrl+Q"));
    else
        ui->actionQuit->setShortcut(QKeySequence::Quit);

    connect(ui->actionQuit, &QAction::triggered, qApp, &QCoreApplication::quit);

    // Edit
    ui->actionFind->setShortcut(QKeySequence::Find);
    connect(ui->actionFind, &QAction::triggered, this, [this] {
        currentTab()->focusSearchEdit();
    });

    connect(ui->actionPreferences, &QAction::triggered, [this]() {
        m_globalShortcut->setEnabled(false);
        QScopedPointer<SettingsDialog> dialog(new SettingsDialog(m_application, this));
        dialog->exec();
        m_globalShortcut->setEnabled(true);
    });

    // Tools Menu
    connect(ui->actionDocsets, &QAction::triggered, [this]() {
        QScopedPointer<DocsetsDialog> dialog(new DocsetsDialog(m_application, this));
        dialog->exec();
    });

    // Help Menu
    connect(ui->actionSubmitFeedback, &QAction::triggered, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/zealdocs/zeal/issues")));
    });
    connect(ui->actionCheckForUpdates, &QAction::triggered,
            m_application, &Core::Application::checkForUpdates);
    connect(ui->actionAboutZeal, &QAction::triggered, [this]() {
        QScopedPointer<AboutDialog> dialog(new AboutDialog(this));
        dialog->exec();
    });
    connect(ui->actionAboutQt, &QAction::triggered, [this]() {
        QMessageBox::aboutQt(this);
    });

    // Update check
    connect(m_application, &Core::Application::updateCheckError, [this](const QString &message) {
        QMessageBox::warning(this, QStringLiteral("Zeal"), message);
    });

    connect(m_application, &Core::Application::updateCheckDone, [this](const QString &version) {
        if (version.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Zeal"),
                                     tr("You are using the latest version."));
            return;
        }

        const int ret
                = QMessageBox::information(this, QStringLiteral("Zeal"),
                                           tr("Zeal <b>%1</b> is available. Open download page?").arg(version),
                                           QMessageBox::Yes | QMessageBox::Default,
                                           QMessageBox::No | QMessageBox::Escape,
                                           QMessageBox::NoButton);
        if (ret == QMessageBox::Yes)
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://zealdocs.org/download.html")));
    });

    createTab();

    ui->actionNewTab->setShortcut(QKeySequence::AddTab);
    connect(ui->actionNewTab, &QAction::triggered, this, [this]() { createTab(); });
    addAction(ui->actionNewTab);

    // Save expanded items
    /*connect(ui->treeView, &QTreeView::expanded, [this](QModelIndex index) {
        if (currentTabState()->expansions.indexOf(index) == -1)
            currentTabState()->expansions.append(index);
    });

    connect(ui->treeView, &QTreeView::collapsed, [this](QModelIndex index) {
        currentTabState()->expansions.removeOne(index);
    });*/

#ifdef Q_OS_WIN32
    ui->actionCloseTab->setShortcut(QKeySequence(Qt::Key_W + Qt::CTRL));
#else
    ui->actionCloseTab->setShortcut(QKeySequence::Close);
#endif
    addAction(ui->actionCloseTab);
    connect(ui->actionCloseTab, &QAction::triggered, this, [this]() { closeTab(); });

    ui->actionNextTab->setShortcuts({QKeySequence::NextChild,
                                     QKeySequence(Qt::ControlModifier| Qt::Key_PageDown)});
    addAction(ui->actionNextTab);
    connect(ui->actionNextTab, &QAction::triggered, [this]() {
        const int nextIndex = (ui->tabWidget->currentIndex() + 1) % ui->tabWidget->count();
        ui->tabWidget->setCurrentIndex(nextIndex);
    });

    // TODO: Use QKeySequence::PreviousChild, when QTBUG-15746 is fixed.
    ui->actionPreviousTab->setShortcuts({QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Tab),
                                         QKeySequence(Qt::ControlModifier| Qt::Key_PageUp)});
    addAction(ui->actionPreviousTab);
    connect(ui->actionPreviousTab, &QAction::triggered, [this]() {
        const int previousIndex = (ui->tabWidget->currentIndex() - 1 + ui->tabWidget->count())
                % ui->tabWidget->count();
        ui->tabWidget->setCurrentIndex(previousIndex);
    });

    connect(m_settings, &Core::Settings::updated, this, &MainWindow::applySettings);
    applySettings();

    if (m_settings->checkForUpdate)
        m_application->checkForUpdates(true);
}

MainWindow::~MainWindow()
{
    //XXX: m_settings->verticalSplitterGeometry = ui->splitter->saveState();
    m_settings->windowGeometry = saveGeometry();
    delete ui;
}

void MainWindow::search(const Registry::SearchQuery &query)
{
    if (query.isEmpty())
        return;

    currentTab()->search(query);
}

void MainWindow::closeTab(int index)
{
    if (index == -1)
        index = ui->tabWidget->currentIndex();

    if (index == -1)
        return;

    delete ui->tabWidget->widget(index);

    ui->tabWidget->removeTab(index);

    if (ui->tabWidget->count() == 0)
        createTab();
}

void MainWindow::createTab(int index)
{
    if (m_settings->openNewTabAfterActive)
        index = ui->tabWidget->currentIndex() + 1;
    else if (index == -1)
        index = ui->tabWidget->count();

    Tab *tab = new Tab(ui->tabWidget);
    ui->tabWidget->insertTab(index, tab, tr("Loading..."));
    ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::duplicateTab(int index)
{
    if (index < 0 || index >= ui->tabWidget->count())
        return;

    Tab *tab = new Tab(ui->tabWidget);

    ++index;
    ui->tabWidget->insertTab(index, tab, tr("Loading..."));
    ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::setupTabBar()
{
    QTabBar *tabBar = ui->tabWidget->tabBar();

    tabBar->installEventFilter(this);

    tabBar->setTabsClosable(true);
    tabBar->setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    tabBar->setExpanding(false);
    tabBar->setUsesScrollButtons(true);
    tabBar->setDrawBase(false);
    tabBar->setDocumentMode(true);
    tabBar->setElideMode(Qt::ElideRight);
    tabBar->setStyleSheet(QStringLiteral("QTabBar::tab { width: 150px; }"));

    /*connect(tabBar, &QTabBar::currentChanged, this, [this](int index) {
        static const char PreviousTabIndexProperty[] = "previousTabIndex";

        if (index == -1)
            return;

        // Save previous tab state
        const QVariant previousTabIndex = tabBar->property(PreviousTabIndexProperty);
        if (previousTabIndex.isValid() && previousTabIndex.toInt() < m_tabStates.size()) {
            TabState *previousTabState = m_tabStates.at(previousTabIndex.toInt());
            previousTabState->selections = ui->treeView->selectionModel()->selectedIndexes();
            previousTabState->searchScrollPosition = ui->treeView->verticalScrollBar()->value();
            previousTabState->tocScrollPosition = ui->tocListView->verticalScrollBar()->value();
            previousTabState->webViewZoomFactor = ui->webView->zoomFactor();
        }

        // Load current tab state
        tabBar->setProperty(PreviousTabIndexProperty, index);
        TabState *tabState = m_tabStates.at(index);

        ui->lineEdit->setText(tabState->searchQuery);
        ui->tocListView->setModel(tabState->tocModel);

        syncTreeView();
        syncToc();

        // Bring back the selections and expansions
        ui->treeView->blockSignals(true);
        for (const QModelIndex &selection: tabState->selections)
            ui->treeView->selectionModel()->select(selection, QItemSelectionModel::Select);
        for (const QModelIndex &expandedIndex: tabState->expansions)
            ui->treeView->expand(expandedIndex);
        ui->treeView->blockSignals(false);

        ui->webView->setPage(tabState->webPage);
        ui->webView->setZoomFactor(tabState->webViewZoomFactor);

        ui->actionBack->setEnabled(ui->webView->canGoBack());
        ui->actionForward->setEnabled(ui->webView->canGoForward());

        ui->treeView->verticalScrollBar()->setValue(tabState->searchScrollPosition);
        ui->tocListView->verticalScrollBar()->setValue(tabState->tocScrollPosition);
    });*/

    connect(tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);

    for (int i = 1; i < 10; ++i) {
        QAction *action = new QAction(tabBar);
#ifdef Q_OS_LINUX
        action->setShortcut(QStringLiteral("Alt+%1").arg(i));
#else
        action->setShortcut(QStringLiteral("Ctrl+%1").arg(i));
#endif
        if (i == 9) {
            connect(action, &QAction::triggered, [=]() {
                tabBar->setCurrentIndex(tabBar->count() - 1);
            });
        } else {
            connect(action, &QAction::triggered, [=]() {
                tabBar->setCurrentIndex(i - 1);
            });
        }

        addAction(action);
    }
}

WidgetUi::Tab *MainWindow::currentTab() const
{
    return qobject_cast<WidgetUi::Tab *>(ui->tabWidget->currentWidget());
}

void MainWindow::createTrayIcon()
{
    if (m_trayIcon)
        return;

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(windowIcon());
    m_trayIcon->setToolTip(QStringLiteral("Zeal"));

    connect(m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
            return;

        toggleWindow();
    });

    QMenu *trayIconMenu = new QMenu(this);
    trayIconMenu->addAction(ui->actionQuit);

    m_trayIcon->setContextMenu(trayIconMenu);

    m_trayIcon->show();
}

void MainWindow::removeTrayIcon()
{
    if (!m_trayIcon)
        return;

    QMenu *trayIconMenu = m_trayIcon->contextMenu();
    delete m_trayIcon;
    m_trayIcon = nullptr;
    delete trayIconMenu;
}

void MainWindow::bringToFront()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();

    currentTab()->focusSearchEdit();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (m_settings->showSystrayIcon && m_settings->minimizeToSystray
            && event->type() == QEvent::WindowStateChange && isMinimized()) {
        hide();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_settings->showSystrayIcon && m_settings->hideOnClose) {
        event->ignore();
        toggleWindow();
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->tabWidget->tabBar()) {
        switch (event->type()) {
        case QEvent::MouseButtonRelease: {
            QMouseEvent *e = reinterpret_cast<QMouseEvent *>(event);
            if (e->button() == Qt::MiddleButton) {
                const int index = ui->tabWidget->tabBar()->tabAt(e->pos());
                if (index != -1) {
                    closeTab(index);
                    return true;
                }
            }
            break;
        }
        case QEvent::Wheel:
            // TODO: Remove in case QTBUG-8428 is fixed on all platforms
            return true;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(object, event);
}

void MainWindow::applySettings()
{
    m_globalShortcut->setShortcut(m_settings->showShortcut);

    if (m_settings->showSystrayIcon)
        createTrayIcon();
    else
        removeTrayIcon();

    // Content
    QByteArray ba;
    if (m_settings->darkModeEnabled) {
        QScopedPointer<QFile> file(new QFile(DarkModeCssUrl));
        if (file->open(QIODevice::ReadOnly)) {
            ba += file->readAll();
        }
    }

    if (m_settings->highlightOnNavigateEnabled) {
        QScopedPointer<QFile> file(new QFile(HighlightOnNavigateCssUrl));
        if (file->open(QIODevice::ReadOnly)) {
            ba += file->readAll();
        }
    }

    if (QFileInfo(m_settings->customCssFile).exists()) {
        QScopedPointer<QFile> file(new QFile(m_settings->customCssFile));
        if (file->open(QIODevice::ReadOnly)) {
            ba += file->readAll();
        }
    }

    const QString cssUrl = QLatin1String("data:text/css;charset=utf-8;base64,") + ba.toBase64();
    QWebSettings::globalSettings()->setUserStyleSheetUrl(QUrl(cssUrl));
}

void MainWindow::toggleWindow()
{
    const bool checkActive = sender() == m_globalShortcut;

    if (!isVisible() || (checkActive && !isActiveWindow())) {
        bringToFront();
    } else {
        if (m_trayIcon) {
            hide();
        } else {
            showMinimized();
        }
    }
}
