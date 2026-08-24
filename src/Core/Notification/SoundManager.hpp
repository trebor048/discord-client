#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBuffer>
#include <QUrl>
#include <QHash>
#include <QList>
#include <QBuffer>
#include <QByteArray>
#include <QMutex>

#include "NotificationTypes.hpp"

namespace Acheron {
namespace Core {

class SoundManager : public QObject
{
    Q_OBJECT
public:
    explicit SoundManager(QObject *parent = nullptr);
    ~SoundManager() override;

    void initialize();
    void shutdown();

    void playNotificationSound(const QString &soundId, int volume = 100);
    void playCustomSound(const QByteArray &audioData, const QString &format, int volume = 100);
    void playUrl(const QUrl &url, int volume = 100);

    void setGlobalVolume(int volume);
    int globalVolume() const;

    void cacheSound(const QString &soundId, const QByteArray &data, const QString &format);
    bool hasCachedSound(const QString &soundId) const;
    void clearCache();
    void playCachedSound(const QString &soundId, int volume = 100);

    // Built-in sound IDs
    static constexpr const char *DefaultNotification = "notification_default";
    static constexpr const char *Message1 = "message1";
    static constexpr const char *Message2 = "message2";
    static constexpr const char *Message3 = "message3";
    static constexpr const char *Mention1 = "mention1";
    static constexpr const char *Mention2 = "mention2";
    static constexpr const char *Mention3 = "mention3";

signals:
    void soundPlayed(const QString &soundId);
    void errorOccurred(const QString &error);

private:
    struct CachedSound {
        QByteArray data;
        QString format;
    };

    /// One bell-like partial in a generated chime.
    struct ChimeNote {
        double frequency;  // Hz
        double start;      // seconds from chime start
        double duration;   // seconds the partial rings for
        double amplitude;  // 0..1, relative peak
    };

    void ensurePlayer();
    QUrl getSoundUrl(const QString &soundId) const;
    void playFromBuffer(const QByteArray &data, const QString &format, int volume);
    void generateBuiltinSounds();
    void generateChime(const QString &soundId, const QList<ChimeNote> &notes, double masterVolume);
    QByteArray audioBufferToWav(const QAudioBuffer& buffer);

    mutable QMutex m_cacheMutex;
    QHash<QString, CachedSound> m_soundCache;
    QHash<QString, QByteArray> m_soundBuffers;  // Pre-generated WAV data

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QBuffer *m_currentBuffer = nullptr;
    int m_globalVolume = 100;
    bool m_initialized = false;
};

} // namespace Core
} // namespace Acheron
