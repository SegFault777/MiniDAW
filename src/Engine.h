#pragma once
#include <QColor>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class QAudioSink;
class QIODevice;

constexpr int kSampleRate = 44100;
constexpr int kPeakStride = 256;   // 파형 요약 단위(프레임)
constexpr int kDrumRows = 4;       // 킥, 스네어, 하이햇, 클랩
constexpr int kSteps = 16;
using DrumPattern = std::array<std::array<bool, kSteps>, kDrumRows>;

struct AudioData {
    QString name;
    std::vector<float> pcm;    // 스테레오 인터리브 float
    std::vector<float> peaks;  // kPeakStride 프레임당 최대 진폭
    qint64 frames() const { return qint64(pcm.size() / 2); }
};

struct Clip {
    std::shared_ptr<const AudioData> audio;
    qint64 start = 0;   // 타임라인 위치(프레임)
    qint64 offset = 0;  // 오디오 내 시작 오프셋
    qint64 length = 0;
};

struct Track {
    QString name;
    QColor color;
    float volume = 0.8f;
    float pan = 0.f;  // -1 ~ 1
    bool mute = false, solo = false, drum = false;
    DrumPattern steps{};
    std::vector<Clip> clips;
    float peakL = 0, peakR = 0;
};

class Engine;

// 별도 오디오 스레드에서 QAudioSink를 구동 (UI가 바빠도 소리가 끊기지 않게)
class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(Engine* e) : m_e(e) {}
public slots:
    void start();
    void stop();
signals:
    void failed(const QString& reason);
private:
    Engine* m_e;
    QAudioSink* m_sink = nullptr;
    QIODevice* m_dev = nullptr;
};

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject* parent = nullptr);
    ~Engine() override;

    // --- 모델 (UI 스레드) ---
    std::vector<Track> snapshot() const;
    int trackCount() const;
    DrumPattern drumPattern() const;
    int addAudioTrack(const QString& name = {});
    void removeTrack(int i);
    void renameTrack(int i, const QString& n);
    void setVolume(int i, float v);
    void setPan(int i, float p);
    void setMute(int i, bool on);
    void setSolo(int i, bool on);
    void setMasterVolume(float v) { m_master = v; }
    void importAudio(const QString& path, int track, qint64 startFrame);
    void moveClip(int t, int c, qint64 start);
    void removeClip(int t, int c);
    void splitClip(int t, int c, qint64 at);
    void setStep(int row, int step, bool on);
    void setDrumPattern(const DrumPattern& p);
    qint64 projectEnd() const;

    // --- 트랜스포트 ---
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return m_playing; }
    void seek(qint64 f) { m_pos = std::max<qint64>(0, f); }
    qint64 position() const { return m_pos; }
    void setBpm(double b);
    double bpm() const { return m_bpm; }
    double framesPerBeat() const { return kSampleRate * 60.0 / m_bpm; }
    void setMetronome(bool on) { m_metro = on; }
    void setLoop(bool on, qint64 a = -1, qint64 b = -1);
    bool loopOn() const { return m_loopOn; }
    qint64 loopA() const { return m_loopA; }
    qint64 loopB() const { return m_loopB; }
    void masterPeaks(float& l, float& r) const;
    bool exportWav(const QString& path, QString* error);

    // 오디오 스레드에서 호출
    void fillInt16(qint16* out, int frames);

signals:
    void tracksChanged();
    void clipsChanged();
    void loopChanged();
    void message(const QString& text);

private:
    void renderBlock(qint64 pos, int frames, float* out);  // m_mutex 잠금 상태에서 호출
    void synthDrums();

    mutable QMutex m_mutex;
    std::vector<Track> m_tracks;
    std::array<std::vector<float>, kDrumRows> m_drum;
    qint64 m_drumMax = 0;
    std::vector<float> m_scratch;
    float m_masterL = 0, m_masterR = 0;

    std::atomic<qint64> m_pos{0};
    std::atomic<double> m_bpm{120.0};
    std::atomic<bool> m_playing{false}, m_metro{false}, m_loopOn{false};
    std::atomic<qint64> m_loopA{0}, m_loopB{kSampleRate * 8};
    std::atomic<float> m_master{0.9f};

    QThread m_thread;
    AudioOutput* m_out = nullptr;
};
