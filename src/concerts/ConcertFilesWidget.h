#pragma once

#include "data/Concert.h"
#include "data/ConcertModel.h"
#include "data/ConcertProxyModel.h"
#include "ui/small_widgets/AlphabeticalList.h"

#include <QLabel>
#include <QMenu>
#include <QModelIndex>
#include <QWidget>

namespace Ui {
class ConcertFilesWidget;
}

/**
 * @brief The ConcertFilesWidget class
 * This widget displays a list of concerts.
 * It's a singleton and gets constructed through the gui,
 * the instance can be retrieved through ConcertFilesWidget::instance
 */
class ConcertFilesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ConcertFilesWidget(QWidget *parent = nullptr);
    ~ConcertFilesWidget() override;

    static ConcertFilesWidget *instance();

    QVector<Concert *> selectedConcerts();
    void renewModel();

public slots:
    void restoreLastSelection();
    void setFilter(QVector<Filter *> filters, QString text);
    void concertSelectedEmitter();
    void setAlphaListData();

signals:
    void noConcertSelected();
    void concertSelected(Concert *);

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void itemActivated(QModelIndex index, QModelIndex previous);
    void showContextMenu(QPoint point);
    void markAsWatched();
    void markAsUnwatched();
    void loadStreamDetails();
    void markForSync();
    void unmarkForSync();
    void openFolder();
    void openNfo();
    void scrollToAlpha(QString alpha);
    void onLeftEdge(bool isEdge);
    void onViewUpdated();
    void playConcert(QModelIndex idx);

private:
    Ui::ConcertFilesWidget *ui;
    ConcertProxyModel *m_concertProxyModel;
    Concert *m_lastConcert;
    QModelIndex m_lastModelIndex;
    QMenu *m_contextMenu;
    AlphabeticalList *m_alphaList;
    bool m_mouseIsIn;

    static ConcertFilesWidget *m_instance;
};
