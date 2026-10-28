// decodeFlac on Linux (flac.hpp): FFmpeg reads what Media Foundation reads on Windows (FLAC, MP3,
// WAV, M4A, WMA) and gives the same result, 16-bit interleaved PCM at the file's own rate and
// channel count, with a plain WAVEFORMATEX.
#include "flac.hpp"
#include "wave_format.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cstring>
#include <memory>

namespace {
struct Close {
    void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
    void operator()(SwrContext* p) const { swr_free(&p); }
    void operator()(AVPacket* p) const { av_packet_free(&p); }
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};
template <class T> using Owned = std::unique_ptr<T, Close>;
}

AudioClip decodeFlac(const std::filesystem::path& path, size_t max_bytes, const std::function<bool()>& cancelled) {
    av_log_set_level(AV_LOG_ERROR); // Not the decoders' notes on recoverable quirks (MP3 padding, ...).
    AudioClip clip;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) return clip;
    AVFormatContext* opened = nullptr;
    if (avformat_open_input(&opened, path.c_str(), nullptr, nullptr) < 0) return {};
    Owned<AVFormatContext> input(opened);
    if (avformat_find_stream_info(input.get(), nullptr) < 0) return {};
    const AVCodec* codec = nullptr;
    const int index = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (index < 0 || !codec) return {};
    Owned<AVCodecContext> decoder(avcodec_alloc_context3(codec));
    if (!decoder || avcodec_parameters_to_context(decoder.get(), input->streams[index]->codecpar) < 0 ||
        avcodec_open2(decoder.get(), codec, nullptr) < 0) return {};
    const int rate = decoder->sample_rate, channels = decoder->ch_layout.nb_channels;
    if (rate <= 0 || channels <= 0 || channels > 8) return {};
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, channels);
    SwrContext* converter = nullptr;
    if (swr_alloc_set_opts2(&converter, &layout, AV_SAMPLE_FMT_S16, rate, &decoder->ch_layout, decoder->sample_fmt, rate, 0, nullptr) < 0) return {};
    Owned<SwrContext> resampler(converter);
    if (swr_init(resampler.get()) < 0) return {};

    WAVEFORMATEX wave{};
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = static_cast<uint16_t>(channels);
    wave.nSamplesPerSec = static_cast<uint32_t>(rate);
    wave.wBitsPerSample = 16;
    wave.nBlockAlign = static_cast<uint16_t>(channels * 2);
    wave.nAvgBytesPerSec = wave.nSamplesPerSec * wave.nBlockAlign;
    clip.format.resize(sizeof(WAVEFORMATEX));
    std::memcpy(clip.format.data(), &wave, sizeof(WAVEFORMATEX));

    Owned<AVPacket> packet(av_packet_alloc());
    Owned<AVFrame> frame(av_frame_alloc());
    if (!packet || !frame) return {};
    // Converts one decoded frame (or, with none, what the resampler still holds); false: over the limit.
    const auto append = [&](const AVFrame* source) {
        const int in = source ? source->nb_samples : 0;
        const int room = swr_get_out_samples(resampler.get(), in);
        if (room <= 0) return true;
        const size_t bytes = static_cast<size_t>(room) * wave.nBlockAlign, at = clip.samples.size();
        clip.samples.resize(at + bytes);
        uint8_t* out = clip.samples.data() + at;
        const int done = swr_convert(resampler.get(), &out, room,
                                     source ? const_cast<const uint8_t**>(source->extended_data) : nullptr, in);
        if (done < 0) return false;
        clip.samples.resize(at + static_cast<size_t>(done) * wave.nBlockAlign);
        return clip.samples.size() <= max_bytes;
    };
    const auto drain = [&] {
        for (;;) {
            const int got = avcodec_receive_frame(decoder.get(), frame.get());
            if (got == AVERROR(EAGAIN) || got == AVERROR_EOF) return true;
            if (got < 0) return false;
            const bool fits = append(frame.get());
            av_frame_unref(frame.get());
            if (!fits) return false;
        }
    };
    for (;;) {
        if (cancelled && cancelled()) return {};
        const int read = av_read_frame(input.get(), packet.get());
        if (read == AVERROR_EOF) break;
        if (read < 0) return {};
        if (packet->stream_index != index) { av_packet_unref(packet.get()); continue; }
        const int sent = avcodec_send_packet(decoder.get(), packet.get());
        av_packet_unref(packet.get());
        if (sent < 0 && sent != AVERROR_INVALIDDATA) return {}; // A damaged frame is skipped, as Media Foundation does.
        if (!drain()) return {};
    }
    if (avcodec_send_packet(decoder.get(), nullptr) < 0 || !drain() || !append(nullptr)) return {};
    return clip.samples.empty() ? AudioClip{} : std::move(clip);
}
