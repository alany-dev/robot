#pragma once

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <string>
#include <vector>
#include <functional>

/**
 * 流式任务分发类
 * 功能：
 * 1. 流式解析 LLM 推理结果 JSON：{"res":"...", "fc":[...]}
 * 2. 流式提取 "res" 字段并发布给 TTS
 * 3. 推理结束后解析 "fc" 字段并执行函数调用
 */
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
    std::string res_content_;
    std::string parse_buffer_;
    std::string tts_buffer_;

    std::string extractResDelta(const std::string& delta);

    std::vector<std::string> parseFunctionCalls(const std::string& json_str);

    bool isPunctuation(const std::string& text, size_t pos, size_t& len);

    void processBufferedText(const std::string& text);

    void flushTTSBuffer();

    void publishToTTS(const std::string& text);
};

