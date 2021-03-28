/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * Flacon - audio File Encoder
 * https://github.com/flacon/flacon
 *
 * Copyright: 2012-2013
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "dataprovider.h"

#include "../disc.h"
#include <QtNetwork/QNetworkAccessManager>

#include "settings.h"
#include "project.h"

#include <QStringList>
#include <QDebug>
#include <QTextCodec>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(LOG, "DataProvider")
}

/************************************************

 ************************************************/
DataProvider::DataProvider(const Disc &disc) :
    QObject(),
    mDisc(disc)
{
}

/************************************************

 ************************************************/
DataProvider::~DataProvider()
{
}

/************************************************

 ************************************************/
bool DataProvider::isFinished() const
{
    foreach (QNetworkReply *reply, mReplies) {
        if (!reply->isFinished())
            return false;
    }

    return true;
}

/************************************************

 ************************************************/
void DataProvider::stop()
{
}

/************************************************

 ************************************************/
void DataProvider::get(const QNetworkRequest &request)
{
    QNetworkReply *reply = networkAccessManager()->get(request);
    mReplies << reply;
    connect(reply, &QNetworkReply::finished,
            this, &DataProvider::replayFinished);
}

/************************************************

 ************************************************/
void DataProvider::error(const QString &message)
{
    foreach (QNetworkReply *reply, mReplies) {
        if (reply->isOpen())
            reply->abort();
    }
    Messages::error(message);
}

/************************************************

 ************************************************/
void DataProvider::replayFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());

    if (reply) {
        switch (reply->error()) {
            case QNetworkReply::NoError:
                mResult << dataReady(reply);
                if (isFinished())
                    emit ready(mResult);

                break;

            case QNetworkReply::OperationCanceledError:
                break;

            default:
                error(reply->errorString());
        }

        mReplies.removeAll(reply);
        reply->deleteLater();
    }

    if (!mReplies.count())
        emit finished();
}

/************************************************

 ************************************************/
QNetworkAccessManager *DataProvider::networkAccessManager() const
{
    static QNetworkAccessManager *inst = new QNetworkAccessManager();
    return inst;
}

/************************************************

 ************************************************/
FreeDbProvider::FreeDbProvider(const Disc &disc) :
    DataProvider(disc)
{
}

/************************************************

 ************************************************/
void FreeDbProvider::start()
{
    QUrl settingsUrl = Settings::i()->value(Settings::Inet_CDDBHost).toString();

    // Categories from http://freedb.freedb.org/~cddb/cddb.cgi?cmd=CDDB+LSCAT&hello=n+h+c+1&proto=6
    constexpr char const *categories[] = {
        "folk",
        "jazz",
        "misc",
        "rock",
        "country",
        "blues",
        "newage",
        "reggae",
        "classical",
        "soundtrack"
    };

    for (const auto category : categories) {
        QUrl url = QString("https://127.0.0.1/~cddb/cddb.cgi?cmd=CDDB+READ+%1+%2&hello=%3+%4+%5+%6&proto=5")
                           .arg(category)
                           .arg(disc().discId())
                           .arg("anonimous")     // Hello user
                           .arg("127.0.0.1")     // Hello host
                           .arg("flacon")        // Hello client name
                           .arg(FLACON_VERSION); // Hello client version

        if (!settingsUrl.scheme().isEmpty())
            url.setScheme(settingsUrl.scheme());

        if (settingsUrl.port(-1) == -1)
            url.setPort(settingsUrl.port());

        url.setHost(settingsUrl.host());
        QNetworkRequest request;
        request.setUrl(url);
        request.setAttribute(QNetworkRequest::User, category);
        qCDebug(LOG) << "CDDB:" << url.toString();
        get(request);
    }
}

/************************************************

 ************************************************/
QVector<Tracks> FreeDbProvider::dataReady(QNetworkReply *reply)
{
    QString statusLine = reply->readLine();
    int     status     = statusLine.section(' ', 0, 0).toInt();

    // CDDB errors .....................................
    switch (status) {
        case 210: // OK
            return QVector<Tracks>() << parse(reply);
            break;

        case 401: // No such CD entry in database, skip.
            break;

        default: // Error
            error(statusLine);
    }

    return QVector<Tracks>();
}

/************************************************

 ************************************************/
Tracks FreeDbProvider::parse(QNetworkReply *reply)
{
    QByteArray category = reply->request().attribute(QNetworkRequest::User).toByteArray();

    Tracks res;
    res.setUri(reply->url().toString());

    QByteArray        album;
    QByteArray        year;
    QByteArray        genre;
    QByteArray        performer;
    QList<QByteArray> tracks;

    while (!reply->atEnd()) {
        QByteArray line = reply->readLine();

        if (line.length() == 0 || line.startsWith('#'))
            continue;

        QByteArray key   = leftPart(line, '=').toUpper();
        QByteArray value = rightPart(line, '=').trimmed();

        if (key == "DYEAR") {
            year = value;
            continue;
        }

        if (key == "DGENRE") {
            genre = value;
            continue;
        }

        if (key == "DTITLE") {
            // The artist and disc title (in that order) separated by a "/" with a
            // single space on either side to separate it from the text.
            performer = leftPart(value, '/').trimmed();
            album     = rightPart(value, '/').trimmed();
            continue;
        }

        if (key.startsWith("TTITLE")) {
            tracks << line;
            continue;
        }
    }

    int n = 0;
    res.resize(tracks.count());
    foreach (QByteArray line, tracks) {
        Track &track = res[n++];
        track.setCodecName(disc().codecName());
        track.setTag(TagId::DiscId, disc().discId());
        track.setTag(TagId::Date, year);
        track.setTag(TagId::Genre, genre);
        track.setTag(TagId::Album, album);

        QByteArray value = rightPart(line, '=').trimmed();

        if (value.contains('/')) {
            // If the disc is a sampler and there are different artists for the
            // track titles, the track artist and the track title (in that order)
            // should be separated by a "/" with a single space on either side
            // to separate it from the text.
            track.setTag(TagId::Artist, leftPart(value, '/').trimmed());
            track.setTag(TagId::Title, rightPart(value, '/').trimmed());
        }
        else {
            track.setTag(TagId::Artist, performer.trimmed());
            track.setTag(TagId::Title, value);
        }
    }

    res.setTitle(category + " / " + performer + " [CDDB " + album + "]");
    return res;
}
