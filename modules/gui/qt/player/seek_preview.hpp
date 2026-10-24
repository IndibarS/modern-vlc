// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_SEEK_PREVIEW_HPP
#define VLC_QT_SEEK_PREVIEW_HPP

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include "maininterface/mainctx.hpp"
#include <vlc_preparser.h>

// One bounded, asynchronous VLC thumbnail request per visible seekbar.
class SeekPreview : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MainCtx* ctx MEMBER m_ctx)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(bool available READ available WRITE setAvailable NOTIFY availableChanged)
    Q_PROPERTY(double imagePosition READ imagePosition NOTIFY imageChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QUrl image READ image NOTIFY imageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit SeekPreview(QObject *parent = nullptr);
    ~SeekPreview() override;
    QUrl source() const { return m_source; }
    int duration() const { return m_duration; }
    bool available() const { return m_available; }
    double imagePosition() const { return m_imageBucket < 0 ? m_position : m_imageBucket / 1000.; }
    void setDuration(int duration);
    void setAvailable(bool available);
    double position() const { return m_position; }
    bool active() const { return m_active; }
    QUrl image() const { return m_image; }
    bool busy() const { return m_request || m_delay.isActive(); }
    void setSource(const QUrl &source);
    void setPosition(double position);
    void setActive(bool active);
signals:
    void sourceChanged();
    void durationChanged();
    void availableChanged();
    void positionChanged();
    void activeChanged();
    void imageChanged();
    void busyChanged();
private:
    void schedule();
    void loadCache();
    QString cachePath(int bucket) const;
    void updateImage();
    int nextBucket() const;
    void generate();
    void completed(bool success, bool cancelled);
    static void onEnded(vlc_preparser_req *, int, const bool *, size_t, void *);
    QPointer<MainCtx> m_ctx;
    QUrl m_source, m_image;
    double m_position = 0;
    int m_duration = 0;
    bool m_active = false, m_available = false;
    bool m_pendingBackground = false;
    QTimer m_delay;
    QString m_directory, m_cacheKey;
    QSet<int> m_attempted;
    int m_imageBucket = -1;
    QMap<int, QString> m_cache;
    vlc_preparser_t *m_thumbnailer = nullptr;
    vlc_preparser_req *m_request = nullptr;
    unsigned m_generation = 0, m_pendingGeneration = 0;
    int m_pendingBucket = -1;
    QString m_pendingPath;
    // The external preparser borrows output paths until request completion.
    QByteArray m_pendingEncodedPath;
};
#endif
