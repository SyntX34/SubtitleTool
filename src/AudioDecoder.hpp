#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

/**
 * AudioDecoder
 * Decodes any audio/video file into a 16-bit PCM float vector at 16 kHz mono,
 * which is exactly what whisper.cpp requires.
 *
 * Supported formats:
 *   - WAV  : always (built-in, zero dependencies)
 *   - MP3, MP4, MKV, AAC, FLAC, OGG, M4A, WEBM, AVI, MOV …
 *            : when compiled with HAVE_FFMPEG=1
 *
 * For a 2-hour movie (~115 MB at 16kHz/16-bit mono) the decoded PCM fits in
 * ~880 MB of RAM.  We handle that by providing a chunked streaming interface
 * for callers that want to process audio in segments.
 */
class AudioDecoder {
public:
    static constexpr int TARGET_SAMPLE_RATE = 16000;  // whisper requires 16 kHz
    static constexpr int TARGET_CHANNELS    = 1;       // whisper requires mono

    /**
     * Decode an entire file into a float PCM vector.
     * Use this for files up to ~30 minutes. For longer content prefer
     * the chunked API below.
     *
     * @param path       Path to audio/video file.
     * @param pcm_out    Output: normalised float samples [-1, 1] at 16 kHz mono.
     * @param duration   Output: total duration in seconds.
     * @throws std::runtime_error on failure.
     */
    static void decode(const std::string& path,
                       std::vector<float>& pcm_out,
                       double& duration);

    /**
     * Returns true if path has a supported extension.
     * This is a fast heuristic — the actual file header is always checked.
     */
    static bool isSupportedFormat(const std::string& path);

    /**
     * Returns a human-readable string of supported extensions.
     */
    static std::string supportedFormats();

    // Chunked streaming API
    // Use this for long files (1+ hour) to avoid loading everything into RAM.
    // Pattern:
    //   auto dec = AudioDecoder::open(path);
    //   std::vector<float> chunk;
    //   while (dec->readChunk(chunk, 30.0)) { process(chunk); }

    struct Decoder {
        virtual ~Decoder() = default;
        /**
         * Read up to chunk_seconds of audio into pcm.
         * Returns false when the stream is exhausted.
         */
        virtual bool readChunk(std::vector<float>& pcm,
                               double chunk_seconds = 30.0) = 0;
        virtual double totalDuration() const = 0;
        virtual double currentPosition() const = 0;
    };

    static std::unique_ptr<Decoder> open(const std::string& path);

private:
    // WAV fallback (no external deps)
    static void decodeWAV(const std::string& path,
                          std::vector<float>& pcm_out,
                          double& duration);

#ifdef HAVE_FFMPEG
    static void decodeFFmpeg(const std::string& path,
                             std::vector<float>& pcm_out,
                             double& duration);

    struct FFmpegDecoder;   // forward-declared; defined in AudioDecoder.cpp
#endif

    struct WAVDecoder;
};