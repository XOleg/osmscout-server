#ifndef MAPMANAGER_H
#define MAPMANAGER_H

#include "filedownloader.h"
#include "mapmanagerfeature.h"

#include <QObject>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QPointer>
#include <QDateTime>
#include <QtSql>

#include <cstdint>

namespace MapManager {

  /// \brief Map Manager
  ///
  /// Map Manager keeps the list of available maps, geocoder and
  /// libpostal databases as well as tracks dependencies between
  /// them
  class Manager : public QObject, public PathProvider
  {
    Q_OBJECT

    /// \brief true when Map's storage dir is available
    Q_PROPERTY(bool storageAvailable READ storageAvailable NOTIFY storageAvailableChanged)

    /// \brief true when download is active
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)

    /// \brief true when some data is missing
    Q_PROPERTY(bool missing READ missing NOTIFY missingChanged)

  public:
    explicit Manager(QObject *parent = 0);
    virtual ~Manager();

    /// \brief Check if the storage directory is available
    Q_INVOKABLE void checkStorageAvailable();

    /// \brief Check if there is a list of provided countries
    Q_INVOKABLE bool checkProvidedAvailable();

    /// \brief Composes a list of countries on device in alphabetical order and returns as an JSON array
    Q_INVOKABLE QString getAvailableCountries();

    /// \brief Composes a list of requested countries in alphabetical order and returns as an JSON array
    Q_INVOKABLE QString getRequestedCountries();

    /// \brief Composes a list of countries provided for download in alphabetical order as an JSON array
    Q_INVOKABLE QString getProvidedCountries();

    /// \brief Add country to the list of requested countries
    ///
    Q_INVOKABLE void addCountry(QString id);

    /// \brief Remove country from the list of requested countries
    ///
    Q_INVOKABLE void rmCountry(QString id);

    /// \brief Get country details in JSON format
    ///
    Q_INVOKABLE QString getCountryDetails(QString id);

    /// \brief Check whether the country is subscribed to
    ///
    /// Checks whether the country was marked as "requested" by
    /// user
    Q_INVOKABLE bool isCountryRequested(QString id);

    /// \brief Check whether the country is available
    Q_INVOKABLE bool isCountryAvailable(QString id);

    /// \brief Check whether the country datasets are compatible with the current version
    Q_INVOKABLE bool isCountryCompatible(QString id);

    /// \brief Download or update missing data files
    ///
    Q_INVOKABLE bool getCountries();

    /// \brief Get missing data
    ///
    Q_INVOKABLE QString missingInfo();

    /// \brief Update a list of provided countries and features
    ///
    /// When the list is retrieved, the installed countries and features
    /// are checked for updates. All found updates are send via signal
    /// updatesFound as a JSON argument. The last found updates are also
    /// available via updatesFound() method
    Q_INVOKABLE bool updateProvided();

    /// \brief List of updates found when fetching the list of provided countries and features
    Q_INVOKABLE QString updatesFound();

    /// \brief Gets missing countries and the found updates
    Q_INVOKABLE void getUpdates();

    /// \brief Create a list of non-required files
    ///
    /// Makes a list of non-required files to show to the user. This
    /// method will fail (return empty list) if there are active downloads.
    /// Otherwise, we could delete partially downloaded files. To signal the
    /// inability to find the list, the calculated size occupied by the non-needed
    /// files will be set to negative (see getNonNeededFilesSize)
    Q_INVOKABLE QStringList getNonNeededFilesList();

    /// \brief Return space occupied by non-required files
    ///
    /// The space is found while composing a list by getNonNeededFilesList. This
    /// method should be called after getNonNeededFilesList. It will be negative if
    /// the list was not found due to active downloads
    Q_INVOKABLE qint64 getNonNeededFilesSize();

    /// \brief Delete non-required files
    ///
    /// Deletes files found by the getNonNeededFilesList earlier. It
    /// is required to call getNonNeededFilesList first, after that, call
    /// deleteNonNeededFiles with the same list as found by getNonNeededFilesList.
    /// If the lists don't match, the files will not get deleted. Returns true if
    /// files were deleted successfully.
    Q_INVOKABLE bool deleteNonNeededFiles(const QStringList files);

    /// Properties exposed to QML
    bool storageAvailable();
    bool downloading();
    bool missing();

    virtual QString fullPath(const QString &path) const; ///< Transform relative path to the full path

    virtual bool isRegistered(const QString &path, QString &version, QString &datetime);

  signals:
    void databaseOsmScoutChanged(QString database);
    void databaseGeocoderNLPChanged(QString database);
    void databasePostalChanged(QString global, QString country);

    void downloadingChanged(bool state);
    void downloadProgress(QString info);

    void missingChanged(bool missing);
    void missingInfoChanged(QString info);

    void subscriptionChanged();
    void availibilityChanged();

    void updatesForDataFound(QString info);

    void errorMessage(QString info);

    void storageAvailableChanged(bool available);

  public slots:
    void onSettingsChanged();

  protected:
    enum DownloadType { NoDownload=0, Countries=1, ServerUrl=2, ProvidedList=3 };

  protected:
    void loadSettings();
    bool isStorageAvailable() const;

    void scanDirectories(bool force_update = false);
    void nothingAvailable(); ///< Helper method called when there are no maps available

    void missingData();

    /// \brief Composes a list of countries in alphabetical order
    ///
    /// This is a method that creates a list. Its called by other methods to retrieve the list.
    void makeCountriesList(bool list_available, QStringList &countries, QStringList &ids, QList<uint64_t> &sz);

    /// \brief Wrapper around makeCountriesList transforming the results to JSON
    QString makeCountriesListAsJSON(bool list_available, bool tree);

    void updateOsmScout();
    void updateGeocoderNLP();
    void updatePostal();

    /// helper functions to deal with JSON representation of the features
    QJsonObject loadJson(QString fname) const;
    QString getId(const QJsonObject &obj) const;
    QString getType(const QJsonObject &obj) const;
    QString getPretty(const QJsonObject &obj) const;

    void checkUpdates();

    // handling of downloads
    void onDownloadFinished(QString path);
    void onDownloadError(QString err);
    void onDownloadedBytes(uint64_t sz);
    void onWrittenBytes(uint64_t sz);
    void onDownloadProgress();

    bool startDownload(DownloadType type, const QString &url, const QString &path, const FileDownloader::Type mode);
    void cleanupDownload();

  protected:

    // settings
    QDir m_root_dir;
    bool m_storage_available{false};
    QList< Feature* > m_features;
    QString m_provided_url;

    // available maps
    QJsonObject m_maps_available;
    QJsonObject m_maps_requested;
    QString m_map_selected;

    QString m_postal_global_path;

    bool m_missing{false};
    QString m_missing_info;
    QList< FilesToDownload > m_missing_data;
    QNetworkAccessManager m_network_manager;
    QPointer<FileDownloader> m_file_downloader;

    DownloadType m_download_type{NoDownload};
    uint64_t m_last_reported_downloaded;
    uint64_t m_last_reported_written;

    QStringList m_not_needed_files;
    qint64 m_not_needed_files_size{-1};

    QJsonArray m_last_found_updates;

    // tracking downloaded files and their versions
    QSqlDatabase m_db_files;
    QSqlQuery m_query_files_available;
    QSqlQuery m_query_files_insert;

    /// const values used to access data
    const QString const_fname_server_url{"url.json"};
    const QString const_fname_countries_provided{"countries_provided.json"};
    const QString const_fname_countries_requested{"countries_requested.json"};
    const QString const_fname_db_files{"files.sqlite"};

    const QString const_db_connection{"MapManager"};


    const QString const_feature_id_postal_global{"postal/global"};
    const QString const_feature_type_country{"territory"};

    const QString const_pretty_separator{" / "};
  };

}
#endif // MAPMANAGER_H
