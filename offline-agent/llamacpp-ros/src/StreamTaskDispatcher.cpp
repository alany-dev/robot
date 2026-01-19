#include "StreamTaskDispatcher.h"
#include "common.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <locale>
#include <codecvt>

using json = nlohmann::ordered_json;
  

StreamTaskDispatcher::StreamTaskDispatcher(ros::Publisher* tts_pub, FunctionCallCallback fc_callback)
    : tts_publisher_(tts_pub)
    , fc_callback_(fc_callback)
    , accumulated_content_("")
    , parse_buffer_("")
    , tts_buffer_(L"")
{
}

std::wstring StreamTaskDispatcher::utf8ToWstring(const std::string& utf8_str) {
    if (utf8_str.empty()) {
        return std::wstring();
    }
    
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(utf8_str);
    } catch (const std::range_error& e) {
        std::cerr << "[StreamTaskDispatcher] UTF-8 to wstring conversion failed: " << e.what() << std::endl;
        return std::wstring();
    }
}

std::string StreamTaskDispatcher::wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
    } catch (const std::range_error& e) {
        std::cerr << "[StreamTaskDispatcher] wstring to UTF-8 conversion failed: " << e.what() << std::endl;
        return std::string();
    }
}

void StreamTaskDispatcher::processDelta(const std::string& delta) {
    if (delta.empty()) {
        std::cout << "[StreamTaskDispatcher] processDelta: 收到空增量，跳过" << std::endl;
        return;
    }

    accumulated_content_ += delta;

    std::string res_delta = extractResDelta(delta);
    
    if (!res_delta.empty()) {
        std::cout << "[StreamTaskDispatcher] processDelta: 当前 LLM 字段总内容: \"" << accumulated_content_ << "\"" << std::endl;
        
        processBufferedText(res_delta);
    }
    
    std::cout << "[StreamTaskDispatcher] processDelta: ---" << std::endl;
}

void StreamTaskDispatcher::processComplete(const std::string& full_content) {

    flushTTSBuffer();

    // 解析完整的 JSON，提取 "fc" 字段
    std::vector<std::string> function_calls = parseFunctionCalls(accumulated_content_);
    
    if (!function_calls.empty() && fc_callback_) {
        std::cout << "[StreamTaskDispatcher] Function calls detected: ";
        for (const auto& fc : function_calls) {
            std::cout << fc << " ";
        }
        std::cout << std::endl;
        
        fc_callback_(function_calls);
    }

    publishToTTS("END");
}

void StreamTaskDispatcher::reset() {
    accumulated_content_.clear();
    parse_buffer_.clear();
    tts_buffer_.clear();
}

std::string StreamTaskDispatcher::extractResDelta(const std::string& delta) {
    parse_buffer_ += delta;
    
    size_t res_start = parse_buffer_.find("\"res\":\"");
    if (res_start == std::string::npos) {
        return "";
    }
    
    size_t content_start = res_start + 7; 
    size_t quote_end = parse_buffer_.find('"', content_start);
    
    std::string current_res;
    if (quote_end == std::string::npos) {
        current_res = delta;
    } else {
        parse_buffer_.clear();
    }
    
    return current_res;
}
std::vector<std::string> StreamTaskDispatcher::parseFunctionCalls(const std::string& json_str) {
    std::vector<std::string> function_calls;
    
    json json_obj = json::parse(json_str);
        
    if (json_obj.contains("fc") && json_obj["fc"].is_array()) {
        for (const auto& fc_item : json_obj["fc"]) {
            if (fc_item.is_string()) {
                function_calls.push_back(fc_item.get<std::string>());
            }
        }
    }

    return function_calls;
}

bool StreamTaskDispatcher::isPunctuation(const std::wstring& text, size_t pos) {
    // 触发发送的标点符号：：。，、？！；
    const std::vector<wchar_t> punctuations = {
        L'：', L'。', L'，', L'、', L'？', L'！', L'；'
    };
    
    if (pos >= text.length()) {
        return false;
    }
    
    wchar_t ch = text[pos];
    for (wchar_t punct : punctuations) {
        if (ch == punct) {
            return true;
        }
    }
    
    return false;
}

void StreamTaskDispatcher::processBufferedText(const std::string& text) {
    if (text.empty()) {
        return;
    }
    
    std::wstring wtext = utf8ToWstring(text);
    if (wtext.empty() && !text.empty()) {
        std::cerr << "[StreamTaskDispatcher] Failed to convert text to wide string" << std::endl;
        return;
    }
    
    std::wstring temp_buffer = tts_buffer_ + wtext;
    
    while (true) {
        size_t punct_pos = std::wstring::npos;
        
        for (size_t i = 0; i < temp_buffer.length(); i++) {
            if (isPunctuation(temp_buffer, i)) {
                punct_pos = i;
                std::wcout << L"[StreamTaskDispatcher] 检测到标点符号，位置: " << i << std::endl;
                break;
            }
        }
        
        if (punct_pos == std::wstring::npos) {
            tts_buffer_ = temp_buffer;
            break;
        }
        
        // 找到标点符号，提取标点符号之前的文本
        std::wstring text_before_punct = temp_buffer.substr(0, punct_pos);
        std::wstring text_after_punct = temp_buffer.substr(punct_pos + 1);
        
        // 发送标点符号之前的文本
        if (!text_before_punct.empty()) {
            std::string utf8_text = wstringToUtf8(text_before_punct);
            std::cout << "[StreamTaskDispatcher] 准备发布给 TTS: \"" << utf8_text << "\"" << std::endl;
            publishToTTS(utf8_text);
        }
        
        temp_buffer = text_after_punct;
    }
    
}

void StreamTaskDispatcher::flushTTSBuffer() {
    if (!tts_buffer_.empty()) {
        std::string utf8_text = wstringToUtf8(tts_buffer_);
        publishToTTS(utf8_text);
        tts_buffer_.clear();
    }
}

void StreamTaskDispatcher::publishToTTS(const std::string& text) {
    if (!tts_publisher_ || !ros::ok() || text.empty()) {
        return;
    }
    
    std_msgs::String tts_msg;
    tts_msg.data = text;
    tts_publisher_->publish(tts_msg);
    ros::spinOnce();
}


