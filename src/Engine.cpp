#include "Engine.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaDevices>
#include <QUrl>
#include <cmath>
#include <random>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr qint64 kClickLen = 1400;

// 풀 모드 QIODevice: 싱크가 읽을 때마다 엔진이 믹싱
class EngineDevice : public QIODevice {
public:
    EngineDevice(Engine* e, QObject* parent) : QIODevice(parent), m_e(e) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* d, qint64 maxLen) override {
        const int frames = int(maxLen / 4);
        if (frames <= 0) return 0;
        m_e->fillInt16(reinterpret_cast<qint16*>(d), frames);
        return qint64(frames) * 4;
    }
    qint64 writeData(const char*, qint64) override { return -1; }
private:
    Engine* m_e;
};

// 디코더가 준 버퍼를 44.1kHz 스테레오 float로 변환해 이어 붙임
void appendBuffer(AudioData& d, const QAudioBuffer& b) {
    if (!b.isValid() || b.frameCount() <= 0) return;
    const QAudioFormat f = b.format();
    const int ch = std::max(1, f.channelCount());
    const int n = b.frameCount();
    std::vector<float> in(size_t(n) * 2);
    auto fill = [&](const auto* p, auto toFloat) {
        for (int i = 0; i < n; ++i) {
            in[size_t(2 * i)] = toFloat(p[i * ch]);
            in[size_t(2 * i + 1)] = toFloat(p[i * ch + (ch > 1 ? 1 : 0)]);
        }
    };
    switch (f.sampleFormat()) {
    case QAudioFormat::UInt8: fill(b.constData<quint8>(), [](quint8 v) { return (int(v) - 128) / 128.f; }); break;
    case QAudioFormat::Int16: fill(b.constData<qint16>(), [](qint16 v) { return v / 32768.f; }); break;
    case QAudioFormat::Int32: fill(b.constData<qint32>(), [](qint32 v) { return float(v / 2147483648.0); }); break;
    case QAudioFormat::Float: fill(b.constData<float>(), [](float v) { return v; }); break;
    default: return;
    }
    const int sr = f.sampleRate();
    if (sr == kSampleRate || sr <= 0) {
        d.pcm.insert(d.pcm.end(), in.begin(), in.end());
        return;
    }
    const double ratio = double(sr) / kSampleRate;  // 선형 보간 리샘플
    const int outN = int(n / ratio);
    for (int i = 0; i < outN; ++i) {
        const double x = i * ratio;
        const int i0 = int(x), i1 = std::min(i0 + 1, n - 1);
        const float fr = float(x - i0);
        for (int c = 0; c < 2; ++c)
            d.pcm.push_back(in[size_t(2 * i0 + c)] * (1.f - fr) + in[size_t(2 * i1 + c)] * fr);
    }
}
}  // namespace

// ---------------------------------------------------------------- AudioOutput
void AudioOutput::start() {
    stop();
    QAudioFormat f;
    f.setSampleRate(kSampleRate);
    f.setChannelCount(2);
    f.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(f)) {
        emit failed(QStringLiteral("사용 가능한 오디오 출력 장치를 찾지 못했습니다."));
        return;
    }
    m_dev = new EngineDevice(m_e, this);
    m_sink = new QAudioSink(dev, f, this);
    m_sink->setBufferSize(2048 * 4);  // 약 46ms
    m_sink->start(m_dev);
}

void AudioOutput::stop() {
    if (m_sink) { m_sink->stop(); delete m_sink; m_sink = nullptr; }
    delete m_dev; m_dev = nullptr;
}

// --------------------------------------------------------------------- Engine
Engine::Engine(QObject* parent) : QObject(parent) {
    synthDrums();
    Track d;
    d.name = QStringLiteral("드럼 머신");
    d.color = QColor(255, 170, 60);
    d.drum = true;
    for (int s : {0, 4, 8, 12}) d.steps[0][size_t(s)] = true;
    for (int s : {4, 12}) d.steps[1][size_t(s)] = true;
    for (int s : {2, 6, 10, 14}) d.steps[2][size_t(s)] = true;
    m_tracks.push_back(d);

    m_out = new AudioOutput(this);
    m_out->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_out, &QObject::deleteLater);
    connect(m_out, &AudioOutput::failed, this, [this](const QString& s) { m_playing = false; emit message(s); });
    m_thread.start();
}

Engine::~Engine() {
    QMetaObject::invokeMethod(m_out, &AudioOutput::stop, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void Engine::synthDrums() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> nd(-1.f, 1.f);
    const double sr = kSampleRate;
    { // 킥
        auto& v = m_drum[0]; v.resize(size_t(0.40 * sr)); double ph = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            ph += 2 * kPi * (42 + 120 * std::exp(-t * 30)) / sr;
            v[i] = float(std::sin(ph) * std::exp(-t * 7.5) * 0.95);
        }
    }
    { // 스네어
        auto& v = m_drum[1]; v.resize(size_t(0.25 * sr));
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            v[i] = float(nd(rng) * std::exp(-t * 20) * 0.6 + std::sin(2 * kPi * 185 * t) * std::exp(-t * 30) * 0.5);
        }
    }
    { // 하이햇
        auto& v = m_drum[2]; v.resize(size_t(0.07 * sr)); float prev = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr; const float n = nd(rng);
            v[i] = (n - prev) * 0.5f * float(std::exp(-t * 70)); prev = n;
        }
    }
    { // 클랩
        auto& v = m_drum[3]; v.resize(size_t(0.22 * sr));
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr; double env = std::exp(-t * 22);
            if (t < 0.035) env *= (std::fmod(t, 0.012) < 0.005 ? 1.0 : 0.3);
            v[i] = float(nd(rng) * env * 0.55);
        }
    }
    for (auto& v : m_drum) m_drumMax = std::max<qint64>(m_drumMax, qint64(v.size()));
}

// ----- 모델 -----
std::vector<Track> Engine::snapshot() const { QMutexLocker l(&m_mutex); return m_tracks; }
int Engine::trackCount() const { QMutexLocker l(&m_mutex); return int(m_tracks.size()); }
DrumPattern Engine::drumPattern() const { QMutexLocker l(&m_mutex); return m_tracks[0].steps; }

int Engine::addAudioTrack(const QString& name) {
    static const QColor pal[] = {QColor(90, 170, 255), QColor(120, 210, 120), QColor(230, 110, 170),
                                 QColor(180, 140, 255), QColor(80, 210, 210), QColor(240, 210, 90)};
    int idx;
    {
        QMutexLocker l(&m_mutex);
        Track t;
        t.name = name.isEmpty() ? tr("오디오 %1").arg(m_tracks.size()) : name;
        t.color = pal[m_tracks.size() % 6];
        m_tracks.push_back(t);
        idx = int(m_tracks.size()) - 1;
    }
    emit tracksChanged();
    return idx;
}

void Engine::removeTrack(int i) {
    {
        QMutexLocker l(&m_mutex);
        if (i < 1 || i >= int(m_tracks.size())) return;  // 0번(드럼)은 삭제 불가
        m_tracks.erase(m_tracks.begin() + i);
    }
    emit tracksChanged();
}

void Engine::renameTrack(int i, const QString& n) {
    { QMutexLocker l(&m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; m_tracks[size_t(i)].name = n; }
    emit tracksChanged();
}

#define TRACK_SET(expr) { QMutexLocker l(&m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; Track& t = m_tracks[size_t(i)]; expr; }
void Engine::setVolume(int i, float v) { TRACK_SET(t.volume = std::clamp(v, 0.f, 1.5f)) }
void Engine::setPan(int i, float p) { TRACK_SET(t.pan = std::clamp(p, -1.f, 1.f)) }
void Engine::setMute(int i, bool on) { TRACK_SET(t.mute = on) }
void Engine::setSolo(int i, bool on) { TRACK_SET(t.solo = on) }
#undef TRACK_SET

void Engine::setStep(int row, int step, bool on) {
    if (row < 0 || row >= kDrumRows || step < 0 || step >= kSteps) return;
    QMutexLocker l(&m_mutex);
    m_tracks[0].steps[size_t(row)][size_t(step)] = on;
}

void Engine::setDrumPattern(const DrumPattern& p) { QMutexLocker l(&m_mutex); m_tracks[0].steps = p; }

void Engine::moveClip(int t, int c, qint64 start) {
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || c < 0 || c >= int(m_tracks[size_t(t)].clips.size())) return;
        m_tracks[size_t(t)].clips[size_t(c)].start = std::max<qint64>(0, start);
    }
    emit clipsChanged();
}

void Engine::removeClip(int t, int c) {
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || c < 0 || c >= int(m_tracks[size_t(t)].clips.size())) return;
        auto& cl = m_tracks[size_t(t)].clips;
        cl.erase(cl.begin() + c);
    }
    emit clipsChanged();
}

void Engine::splitClip(int t, int c, qint64 at) {
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || c < 0 || c >= int(m_tracks[size_t(t)].clips.size())) return;
        auto& cl = m_tracks[size_t(t)].clips;
        const Clip a = cl[size_t(c)];
        const qint64 rel = at - a.start;
        if (rel <= 0 || rel >= a.length) return;
        cl[size_t(c)].length = rel;
        cl.insert(cl.begin() + c + 1, Clip{a.audio, at, a.offset + rel, a.length - rel});
    }
    emit clipsChanged();
}

qint64 Engine::projectEnd() const {
    QMutexLocker l(&m_mutex);
    qint64 e = 0;
    for (const Track& t : m_tracks) for (const Clip& c : t.clips) e = std::max(e, c.start + c.length);
    return e;
}

void Engine::importAudio(const QString& path, int track, qint64 startFrame) {
    auto* dec = new QAudioDecoder(this);
    auto data = std::make_shared<AudioData>();
    data->name = QFileInfo(path).completeBaseName();
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    dec->setAudioFormat(fmt);
    dec->setSource(QUrl::fromLocalFile(path));

    connect(dec, &QAudioDecoder::bufferReady, this, [dec, data]() {
        while (dec->bufferAvailable()) appendBuffer(*data, dec->read());
    });
    connect(dec, &QAudioDecoder::finished, this, [=, this]() {
        dec->deleteLater();
        if (data->frames() == 0) { emit message(tr("파일을 읽지 못했습니다: %1").arg(data->name)); return; }
        data->peaks.assign(size_t((data->frames() + kPeakStride - 1) / kPeakStride), 0.f);
        for (qint64 i = 0; i < data->frames(); ++i) {
            float& pk = data->peaks[size_t(i / kPeakStride)];
            pk = std::max({pk, std::fabs(data->pcm[size_t(2 * i)]), std::fabs(data->pcm[size_t(2 * i + 1)])});
        }
        int t = track;
        if (t < 1 || t >= trackCount()) t = addAudioTrack(data->name);
        {
            QMutexLocker l(&m_mutex);
            m_tracks[size_t(t)].clips.push_back(Clip{data, std::max<qint64>(0, startFrame), 0, data->frames()});
        }
        emit clipsChanged();
    });
    connect(dec, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this, [=, this](QAudioDecoder::Error) {
        emit message(tr("가져오기 실패: %1").arg(dec->errorString()));
        dec->deleteLater();
    });
    dec->start();
}

// ----- 트랜스포트 -----
void Engine::play() {
    if (m_playing) return;
    m_playing = true;
    QMetaObject::invokeMethod(m_out, &AudioOutput::start, Qt::QueuedConnection);
}

void Engine::pause() {
    if (!m_playing) return;
    m_playing = false;
    QMetaObject::invokeMethod(m_out, &AudioOutput::stop, Qt::BlockingQueuedConnection);
}

void Engine::stop() {
    pause();
    m_pos = m_loopOn ? m_loopA.load() : 0;
}

void Engine::setBpm(double b) {
    b = std::clamp(b, 30.0, 300.0);
    const double old = m_bpm;
    if (old == b) return;
    { QMutexLocker l(&m_mutex); m_bpm = b; }
    m_loopA = qint64(std::llround(m_loopA * old / b));  // 루프는 박자 기준 유지
    m_loopB = qint64(std::llround(m_loopB * old / b));
    emit loopChanged();
}

void Engine::setLoop(bool on, qint64 a, qint64 b) {
    if (a >= 0 && b > a) { m_loopA = a; m_loopB = b; }
    m_loopOn = on;
    emit loopChanged();
}

void Engine::masterPeaks(float& l, float& r) const { QMutexLocker lk(&m_mutex); l = m_masterL; r = m_masterR; }

// ----- 렌더링 (상태 비의존: 위치만 주면 그 구간을 계산) -----
void Engine::renderBlock(qint64 pos, int frames, float* out) {
    std::fill(out, out + size_t(frames) * 2, 0.f);
    const bool anySolo = std::any_of(m_tracks.begin(), m_tracks.end(), [](const Track& t) { return t.solo; });
    const double fpb = framesPerBeat();
    const double stepLen = fpb / 4.0;

    for (Track& t : m_tracks) {
        if (t.mute || (anySolo && !t.solo)) { t.peakL = t.peakR = 0; continue; }
        const float ang = (t.pan + 1.f) * 0.25f * float(kPi);
        const float gl = t.volume * std::cos(ang) * 1.41421f;
        const float gr = t.volume * std::sin(ang) * 1.41421f;
        float pl = 0, pr = 0;
        auto add = [&](qint64 f, float l, float r) {
            l *= gl; r *= gr;
            out[(f - pos) * 2] += l; out[(f - pos) * 2 + 1] += r;
            pl = std::max(pl, std::fabs(l)); pr = std::max(pr, std::fabs(r));
        };
        if (t.drum) {
            const qint64 first = std::max<qint64>(0, qint64((pos - m_drumMax) / stepLen) - 1);
            const qint64 last = qint64((pos + frames) / stepLen);
            for (qint64 k = first; k <= last; ++k) {
                const size_t s = size_t(k % kSteps);
                const qint64 st = qint64(std::llround(k * stepLen));
                for (int r = 0; r < kDrumRows; ++r) {
                    if (!t.steps[size_t(r)][s]) continue;
                    const auto& snd = m_drum[size_t(r)];
                    const qint64 a = std::max(pos, st), b = std::min<qint64>(pos + frames, st + qint64(snd.size()));
                    for (qint64 f = a; f < b; ++f) { const float v = snd[size_t(f - st)]; add(f, v, v); }
                }
            }
        } else {
            for (const Clip& c : t.clips) {
                const qint64 a = std::max(pos, c.start), b = std::min(pos + frames, c.start + c.length);
                if (a >= b || !c.audio) continue;
                const float* s = c.audio->pcm.data() + (c.offset + a - c.start) * 2;
                for (qint64 f = a; f < b; ++f, s += 2) add(f, s[0], s[1]);
            }
        }
        t.peakL = pl; t.peakR = pr;
    }

    if (m_metro) {
        const qint64 first = std::max<qint64>(0, qint64((pos - kClickLen) / fpb) - 1);
        const qint64 last = qint64((pos + frames) / fpb);
        for (qint64 k = first; k <= last; ++k) {
            const qint64 st = qint64(std::llround(k * fpb));
            const qint64 a = std::max(pos, st), b = std::min(pos + frames, st + kClickLen);
            const double freq = (k % 4 == 0) ? 1600.0 : 1000.0;
            for (qint64 f = a; f < b; ++f) {
                const double n = double(f - st);
                const float v = float(std::sin(2 * kPi * freq * n / kSampleRate) * std::exp(-n / 250.0) * 0.35);
                out[(f - pos) * 2] += v; out[(f - pos) * 2 + 1] += v;
            }
        }
    }
}

void Engine::fillInt16(qint16* out, int frames) {
    QMutexLocker lk(&m_mutex);
    if (m_scratch.size() < size_t(frames) * 2) m_scratch.resize(size_t(frames) * 2);
    qint64 pos = m_pos;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (m_loopOn && m_loopB > m_loopA) {
            if (pos >= m_loopB) pos = m_loopA;
            n = int(std::min<qint64>(n, m_loopB - pos));
        }
        renderBlock(pos, n, m_scratch.data() + size_t(done) * 2);
        pos += n; done += n;
    }
    m_pos = pos;
    const float g = m_master;
    float ml = 0, mr = 0;
    for (int i = 0; i < frames; ++i) {
        const float l = m_scratch[size_t(2 * i)] * g, r = m_scratch[size_t(2 * i + 1)] * g;
        ml = std::max(ml, std::fabs(l)); mr = std::max(mr, std::fabs(r));
        out[2 * i] = qint16(std::clamp(l, -1.f, 1.f) * 32767.f);
        out[2 * i + 1] = qint16(std::clamp(r, -1.f, 1.f) * 32767.f);
    }
    m_masterL = ml; m_masterR = mr;
}

// ----- WAV 내보내기 (16bit/44.1kHz 스테레오) -----
bool Engine::exportWav(const QString& path, QString* error) {
    QMutexLocker lk(&m_mutex);
    qint64 end = 0;
    bool anyDrum = false;
    for (const Track& t : m_tracks) {
        for (const Clip& c : t.clips) end = std::max(end, c.start + c.length);
        if (t.drum) for (const auto& row : t.steps) for (bool b : row) anyDrum |= b;
    }
    if (anyDrum) end = std::max(end, qint64(framesPerBeat() * 16));  // 최소 4마디
    if (end <= 0) { if (error) *error = QStringLiteral("내보낼 내용이 없습니다."); return false; }
    end += kSampleRate / 2;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    auto w32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const quint32 dataBytes = quint32(end * 4);
    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVEfmt ", 8);
    w32(16); w16(1); w16(2); w32(kSampleRate); w32(kSampleRate * 4); w16(4); w16(16);
    f.write("data", 4); w32(dataBytes);

    constexpr int kBlock = 4096;
    std::vector<float> buf(size_t(kBlock) * 2);
    std::vector<qint16> pcm(size_t(kBlock) * 2);
    const float g = m_master;
    for (qint64 p = 0; p < end; p += kBlock) {
        const int n = int(std::min<qint64>(kBlock, end - p));
        renderBlock(p, n, buf.data());
        for (int i = 0; i < n * 2; ++i) pcm[size_t(i)] = qint16(std::clamp(buf[size_t(i)] * g, -1.f, 1.f) * 32767.f);
        f.write(reinterpret_cast<const char*>(pcm.data()), qint64(n) * 4);
    }
    return true;
}
