#include "TimestampFormatter.hpp"
#include <regex>
#include <cmath>

std::string TimestampFormatter::formatSRT(double seconds) {
    if (seconds < 0) seconds = 0;
    
    int hours = static_cast<int>(seconds / 3600.0);
    int minutes = static_cast<int>((seconds - hours * 3600.0) / 60.0);
    int secs = static_cast<int>(seconds - hours * 3600.0 - minutes * 60.0);
    int millis = static_cast<int>((seconds - std::floor(seconds)) * 1000.0);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << ","
        << std::setw(3) << millis;
    return oss.str();
}

std::string TimestampFormatter::formatVTT(double seconds) {
    if (seconds < 0) seconds = 0;
    
    int hours = static_cast<int>(seconds / 3600.0);
    int minutes = static_cast<int>((seconds - hours * 3600.0) / 60.0);
    int secs = static_cast<int>(seconds - hours * 3600.0 - minutes * 60.0);
    int millis = static_cast<int>((seconds - std::floor(seconds)) * 1000.0);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << "."
        << std::setw(3) << millis;
    return oss.str();
}

double TimestampFormatter::parseSRT(const std::string& timestamp) {
    std::regex pattern(R"((\d{2}):(\d{2}):(\d{2}),(\d{3}))");
    std::smatch matches;
    
    if (std::regex_search(timestamp, matches, pattern)) {
        int hours = std::stoi(matches[1]);
        int minutes = std::stoi(matches[2]);
        int seconds = std::stoi(matches[3]);
        int millis = std::stoi(matches[4]);
        
        return hours * 3600.0 + minutes * 60.0 + seconds + millis / 1000.0;
    }
    
    return 0.0;
}

std::string TimestampFormatter::formatDuration(double seconds) {
    if (seconds < 60) {
        return std::to_string(static_cast<int>(seconds)) + "s";
    } else if (seconds < 3600) {
        int mins = static_cast<int>(seconds / 60);
        int secs = static_cast<int>(seconds - mins * 60);
        return std::to_string(mins) + "m " + std::to_string(secs) + "s";
    } else {
        int hours = static_cast<int>(seconds / 3600);
        int mins = static_cast<int>((seconds - hours * 3600) / 60);
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }
}