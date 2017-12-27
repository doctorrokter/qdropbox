/*
 * QDropboxUpload.hpp
 *
 *  Created on: Dec 27, 2017
 *      Author: doctorrokter
 */

#ifndef QDROPBOXUPLOAD_HPP_
#define QDROPBOXUPLOAD_HPP_

#include <QObject>
#include <QByteArray>

class QDropboxUpload : public QObject {
    Q_OBJECT
public:
    QDropboxUpload(const QString& path, const QString& remotePath, QObject* parent = 0);
    QDropboxUpload(const QDropboxUpload& upload);
    virtual ~QDropboxUpload();

    QDropboxUpload& operator=(const QDropboxUpload& upload);

    const qint64& getOffset() const;
    const qint64& getSize() const;
    const QString& getPath() const;
    const QString& getRemotePath() const;
    const QString& getSessionId() const;
    QDropboxUpload& setSessionId(const QString& sessionId);
    const qint64& getUploadSize() const;
    QDropboxUpload& setUploadSize(const qint64& uploadSize);

    void resize();
    void increment();
    bool isNew();
    bool started();
    bool lastPortion();
    QByteArray next();

private:
    qint64 m_offset;
    qint64 m_size;
    qint64 m_uploadSize;
    QString m_path;
    QString m_remotePath;
    QString m_sessionId;

    QDropboxUpload& swap(const QDropboxUpload& upload);
};

#endif /* QDROPBOXUPLOAD_HPP_ */
