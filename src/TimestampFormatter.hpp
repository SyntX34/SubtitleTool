#pragma once

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

/**
 * @brief Formats timestamps for subtitle files
 */
class TimestampFormatter {
public:
    /**
     * @brief Format seconds to SRT timestamp (HH:MM:SS,mmm)
     * @param seconds Time in seconds
     * @return Formatted string
     */
    static std::string formatSRT(double seconds);
    
    /**
     * @brief Format seconds to WebVTT timestamp (HH:MM:SS.mmm)
     * @param seconds Time in seconds
     * @return Formatted string
     */
    static std::string formatVTT(double seconds);
    
    /**
     * @brief Parse SRT timestamp to seconds
     * @param timestamp SRT formatted timestamp
     * @return Time in seconds
     */
    static double parseSRT(const std::string& timestamp);
    
    /**
     * @brief Format duration as human-readable string
     * @param seconds Time in seconds
     * @return Human-readable string
     */
    static std::string formatDuration(double seconds);
};