#pragma once

#include "globals/Meta.h"

#include <QObject>
#include <QString>
#include <QVector>

class MovieScraperInterface;
class ConcertScraperInterface;
class MusicScraperInterface;
class MovieScraperInterface;
class TvScraperInterface;

namespace mediaelch {

class ScraperManager : public QObject
{
    Q_OBJECT

public:
    explicit ScraperManager(QObject* parent = nullptr);
    ~ScraperManager() override = default;

    ELCH_NODISCARD const QVector<MovieScraperInterface*>& movieScrapers();
    ELCH_NODISCARD const QVector<TvScraperInterface*>& tvScrapers();
    ELCH_NODISCARD const QVector<ConcertScraperInterface*>& concertScrapers();
    ELCH_NODISCARD const QVector<MusicScraperInterface*>& musicScrapers();

    ELCH_NODISCARD MovieScraperInterface* movieScraper(const QString& identifier);
    ELCH_NODISCARD TvScraperInterface* tvScraper(const QString& identifier);

    static ELCH_NODISCARD QVector<MovieScraperInterface*> constructNativeScrapers(QObject* scraperParent);

private:
    void initMovieScrapers();
    void initTvScrapers();
    void initConcertScrapers();
    void initMusicScrapers();

private:
    QVector<MovieScraperInterface*> m_movieScrapers;
    QVector<TvScraperInterface*> m_tvScrapers;
    QVector<ConcertScraperInterface*> m_concertScrapers;
    QVector<MusicScraperInterface*> m_musicScrapers;
};

} // namespace mediaelch
