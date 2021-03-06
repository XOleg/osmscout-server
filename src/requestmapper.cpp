/**
  @file
  @author Stefan Frings
*/

#include "requestmapper.h"
#include "dbmaster.h"
#include "geomaster.h"
#include "infohub.h"
#include "config.h"

#include "microhttpconnectionstore.h"

#include <microhttpd.h>

#include <QTextStream>
#include <QUrl>
#include <QRunnable>
#include <QThreadPool>
#include <QDir>

#include <QDebug>

#include <functional>

//#define DEBUG_CONNECTIONS

RequestMapper::RequestMapper()
{
#ifdef IS_SAILFISH_OS
    // In Sailfish, CPUs could be switched off one by one. As a result,
    // "ideal thread count" set by Qt could be off.
    // In other systems, this procedure is not needed and the defaults can be used
    //
    int cpus = 0;
    QDir dir;
    while ( dir.exists(QString("/sys/devices/system/cpu/cpu") + QString::number(cpus)) )
        ++cpus;

    m_pool.setMaxThreadCount(cpus);

#endif

    InfoHub::logInfo("Number of parallel worker threads: " + QString::number(m_pool.maxThreadCount()));
}


RequestMapper::~RequestMapper()
{
}


//////////////////////////////////////////////////////////////////////
/// Helper functions to get tile coordinates
//////////////////////////////////////////////////////////////////////

//static int long2tilex(double lon, int z)
//{
//    return (int)(floor((lon + 180.0) / 360.0 * pow(2.0, z)));
//}

//static int lat2tiley(double lat, int z)
//{
//    return (int)(floor((1.0 - log( tan(lat * M_PI/180.0) + 1.0 / cos(lat * M_PI/180.0)) / M_PI) / 2.0 * pow(2.0, z)));
//}

static double tilex2long(int x, int z)
{
    return x / pow(2.0, z) * 360.0 - 180;
}

static double tiley2lat(int y, int z)
{
    double n = M_PI - 2.0 * M_PI * y / pow(2.0, z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

//////////////////////////////////////////////////////////////////////
/// Helper functions to get extract values from query
//////////////////////////////////////////////////////////////////////

template <typename T>
T qstring2value(const QString &, bool &)
{
    // dummy function, implement the specific version for each type separately
    T v;
    return v;
}

template <> int qstring2value(const QString &s, bool &ok)
{
    return s.toInt(&ok);
}

template <> size_t qstring2value(const QString &s, bool &ok)
{
    return s.toUInt(&ok);
}

template <> bool qstring2value(const QString &s, bool &ok)
{
    return (s.toInt(&ok) > 0);
}

template <> double qstring2value(const QString &s, bool &ok)
{
    return s.toDouble(&ok);
}

template <> QString qstring2value(const QString &s, bool &ok)
{
    ok = true;
    return s;
}

template <typename T>
T q2value(const QString &key, T default_value, MHD_Connection *q, bool &ok)
{
    const char *vstr = MHD_lookup_connection_value(q, MHD_GET_ARGUMENT_KIND, key.toStdString().c_str());
    if (vstr == NULL)
        return default_value;

    bool this_ok = true;
    T v = qstring2value<T>(vstr,this_ok);
    if (!this_ok)
        v = default_value;
    ok = (ok && this_ok);
    return v;
}

static bool has(const char *key, MHD_Connection *q)
{
    return ( MHD_lookup_connection_value(q, MHD_GET_ARGUMENT_KIND, key)!=nullptr );
}

static bool has(const QString &key, MHD_Connection *q)
{
    return has(key.toStdString().c_str(), q);
}

//////////////////////////////////////////////////////////////////////
/// Default error function
//////////////////////////////////////////////////////////////////////
static void errorText(MHD_Response *response, MicroHTTP::Connection::keytype connection_id, const char *txt)
{
    InfoHub::logWarning(txt);

    QByteArray data;
    {
        QTextStream output(&data, QIODevice::WriteOnly);
        output << txt;
    }

    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/html; charset=UTF-8");
    MicroHTTP::ConnectionStore::setData(connection_id, data, false);
}

static void makeEmptyJson(QByteArray &result)
{
    QTextStream output(&result, QIODevice::WriteOnly);
    output << "{ }";
}

void RequestMapper::loguri(const char *uri)
{
    InfoHub::logInfo("Request: " + QString(uri));
}

/////////////////////////////////////////////////////////////////////////////
/// Runnable classes used to solve the tasks
/////////////////////////////////////////////////////////////////////////////

class Task: public QRunnable
{
public:
    Task(MicroHTTP::Connection::keytype key,
         std::function<bool(QByteArray &)> caller,
         QString error_message):
        QRunnable(),
        m_key(key),
        m_caller(caller),
        m_error_message(error_message)
    {
#ifdef DEBUG_CONNECTIONS
        InfoHub::logInfo("Runnable created: " + QString::number((size_t)m_key));
#endif
        InfoHub::addJobToQueue();
    }

    virtual ~Task()
    {
#ifdef DEBUG_CONNECTIONS
        InfoHub::logInfo("Runnable destroyed: " + QString::number((size_t)m_key));
#endif
        InfoHub::removeJobFromQueue();
    }

    virtual void run()
    {
#ifdef DEBUG_CONNECTIONS
        InfoHub::logInfo("Runnable running: " + QString::number((size_t)m_key));
#endif

        QByteArray data;
        if ( !m_caller(data) )
        {
            QByteArray err;
            {
                QTextStream output(&err, QIODevice::WriteOnly);
                output << m_error_message;
            }

#ifdef DEBUG_CONNECTIONS
            InfoHub::logInfo("Runnable submitting error: " + QString::number((size_t)m_key));
#endif
            MicroHTTP::ConnectionStore::setData(m_key, err, false);
            return;
        }

#ifdef DEBUG_CONNECTIONS
        InfoHub::logInfo("Runnable submitting data: " + QString::number((size_t)m_key));
#endif
        MicroHTTP::ConnectionStore::setData(m_key, data, false);
    }

protected:
    MicroHTTP::Connection::keytype m_key;
    std::function<bool(QByteArray &)> m_caller;
    QString m_error_message;
};

/////////////////////////////////////////////////////////////////////////////
/// Request mapper main service function
/////////////////////////////////////////////////////////////////////////////
unsigned int RequestMapper::service(const char *url_c,
                                    MHD_Connection *connection, MHD_Response *response,
                                    MicroHTTP::Connection::keytype connection_id)
{
    QUrl url(url_c);
    QString path(url.path());

    //////////////////////////////////////////////////////////////////////
    /// TILES
    if (path == "/v1/tile")
    {
        bool ok = true;
        bool daylight = q2value<bool>("daylight", true, connection, ok);
        int shift = q2value<int>("shift", 0, connection, ok);
        int scale = q2value<int>("scale", 1, connection, ok);
        int x = q2value<int>("x", 0, connection, ok);
        int y = q2value<int>("y", 0, connection, ok);
        int z = q2value<int>("z", 0, connection, ok);

        if (!ok)
        {
            errorText(response, connection_id, "Error while reading tile query parameters");
            return MHD_HTTP_BAD_REQUEST;
        }

        int ntiles = 1 << shift;

        Task *task = new Task(connection_id,
                              std::bind(&DBMaster::renderMap, osmScoutMaster,
                                        daylight, 96*scale/ntiles, z + shift, 256*scale, 256*scale,
                                        (tiley2lat(y, z) + tiley2lat(y+1, z))/2.0,
                                        (tilex2long(x, z) + tilex2long(x+1, z))/2.0, std::placeholders::_1),
                              "Error while rendering a tile" );

        m_pool.start(task);

        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
        return MHD_HTTP_OK;
    }

    //////////////////////////////////////////////////////////////////////
    /// SEARCH
    else if (path == "/v1/search" || path == "/v2/search")
    {
        bool ok = true;
        size_t limit = q2value<size_t>("limit", 25, connection, ok);
        QString search = q2value<QString>("search", "", connection, ok);

        search = search.simplified();

        if (!ok || search.length() < 1)
        {
            errorText(response, connection_id, "Error while reading search query parameters");
            return MHD_HTTP_BAD_REQUEST;
        }

        Task *task;
        bool extended_reply = (path == "/v2/search");

        if ( !useGeocoderNLP )
            task = new Task(connection_id,
                              std::bind( &DBMaster::searchExposed, osmScoutMaster,
                                         search, std::placeholders::_1, limit),
                              "Error while searching");
        else
            task = new Task(connection_id,
                            std::bind( &GeoMaster::searchExposed, geoMaster,
                                       search, std::placeholders::_1, limit,
                                       extended_reply),
                            "Error while searching");

        m_pool.start(task);

        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    //////////////////////////////////////////////////////////////////////
    /// GUIDE: LOOKUP POIs NEAR REFERENCE POINT
    else if (path == "/v1/guide")
    {
        bool ok = true;
        double radius = q2value<double>("radius", 1000.0, connection, ok);
        size_t limit = q2value<size_t>("limit", 50, connection, ok);
        QString poitype = q2value<QString>("poitype", "", connection, ok);
        QString search = q2value<QString>("search", "", connection, ok);
        double lon = q2value<double>("lng", 0, connection, ok);
        double lat = q2value<double>("lat", 0, connection, ok);

        if (!ok)
        {
            errorText(response, connection_id, "Error while reading guide query parameters");
            return MHD_HTTP_BAD_REQUEST;
        }

        search = search.simplified();

        if ( has("lng", connection) && has("lat", connection) )
        {
            Task *task = new Task(connection_id,
                                  std::bind(&DBMaster::guide, osmScoutMaster,
                                            poitype, lat, lon, radius, limit, std::placeholders::_1),
                                  "Error while looking for POIs in guide");
            m_pool.start(task);
        }

        else if ( has("search", connection) && search.length() > 0 )
        {
            std::string name;
            if (osmScoutMaster->search(search, lat, lon, name))
            {
                Task *task = new Task(connection_id,
                                      std::bind(&DBMaster::guide, osmScoutMaster,
                                                poitype, lat, lon, radius, limit, std::placeholders::_1),
                                      "Error while looking for POIs in guide");
                m_pool.start(task);
            }
            else
            {
                QByteArray bytes;
                makeEmptyJson(bytes);
                MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
            }
        }

        else
        {
            errorText(response, connection_id, "Error in guide query parameters");
            return MHD_HTTP_BAD_REQUEST;
        }

        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    //////////////////////////////////////////////////////////////////////
    /// LIST AVAILABLE POI TYPES
    else if (path == "/v1/poi_types")
    {
        QByteArray bytes;
        if (!osmScoutMaster->poiTypes(bytes))
        {
            errorText(response, connection_id, "Error while listing available POI types");
            return MHD_HTTP_INTERNAL_SERVER_ERROR;
        }

        MicroHTTP::ConnectionStore::setData(connection_id, bytes, false);
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    //////////////////////////////////////////////////////////////////////
    /// ROUTING
    else if (path == "/v1/route")
    {
        bool ok = true;
        QString type = q2value<QString>("type", "car", connection, ok);
        double radius = q2value<double>("radius", 1000.0, connection, ok);
        bool gpx = q2value<int>("gpx", 0, connection, ok);

        std::vector<osmscout::GeoCoord> points;
        std::vector< std::string > names;

        bool points_done = false;
        for (int i=0; !points_done && ok; ++i)
        {
            QString prefix = "p[" + QString::number(i) + "]";
            if ( has(prefix + "[lng]", connection) && has(prefix + "[lat]", connection) )
            {
                double lon = q2value<double>(prefix + "[lng]", 0, connection, ok);
                double lat = q2value<double>(prefix + "[lat]", 0, connection, ok);
                osmscout::GeoCoord c(lat,lon);
                points.push_back(c);
                names.push_back(std::string());
            }

            else if ( has(prefix + "[search]", connection) )
            {
                QString search = q2value<QString>(prefix + "[search]", "", connection, ok);                
                search = search.simplified();
                if (search.length()<1)
                {
                    errorText(response, connection_id, "Error in routing parameters: search term is missing" );
                    return MHD_HTTP_BAD_REQUEST;
                }

                double lat, lon;
                std::string name;
                bool unlp = useGeocoderNLP;
                if ( (unlp && geoMaster->search(search, lat, lon, name)) ||
                     (!unlp && osmScoutMaster->search(search, lat, lon, name)) )
                {
                    osmscout::GeoCoord c(lat,lon);
                    points.push_back(c);
                    names.push_back(name);
                }
                else
                    ok = false;
            }

            else points_done = true;
        }

        if (!ok || points.size() < 2)
        {
            errorText(response, connection_id, "Error in routing parameters: too few routing points" );
            return MHD_HTTP_BAD_REQUEST;
        }

        osmscout::Vehicle vehicle;
        if (type == "car") vehicle = osmscout::vehicleCar;
        else if (type == "bicycle") vehicle = osmscout::vehicleBicycle;
        else if (type == "foot") vehicle = osmscout::vehicleFoot;
        else
        {
            errorText(response, connection_id, "Error in routing parameters: unknown vehicle" );
            return MHD_HTTP_BAD_REQUEST;
        }

        Task *task = new Task(connection_id,
                              std::bind(&DBMaster::route, osmScoutMaster,
                                        vehicle, points, radius, names, gpx, std::placeholders::_1),
                              "Error while looking for route");
        m_pool.start(task);

        if (!gpx) MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/plain; charset=UTF-8");
        else MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "text/xml; charset=UTF-8");
        return MHD_HTTP_OK;
    }

    else // command unidentified. return help string
    {
        errorText(response, connection_id, "Unknown URL path");
        return MHD_HTTP_BAD_REQUEST;
    }
}
