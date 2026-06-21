#include "SubtitleGenerator.hpp"
#include "TimestampFormatter.hpp"
#include "AudioDecoder.hpp"
#include "whisper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

SubtitleGenerator::SubtitleGenerator(const SubtitleConfig& config)
    : m_config(config) {
    initWhisper();
}

SubtitleGenerator::~SubtitleGenerator() {
    cleanupWhisper();
}

void SubtitleGenerator::initWhisper() {
    cleanupWhisper();
    m_whisper_ctx = whisper_init_from_file(m_config.model_path.c_str());
    if (!m_whisper_ctx)
        throw std::runtime_error("Failed to load whisper model: " + m_config.model_path);
    std::cout << "[Whisper] Model loaded: " << m_config.model_path << "\n";
}

void SubtitleGenerator::cleanupWhisper() {
    if (m_whisper_ctx) {
        whisper_free(m_whisper_ctx);
        m_whisper_ctx = nullptr;
    }
}

void SubtitleGenerator::generate(const std::string& audio_path) {
    auto wall_start = std::chrono::high_resolution_clock::now();

    m_subtitles.clear();
    m_stats = Stats{};

    if (!AudioDecoder::isSupportedFormat(audio_path)) {
        throw std::runtime_error(
            "Unsupported format. Supported: " + AudioDecoder::supportedFormats());
    }

    std::cout << "[SubtitleGenerator] Input  : " << audio_path << "\n";

    auto decoder = AudioDecoder::open(audio_path);
    m_audio_duration = decoder->totalDuration();

    if (m_audio_duration > 0)
        std::cout << "[SubtitleGenerator] Duration: "
                  << TimestampFormatter::formatDuration(m_audio_duration) << "\n";

    double chunk_secs   = m_config.chunk_duration_seconds > 0
                          ? m_config.chunk_duration_seconds : 30.0;
    double overlap_secs = m_config.chunk_overlap_seconds;
    double offset       = 0.0;

    std::vector<float> chunk;
    int chunk_idx = 0;

    while (decoder->readChunk(chunk, chunk_secs + overlap_secs)) {
        double chunk_duration = static_cast<double>(chunk.size()) /
                                AudioDecoder::TARGET_SAMPLE_RATE;

        std::cout << "[SubtitleGenerator] Chunk " << (++chunk_idx)
                  << " @ " << std::fixed << std::setprecision(1)
                  << offset << "s  (" << chunk_duration << "s)\n";

        if (m_progress_fn && m_audio_duration > 0)
            m_progress_fn(offset, m_audio_duration);

        transcribeBuffer(chunk, offset);

        double advance = chunk_duration - overlap_secs;
        if (advance <= 0) advance = chunk_duration;
        offset += advance;

        chunk.clear();
        chunk.shrink_to_fit();
    }

    if (m_progress_fn)
        m_progress_fn(m_audio_duration, m_audio_duration);

    postProcessSubtitles();

    auto wall_end = std::chrono::high_resolution_clock::now();
    m_stats.processing_time  = std::chrono::duration<double>(wall_end - wall_start).count();
    m_stats.total_segments   = m_subtitles.size();
    m_stats.total_duration   = m_audio_duration;
    m_stats.realtime_factor  = (m_stats.processing_time > 0)
                               ? m_audio_duration / m_stats.processing_time : 0.0;

    if (!m_subtitles.empty()) {
        double sum_dur  = 0.0;
        float  sum_conf = 0.0f;
        for (const auto& s : m_subtitles) {
            sum_dur  += s.duration();
            sum_conf += s.confidence;
        }
        m_stats.average_segment_length = sum_dur  / m_subtitles.size();
        m_stats.average_confidence     = sum_conf / m_subtitles.size();
    }

    std::cout << "[SubtitleGenerator] Done. " << m_subtitles.size()
              << " segments in " << std::fixed << std::setprecision(1)
              << m_stats.processing_time << "s  ("
              << std::setprecision(1) << m_stats.realtime_factor << "x real-time)\n";
}

void SubtitleGenerator::transcribeBuffer(const std::vector<float>& pcm,
                                         double audio_offset_seconds) {
    if (pcm.empty()) return;

    whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    params.print_progress   = false;  // handle progress
    params.print_special    = m_config.print_special;
    params.print_timestamps = false;
    params.single_segment   = m_config.single_segment;
    params.max_tokens       = m_config.max_tokens;
    params.n_threads        = m_config.n_threads;
    params.translate        = m_config.translate;
    params.offset_ms        = 0;      // apply offset

    // Language
    if (m_config.language == "auto" || m_config.language.empty()) {
        params.language = nullptr;  // auto-detect whisper
    } else {
        params.language = m_config.language.c_str();
    }

    if (whisper_full(m_whisper_ctx, params, pcm.data(),
                     static_cast<int>(pcm.size())) != 0) {
        std::cerr << "[Whisper] Warning: transcription failed for chunk at "
                  << audio_offset_seconds << "s\n";
        return;
    }

    if (m_stats.detected_language.empty()) {
        const char* lang = whisper_lang_str(whisper_full_lang_id(m_whisper_ctx));
        if (lang) m_stats.detected_language = lang;
    }

    int n_segments = whisper_full_n_segments(m_whisper_ctx);
    std::vector<Subtitle> chunk_subs;
    chunk_subs.reserve(n_segments);

    for (int i = 0; i < n_segments; ++i) {
        const char* raw = whisper_full_get_segment_text(m_whisper_ctx, i);
        if (!raw) continue;

        double t0 = whisper_full_get_segment_t0(m_whisper_ctx, i) / 100.0
                    + audio_offset_seconds;
        double t1 = whisper_full_get_segment_t1(m_whisper_ctx, i) / 100.0
                    + audio_offset_seconds;

        int n_tokens = whisper_full_n_tokens(m_whisper_ctx, i);
        float conf_sum = 0.0f;
        int   conf_cnt = 0;
        for (int t = 0; t < n_tokens; ++t) {
            whisper_token_data td = whisper_full_get_token_data(m_whisper_ctx, i, t);
            if (td.id >= whisper_token_eot(m_whisper_ctx)) continue; // skip special
            conf_sum += td.p;
            ++conf_cnt;
        }
        float confidence = (conf_cnt > 0) ? conf_sum / conf_cnt : 1.0f;

        std::string text = cleanText(raw);
        if (!text.empty() && t1 > t0) {
            chunk_subs.emplace_back(t0, t1, text, confidence);
        }
    }

    m_subtitles.insert(m_subtitles.end(), chunk_subs.begin(), chunk_subs.end());

    if (m_chunk_fn && !chunk_subs.empty())
        m_chunk_fn(chunk_subs);
}

void SubtitleGenerator::postProcessSubtitles() {
    if (m_subtitles.empty()) return;

    std::sort(m_subtitles.begin(), m_subtitles.end(),
              [](const Subtitle& a, const Subtitle& b) {
                  return a.start_time < b.start_time;
              });

    removeDuplicates();

    if (m_config.merge_short_segments) mergeShortSegments();

    splitLongSegments();

    filterLowConfidence();

    if (m_config.remove_filler_words) {
        static const std::regex filler(
            R"(\b(um|uh|er|ah|hmm|hm|erm)\b)", std::regex::icase);
        for (auto& s : m_subtitles)
            s.text = std::regex_replace(s.text, filler, "");
    }

    for (auto& s : m_subtitles)
        s.text = wrapText(cleanText(s.text));

    m_subtitles.erase(
        std::remove_if(m_subtitles.begin(), m_subtitles.end(),
                       [](const Subtitle& s) { return s.text.empty(); }),
        m_subtitles.end());
}

void SubtitleGenerator::removeDuplicates() {
    if (m_subtitles.size() < 2) return;
    std::vector<Subtitle> out;
    out.reserve(m_subtitles.size());
    out.push_back(m_subtitles[0]);
    for (size_t i = 1; i < m_subtitles.size(); ++i) {
        const auto& prev = out.back();
        const auto& cur  = m_subtitles[i];
        if (cur.text == prev.text &&
            cur.start_time < prev.end_time + 0.5)
            continue;
        out.push_back(cur);
    }
    m_subtitles = std::move(out);
}

void SubtitleGenerator::mergeShortSegments() {
    if (m_subtitles.size() < 2) return;
    std::vector<Subtitle> merged;
    merged.reserve(m_subtitles.size());
    Subtitle cur = m_subtitles[0];
    for (size_t i = 1; i < m_subtitles.size(); ++i) {
        const auto& nxt = m_subtitles[i];
        double gap = nxt.start_time - cur.end_time;
        if (gap < 0.4 && cur.duration() < m_config.min_segment_duration * 2) {
            cur.end_time  = nxt.end_time;
            cur.text     += " " + nxt.text;
            cur.confidence = (cur.confidence + nxt.confidence) * 0.5f;
        } else {
            merged.push_back(cur);
            cur = nxt;
        }
    }
    merged.push_back(cur);
    m_subtitles = std::move(merged);
}

void SubtitleGenerator::splitLongSegments() {
    std::vector<Subtitle> out;
    out.reserve(m_subtitles.size() * 2);
    for (const auto& sub : m_subtitles) {
        if (sub.duration() <= m_config.max_segment_duration) {
            out.push_back(sub);
            continue;
        }
        int    num_parts    = static_cast<int>(
            std::ceil(sub.duration() / m_config.max_segment_duration));
        double part_dur     = sub.duration() / num_parts;

        std::vector<std::string> words;
        std::istringstream iss(sub.text);
        std::string w;
        while (iss >> w) words.push_back(w);
        int wpb = std::max(1, (int)words.size() / num_parts);

        for (int p = 0; p < num_parts; ++p) {
            double start = sub.start_time + p * part_dur;
            double end   = (p == num_parts - 1) ? sub.end_time : start + part_dur;
            int    wi0   = p * wpb;
            int    wi1   = (p == num_parts - 1)
                           ? (int)words.size()
                           : std::min((p + 1) * wpb, (int)words.size());
            std::string txt;
            for (int j = wi0; j < wi1; ++j) {
                if (j > wi0) txt += " ";
                txt += words[j];
            }
            if (!txt.empty())
                out.emplace_back(start, end, txt, sub.confidence);
        }
    }
    m_subtitles = std::move(out);
}

void SubtitleGenerator::filterLowConfidence() {
    if (m_config.min_confidence <= 0.0f) return;
    m_subtitles.erase(
        std::remove_if(m_subtitles.begin(), m_subtitles.end(),
                       [this](const Subtitle& s) {
                           return s.confidence < m_config.min_confidence;
                       }),
        m_subtitles.end());
}

std::string SubtitleGenerator::cleanText(const std::string& text) const {
    if (text.empty()) return "";
    std::string r = text;
    r.erase(0, r.find_first_not_of(" \t\n\r"));
    auto last = r.find_last_not_of(" \t\n\r");
    if (last != std::string::npos) r.erase(last + 1);
    r = std::regex_replace(r, std::regex(R"(\s+)"), " ");
    return r;
}

std::string SubtitleGenerator::wrapText(const std::string& text) const {
    if (text.empty() ||
        text.size() <= static_cast<size_t>(m_config.max_chars_per_line))
        return text;

    std::string result, remaining = text;
    int lines = 0;
    while (!remaining.empty() && lines < m_config.max_lines) {
        if (remaining.size() <= static_cast<size_t>(m_config.max_chars_per_line)) {
            result += remaining; break;
        }
        size_t bp = remaining.rfind(' ', m_config.max_chars_per_line);
        if (bp == std::string::npos) bp = m_config.max_chars_per_line;
        if (lines > 0) result += '\n';
        result   += remaining.substr(0, bp);
        remaining = remaining.substr(bp + 1);
        ++lines;
    }
    return result;
}

std::string SubtitleGenerator::escapeJSON(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;
        }
    }
    return out;
}

std::string SubtitleGenerator::escapeASS(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\N";
        else           out += c;
    }
    return out;
}

void SubtitleGenerator::saveSRT(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open for writing: " + path);
    for (size_t i = 0; i < m_subtitles.size(); ++i) {
        const auto& s = m_subtitles[i];
        f << (i + 1) << "\n"
          << TimestampFormatter::formatSRT(s.start_time)
          << " --> "
          << TimestampFormatter::formatSRT(s.end_time)
          << "\n"
          << s.text << "\n\n";
    }
    std::cout << "[Save] SRT → " << path << "  (" << m_subtitles.size() << " entries)\n";
}

void SubtitleGenerator::saveVTT(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open for writing: " + path);
    f << "WEBVTT\n"
      << "NOTE generated by SubtitleGenerator v2.0\n\n";
    for (size_t i = 0; i < m_subtitles.size(); ++i) {
        const auto& s = m_subtitles[i];
        f << (i + 1) << "\n"
          << TimestampFormatter::formatVTT(s.start_time)
          << " --> "
          << TimestampFormatter::formatVTT(s.end_time)
          << "\n"
          << s.text << "\n\n";
    }
    std::cout << "[Save] VTT → " << path << "\n";
}

void SubtitleGenerator::saveJSON(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open for writing: " + path);
    f << "{\n"
      << "  \"generator\": \"SubtitleGenerator v2.0\",\n"
      << "  \"language\": \"" << escapeJSON(m_stats.detected_language) << "\",\n"
      << "  \"total_duration\": " << std::fixed << std::setprecision(3)
                                  << m_stats.total_duration << ",\n"
      << "  \"total_segments\": " << m_subtitles.size() << ",\n"
      << "  \"subtitles\": [\n";

    for (size_t i = 0; i < m_subtitles.size(); ++i) {
        const auto& s = m_subtitles[i];
        f << "    {\n"
          << "      \"index\": " << (i + 1) << ",\n"
          << "      \"start\": " << std::setprecision(3) << s.start_time << ",\n"
          << "      \"end\": "   << s.end_time << ",\n"
          << "      \"confidence\": " << std::setprecision(4) << s.confidence << ",\n"
          << "      \"text\": \"" << escapeJSON(s.text) << "\"\n"
          << "    }" << (i + 1 < m_subtitles.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    std::cout << "[Save] JSON → " << path << "\n";
}

void SubtitleGenerator::saveASS(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open for writing: " + path);

    f << "[Script Info]\n"
      << "ScriptType: v4.00+\n"
      << "PlayResX: 1920\n"
      << "PlayResY: 1080\n"
      << "Collisions: Normal\n"
      << "Timer: 100.0000\n\n"

      << "[V4+ Styles]\n"
      << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
         "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
         "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
         "Alignment, MarginL, MarginR, MarginV, Encoding\n"
      << "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,"
         "&H80000000,-1,0,0,0,100,100,0,0,1,2,1,2,10,10,20,1\n\n"

      << "[Events]\n"
      << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
         "MarginV, Effect, Text\n";

    auto toASS = [](double secs) {
        int h  = (int)(secs / 3600);
        int m  = (int)((secs - h * 3600) / 60);
        int s  = (int)(secs - h * 3600 - m * 60);
        int cs = (int)((secs - std::floor(secs)) * 100);
        std::ostringstream o;
        o << std::setfill('0')
          << h << ":" << std::setw(2) << m << ":" << std::setw(2) << s
          << "." << std::setw(2) << cs;
        return o.str();
    };

    for (const auto& s : m_subtitles) {
        f << "Dialogue: 0,"
          << toASS(s.start_time) << ","
          << toASS(s.end_time)
          << ",Default,,0,0,0,,"
          << escapeASS(s.text) << "\n";
    }
    std::cout << "[Save] ASS → " << path << "\n";
}

void SubtitleGenerator::saveTXT(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open for writing: " + path);
    for (const auto& s : m_subtitles) {
        f << "[" << TimestampFormatter::formatSRT(s.start_time) << "] "
          << s.text << "\n";
    }
    std::cout << "[Save] TXT → " << path << "\n";
}
