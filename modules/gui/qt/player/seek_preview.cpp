// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "seek_preview.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <cerrno>

SeekPreview::SeekPreview(QObject *parent) : QObject(parent)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(160);
    connect(&m_delay, &QTimer::timeout, this, &SeekPreview::generate);
}

SeekPreview::~SeekPreview()
{
    // Delete joins callbacks before QObject removes their queued deliveries.
    if (m_thumbnailer)
        vlc_preparser_Delete(m_thumbnailer);
    if (m_request)
        vlc_preparser_req_Release(m_request);
}

void SeekPreview::setSource(const QUrl &source)
{
    if (m_source == source)
        return;
    ++m_generation;
    m_source = source;
    if (m_request)
        vlc_preparser_Cancel(m_thumbnailer, m_request);
    loadCache();
    emit sourceChanged();
    schedule();
}

void SeekPreview::setDuration(int duration)
{
    if (m_duration == duration)
        return;
    m_duration = duration;
    emit durationChanged();
    schedule();
}

void SeekPreview::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    if (!available && m_request)
        vlc_preparser_Cancel(m_thumbnailer, m_request);
    emit availableChanged();
    schedule();
}

QString SeekPreview::cachePath(int bucket) const
{
    return QDir(m_directory).filePath(m_cacheKey + "-" + QString::number(bucket) + ".jpg");
}

void SeekPreview::loadCache()
{
    m_cache.clear();
    m_attempted.clear();
    m_image = QUrl();
    m_imageBucket = -1;
    m_directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/seek-previews";
    QDir().mkpath(m_directory);
    const QFileInfo file(m_source.toLocalFile());
    const QByteArray identity = m_source.toEncoded() + '\n' + QByteArray::number(file.size())
        + '\n' + QByteArray::number(file.lastModified().toMSecsSinceEpoch());
    m_cacheKey = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    QDir cacheDir(m_directory);
    // Bound the shared cache to 128 MiB, discarding oldest other-media files.
    const auto files = cacheDir.entryInfoList({"*.jpg"}, QDir::Files, QDir::Time);
    qint64 size = 0;
    for (const auto &entry : files)
    {
        size += entry.size();
        if (size > 128 * 1024 * 1024 && !entry.fileName().startsWith(m_cacheKey))
            QFile::remove(entry.absoluteFilePath());
    }
    if (m_source.isLocalFile())
        for (const auto &entry : files)
        {
            if (!entry.fileName().startsWith(m_cacheKey + "-") || entry.size() <= 0)
                continue;
            bool ok = false;
            const int bucket = entry.completeBaseName().mid(m_cacheKey.size() + 1).toInt(&ok);
            if (ok && bucket >= 0 && bucket < 1000)
                m_cache.insert(bucket, entry.absoluteFilePath());
        }
    emit imageChanged();
}

void SeekPreview::setPosition(double position)
{
    if (!std::isfinite(position))
        return;
    position = std::clamp(position, 0., .999);
    if (m_position == position)
        return;
    m_position = position;
    emit positionChanged();
    schedule();
}

void SeekPreview::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // Hover requests take priority over speculative background samples.
    if (active && m_request && m_pendingBackground
        && !m_cache.contains(int(m_position * 1000)))
        vlc_preparser_Cancel(m_thumbnailer, m_request);
    emit activeChanged();
    schedule();
}

void SeekPreview::updateImage()
{
    int nearest = -1;
    const int desired = int(m_position * 1000);
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it)
        if (nearest < 0 || std::abs(it.key() - desired) < std::abs(nearest - desired))
            nearest = it.key();
    const QUrl image = nearest < 0 ? QUrl() : QUrl::fromLocalFile(m_cache.value(nearest));
    if (image != m_image)
    {
        m_imageBucket = nearest;
        m_image = image;
        emit imageChanged();
    }
}

int SeekPreview::nextBucket() const
{
    const int desired = int(m_position * 1000);
    if (m_active && !m_cache.contains(desired) && !m_attempted.contains(desired))
        return desired;
    // Precompute 32 spread-out samples, coarse to fine, only for local files.
    if (!m_source.isLocalFile())
        return -1;
    for (int i = 0; i < 32; ++i)
    {
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < 5; ++bit)
            reversed = (reversed << 1) | ((i >> bit) & 1);
        const int bucket = reversed * 1000 / 32;
        if (!m_cache.contains(bucket) && !m_attempted.contains(bucket))
            return bucket;
    }
    return -1;
}

void SeekPreview::schedule()
{
    m_delay.stop();
    updateImage();
    if (m_available && m_duration > 0 && !m_source.isEmpty() && !m_request && nextBucket() >= 0)
        m_delay.start(m_active ? 60 : 1500);
    emit busyChanged();
}

void SeekPreview::generate()
{
    if (!m_ctx || !m_available || m_request || m_directory.isEmpty() || nextBucket() < 0)
    {
        emit busyChanged();
        return;
    }
    if (!m_thumbnailer)
    {
        vlc_preparser_cfg cfg{};
        cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES;
        cfg.max_thumbnailer_threads = 1;
        cfg.timeout = VLC_TICK_FROM_SEC(3);
#ifdef HAVE_VLC_EXTERNAL_PREPARSER
        // Isolate speculative decoding from the GUI/player process.
        cfg.external_process = true;
#endif
        m_thumbnailer = vlc_preparser_New(VLC_OBJECT(m_ctx->getIntf()), &cfg);
        if (!m_thumbnailer)
        {
            emit busyChanged();
            return;
        }
    }
    const QByteArray uri = m_source.toEncoded();
    input_item_t *item = input_item_New(uri.constData(), nullptr);
    if (!item)
    {
        emit busyChanged();
        return;
    }
    input_item_AddOption(item, "avcodec-threads=1", VLC_INPUT_OPTION_TRUSTED);
    m_pendingBackground = !m_active;
    m_pendingBucket = nextBucket();
    m_attempted.insert(m_pendingBucket);
    m_pendingGeneration = m_generation;
    m_pendingPath = cachePath(m_pendingBucket);
    m_pendingEncodedPath = QFile::encodeName(m_pendingPath);
    vlc_thumbnailer_arg arg{};
    arg.seek.type = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_TIME;
    arg.seek.time = VLC_TICK_FROM_SEC(m_duration) * m_pendingBucket / 1000;
    arg.seek.speed = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_PRECISE;
    vlc_thumbnailer_output output{};
    output.format = VLC_THUMBNAILER_FORMAT_JPEG;
    output.width = 240;
    output.height = 135;
    output.crop = false;
    output.file_path = m_pendingEncodedPath.constData();
    output.creat_mode = 0600;
    static const vlc_thumbnailer_to_files_cbs cbs { onEnded };
    m_request = vlc_preparser_req_NewThumbnailToFiles(m_thumbnailer, item, &arg,
                                                      &output, 1, &cbs, this);
    input_item_Release(item);
    if (!m_request || vlc_preparser_Submit(m_thumbnailer, m_request) != VLC_SUCCESS)
    {
        if (m_request)
            vlc_preparser_req_Release(m_request);
        m_request = nullptr;
        schedule();
    }
    emit busyChanged();
}

void SeekPreview::onEnded(vlc_preparser_req *, int status, const bool *results,
                          size_t count, void *data)
{
    auto *self = static_cast<SeekPreview *>(data);
    const bool success = status == VLC_SUCCESS && count > 0 && results && results[0];
    const bool cancelled = status == -EINTR;
    QMetaObject::invokeMethod(self, [self, success, cancelled] { self->completed(success, cancelled); },
                              Qt::QueuedConnection);
}

void SeekPreview::completed(bool success, bool cancelled)
{
    vlc_preparser_req_Release(m_request);
    m_request = nullptr;
    const bool current = m_pendingGeneration == m_generation;
    if (success && current)
    {
        // Bounded disk cache; revisiting a cached position avoids decoding again.
        if (m_cache.size() >= 96)
        {
            // Keep the 32 overview samples; evict the farthest detailed frame.
            // Forget its attempt so returning to that position can decode again.
            auto victim = m_cache.end();
            for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            {
                bool overview = false;
                for (int i = 0; i < 32; ++i)
                    overview |= it.key() == i * 1000 / 32;
                if (!overview && (victim == m_cache.end()
                    || std::abs(it.key() - m_pendingBucket) > std::abs(victim.key() - m_pendingBucket)))
                    victim = it;
            }
            if (victim != m_cache.end())
            {
                QFile::remove(victim.value());
                m_attempted.remove(victim.key());
                m_cache.erase(victim);
            }
        }
        m_cache.insert(m_pendingBucket, m_pendingPath);
    }
    else
        QFile::remove(m_pendingPath);
    if (cancelled && current)
        m_attempted.remove(m_pendingBucket);
    schedule();
}
