#include "AudioDecoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#ifdef HAVE_FFMPEG
extern "C" {
#  include <libavformat/avformat.h>
#  include <libavcodec/avcodec.h>
#  include <libavutil/opt.h>
#  include <libavutil/channel_layout.h>
#  include <libswresample/swresample.h>
}
#endif


static std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string fileExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return toLower(path.substr(pos));
}


bool AudioDecoder::isSupportedFormat(const std::string& path) {
    std::string ext = fileExtension(path);
    if (ext == ".wav") return true;
#ifdef HAVE_FFMPEG
    static const char* exts[] = {
        ".mp3", ".mp4", ".m4a", ".aac", ".flac", ".ogg", ".opus",
        ".mkv", ".webm", ".avi", ".mov", ".wmv", ".wma", ".alac",
        ".aiff", ".aif", ".mka", ".ts", ".m2ts", nullptr
    };
    for (int i = 0; exts[i]; ++i)
        if (ext == exts[i]) return true;
#endif
    return false;
}

std::string AudioDecoder::supportedFormats() {
    std::string s = "WAV";
#ifdef HAVE_FFMPEG
    s += ", MP3, MP4, M4A, AAC, FLAC, OGG, OPUS, MKV, WEBM, AVI, MOV, WMA, ALAC, AIFF, TS";
#endif
    return s;
}

void AudioDecoder::decode(const std::string& path,
                          std::vector<float>& pcm_out,
                          double& duration) {
    std::string ext = fileExtension(path);

    if (ext == ".wav") {
        decodeWAV(path, pcm_out, duration);
        return;
    }

#ifdef HAVE_FFMPEG
    decodeFFmpeg(path, pcm_out, duration);
    return;
#endif

    throw std::runtime_error(
        "Unsupported format '" + ext + "'.\n"
        "Supported: " + supportedFormats() + "\n"
        "Tip: Rebuild with -DUSE_FFMPEG=ON for mp3/mp4/mkv support.");
}

void AudioDecoder::decodeWAV(const std::string& path,
                              std::vector<float>& pcm_out,
                              double& duration) {
    FILE* fp = nullptr;
#ifdef _WIN32
    fopen_s(&fp, path.c_str(), "rb");
#else
    fp = fopen(path.c_str(), "rb");
#endif
    if (!fp)
        throw std::runtime_error("Cannot open WAV file: " + path);
    struct FileGuard {
        FILE* f;
        ~FileGuard() { if (f) fclose(f); }
    } file_guard{fp};

    char riff[4]; fread(riff, 1, 4, fp);
    if (memcmp(riff, "RIFF", 4) != 0) {
        throw std::runtime_error("Not a RIFF file: " + path);
    }
    fseek(fp, 4, SEEK_CUR); // skip file size
    char wave[4]; fread(wave, 1, 4, fp);
    if (memcmp(wave, "WAVE", 4) != 0) {
        throw std::runtime_error("Not a WAVE file: " + path);
    }

    uint16_t audio_format    = 0;
    uint16_t num_channels    = 0;
    uint32_t sample_rate     = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size       = 0;
    bool     found_fmt       = false;
    bool     found_data      = false;
    long     data_offset     = 0;

    char chunk_id[4];
    uint32_t chunk_size = 0;
    while (fread(chunk_id, 1, 4, fp) == 4 && fread(&chunk_size, 4, 1, fp) == 1) {
        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            fread(&audio_format,    2, 1, fp);
            fread(&num_channels,    2, 1, fp);
            fread(&sample_rate,     4, 1, fp);
            fseek(fp, 4, SEEK_CUR); // byte rate
            fseek(fp, 2, SEEK_CUR); // block align
            fread(&bits_per_sample, 2, 1, fp);
            if (chunk_size > 16)
                fseek(fp, chunk_size - 16, SEEK_CUR);
            found_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_size   = chunk_size;
            data_offset = ftell(fp);
            found_data  = true;
            break;
        } else {
            fseek(fp, chunk_size, SEEK_CUR);
        }
    }

    if (!found_fmt || !found_data) {
        throw std::runtime_error("Malformed WAV (missing fmt or data chunk): " + path);
    }
    if (audio_format != 1) {
        throw std::runtime_error("Only PCM WAV is supported (format=" +
                                 std::to_string(audio_format) + "): " + path);
    }
    if (num_channels == 0 || bits_per_sample == 0) {
        throw std::runtime_error("Malformed WAV (zero channels or bit depth): " + path);
    }

    if (sample_rate != TARGET_SAMPLE_RATE) {
        std::cout << "[AudioDecoder] Warning: sample rate " << sample_rate
                  << " Hz != 16000 Hz. Simple linear resampling will be applied.\n";
    }

    fseek(fp, data_offset, SEEK_SET);
    int bytes_per_sample = bits_per_sample / 8;
    size_t total_frames  = data_size / (bytes_per_sample * num_channels);
    std::vector<uint8_t> raw(data_size);
    size_t read_bytes = fread(raw.data(), 1, data_size, fp);

    if (read_bytes == 0)
        throw std::runtime_error("WAV data chunk is empty: " + path);

    std::vector<float> mono(total_frames);
    for (size_t f = 0; f < total_frames; ++f) {
        double sum = 0.0;
        for (int c = 0; c < num_channels; ++c) {
            size_t idx = (f * num_channels + c) * bytes_per_sample;
            if (bits_per_sample == 16) {
                int16_t s;
                memcpy(&s, &raw[idx], 2);
                sum += s / 32768.0;
            } else if (bits_per_sample == 24) {
                int32_t s = (raw[idx+2] << 16) | (raw[idx+1] << 8) | raw[idx];
                if (s & 0x800000) s |= ~0xFFFFFF;
                sum += s / 8388608.0;
            } else if (bits_per_sample == 32) {
                float s;
                memcpy(&s, &raw[idx], 4);
                sum += s;
            } else if (bits_per_sample == 8) {
                sum += (raw[idx] - 128) / 128.0;
            }
        }
        mono[f] = static_cast<float>(sum / num_channels);
    }

    if (sample_rate == static_cast<uint32_t>(TARGET_SAMPLE_RATE)) {
        pcm_out = std::move(mono);
    } else {
        double ratio = static_cast<double>(TARGET_SAMPLE_RATE) / sample_rate;
        size_t out_frames = static_cast<size_t>(total_frames * ratio);
        pcm_out.resize(out_frames);
        for (size_t i = 0; i < out_frames; ++i) {
            double src_pos = i / ratio;
            size_t s0 = static_cast<size_t>(src_pos);
            size_t s1 = std::min(s0 + 1, total_frames - 1);
            double t  = src_pos - s0;
            pcm_out[i] = static_cast<float>(mono[s0] * (1.0 - t) + mono[s1] * t);
        }
    }

    duration = static_cast<double>(pcm_out.size()) / TARGET_SAMPLE_RATE;
}

#ifdef HAVE_FFMPEG

static void swr_set_channel_layout(SwrContext* swr, AVCodecContext* codec_ctx) {
#if LIBAVUTIL_VERSION_MAJOR >= 58
    // FFmpeg 6.x+: use AVChannelLayout
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_MONO;
    av_opt_set_chlayout(swr, "in_chlayout",  &codec_ctx->ch_layout, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_layout, 0);
#elif LIBAVUTIL_VERSION_MAJOR >= 57
    // FFmpeg 5.x: ch_layout exists but old API also works
    int64_t in_layout = codec_ctx->ch_layout.u.mask
                        ? (int64_t)codec_ctx->ch_layout.u.mask
                        : av_get_default_channel_layout(codec_ctx->ch_layout.nb_channels);
    av_opt_set_int(swr, "in_channel_layout",  in_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
#else
    // FFmpeg 4.x: old channel_layout / channels fields
    int64_t in_layout = codec_ctx->channel_layout
                        ? (int64_t)codec_ctx->channel_layout
                        : av_get_default_channel_layout(codec_ctx->channels);
    av_opt_set_int(swr, "in_channel_layout",  in_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
#endif
}

void AudioDecoder::decodeFFmpeg(const std::string& path,
                                std::vector<float>& pcm_out,
                                double& duration) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("FFmpeg: cannot open file: " + path);

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        throw std::runtime_error("FFmpeg: cannot read stream info: " + path);
    }

    int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO,
                                        -1, -1, nullptr, 0);
    if (audio_idx < 0) {
        avformat_close_input(&fmt_ctx);
        throw std::runtime_error("FFmpeg: no audio stream found in: " + path);
    }

    AVStream* stream = fmt_ctx->streams[audio_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        throw std::runtime_error("FFmpeg: no decoder for audio codec.");
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        throw std::runtime_error("FFmpeg: cannot open codec.");
    }

    SwrContext* swr = swr_alloc();
    swr_set_channel_layout(swr, codec_ctx);
    av_opt_set_int(swr, "in_sample_rate",     codec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    TARGET_SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    swr_init(swr);

    if (fmt_ctx->duration != AV_NOPTS_VALUE)
        duration = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    else
        duration = 0.0;

    pcm_out.reserve(static_cast<size_t>(duration * TARGET_SAMPLE_RATE + 1024));

    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();

    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index != audio_idx) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(codec_ctx, packet) == 0) {
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                int max_out = av_rescale_rnd(
                    swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                    TARGET_SAMPLE_RATE, codec_ctx->sample_rate, AV_ROUND_UP);

                size_t old_size = pcm_out.size();
                pcm_out.resize(old_size + max_out);
                float* out_ptr = pcm_out.data() + old_size;

                int converted = swr_convert(swr,
                    reinterpret_cast<uint8_t**>(&out_ptr), max_out,
                    const_cast<const uint8_t**>(frame->data), frame->nb_samples);

                if (converted < max_out)
                    pcm_out.resize(old_size + converted);

                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }
    {
        size_t old_size = pcm_out.size();
        int flushed = swr_convert(swr, nullptr, 0, nullptr, 0);
        if (flushed > 0) {
            pcm_out.resize(old_size + flushed);
            float* out_ptr = pcm_out.data() + old_size;
            swr_convert(swr, reinterpret_cast<uint8_t**>(&out_ptr), flushed,
                        nullptr, 0);
        }
    }

    duration = static_cast<double>(pcm_out.size()) / TARGET_SAMPLE_RATE;

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
}

struct AudioDecoder::FFmpegDecoder : public AudioDecoder::Decoder {
    AVFormatContext* fmt_ctx   = nullptr;
    AVCodecContext*  codec_ctx = nullptr;
    SwrContext*      swr       = nullptr;
    AVPacket*        packet    = nullptr;
    AVFrame*         frame     = nullptr;
    int              audio_idx = -1;
    double           total_dur = 0.0;
    double           pos_secs  = 0.0;

    explicit FFmpegDecoder(const std::string& path) {
        if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0)
            throw std::runtime_error("FFmpeg: cannot open: " + path);
        avformat_find_stream_info(fmt_ctx, nullptr);

        audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_idx < 0) throw std::runtime_error("FFmpeg: no audio stream in: " + path);

        AVStream* stream = fmt_ctx->streams[audio_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        avcodec_open2(codec_ctx, codec, nullptr);

        swr = swr_alloc();
        swr_set_channel_layout(swr, codec_ctx);
        av_opt_set_int(swr, "in_sample_rate",  codec_ctx->sample_rate, 0);
        av_opt_set_int(swr, "out_sample_rate", TARGET_SAMPLE_RATE, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt",  codec_ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        swr_init(swr);

        total_dur = (fmt_ctx->duration != AV_NOPTS_VALUE)
            ? static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE : 0.0;

        packet = av_packet_alloc();
        frame  = av_frame_alloc();
    }

    ~FFmpegDecoder() override {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
    }

    bool readChunk(std::vector<float>& pcm, double chunk_seconds) override {
        pcm.clear();
        size_t wanted = static_cast<size_t>(chunk_seconds * TARGET_SAMPLE_RATE);
        pcm.reserve(wanted);

        while (pcm.size() < wanted) {
            if (av_read_frame(fmt_ctx, packet) < 0) break;
            if (packet->stream_index != audio_idx) {
                av_packet_unref(packet); continue;
            }
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int max_out = av_rescale_rnd(
                        swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                        TARGET_SAMPLE_RATE, codec_ctx->sample_rate, AV_ROUND_UP);
                    size_t old = pcm.size();
                    pcm.resize(old + max_out);
                    float* out_ptr = pcm.data() + old;
                    int got = swr_convert(swr,
                        reinterpret_cast<uint8_t**>(&out_ptr), max_out,
                        const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                    pcm.resize(old + got);
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(packet);
        }

        pos_secs += static_cast<double>(pcm.size()) / TARGET_SAMPLE_RATE;
        return !pcm.empty();
    }

    double totalDuration()   const override { return total_dur; }
    double currentPosition() const override { return pos_secs;  }
};

#endif // HAVE_FFMPEG

struct AudioDecoder::WAVDecoder : public AudioDecoder::Decoder {
    FILE*    fp           = nullptr;
    uint32_t sample_rate  = 16000;
    uint16_t channels     = 1;
    uint16_t bps          = 16;
    size_t   total_frames = 0;
    size_t   frames_read  = 0;

    explicit WAVDecoder(const std::string& path) {
#ifdef _WIN32
        fopen_s(&fp, path.c_str(), "rb");
#else
        fp = fopen(path.c_str(), "rb");
#endif
        if (!fp) throw std::runtime_error("Cannot open WAV: " + path);
        try {
            char riff[4]; fread(riff, 1, 4, fp); fseek(fp, 4, SEEK_CUR);
            char wave[4]; fread(wave, 1, 4, fp);

            char cid[4]; uint32_t csz = 0;
            bool found_fmt = false;
            while (fread(cid, 1, 4, fp) == 4 && fread(&csz, 4, 1, fp) == 1) {
                if (memcmp(cid, "fmt ", 4) == 0) {
                    uint16_t fmt; fread(&fmt, 2, 1, fp);
                    fread(&channels, 2, 1, fp);
                    fread(&sample_rate, 4, 1, fp);
                    fseek(fp, 6, SEEK_CUR);
                    fread(&bps, 2, 1, fp);
                    if (csz > 16) fseek(fp, csz - 16, SEEK_CUR);
                    found_fmt = true;
                } else if (memcmp(cid, "data", 4) == 0) {
                    if (!found_fmt || channels == 0 || bps == 0) {
                        throw std::runtime_error(
                            "Malformed WAV (data chunk before/without valid fmt): " + path);
                    }
                    total_frames = csz / (bps / 8 * channels);
                    break;
                } else {
                    fseek(fp, csz, SEEK_CUR);
                }
            }
        } catch (...) {
            fclose(fp);
            fp = nullptr;
            throw;
        }
    }

    ~WAVDecoder() override { if (fp) fclose(fp); }

    bool readChunk(std::vector<float>& pcm, double chunk_seconds) override {
        if (frames_read >= total_frames) return false;
        size_t want = static_cast<size_t>(chunk_seconds * sample_rate);
        size_t can  = std::min(want, total_frames - frames_read);

        int bps_bytes = bps / 8;
        std::vector<uint8_t> raw(can * bps_bytes * channels);
        size_t got = fread(raw.data(), bps_bytes * channels, can, fp);
        if (got == 0) return false;

        pcm.resize(got);
        for (size_t f = 0; f < got; ++f) {
            double sum = 0.0;
            for (int c = 0; c < channels; ++c) {
                size_t idx = (f * channels + c) * bps_bytes;
                if (bps == 16) {
                    int16_t s; memcpy(&s, &raw[idx], 2);
                    sum += s / 32768.0;
                } else if (bps == 24) {
                    int32_t s = (raw[idx+2] << 16) | (raw[idx+1] << 8) | raw[idx];
                    if (s & 0x800000) s |= ~0xFFFFFF;
                    sum += s / 8388608.0;
                } else if (bps == 32) {
                    float s; memcpy(&s, &raw[idx], 4);
                    sum += s;
                } else if (bps == 8) {
                    sum += (raw[idx] - 128) / 128.0;
                }
            }
            pcm[f] = static_cast<float>(sum / channels);
        }

        if (sample_rate != TARGET_SAMPLE_RATE) {
            double ratio = static_cast<double>(TARGET_SAMPLE_RATE) / sample_rate;
            std::vector<float> resampled(static_cast<size_t>(pcm.size() * ratio));
            for (size_t i = 0; i < resampled.size(); ++i) {
                double p = i / ratio;
                size_t s0 = static_cast<size_t>(p);
                size_t s1 = std::min(s0 + 1, pcm.size() - 1);
                resampled[i] = static_cast<float>(pcm[s0] * (1.0 - (p - s0)) + pcm[s1] * (p - s0));
            }
            pcm = std::move(resampled);
        }

        frames_read += got;
        return true;
    }

    double totalDuration() const override {
        return static_cast<double>(total_frames) / sample_rate;
    }
    double currentPosition() const override {
        return static_cast<double>(frames_read) / sample_rate;
    }
};

std::unique_ptr<AudioDecoder::Decoder> AudioDecoder::open(const std::string& path) {
    std::string ext = fileExtension(path);
    if (ext == ".wav")
        return std::make_unique<WAVDecoder>(path);
#ifdef HAVE_FFMPEG
    return std::make_unique<FFmpegDecoder>(path);
#endif
    throw std::runtime_error("Unsupported format (chunked): " + ext);
}