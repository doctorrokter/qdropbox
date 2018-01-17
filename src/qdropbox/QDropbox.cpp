/*
 * QDropbox.cpp
 *
 *  Created on: Nov 25, 2017
 *      Author: doctorrokter
 */

#include "../../include/qdropbox/QDropbox.hpp"
#include <QUrl>
#include <QDebug>
#include <QList>
#include <QDir>
#include "../qjson/serializer.h"
#include "../qjson/parser.h"
#include "../../include/qdropbox/QDropboxFile.hpp"
#include "../../include/qdropbox/QDropboxCommon.hpp"

Logger QDropbox::logger = Logger::getLogger("QDropbox");

qint64 QDropbox::uploadSize = 157286400; // 150MB
#define DROPBOX_UPLOAD_SIZE 157286400 // 150 MB
#define UPLOAD_SIZE 1048576 // 1 MB
#define CONCURRENT_UPLOADS 5

QDropbox::QDropbox(QObject* parent) : QObject(parent) {
    init();
}

QDropbox::QDropbox(const QString& accessToken, QObject* parent) : QObject(parent) {
    init();
    m_accessToken = accessToken;
}

QDropbox::QDropbox(const QString& appSecret, const QString& appKey, const QString& redirectUri, QObject* parent) : QObject(parent) {
    init();
    m_appSecret = appSecret;
    m_appKey = appKey;
    m_redirectUri = redirectUri;
}

QDropbox::~QDropbox() {}

const QString& QDropbox::getUrl() const { return m_url; }
QDropbox& QDropbox::setUrl(const QString& url) {
    m_url = url;
    generateFullUrl();
    return *this;
}

const int& QDropbox::getVersion() const { return m_version; }
QDropbox& QDropbox::setVersion(const int& version) {
    m_version = version;
    generateFullUrl();
    return *this;
}

const QString& QDropbox::getAppSecret() const { return m_appSecret; }
QDropbox& QDropbox::setAppSecret(const QString& appSecret) {
    m_appSecret = appSecret;
    return *this;
}

const QString& QDropbox::getAppKey() const { return m_appKey; }
QDropbox& QDropbox::setAppKey(const QString& appKey) {
    m_appKey = appKey;
    return *this;
}

const QString& QDropbox::getRedirectUri() const { return m_redirectUri; }
QDropbox& QDropbox::setRedirectUri(const QString& redirectUri) {
    m_redirectUri = redirectUri;
    return *this;
}

const QString& QDropbox::getAccessToken() const { return m_accessToken; }
QDropbox& QDropbox::setAccessToken(const QString& accessToken) {
    if (m_accessToken.compare(accessToken) != 0) {
        m_accessToken = accessToken;
        emit accessTokenChanged(m_accessToken);
    }
    return *this;
}

const QString& QDropbox::getDownloadsFolder() const { return m_downloadsFolder; }
QDropbox& QDropbox::setDownloadsFolder(const QString& downloadsFolder) {
    m_downloadsFolder = downloadsFolder;
    return *this;
}

QDropbox& QDropbox::setReadBufferSize(qint64 readBufferSize) {
    m_readBufferSize = readBufferSize;
    return *this;
}

QString QDropbox::authUrl() const {
    return QString(m_authUrl).append("/authorize?response_type=token&client_id=").append(m_appKey).append("&redirect_uri=").append(m_redirectUri);
}

void QDropbox::authTokenRevoke() {
    QNetworkRequest req = prepareRequest("/auth/token/revoke");

    QNetworkReply* reply = m_network.post(req, "");
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onAuthTokenRevoked()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onAuthTokenRevoked() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        emit authTokenRevoked();
    }

    reply->deleteLater();
}

void QDropbox::listFolder(const QString& path, const bool& includeMediaInfo, const bool& recursive,
                    const bool& includeDeleted, const bool& includeHasExplicitSharedMembers, const bool& includeMountedFolders,
                    const int& limit, SharedLink sharedLink) {

    QNetworkRequest req = prepareRequest("/files/list_folder");
    QVariantMap map;
    if (!sharedLink.isEmpty()) {
        map["shared_link"] = sharedLink.toMap();
    }
    if (limit != 0) {
        map["limit"] = limit;
    }
    map["path"] = path;
    map["include_media_info"] = includeMediaInfo;
    map["recursive"] = recursive;
    map["include_deleted"] = includeDeleted;
    map["include_has_explicit_shared_members"] = includeHasExplicitSharedMembers;
    map["include_mounted_folders"] = includeMountedFolders;

    QNetworkReply* reply = m_network.post(req, QJson::Serializer().serialize(map));
    reply->setProperty("path", path);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onListFolderLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onListFolderLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap dataMap = data.toMap();
            QString cursor = dataMap.value("cursor").toString();
            bool hasMore = dataMap.value("has_more").toBool();
            QVariantList entries = dataMap.value("entries").toList();
            QList<QDropboxFile*> files;
            foreach(QVariant v, entries) {
                QDropboxFile* pFile = new QDropboxFile(this);
                pFile->fromMap(v.toMap());
                files.append(pFile);
            }
            QString path = reply->property("path").toString();
            emit listFolderLoaded(path, files, cursor, hasMore);
        }
    }

    reply->deleteLater();
}

void QDropbox::listFolderContinue(const QString& cursor) {
    QNetworkRequest req = prepareRequest("/files/list_folder/continue");
    QVariantMap map;
    map["cursor"] = cursor;

    QNetworkReply* reply = m_network.post(req, QJson::Serializer().serialize(map));
    reply->setProperty("cursor", cursor);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onListFolderContinueLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);

}

void QDropbox::onListFolderContinueLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap dataMap = data.toMap();
            QString cursor = dataMap.value("cursor").toString();
            bool hasMore = dataMap.value("has_more").toBool();
            QVariantList entries = dataMap.value("entries").toList();
            QList<QDropboxFile*> files;
            foreach(QVariant v, entries) {
                QDropboxFile* pFile = new QDropboxFile(this);
                pFile->fromMap(v.toMap());
                files.append(pFile);
            }
            QString prevCursor = reply->property("cursor").toString();
            emit listFolderContinueLoaded(files, prevCursor, cursor, hasMore);
        }
    }

    reply->deleteLater();
}

void QDropbox::createFolder(const QString& path, const bool& autorename) {
    QNetworkRequest req = prepareRequest("/files/create_folder_v2");
    QVariantMap map;
    map["path"] = path;
    map["autorename"] = autorename;

    QNetworkReply* reply = m_network.post(req, QJson::Serializer().serialize(map));
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderCreated()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderCreated() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxFile* pFolder = new QDropboxFile(this);
            pFolder->fromMap(data.toMap().value("metadata").toMap());
            pFolder->setTag("folder");
            emit folderCreated(pFolder);
        }
    }

    reply->deleteLater();
}

void QDropbox::deleteFile(const QString& path) {
    QNetworkRequest req = prepareRequest("/files/delete_v2");
    QVariantMap map;
    map["path"] = path;

    QNetworkReply* reply = m_network.post(req, QJson::Serializer().serialize(map));
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFileDeleted()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFileDeleted() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxFile* pFile = new QDropboxFile(this);
            pFile->fromMap(data.toMap().value("metadata").toMap());
            emit fileDeleted(pFile);
        }
    }

    reply->deleteLater();
}

void QDropbox::deleteBatch(const QStringList& paths) {
    QNetworkRequest req = prepareRequest("/files/delete_batch");
    QVariantMap map;
    QVariantList entries;
    foreach(QString path, paths) {
        QVariantMap p;
        p["path"] = path;
        entries.append(p);
    }
    map["entries"] = entries;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("paths", paths);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onDeletedBatch()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onDeletedBatch() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QJson::Parser parser;
        bool res = false;
        QVariant data = parser.parse(reply->readAll(), &res);
        if (res) {
            emit deletedBatch(reply->property("paths").toStringList());
        }
    }

    reply->deleteLater();
}

void QDropbox::move(const QString& fromPath, const QString& toPath, const bool& allowSharedFolder, const bool& autorename, const bool& allowOwnershipTransfer) {
    QNetworkReply* reply = moveFile(fromPath, toPath, allowSharedFolder, autorename, allowOwnershipTransfer);
    reply->setProperty("from_path", fromPath);
    reply->setProperty("to_path", toPath);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onMoved()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onMoved() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxFile* pFile = new QDropboxFile(this);
            pFile->fromMap(data.toMap().value("metadata").toMap());
            emit moved(pFile, reply->property("from_path").toString(), reply->property("to_path").toString());
        }
    }

    reply->deleteLater();
}

void QDropbox::moveBatch(const QList<MoveEntry>& moveEntries, const bool& allowSharedFolder, const bool& autorename, const bool& allowOwnershipTransfer) {
    QNetworkRequest req = prepareRequest("/files/move_batch");
    QVariantMap map;
    QVariantList entries;
    foreach(MoveEntry e, moveEntries) {
        entries.append(e.toMap());
    }
    map["entries"] = entries;
    map["allow_shared_folder"] = allowSharedFolder;
    map["autorename"] = autorename;
    map["allow_ownership_transfer"] = allowOwnershipTransfer;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("entries", entries);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onMovedBatch()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onMovedBatch() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QList<MoveEntry> moveEntries;
        QVariantList entries = reply->property("entries").toList();
        foreach(QVariant v, entries) {
            MoveEntry e;
            e.fromMap(v.toMap());
            moveEntries.append(e);
        }
        emit movedBatch(moveEntries);
    }

    reply->deleteLater();
}

void QDropbox::rename(const QString& fromPath, const QString& toPath, const bool& allowSharedFolder, const bool& autorename, const bool& allowOwnershipTransfer) {
    QNetworkReply* reply = moveFile(fromPath, toPath, allowSharedFolder, autorename, allowOwnershipTransfer);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onRenamed()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onRenamed() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxFile* pFile = new QDropboxFile(this);
            pFile->fromMap(data.toMap().value("metadata").toMap());
            emit renamed(pFile);
        }
    }

    reply->deleteLater();
}

void QDropbox::getThumbnail(const QString& path, const QString& size, const QString& format) {
    if (!path.trimmed().isEmpty()) {
        QNetworkRequest req = prepareContentRequest("/files/get_thumbnail", false);

        QVariantMap map;

        QVariantMap sizeMap;
        sizeMap[".tag"] = size;
        map["size"] = sizeMap;
        map["path"] = path;
        map["format"] = format;

        QJson::Serializer serializer;
        QByteArray data = serializer.serialize(map);
        req.setRawHeader("Dropbox-API-Arg", data);

//        logger.debug("Dropbox-API-Arg: " + data);

        QNetworkReply* reply = m_network.post(req, "");
        reply->setProperty("path", path);
        reply->setProperty("size", size);
        bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onThumbnailLoaded()));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
        Q_ASSERT(res);
        Q_UNUSED(res);
    }

}

void QDropbox::onThumbnailLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QImage* thumbnail = new QImage();
        thumbnail->loadFromData(reply->readAll());
        emit thumbnailLoaded(reply->property("path").toString(), reply->property("size").toString(), thumbnail);
    }

    reply->deleteLater();
}

void QDropbox::download(const QString& path, const QString& rev) {
    QNetworkRequest req = prepareContentRequest("/files/download");

    QVariantMap map;
    map["path"] = path;
    if (!rev.isEmpty()) {
        map["rev"] = rev;
    }

    req.setRawHeader("Dropbox-API-Arg", QJson::Serializer().serialize(map));

    QNetworkReply* reply = m_network.post(req, "");
    reply->setReadBufferSize(m_readBufferSize);
    reply->setProperty("path", path);
    m_downloadsQueue.append(reply);

    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onDownloaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(onDownloadProgress(qint64,qint64)));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(readyRead()), this, SLOT(read()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
    emit downloadStarted(path);
}

void QDropbox::downloadZip(const QString& path, const QString& rev) {
    QNetworkRequest req = prepareContentRequest("/files/download_zip");

    QVariantMap map;
    map["path"] = path;
    if (!rev.isEmpty()) {
        map["rev"] = rev;
    }

    req.setRawHeader("Dropbox-API-Arg", QJson::Serializer().serialize(map));

    QNetworkReply* reply = m_network.post(req, "");
    reply->setReadBufferSize(m_readBufferSize);
    reply->setProperty("path", path);
    m_downloadsQueue.append(reply);

    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onDownloadedZip()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(onDownloadProgress(qint64,qint64)));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(readyRead()), this, SLOT(readZip()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
    emit downloadStarted(path);
}

void QDropbox::read() {
    QNetworkReply* reply = getReply();
    QString path = reply->property("path").toString();
    QDir dir(m_downloadsFolder);
    if (!dir.exists()) {
        dir.mkpath(m_downloadsFolder);
    }

    QFile file(m_downloadsFolder + "/" + getFilename(path));
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write(reply->readAll());
    file.flush();
    file.close();
}

void QDropbox::readZip() {
    QNetworkReply* reply = getReply();
    QString path = reply->property("path").toString();
    QDir dir(m_downloadsFolder);
    if (!dir.exists()) {
        dir.mkpath(m_downloadsFolder);
    }

    QFile file(m_downloadsFolder + "/" + getFilename(path) + ".zip");
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write(reply->readAll());
    file.flush();
    file.close();
}

void QDropbox::onDownloadProgress(qint64 loaded, qint64 total) {
    QNetworkReply* reply = getReply();
    emit downloadProgress(reply->property("path").toString(), loaded, total);
}

void QDropbox::onDownloaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QString path = reply->property("path").toString();
        QString filename = getFilename(path);
        QString localPath = m_downloadsFolder + "/" + filename;
        logger.debug("File downloaded: " + localPath);
        emit downloaded(path, localPath);
    }

    m_downloadsQueue.removeAll(reply);
    reply->deleteLater();
}

void QDropbox::onDownloadedZip() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QString path = reply->property("path").toString();
        QString filename = getFilename(path);
        QString localPath = m_downloadsFolder + "/" + filename + ".zip";
        logger.debug("File downloaded: " + localPath);
        emit downloaded(path, localPath);
    }

    m_downloadsQueue.removeAll(reply);
    reply->deleteLater();
}

void QDropbox::upload(QFile* file, const QString& remotePath, const QString& mode, const bool& autorename, const bool& mute) {
    if (file->exists()) {
        QNetworkRequest req = prepareContentRequest("/files/upload");

        QVariantMap map;
        map["path"] = remotePath;
        map["mode"] = mode;
        map["autorename"] = autorename;
        map["mute"] = mute;

        QByteArray data = QJson::Serializer().serialize(map);
        logger.debug(data);
        req.setRawHeader("Dropbox-API-Arg", data);

        file->open(QIODevice::ReadOnly);
        QNetworkReply* reply = m_network.post(req, file);
        reply->setProperty("path", remotePath);
        file->setParent(reply);
        m_uploadsQueue.append(reply);

        bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onUploaded()));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(uploadProgress(qint64,qint64)), this, SLOT(onUploadProgress(qint64,qint64)));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onUploadError(QNetworkReply::NetworkError)));
        Q_ASSERT(res);
        Q_UNUSED(res);
        emit uploadStarted(remotePath);
    } else {
        QString error = "Cannot open file: " + file->fileName() + "\n" + QString::number(file->error());
        logger.error(error);
        emit uploadFailed(error);
        file->deleteLater();
    }
}

void QDropbox::onUploaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QJson::Parser parser;
        bool* res = new bool(false);
        QVariant data = parser.parse(reply->readAll(), res);
        if (*res) {
            QVariantMap map = data.toMap();
            map[".tag"] = FILE_TAG;
            QDropboxFile* file = new QDropboxFile(this);
            file->fromMap(map);
            logger.debug("File uploaded: " + file->getPathDisplay());
            emit uploaded(file);
        }
        delete res;
    }

    m_uploadsQueue.removeAll(reply);
    reply->deleteLater();
}

void QDropbox::onUploadError(QNetworkReply::NetworkError e) {
    QNetworkReply* reply = getReply();
    if (reply->error() != QNetworkReply::NoError) {
        emit uploadFailed(reply->errorString());
    }
    Q_UNUSED(e);
}

void QDropbox::uploadSessionStart(const QString& remotePath, const QByteArray& data , const bool& close) {
    if (data.size()) {
        QNetworkRequest req = prepareContentRequest("/files/upload_session/start");
        QVariantMap map;
        map["close"] = close;

        QByteArray params = QJson::Serializer().serialize(map);
        logger.debug(params);
        req.setRawHeader("Dropbox-API-Arg", params);

        QNetworkReply* reply = m_network.post(req, data);
        reply->setProperty("remote_path", remotePath);
        bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onUploadSessionStarted()));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
        Q_ASSERT(res);
        Q_UNUSED(res);
    }
}

void QDropbox::onUploadSessionStarted() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            emit uploadSessionStarted(reply->property("remote_path").toString(), data.toMap().value("session_id").toString());
        }
    }

    reply->deleteLater();
}

void QDropbox::uploadSessionAppend(const QString& sessionId, const QByteArray& data, const qint64& offset, const bool& close) {
    if (data.size()) {
        QNetworkRequest req = prepareContentRequest("/files/upload_session/append_v2");
        QVariantMap map;
        QVariantMap cursor;
        cursor["session_id"] = sessionId;
        cursor["offset"] = offset;
        map["cursor"] = cursor;
        map["close"] = close;

        QByteArray params = QJson::Serializer().serialize(map);
        logger.debug(params);
        req.setRawHeader("Dropbox-API-Arg", params);

        QNetworkReply* reply = m_network.post(req, data);
        reply->setProperty("session_id", sessionId);
        bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onUploadSessionAppended()));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
        Q_ASSERT(res);
        Q_UNUSED(res);
    }
}

void QDropbox::onUploadSessionAppended() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        emit uploadSessionAppended(reply->property("session_id").toString());
    }

    reply->deleteLater();
}

void QDropbox::uploadSessionFinish(const QString& sessionId, const QByteArray& data, const qint64& offset, const QString& path, const QString& mode, const bool& autorename, const bool& mute) {
    if (data.size()) {
        QNetworkRequest req = prepareContentRequest("/files/upload_session/finish");
        QVariantMap map;
        QVariantMap cursor;
        cursor["session_id"] = sessionId;
        cursor["offset"] = offset;
        map["cursor"] = cursor;
        QVariantMap commit;
        commit["path"] = path;
        commit["mode"] = mode;
        commit["autorename"] = autorename;
        commit["mute"] = mute;
        map["commit"] = commit;

        QByteArray params = QJson::Serializer().serialize(map);
        logger.debug(params);
        req.setRawHeader("Dropbox-API-Arg", params);

        QNetworkReply* reply = m_network.post(req, data);
        bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onUploadSessionFinished()));
        Q_ASSERT(res);
        res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
        Q_ASSERT(res);
        Q_UNUSED(res);
    }
}

void QDropbox::onUploadSessionFinished() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap map = data.toMap();
            map[".tag"] = FILE_TAG;
            QDropboxFile* file = new QDropboxFile(this);
            file->fromMap(map);
            emit uploadSessionFinished(file);
        }
    }

    reply->deleteLater();
}

void QDropbox::onUploadProgress(qint64 loaded, qint64 total) {
    QNetworkReply* reply = getReply();
    emit uploadProgress(reply->property("path").toString(), loaded, total);
}

void QDropbox::getTemporaryLink(const QString& path) {
    QNetworkRequest req = prepareRequest("/files/get_temporary_link");
    QVariantMap map;
    map["path"] = path;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onTemporaryLinkLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onTemporaryLinkLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap map = data.toMap();
            QVariantMap metadata = map.value("metadata").toMap();
            metadata[".tag"] = FILE_TAG;
            map["metadata"] = metadata;
            QDropboxTempLink* link = new QDropboxTempLink(this);
            link->fromMap(map);
            emit temporaryLinkLoaded(link);
        }
    }

    reply->deleteLater();
}

void QDropbox::saveUrl(const QString& path, const QString& url) {
    QNetworkRequest req = prepareRequest("/files/save_url");
    QString filename = url.split("/").last();
    QVariantMap map;
    map["path"] = path + "/" + filename;
    map["url"] = url;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onUrlSaved()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onUrlSaved() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        emit urlSaved();
    }

    reply->deleteLater();
}

void QDropbox::getMetadata(const QString& path, const bool& includeMediaInfo, const bool& includeDeleted, const bool& includeHasExplicitSharedMembers) {
    QNetworkRequest req = prepareRequest("/files/get_metadata");
    QVariantMap map;
    map["path"] = path;
    map["include_media_info"] = includeMediaInfo;
    map["include_deleted"] = includeDeleted;
    map["include_has_explicit_shared_members"] = includeHasExplicitSharedMembers;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);
    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onMetadataReceived()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onMetadataReceived() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxFile* file = new QDropboxFile(this);
            file->fromMap(data.toMap());
            emit metadataReceived(file);
        }
    }

    reply->deleteLater();
}

void QDropbox::addFolderMember(const QString& sharedFolderId, const QList<QDropboxMember>& members, const bool& quiet, const QString& customMessage) {
    QNetworkRequest req = prepareRequest("/sharing/add_folder_member");
    QVariantMap map;
    map["shared_folder_id"] = sharedFolderId;
    map["quiet"] = quiet;
    if (!customMessage.isEmpty()) {
        map["custom_message"] = customMessage;
    }
    QVariantList membersList;
    foreach(QDropboxMember m, members) {
        membersList.append(m.toMap());
    }
    map["members"] = membersList;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("shared_folder_id", sharedFolderId);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderMemberAdded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderMemberAdded() {
    QNetworkReply* reply = getReply();
    emit folderMemberAdded(reply->property("shared_folder_id").toString());
    reply->deleteLater();
}

void QDropbox::removeFolderMember(const QString& sharedFolderId, QDropboxMember& member, const bool& leaveACopy) {
    QNetworkRequest req = prepareRequest("/sharing/remove_folder_member");
    QVariantMap map;
    map["shared_folder_id"] = sharedFolderId;
    map["member"] = member.toMap().value("member").toMap();
    map["leave_a_copy"] = leaveACopy;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("shared_folder_id", sharedFolderId);
    reply->setProperty("member", member.toMap());
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderMemberRemoved()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderMemberRemoved() {
    QNetworkReply* reply = getReply();
    QDropboxMember* member = new QDropboxMember(this);
    member->fromMap(reply->property("member").toMap());
    emit folderMemberRemoved(reply->property("shared_folder_id").toString(), member);
    reply->deleteLater();
}

void QDropbox::updateFolderMember(const QString& sharedFolderId, QDropboxMember& member) {
    QNetworkRequest req = prepareRequest("/sharing/update_folder_member");
    QVariantMap map;
    map["shared_folder_id"] = sharedFolderId;
    map["member"] = member.toMap().value("member").toMap();
    map["access_level"] = member.getAccessLevel().name();

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("shared_folder_id", sharedFolderId);
    reply->setProperty("member", member.toMap());
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderMemberUpdated()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderMemberUpdated() {
    QNetworkReply* reply = getReply();
    QDropboxMember* member = new QDropboxMember(this);
    member->fromMap(reply->property("member").toMap());
    emit folderMemberUpdated(reply->property("shared_folder_id").toString(), member);
    reply->deleteLater();
}

void QDropbox::listFolderMembers(const QString& sharedFolderId, const int& limit) {
    QNetworkRequest req = prepareRequest("/sharing/list_folder_members");
    QVariantMap map;
    map["shared_folder_id"] = sharedFolderId;
    if (limit != 0) {
        map["limit"] = limit;
    }

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("shared_folder_id", sharedFolderId);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onListFolderMembers()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onListFolderMembers() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool* res = new bool(false);
        QVariant data = QJson::Parser().parse(reply->readAll(), res);
        if (*res) {
            QVariantMap map = data.toMap();
            QList<QDropboxFolderMember*> members;
            QVariantList users = map.value("users").toList();
            foreach(QVariant v, users) {
                QDropboxFolderMember* m = new QDropboxFolderMember(this);
                m->fromMap(v.toMap());
                members.append(m);
            }
            emit listFolderMembersLoaded(reply->property("shared_folder_id").toString(), members, map.value("cursor", "").toString());
        }
        delete res;
    }

    reply->deleteLater();
}

void QDropbox::shareFolder(const QString& path, const bool& forceAsync, const QDropboxAclUpdatePolicy& aclUpdatePolicy,  const QDropboxMemberPolicy& memberPolicy,
            const QDropboxSharedLinkPolicy& sharedLinkPolicy, const QDropboxViewerInfoPolicy& viewerInfoPolicy,
            const QList<QDropboxFolderAction>& folderActions) {

    QNetworkRequest req = prepareRequest("/sharing/share_folder");
    QVariantMap map;
    map["path"] = path;
    map["force_async"] = forceAsync;
    if (aclUpdatePolicy.value() != QDropboxAclUpdatePolicy::NONE) {
        map["acl_update_policy"] = aclUpdatePolicy.name();
    }

    if (memberPolicy.value() != QDropboxMemberPolicy::NONE) {
        map["member_policy"] = memberPolicy.name();
    }

    if (sharedLinkPolicy.value() != QDropboxSharedLinkPolicy::NONE) {
        map["shared_link_policy"] = sharedLinkPolicy.name();
    }

    if (viewerInfoPolicy.value() != QDropboxViewerInfoPolicy::NONE) {
        map["viewer_info_policy"] = viewerInfoPolicy.name();
    }

    if (folderActions.size() > 0) {
        QVariantList actions;
        foreach(QDropboxFolderAction a, folderActions) {
            if (a.value() != QDropboxFolderAction::NONE) {
                QVariantMap aMap;
                aMap[".tag"] = a.name();
                actions.append(aMap);
            }
        }
        if (actions.size() > 0) {
            map["actions"] = actions;
        }
    }

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("path", path);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderShared()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderShared() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        QJson::Parser parser;
        bool* res = new bool(false);
        QVariant data = parser.parse(reply->readAll(), res);
        if (*res) {

            // TODO: process full data in the future

            QVariantMap map = data.toMap();
            emit folderShared(reply->property("path").toString(), map.value("shared_folder_id").toString());
        }
        delete res;
    }

    reply->deleteLater();
}

void QDropbox::unshareFolder(const QString& sharedFolderId, const bool& leaveACopy) {
    QNetworkRequest req = prepareRequest("/sharing/unshare_folder");
    QVariantMap map;
    map["shared_folder_id"] = sharedFolderId;
    map["leave_a_copy"] = leaveACopy;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("shared_folder_id", sharedFolderId);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onFolderUnshared()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onFolderUnshared() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        UnshareJobStatus status;
        status.asyncJobId = "";
        status.status = UnshareJobStatus::InProgress;
        status.sharedFolderId = reply->property("shared_folder_id").toString();
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap map = data.toMap();
            if (map.value(".tag").toString().compare("complete") == 0) {
                status.status = UnshareJobStatus::Complete;
            } else {
                status.asyncJobId = map.value("async_job_id").toString();
            }
        }

        emit folderUnshared(status);
    }

    reply->deleteLater();
}

void QDropbox::createSharedLink(const QString& path, const bool& shortUrl, const QDropboxPendingUpload& pendingUpload) {
    QNetworkRequest req = prepareRequest("/sharing/create_shared_link");
    QVariantMap map;
    map["path"] = path;
    map["short_url"] = shortUrl;
    if (pendingUpload.value() != QDropboxPendingUpload::NONE) {
        map["pending_upload"] = pendingUpload.name();
    }

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onSharedLinkCreated()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onSharedLinkCreated() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            SharedLink* link = new SharedLink(this);
            link->fromMap(data.toMap());
            emit sharedLinkCreated(link);
        }
    }

    reply->deleteLater();
}

void QDropbox::revokeSharedLink(const QString& sharedLinkUrl) {
    QNetworkRequest req = prepareRequest("/sharing/revoke_shared_link");
    QVariantMap map;
    map["url"] = sharedLinkUrl;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("url", sharedLinkUrl);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onSharedLinkRevoked()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onSharedLinkRevoked() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        emit sharedLinkRevoked(reply->property("url").toString());
    }

    reply->deleteLater();
}

void QDropbox::getSharedLinks(const QString& path) {
    QNetworkRequest req = prepareRequest("/sharing/get_shared_links");
    QVariantMap map;
    map["path"] = path;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onSharedLinksLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onSharedLinksLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantList list = data.toMap().value("links").toList();
            QList<SharedLink*> links;
            foreach(QVariant v, list) {
                SharedLink* link = new SharedLink(this);
                link->fromMap(v.toMap());
                links.append(link);
            }
            emit sharedLinksLoaded(links);
        }
    }

    reply->deleteLater();
}

void QDropbox::checkJobStatus(const QString& asyncJobId) {
    QNetworkRequest req = prepareRequest("/sharing/check_job_status");
    QVariantMap map;
    map["async_job_id"] = asyncJobId;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    reply->setProperty("async_job_id", asyncJobId);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onJobStatusChecked()));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onJobStatusChecked() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QVariantMap map = data.toMap();
            if (map.contains(".tag")) {
                UnshareJobStatus status;
                status.asyncJobId = reply->property("async_job_id").toString();
                status.status = map.value(".tag").toString().compare("complete") == 0 ? UnshareJobStatus::Complete : UnshareJobStatus::InProgress;
                emit jobStatusChecked(status);
            }
        }
    } else {
        QString errorString = reply->errorString();
        logger.error(errorString);
        logger.error(reply->error());
        if (reply->bytesAvailable()) {
            logger.error(reply->readAll());
        }
    }

    reply->deleteLater();
}

void QDropbox::getAccount(const QString& accountId) {
    QNetworkRequest req = prepareRequest("/users/get_account");
    QVariantMap map;
    map["account_id"] = accountId;

    QJson::Serializer serializer;

    QNetworkReply* reply = m_network.post(req, serializer.serialize(map));
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onAccountLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onAccountLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            Account* account = new Account(this);
            account->fromMap(data.toMap());
            emit accountLoaded(account);
        }
    }

    reply->deleteLater();
}

void QDropbox::getAccountBatch(const QStringList& accountIds) {
    QNetworkRequest req = prepareRequest("/users/get_account_batch");
    QVariantMap map;
    map["account_ids"] = accountIds;

    QByteArray data = QJson::Serializer().serialize(map);
    logger.debug(data);

    QNetworkReply* reply = m_network.post(req, data);
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onAccountBatchLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onAccountBatchLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QList<Account*> accounts;
            QVariantList list = data.toList();
            foreach(QVariant v, list) {
                Account* account = new Account(this);
                account->fromMap(v.toMap());
                accounts.append(account);
            }
            emit accountBatchLoaded(accounts);
        }
    }

    reply->deleteLater();
}

void QDropbox::getCurrentAccount() {
    QNetworkRequest req = prepareRequest("/users/get_current_account");
    QNetworkReply* reply = m_network.post(req, "null");
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onCurrentAccountLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onCurrentAccountLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            Account* account = new Account(this);
            account->fromMap(data.toMap());
            emit currentAccountLoaded(account);
        }
    }

    reply->deleteLater();
}

void QDropbox::getSpaceUsage() {
    QNetworkRequest req = prepareRequest("/users/get_space_usage");
    QNetworkReply* reply = m_network.post(req, "null");
    bool res = QObject::connect(reply, SIGNAL(finished()), this, SLOT(onSpaceUsageLoaded()));
    Q_ASSERT(res);
    res = QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onError(QNetworkReply::NetworkError)));
    Q_ASSERT(res);
    Q_UNUSED(res);
}

void QDropbox::onSpaceUsageLoaded() {
    QNetworkReply* reply = getReply();

    if (reply->error() == QNetworkReply::NoError) {
        bool res = false;
        QVariant data = QJson::Parser().parse(reply->readAll(), &res);
        if (res) {
            QDropboxSpaceUsage* spaceUsage = new QDropboxSpaceUsage(this);
            spaceUsage->fromMap(data.toMap());
            emit spaceUsageLoaded(spaceUsage);
        }
    }

    reply->deleteLater();
}

void QDropbox::onError(QNetworkReply::NetworkError e) {
    QNetworkReply* reply = getReply();
    QString errorString = reply->errorString();
    logger.error(errorString);
    logger.error(e);
    if (reply->bytesAvailable()) {
        logger.error(reply->readAll());
    }
    emit error(e, errorString);
}

void QDropbox::init() {
    m_authUrl = "https://dropbox.com/oauth2";
    m_url = "https://api.dropboxapi.com";
    m_contentUrl = "https://content.dropboxapi.com";
    m_version = 2;
    m_redirectUri = "";
    m_appKey = "";
    m_appSecret = "";
    m_accessToken = "";
    m_downloadsFolder = QDir::currentPath() + "/downloads";
    m_readBufferSize = 5242880; // 5MB
    generateFullUrl();
    generateFullContentUrl();
}

void QDropbox::generateFullUrl() {
    m_fullUrl = QString(m_url).append("/").append(QString::number(m_version));
}

void QDropbox::generateFullContentUrl() {
    m_fullContentUrl = QString(m_contentUrl).append("/").append(QString::number(m_version));
}

QString QDropbox::getFilename(const QString& path) {
    return path.split("/").last();
}

QNetworkRequest QDropbox::prepareRequest(const QString& apiMethod) {
    QUrl url(m_fullUrl + apiMethod);

    QNetworkRequest req;
    req.setUrl(url);
    req.setRawHeader("Authorization", QString("Bearer ").append(m_accessToken).toUtf8());
    req.setRawHeader("Content-Type", "application/json");

    logger.debug(url);

    return req;
}

QNetworkRequest QDropbox::prepareContentRequest(const QString& apiMethod, const bool& log) {
    QUrl url(m_fullContentUrl + apiMethod);

    QNetworkRequest req;
    req.setUrl(url);
    req.setRawHeader("Authorization", QString("Bearer ").append(m_accessToken).toUtf8());
    req.setRawHeader("Content-Type", "application/octet-stream");

    if (log) {
        logger.debug(url);
    }

    return req;
}

QNetworkReply* QDropbox::getReply() {
    return qobject_cast<QNetworkReply*>(QObject::sender());
}

QNetworkReply* QDropbox::moveFile(const QString& fromPath, const QString& toPath, const bool& allowSharedFolder, const bool& autorename, const bool& allowOwnershipTransfer) {
    QNetworkRequest req = prepareRequest("/files/move_v2");
    QVariantMap map;
    map["from_path"] = fromPath;
    map["to_path"] = toPath;
    map["allow_shared_folder"] = allowSharedFolder;
    map["autorename"] = autorename;
    map["allow_ownership_transfer"] = allowOwnershipTransfer;

    QJson::Serializer serializer;
    return m_network.post(req, serializer.serialize(map));
}

void QDropbox::processUploadsQueue() {
    QDropboxUpload& uploadObj = m_uploads.head();
    if (uploadObj.getSize() == 0) {
        uploadObj.resize();
    }
    if (uploadObj.getSize() <= DROPBOX_UPLOAD_SIZE) {
        QFile* file = new QFile(uploadObj.getPath());
        upload(file, uploadObj.getRemotePath());
    } else {
        if (uploadObj.isNew()) {
            uploadObj.setUploadSize(UPLOAD_SIZE);
            uploadSessionStart(uploadObj.getRemotePath(), uploadObj.next());
        } else {
            qint64 offset = uploadObj.getOffset();
            if (uploadObj.lastPortion()) {
                uploadSessionFinish(uploadObj.getSessionId(), uploadObj.next(), offset, uploadObj.getRemotePath());
            } else {
                uploadSessionAppend(uploadObj.getSessionId(), uploadObj.next(), offset);
            }
        }
    }
}

void QDropbox::dequeue(QDropboxFile* file) {
    if (file != 0) {
        logger.info("File uploaded: " + file->getPathDisplay());
        logger.info("File size: " + QString::number(file->getSize()));
        file->deleteLater();
    }

    if (m_uploads.size()) {
        m_uploads.dequeue();
        logger.debug("upload dequeued");
    }

    if (m_uploads.size()) {
        processUploadsQueue();
    }
}
