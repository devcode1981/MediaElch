#include "TvShowFileSearcher.h"

#include <QApplication>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtConcurrent/QtConcurrentMap>

#include "globals/Helper.h"
#include "globals/Manager.h"
#include "tv_shows/TvShow.h"
#include "tv_shows/TvShowEpisode.h"
#include "tv_shows/model/EpisodeModelItem.h"
#include "tv_shows/model/SeasonModelItem.h"
#include "tv_shows/model/TvShowModelItem.h"

/**
 * @brief TvShowFileSearcher::TvShowFileSearcher
 * @param parent
 */
TvShowFileSearcher::TvShowFileSearcher(QObject* parent) :
    QObject(parent),
    m_progressMessageId{Constants::TvShowSearcherProgressMessageId},
    m_aborted{false}
{
}

/**
 * @brief Sets the directories
 * @param directories List of directories
 */
void TvShowFileSearcher::setTvShowDirectories(QVector<SettingsDir> directories)
{
    m_directories.clear();
    for (int i = 0, n = directories.count(); i < n; ++i) {
        QFileInfo fi(directories.at(i).path);
        if (fi.isDir()) {
            qDebug() << "Adding tv show directory" << directories.at(i).path;
            m_directories.append(directories.at(i));
        }
    }
}

/// @brief Starts the scan process
void TvShowFileSearcher::reload(bool force)
{
    m_aborted = false;

    if (force) {
        Manager::instance()->database()->clearTvShows();
    }

    emit searchStarted(tr("Searching for TV Shows..."), m_progressMessageId);
    QVector<TvShow*> dbShows;
    Manager::instance()->tvShowModel()->clear();
    Manager::instance()->tvShowFilesWidget()->renewModel();
    QMap<QString, QVector<QStringList>> contents;
    for (SettingsDir dir : m_directories) {
        if (m_aborted) {
            return;
        }

        QVector<TvShow*> showsFromDatabase = Manager::instance()->database()->shows(dir.path);
        if (dir.autoReload || force || showsFromDatabase.count() == 0) {
            Manager::instance()->database()->clearTvShows(dir.path);
            getTvShows(dir.path, contents);

        } else {
            dbShows.append(showsFromDatabase);
        }
    }
    emit currentDir("");

    emit searchStarted(tr("Loading TV Shows..."), m_progressMessageId);
    int episodeCounter = 0;
    int episodeSum = Manager::instance()->database()->episodeCount();
    QMapIterator<QString, QVector<QStringList>> it(contents);
    while (it.hasNext()) {
        it.next();
        episodeSum += it.value().size();
    }
    it.toFront();

    // Setup shows
    while (it.hasNext()) {
        if (m_aborted) {
            return;
        }

        it.next();

        // get path
        QString path;
        int index = -1;
        for (int i = 0, n = m_directories.count(); i < n; ++i) {
            if (it.key().startsWith(m_directories[i].path)) {
                if (index == -1) {
                    index = i;
                } else if (m_directories[index].path.length() < m_directories[i].path.length()) {
                    index = i;
                }
            }
        }
        if (index != -1) {
            path = m_directories[index].path;
        }

        TvShow* show = new TvShow(it.key(), this);
        show->loadData(Manager::instance()->mediaCenterInterfaceTvShow());
        emit currentDir(show->name());
        Manager::instance()->database()->add(show, path);
        TvShowModelItem* showItem = Manager::instance()->tvShowModel()->appendChild(show);

        Manager::instance()->database()->transaction();
        QMap<SeasonNumber, SeasonModelItem*> seasonItems;
        QVector<TvShowEpisode*> episodes;

        // Setup episodes list
        for (const QStringList& files : it.value()) {
            SeasonNumber seasonNumber = getSeasonNumber(files);
            QVector<EpisodeNumber> episodeNumbers = getEpisodeNumbers(files);
            for (const EpisodeNumber& episodeNumber : episodeNumbers) {
                TvShowEpisode* episode = new TvShowEpisode(files, show);
                episode->setSeason(seasonNumber);
                episode->setEpisode(episodeNumber);
                episodes.append(episode);
            }
        }

        // Load episodes data
        QtConcurrent::blockingMapped(episodes, TvShowFileSearcher::reloadEpisodeData);

        // Add episodes to model
        for (TvShowEpisode* episode : episodes) {
            Manager::instance()->database()->add(episode, path, show->databaseId());
            show->addEpisode(episode);
            if (!seasonItems.contains(episode->season())) {
                seasonItems.insert(
                    episode->season(), showItem->appendSeason(episode->season(), episode->seasonString(), show));
            }
            seasonItems.value(episode->season())->appendEpisode(episode);
            emit progress(++episodeCounter, episodeSum, m_progressMessageId);
        }

        Manager::instance()->database()->commit();
    }

    emit currentDir("");

    // Setup shows loaded from database
    for (TvShow* show : dbShows) {
        if (m_aborted) {
            return;
        }

        show->loadData(Manager::instance()->mediaCenterInterfaceTvShow(), false);
        TvShowModelItem* showItem = Manager::instance()->tvShowModel()->appendChild(show);

        QMap<SeasonNumber, SeasonModelItem*> seasonItems;
        QVector<TvShowEpisode*> episodes = Manager::instance()->database()->episodes(show->databaseId());
        QtConcurrent::blockingMapped(episodes, TvShowFileSearcher::loadEpisodeData);
        for (TvShowEpisode* episode : episodes) {
            episode->setShow(show);
            show->addEpisode(episode);
            if (!seasonItems.contains(episode->season())) {
                seasonItems.insert(
                    episode->season(), showItem->appendSeason(episode->season(), episode->seasonString(), show));
            }
            seasonItems.value(episode->season())->appendEpisode(episode);
            emit progress(++episodeCounter, episodeSum, m_progressMessageId);
            if (episodeCounter % 1000 == 0) {
                emit currentDir("");
            }
        }
    }

    for (TvShow* show : Manager::instance()->tvShowModel()->tvShows()) {
        if (show->showMissingEpisodes()) {
            show->fillMissingEpisodes();
        }
    }

    qDebug() << "Searching for tv shows done";
    if (!m_aborted) {
        emit tvShowsLoaded(m_progressMessageId);
    }
}

TvShowEpisode* TvShowFileSearcher::loadEpisodeData(TvShowEpisode* episode)
{
    episode->loadData(Manager::instance()->mediaCenterInterfaceTvShow(), false);
    return episode;
}

void TvShowFileSearcher::reloadEpisodes(QString showDir)
{
    Manager::instance()->database()->clearTvShow(showDir);
    emit searchStarted(tr("Searching for Episodes..."), m_progressMessageId);

    // remove old show object
    for (TvShow* s : Manager::instance()->tvShowModel()->tvShows()) {
        if (m_aborted) {
            return;
        }

        if (s->dir() == showDir) {
            Manager::instance()->tvShowModel()->removeShow(s);
            break;
        }
    }

    // get path
    QString path;
    int index = -1;
    for (int i = 0, n = m_directories.count(); i < n; ++i) {
        if (m_aborted) {
            return;
        }

        if (showDir.startsWith(m_directories[i].path)) {
            if (index == -1) {
                index = i;
            } else if (m_directories[index].path.length() < m_directories[i].path.length()) {
                index = i;
            }
        }
    }
    if (index != -1) {
        path = m_directories[index].path;
    }

    // search for contents
    QVector<QStringList> contents;
    scanTvShowDir(path, showDir, contents);
    TvShow* show = new TvShow(showDir, this);
    show->loadData(Manager::instance()->mediaCenterInterfaceTvShow());
    Manager::instance()->database()->add(show, path);
    TvShowModelItem* showItem = Manager::instance()->tvShowModel()->appendChild(show);

    emit searchStarted(tr("Loading Episodes..."), m_progressMessageId);
    emit currentDir(show->name());

    int episodeCounter = 0;
    int episodeSum = contents.count();
    QMap<SeasonNumber, SeasonModelItem*> seasonItems;
    QVector<TvShowEpisode*> episodes;
    for (const QStringList& files : contents) {
        if (m_aborted) {
            return;
        }
        SeasonNumber seasonNumber = getSeasonNumber(files);
        QVector<EpisodeNumber> episodeNumbers = getEpisodeNumbers(files);
        for (const EpisodeNumber& episodeNumber : episodeNumbers) {
            TvShowEpisode* episode = new TvShowEpisode(files, show);
            episode->setSeason(seasonNumber);
            episode->setEpisode(episodeNumber);
            episodes.append(episode);
        }
    }

    QtConcurrent::blockingMapped(episodes, TvShowFileSearcher::reloadEpisodeData);

    for (TvShowEpisode* episode : episodes) {
        Manager::instance()->database()->add(episode, path, show->databaseId());
        show->addEpisode(episode);
        if (!seasonItems.contains(episode->season())) {
            seasonItems.insert(
                episode->season(), showItem->appendSeason(episode->season(), episode->seasonString(), show));
        }
        seasonItems.value(episode->season())->appendEpisode(episode);
        emit progress(++episodeCounter, episodeSum, m_progressMessageId);
        qApp->processEvents();
    }

    emit tvShowsLoaded(m_progressMessageId);
}

TvShowEpisode* TvShowFileSearcher::reloadEpisodeData(TvShowEpisode* episode)
{
    episode->loadData(Manager::instance()->mediaCenterInterfaceTvShow());
    return episode;
}

/**
 * @brief Scans a dir for tv show files
 * @param path Directory to scan
 * @param contents
 */
void TvShowFileSearcher::getTvShows(QString path, QMap<QString, QVector<QStringList>>& contents)
{
    QDir dir(path);
    QStringList tvShows = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& cDir : tvShows) {
        if (m_aborted) {
            return;
        }

        QVector<QStringList> tvShowContents;
        scanTvShowDir(path, path + QDir::separator() + cDir, tvShowContents);
        contents.insert(QDir::toNativeSeparators(dir.path() + QDir::separator() + cDir), tvShowContents);
    }
}

/**
 * @brief Scans the given path for tv show files.
 * Results are in a list which contains a QStringList for every episode.
 * @param startPath Scanning started at this path
 * @param path Path to scan
 * @param contents List of contents
 */
void TvShowFileSearcher::scanTvShowDir(QString startPath, QString path, QVector<QStringList>& contents)
{
    emit currentDir(path.mid(startPath.length()));

    QDir dir(path);
    for (const QString& cDir : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (m_aborted) {
            return;
        }

        // Skip "Extras" folder
        if (QString::compare(cDir, "Extras", Qt::CaseInsensitive) == 0
            || QString::compare(cDir, ".actors", Qt::CaseInsensitive) == 0
            || QString::compare(cDir, "extrafanarts", Qt::CaseInsensitive) == 0) {
            continue;
        }

        // Handle DVD
        if (Helper::isDvd(path + QDir::separator() + cDir)) {
            contents.append(QStringList() << QDir::toNativeSeparators(path + "/" + cDir + "/VIDEO_TS/VIDEO_TS.IFO"));
            continue;
        }
        if (Helper::isDvd(path + QDir::separator() + cDir, true)) {
            contents.append(QStringList() << QDir::toNativeSeparators(path + "/" + cDir + "/VIDEO_TS.IFO"));
            continue;
        }

        // Handle BluRay
        if (Helper::isBluRay(path + QDir::separator() + cDir)) {
            contents.append(QStringList() << QDir::toNativeSeparators(path + "/" + cDir + "/BDMV/index.bdmv"));
            continue;
        }
        scanTvShowDir(startPath, path + "/" + cDir, contents);
    }

    QStringList files;
    QStringList entries = getFiles(path);
    for (const QString& file : entries) {
        // Skip Trailers and Sample files
        if (file.contains("-trailer", Qt::CaseInsensitive) || file.contains("-sample", Qt::CaseInsensitive)) {
            continue;
        }
        files.append(file);
    }
    files.sort();

    QRegExp rx("((part|cd)[\\s_]*)(\\d+)", Qt::CaseInsensitive);
    for (int i = 0, n = files.size(); i < n; i++) {
        if (m_aborted) {
            return;
        }

        QStringList tvShowFiles;
        QString file = files.at(i);
        if (file.isEmpty()) {
            continue;
        }

        tvShowFiles << QDir::toNativeSeparators(path + QDir::separator() + file);

        int pos = rx.indexIn(file);
        if (pos != -1) {
            QString left = file.left(pos) + rx.cap(1);
            QString right = file.mid(pos + rx.cap(1).size() + rx.cap(2).size());
            for (int x = 0; x < n; x++) {
                QString subFile = files.at(x);
                if (subFile != file) {
                    if (subFile.startsWith(left) && subFile.endsWith(right)) {
                        tvShowFiles << QDir::toNativeSeparators(path + QDir::separator() + subFile);
                        files[x] = ""; // set an empty file name, this way we can skip this file in the main loop
                    }
                }
            }
        }
        if (tvShowFiles.count() > 0) {
            contents.append(tvShowFiles);
        }
    }
}

/**
 * @brief Get a list of files in a directory
 * @param path
 * @return
 */
QStringList TvShowFileSearcher::getFiles(QString path)
{
    return Settings::instance()->advanced()->tvShowFilters().files(QDir(path));
}

void TvShowFileSearcher::abort()
{
    m_aborted = true;
}

SeasonNumber TvShowFileSearcher::getSeasonNumber(QStringList files)
{
    if (files.isEmpty()) {
        return SeasonNumber::NoSeason;
    }

    QStringList filenameParts = files.at(0).split(QDir::separator());
    QString filename = filenameParts.last();
    if (filename.endsWith("VIDEO_TS.IFO", Qt::CaseInsensitive)) {
        if (filenameParts.count() > 1 && Helper::isDvd(files.at(0))) {
            filename = filenameParts.at(filenameParts.count() - 3);
        } else if (filenameParts.count() > 2 && Helper::isDvd(files.at(0), true)) {
            filename = filenameParts.at(filenameParts.count() - 2);
        }
    } else if (filename.endsWith("index.bdmv", Qt::CaseInsensitive)) {
        if (filenameParts.count() > 2) {
            filename = filenameParts.at(filenameParts.count() - 3);
        }
    }

    QRegExp rx(R"(S(\d+)[\._\-]?E)", Qt::CaseInsensitive);
    if (rx.indexIn(filename) != -1) {
        return SeasonNumber(rx.cap(1).toInt());
    }
    rx.setPattern("(\\d+)?x(\\d+)");
    if (rx.indexIn(filename) != -1) {
        return SeasonNumber(rx.cap(1).toInt());
    }
    rx.setPattern("(\\d+)(\\d){2}");
    if (rx.indexIn(filename) != -1) {
        return SeasonNumber(rx.cap(1).toInt());
    }
    rx.setPattern("Season[._ ]?(\\d+)[._ ]?Episode");
    if (rx.indexIn(filename) != -1) {
        return SeasonNumber(rx.cap(1).toInt());
    }

    // Default if no valid season could be parsed.
    return SeasonNumber::SpecialsSeason;
}

QVector<EpisodeNumber> TvShowFileSearcher::getEpisodeNumbers(QStringList files)
{
    QVector<EpisodeNumber> episodes;
    if (files.isEmpty()) {
        return episodes;
    }

    QStringList filenameParts = files.at(0).split(QDir::separator());
    QString filename = filenameParts.last();
    if (filename.endsWith("VIDEO_TS.IFO", Qt::CaseInsensitive)) {
        if (filenameParts.count() > 1 && Helper::isDvd(files.at(0))) {
            filename = filenameParts.at(filenameParts.count() - 3);
        } else if (filenameParts.count() > 2 && Helper::isDvd(files.at(0), true)) {
            filename = filenameParts.at(filenameParts.count() - 2);
        }
    } else if (filename.endsWith("index.bdmv", Qt::CaseInsensitive)) {
        if (filenameParts.count() > 2) {
            filename = filenameParts.at(filenameParts.count() - 3);
        }
    }

    QStringList patterns;
    patterns << R"(S(\d+)[\._\-]?E(\d+))"
             << "S(\\d+)EP(\\d+)"
             << "(\\d+)x(\\d+)"
             << "(\\d+)(\\d){2}"
             << "Season[._ ]?(\\d+)[._ ]?Episode[._ ]?(\\d+)";
    QRegExp rx;
    rx.setCaseSensitivity(Qt::CaseInsensitive);

    for (const QString& pattern : patterns) {
        rx.setPattern(pattern);
        int pos = 0;
        int lastPos = -1;
        while ((pos = rx.indexIn(filename, pos)) != -1) {
            // if between the last match and this one are more than five characters: break
            // this way we can try to filter "false matches" like in "21x04 - Hammond vs. 6x6.mp4"
            if (lastPos != -1 && lastPos < pos + 5) {
                break;
            }
            episodes << EpisodeNumber(rx.cap(2).toInt());
            pos += rx.matchedLength();
            lastPos = pos;
        }
        pos = lastPos;

        // Pattern matched
        if (!episodes.isEmpty()) {
            if (episodes.count() == 1) {
                rx.setPattern(R"(^[-_EeXx]+([0-9]+)($|[\-\._\sE]))");
                while (rx.indexIn(filename, pos, QRegExp::CaretAtOffset) != -1) {
                    episodes << EpisodeNumber(rx.cap(1).toInt());
                    pos += rx.matchedLength() - 1;
                }
            }
            break;
        }
    }

    return episodes;
}
