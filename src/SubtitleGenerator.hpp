#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>

struct whisper_context;

struct Subtitle {
    double      start_time;   // seconds
    double      end_time;     // seconds
    std::string text;
    float       confidence;   // 0.0 – 1.0 (whisper token probability avg)

    Subtitle() : start_time(0), end_time(0), confidence(1.0f) {}
    Subtitle(double s, double e, const std::string& t, float conf = 1.0f)
        : start_time(s), end_time(e), text(t), confidence(conf) {}

    double duration() const { return end_time - start_time; }
};

struct SubtitleConfig {
    // Model
    std::string model_path    = "models/ggml-base.en.bin";
    std::string language      = "auto";   // "auto" = let whisper detect
    bool        translate     = false;    // translate to English

    // Transcription
    int  max_tokens           = 224;
    bool single_segment       = false;
    bool print_progress       = true;
    bool print_special        = false;
    bool print_timestamps     = true;

    // Text formatting
    int  max_chars_per_line   = 42;
    int  max_lines            = 2;

    bool   merge_short_segments    = true;
    double min_segment_duration    = 0.5;   // seconds
    double max_segment_duration    = 5.0;   // seconds

    double chunk_duration_seconds  = 30.0;
    double chunk_overlap_seconds   = 1.0;   // overlap to avoid cutting words

    float  min_confidence          = 0.0f;  // drop segments below this
    bool   remove_filler_words     = false; // drop "um", "uh", etc.

    int    n_threads               = 4;
};

class SubtitleGenerator {
public:
    explicit SubtitleGenerator(const SubtitleConfig& config = SubtitleConfig());
    ~SubtitleGenerator();

    SubtitleGenerator(const SubtitleGenerator&) = delete;
    SubtitleGenerator& operator=(const SubtitleGenerator&) = delete;

    /**
     * Generate subtitles from any supported audio/video file.
     * Handles WAV natively; MP3/MP4/MKV/etc. require FFmpeg.
     * Long files (2-hour movies) are processed in chunks to keep RAM low.
     */
    void generate(const std::string& audio_path);

    void saveSRT (const std::string& path) const;
    void saveVTT (const std::string& path) const;
    void saveJSON(const std::string& path) const;
    void saveASS (const std::string& path) const;  // Advanced SubStation Alpha
    void saveTXT (const std::string& path) const;  // plain transcript

    const std::vector<Subtitle>& getSubtitles() const { return m_subtitles; }

    struct Stats {
        size_t total_segments        = 0;
        double total_duration        = 0.0;
        double processing_time       = 0.0;
        double average_segment_length = 0.0;
        double realtime_factor        = 0.0;  // total_duration / processing_time
        std::string detected_language;
        float  average_confidence    = 0.0f;
    };
    Stats getStats() const { return m_stats; }

    using ProgressFn = std::function<void(double, double)>;
    void setProgressCallback(ProgressFn fn) { m_progress_fn = fn; }

    using ChunkFn = std::function<void(const std::vector<Subtitle>&)>;
    void setChunkCallback(ChunkFn fn) { m_chunk_fn = fn; }

private:
    void initWhisper();
    void cleanupWhisper();

    void transcribeBuffer(const std::vector<float>& pcm,
                          double audio_offset_seconds);

    void postProcessSubtitles();
    void mergeShortSegments();
    void splitLongSegments();
    void removeDuplicates();
    void filterLowConfidence();

    std::string cleanText(const std::string& text) const;
    std::string wrapText(const std::string& text) const;

    static std::string escapeJSON(const std::string& s);
    static std::string escapeASS(const std::string& s);

    SubtitleConfig          m_config;
    whisper_context*        m_whisper_ctx  = nullptr;
    std::vector<Subtitle>   m_subtitles;
    Stats                   m_stats;
    ProgressFn              m_progress_fn;
    ChunkFn                 m_chunk_fn;
    double                  m_audio_duration = 0.0;
};
