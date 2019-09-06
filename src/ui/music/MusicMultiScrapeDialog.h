#pragma once

#include "globals/Globals.h"
#include "globals/ScraperInfos.h"

#include <QDialog>
#include <QQueue>
#include <QVector>

class Album;
class Artist;
class MusicScraperInterface;

namespace Ui {
class MusicMultiScrapeDialog;
}

class MusicMultiScrapeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MusicMultiScrapeDialog(QWidget* parent = nullptr);
    ~MusicMultiScrapeDialog() override;
    static MusicMultiScrapeDialog* instance(QWidget* parent = nullptr);
    void setItems(QVector<Artist*> artists, QVector<Album*> albums);

public slots:
    int exec() override;
    void reject() override;
    void accept() override;

private slots:
    void onChkToggled();
    void onChkAllToggled(bool toggled);
    void onStartScraping();
    void onScrapingFinished();
    void onSearchFinished(QVector<ScraperSearchResult> results);
    void scrapeNext();
    void onProgress(Artist* artist, int current, int maximum);
    void onProgress(Album* album, int current, int maximum);

private:
    Ui::MusicMultiScrapeDialog* ui;

    struct QueueItem
    {
        Artist* artist;
        Album* album;
    };

    void disconnectScrapers();
    bool isExecuted();

    QQueue<QueueItem> m_queue;
    bool m_executed;
    Artist* m_currentArtist = nullptr;
    Album* m_currentAlbum = nullptr;
    QVector<MusicScraperInfos> m_artistInfosToLoad;
    QVector<MusicScraperInfos> m_albumInfosToLoad;
    QVector<Artist*> m_artists;
    QVector<Album*> m_albums;
    MusicScraperInterface* m_scraperInterface = nullptr;
};
