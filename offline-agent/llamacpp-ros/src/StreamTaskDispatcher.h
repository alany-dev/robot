#pragma once

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <string>
#include <vector>
#include <functional>
#include <locale>
#include <codecvt>

class StreamTaskDispatcher {
public:
    using FunctionCallCallback = std::function<void(const std::vector<std::string>&)>;

    StreamTaskDispatcher(ros::Publisher* tts_pub, FunctionCallCallback fc_callback = nullptr);
    void processDelta(const std::string& delta);
    void processComplete(const std::string& full_content);

    void reset();

private:
    ros::Publisher* tts_publisher_;
    FunctionCallCallback fc_callback_;
    
    std::string accumulated_content_;
    std::string parse_buffer_;
    
    std::wstring tts_buffer_;

    std::wstring utf8ToWstring(const std::string& utf8_str);
    std::string wstringToUtf8(const std::wstring& wstr);

    std::string extractResDelta(const std::string& delta);

    std::vector<std::string> parseFunctionCalls(const std::string& json_str);

    bool isPunctuation(const std::wstring& text, size_t pos);

    void processBufferedText(const std::string& text);

    void flushTTSBuffer();

    void publishToTTS(const std::string& text);
};

