#include "tab.h"
#include "ui_tab.h"

#include "searchitemdelegate.h"

#include <core/application.h>
#include <registry/docsetregistry.h>
#include <registry/itemdatarole.h>
#include <registry/listmodel.h>
#include <registry/searchmodel.h>
#include <registry/searchquery.h>

#include <QDesktopServices>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QWebFrame>
#include <QWebHistory>
#include <QWebPage>

using namespace Zeal;
using namespace Zeal::WidgetUi;

namespace {
const char startPageUrl[] = "qrc:///browser/start.html";
}

/*struct TabState
{
    TabState(const TabState &other)
        : searchQuery(other.searchQuery)
        , selections(other.selections)
        , expansions(other.expansions)
        , searchScrollPosition(other.searchScrollPosition)
        , tocScrollPosition(other.tocScrollPosition)
        , webViewZoomFactor(other.webViewZoomFactor)
    {
        searchModel = new Registry::SearchModel(*other.searchModel);
        tocModel = new Registry::SearchModel(*other.tocModel);

        restoreHistory(other.saveHistory());
    }

    void restoreHistory(const QByteArray &array) const
    {
        QDataStream stream(array);
        stream >> *webPage->history();
    }

    QByteArray saveHistory() const
    {
        QByteArray array;
        QDataStream stream(&array, QIODevice::WriteOnly);
        stream << *webPage->history();
        return array;
    }

    QString searchQuery;

    // Content/Search results tree view state
    QModelIndexList selections;
    QModelIndexList expansions;
    int searchScrollPosition = 0;

    // TOC list view state
    int tocScrollPosition = 0;

    int webViewZoomFactor = 0;
};*/

Tab::Tab(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Tab)
    , m_openDocsetTimer(new QTimer(this))
    , m_docsetRegistry(Core::Application::instance()->docsetRegistry())
    , m_docsetIndexModel(new Registry::ListModel(m_docsetRegistry, this))
{
    ui->setupUi(this);

    // Docset Registry
    connect(m_docsetRegistry, &Registry::DocsetRegistry::searchCompleted,
            this, [this](const QList<Registry::SearchResult> &results) {
        m_searchModel->setResults(results);
    });

    connect(m_docsetRegistry, &Registry::DocsetRegistry::docsetRemoved,
            this, [this](const QString &name) {
        setupSearchBoxCompletions();

        if (docsetName(ui->webView->page()->mainFrame()->url()) != name)
            return;

        m_tocModel->setResults();

        // optimization: disable updates temporarily because
        // removeSearchResultWithName can call {begin,end}RemoveRows
        // multiple times which can cause GUI updates to be suboptimal
        // in case of many rows to be removed
        ui->treeView->setUpdatesEnabled(false);
        m_searchModel->removeSearchResultWithName(name);
        ui->treeView->setUpdatesEnabled(true);

        ui->webView->load(QUrl(startPageUrl));

        // TODO: Cleanup history
    });

    connect(m_docsetRegistry, &Registry::DocsetRegistry::docsetAdded,
            this, [this](const QString &) {
        setupSearchBoxCompletions();
    });

    // Navigation Toolbar
    m_backMenu = new QMenu(ui->backButton);
    connect(m_backMenu, &QMenu::aboutToShow, this, [this]() {
        m_backMenu->clear();
        QWebHistory *history = ui->webView->page()->history();
        QList<QWebHistoryItem> items = history->backItems(10);
        // TODO: [Qt 5.6]
        //for (auto it = items.crbegin(); it != items.crend(); ++it) {
        for (auto it = items.cend() - 1; it >= items.cbegin(); --it) {
            const QIcon icon = docsetIcon(docsetName(it->url()));
            const QWebHistoryItem item = *it;
            // TODO: [Qt 5.6]
            // m_backMenu->addAction(icon, it->title(), [=](bool) { history->goToItem(item); });
            QAction *action = m_backMenu->addAction(icon, it->title());
            connect(action, &QAction::triggered, [=](bool) { history->goToItem(item); });
        }
    });
    ui->backButton->setDefaultAction(ui->actionBack);
    ui->backButton->setMenu(m_backMenu);

    m_forwardMenu = new QMenu(ui->forwardButton);
    connect(m_forwardMenu, &QMenu::aboutToShow, this, [this]() {
        m_forwardMenu->clear();
        QWebHistory *history = ui->webView->page()->history();
        for (const QWebHistoryItem &item: history->forwardItems(10)) {
            const QIcon icon = docsetIcon(docsetName(item.url()));
            // TODO: [Qt 5.6]
            //m_forwardMenu->addAction(icon, item.title(), [=](bool) { history->goToItem(item); });
            QAction *action = m_forwardMenu->addAction(icon, item.title());
            connect(action, &QAction::triggered, [=](bool) { history->goToItem(item); });
        }
    });
    ui->forwardButton->setDefaultAction(ui->actionForward);
    ui->forwardButton->setMenu(m_forwardMenu);

    ui->actionBack->setShortcut(QKeySequence::Back);
    addAction(ui->actionBack);
    ui->actionForward->setShortcut(QKeySequence::Forward);
    addAction(ui->actionForward);
    connect(ui->actionBack, &QAction::triggered, ui->webView, &SearchableWebView::back);
    connect(ui->actionForward, &QAction::triggered, ui->webView, &SearchableWebView::forward);

    connect(ui->openUrlButton, &QPushButton::clicked, [this]() {
        const QUrl url(ui->webView->page()->history()->currentItem().url());
        if (url.scheme() != QLatin1String("qrc"))
            QDesktopServices::openUrl(url);
    });

    // Index/Search View

    SearchItemDelegate *delegate = new SearchItemDelegate(ui->treeView);
    delegate->setDecorationRoles({Registry::ItemDataRole::DocsetIconRole, Qt::DecorationRole});
    connect(ui->lineEdit, &QLineEdit::textChanged, [delegate](const QString &text) {
        delegate->setHighlight(Registry::SearchQuery::fromString(text).query());
    });
    ui->treeView->setItemDelegate(delegate);

    connect(ui->treeView, &QTreeView::clicked, this, &Tab::openUrlFromModelIndex);
    connect(ui->treeView, &QTreeView::activated, this, &Tab::openUrlFromModelIndex);

    m_searchModel = new Registry::SearchModel(this);
    connect(m_searchModel, &Registry::SearchModel::updated, this, [this] {
        m_openDocsetTimer->stop();

        syncTreeView();

        ui->treeView->setCurrentIndex(m_searchModel->index(0, 0, QModelIndex()));

        m_openDocsetTimer->setProperty("index", ui->treeView->currentIndex());
        m_openDocsetTimer->start();
    });

    // TOC View
    m_tocModel = new Registry::SearchModel(this);
    connect(m_tocModel, &Registry::SearchModel::updated, this, &Tab::syncTocView);

    ui->tocListView->setModel(m_tocModel);
    ui->tocListView->setItemDelegate(new SearchItemDelegate(ui->tocListView));
    connect(ui->tocListView, &QListView::clicked, this, &Tab::openUrlFromModelIndex);
    connect(ui->tocListView, &QListView::activated, this, &Tab::openUrlFromModelIndex);

    /*XXX: connect(ui->tocSplitter, &QSplitter::splitterMoved, this, [this]() {
        m_settings->tocSplitterState = ui->tocSplitter->saveState();
    });*/

#ifdef Q_OS_OSX
    ui->treeView->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->tocListView->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif

    // Search Line Edit
    ui->lineEdit->setTreeView(ui->treeView);
    ui->lineEdit->setFocus();
    setupSearchBoxCompletions();
    connect(ui->lineEdit, &QLineEdit::textChanged, [this](const QString &text) {
        /*if (text == currentTabState()->searchQuery)
            return;

        currentTabState()->searchQuery = text;*/
        m_docsetRegistry->search(text);
        if (text.isEmpty()) {
            m_tocModel->setResults();
            syncTreeView();
        }
    });

    // WebView
    ui->webView->page()->setLinkDelegationPolicy(QWebPage::DelegateExternalLinks);
    ui->webView->page()->setNetworkAccessManager(Core::Application::instance()->networkManager());

    connect(ui->webView, &SearchableWebView::linkClicked, [this](const QUrl &url) {
        const QString message = tr("Do you want to open an external link?<br>URL: <b>%1</b>");
        int ret = QMessageBox::question(this, QStringLiteral("Zeal"), message.arg(url.toString()));
        if (ret == QMessageBox::Yes)
            QDesktopServices::openUrl(url);
    });

    connect(ui->webView, &SearchableWebView::titleChanged, [this](const QString &title) {
        if (title.isEmpty())
            return;

        emit titleChanged(title);

        /*setWindowTitle(QStringLiteral("%1 - Zeal").arg(title));
        ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), title);
        ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), title);*/
    });

    connect(ui->webView, &SearchableWebView::urlChanged, [this](const QUrl &url) {
        const QString name = docsetName(url);
        // FIXME: Emit only on real change
        emit iconChanged(docsetIcon(name));

        //ui->tabWidget->setTabIcon(ui->tabWidget->currentIndex(), docsetIcon(name));

        Registry::Docset *docset = m_docsetRegistry->docset(name);
        if (docset)
            m_tocModel->setResults(docset->relatedLinks(url));

        ui->actionBack->setEnabled(ui->webView->canGoBack());
        ui->actionForward->setEnabled(ui->webView->canGoForward());
    });

    ui->webView->load(QUrl(startPageUrl));

    // Page load timer.
    // Setup delayed navigation to a page until user makes a pause in typing a search query.
    m_openDocsetTimer->setInterval(400);
    m_openDocsetTimer->setSingleShot(true);
    connect(m_openDocsetTimer, &QTimer::timeout, this, [this]() {
        QModelIndex index = m_openDocsetTimer->property("index").toModelIndex();
        if (!index.isValid())
            return;

        openUrlFromModelIndex(index);

        // Get focus back.
        ui->lineEdit->setFocus(Qt::MouseFocusReason);
    });

    syncTreeView();
    syncTocView();
}

Tab::~Tab()
{
    delete ui;
}

void Tab::search(const Registry::SearchQuery &query)
{
    ui->lineEdit->setText(query.toString());
    //ui->treeView->activated(ui->treeView->currentIndex());
}

void Tab::focusSearchEdit()
{
    ui->lineEdit->setFocus();
}

void Tab::showSearchBar()
{
    ui->webView->showSearchBar();
}

void Tab::keyPressEvent(QKeyEvent *keyEvent)
{
    switch (keyEvent->key()) {
    case Qt::Key_Escape:
        ui->lineEdit->setFocus();
        ui->lineEdit->clearQuery();
        break;
    case Qt::Key_Question:
        ui->lineEdit->setFocus();
        ui->lineEdit->selectQuery();
        break;
    default:
        QWidget::keyPressEvent(keyEvent);
        break;
    }
}

void Tab::openUrlFromModelIndex(const QModelIndex &index)
{
    const QVariant url = index.data(Registry::ItemDataRole::UrlRole);
    if (url.isNull())
        return;

    ui->webView->load(url.toUrl());
    ui->webView->focus();
}

void Tab::syncTreeView()
{
    const bool showIndex = ui->lineEdit->text().isEmpty();
    ui->treeView->setModel(showIndex ? m_docsetIndexModel : m_searchModel);
    ui->treeView->setColumnHidden(1, showIndex);
    ui->treeView->setRootIsDecorated(showIndex);
    ui->treeView->reset();
}

void Tab::syncTocView()
{
    const bool isVisible = !m_tocModel->isEmpty();
    ui->tocListView->setVisible(isVisible);

    //XXX: if (isVisible)
    //ui->tocSplitter->restoreState(m_settings->tocSplitterState);
}

QString Tab::docsetName(const QUrl &url) const
{
    const QRegExp docsetRegex(QStringLiteral("/([^/]+)[.]docset"));
    return docsetRegex.indexIn(url.path()) != -1 ? docsetRegex.cap(1) : QString();
}

QIcon Tab::docsetIcon(const QString &docsetName) const
{
    Registry::Docset *docset = m_docsetRegistry->docset(docsetName);
    return docset ? docset->icon() : QIcon(QStringLiteral(":/icons/logo/icon.png"));
}

void Tab::setupSearchBoxCompletions()
{
    QStringList completions;
    for (const Registry::Docset * const docset: m_docsetRegistry->docsets()) {
        if (docset->keywords().isEmpty())
            continue;

        completions << docset->keywords().first() + QLatin1Char(':');
    }

    ui->lineEdit->setCompletions(completions);
}
