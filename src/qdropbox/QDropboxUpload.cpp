/*
 * QDropboxUpload.cpp
 *
 *  Created on: Dec 27, 2017
 *      Author: doctorrokter
 */

#include "../../include/qdropbox/QDropboxUpload.hpp"
#include <QFile>
#include "../../include/qdropbox/QDropbox.hpp"

QDropboxUpload::QDropboxUpload(const QString& path, const QString& remotePath, QObject* parent) : QObject(parent), m_offset(0), m_size(0), m_uploadSize(0), m_path(path), m_remotePath(remotePath), m_sessionId("") {}

QDropboxUpload::QDropboxUpload(const QDropboxUpload& upload) : QObject(upload.parent()) {
    swap(upload);
}

QDropboxUpload::~QDropboxUpload() {}

QDropboxUpload& QDropboxUpload::operator =(const QDropboxUpload& upload) {
    return swap(upload);
}

const qint64& QDropboxUpload::getOffset() const { return m_offset; }

const qint64& QDropboxUpload::getSize() const { return m_size; }

const QString& QDropboxUpload::getPath() const { return m_path; }

const QString& QDropboxUpload::getRemotePath() const { return m_remotePath; }

const QString& QDropboxUpload::getSessionId() const { return m_sessionId; }

QDropboxUpload& QDropboxUpload::setSessionId(const QString& sessionId) {
    m_sessionId = sessionId;
    return *this;
}

const qint64& QDropboxUpload::getUploadSize() const { return m_uploadSize; }

QDropboxUpload& QDropboxUpload::setUploadSize(const qint64& uploadSize) {
    m_uploadSize = uploadSize;
    return *this;
}

QDropboxUpload& QDropboxUpload::swap(const QDropboxUpload& upload) {
    m_offset = upload.getOffset();
    m_path = upload.getPath();
    m_remotePath = upload.getRemotePath();
    m_sessionId = upload.getSessionId();
    resize();
    return *this;
}

void QDropboxUpload::resize() {
    QFile file(m_path);
    m_size = file.size();
}

void QDropboxUpload::increment() {
    m_offset += m_uploadSize;
}

bool QDropboxUpload::isNew() {
    return m_sessionId.isEmpty();
}

bool QDropboxUpload::started() {
    return !m_sessionId.isEmpty();
}

bool QDropboxUpload::lastPortion() {
    return m_size <= (m_offset + m_uploadSize);
}

QByteArray QDropboxUpload::next() {
    QByteArray data;
    QFile file(m_path);
    bool res = file.open(QIODevice::ReadOnly);
    if (res) {
        if (m_offset == 0) {
            data = file.read(m_uploadSize);
        } else {
            file.seek(m_offset);
            data = file.read(m_uploadSize);
        }
        file.close();
    } else {
        qDebug() << "File didn't opened!!!" << endl;
    }
    return data;
}

