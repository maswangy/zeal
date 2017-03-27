#ifndef ZEAL_WIDGETUI_TAB_H
#define ZEAL_WIDGETUI_TAB_H

#include <QWidget>

class QAbstractItemModel;
class QMenu;
class QTimer;

namespace Zeal {

namespace Registry {
class DocsetRegistry;
class SearchModel;
class SearchQuery;
}

namespace WidgetUi {

namespace Ui {
class Tab;
}

class Tab : public QWidget
{
    Q_OBJECT

public:
    explicit Tab(QWidget *parent = nullptr);
    ~Tab() override;

    void search(const Registry::SearchQuery &query);

    QIcon icon() const;
    QString title() const;

public slots:
    void focusSearchEdit();
    void showSearchBar();

signals:
    void iconChanged(const QIcon &icon);
    void titleChanged(const QString &title);

protected:
    void keyPressEvent(QKeyEvent *keyEvent) override;

private slots:
    void openUrlFromModelIndex(const QModelIndex &index);
    void syncTreeView();
    void syncTocView();

private:
    QString docsetName(const QUrl &url) const;
    QIcon docsetIcon(const QString &docsetName) const;

    void setupSearchBoxCompletions();

    Ui::Tab *ui = nullptr;
    QMenu *m_backMenu = nullptr;
    QMenu *m_forwardMenu = nullptr;

    QTimer *m_openDocsetTimer = nullptr;

    Registry::DocsetRegistry *m_docsetRegistry = nullptr;
    QAbstractItemModel *m_docsetIndexModel = nullptr;
    Registry::SearchModel *m_searchModel = nullptr;
    Registry::SearchModel *m_tocModel = nullptr;
};

} // namespace WidgetUi
} // namespace Zeal

#endif // ZEAL_WIDGETUI_TAB_H
