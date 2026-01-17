#include "StreamTaskDispatcher.h"
#include "common.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>

using json = nlohmann::ordered_json;

namespace {
    // 跳过空白字符
    void skipWhitespace(const std::string& str, size_t& pos) {
        while (pos < str.length() && (str[pos] == ' ' || str[pos] == '\t' || str[pos] == '\n' || str[pos] == '\r')) {
            pos++;
        }
    }

    // 读取字符串值（处理转义字符）
    std::string readStringValue(const std::string& str, size_t& pos) {
        skipWhitespace(str, pos);
        if (pos >= str.length() || str[pos] != '"') {
            return "";
        }
        pos++; // 跳过开始的引号
        
        std::string result;
        bool escaped = false;
        while (pos < str.length()) {
            char c = str[pos++];
            if (escaped) {
                if (c == 'n') result += '\n';
                else if (c == 't') result += '\t';
                else if (c == 'r') result += '\r';
                else if (c == '\\') result += '\\';
                else if (c == '"') result += '"';
                else result += c;
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                break; // 字符串结束
            } else {
                result += c;
            }
        }
        return result;
    }
}

StreamTaskDispatcher::StreamTaskDispatcher(ros::Publisher* tts_pub, FunctionCallCallback fc_callback)
    : tts_publisher_(tts_pub)
    , fc_callback_(fc_callback)
    , accumulated_content_("")
    , res_content_("")
    , parse_buffer_("")
    , tts_buffer_("")
{
}

void StreamTaskDispatcher::processDelta(const std::string& delta) {
    if (delta.empty()) {
        std::cout << "[StreamTaskDispatcher] processDelta: 收到空增量，跳过" << std::endl;
        return;
    }


    // 累积内容
    accumulated_content_ += delta;

    // 流式提取 "res" 字段
    std::string res_delta = extractResDelta(delta);
    
    if (!res_delta.empty()) {
        // 注意：extractResDelta 已经更新了 res_content_，这里不需要再次累加
        std::cout << "[StreamTaskDispatcher] processDelta: 当前 res 字段总内容: \"" << res_content_ << "\"" << std::endl;
        
        // 处理缓存文本
        processBufferedText(res_delta);
    } else {
    }
    
    std::cout << "[StreamTaskDispatcher] processDelta: ---" << std::endl;
}

void StreamTaskDispatcher::processComplete(const std::string& full_content) {
    // 确保所有内容都已处理
    if (!full_content.empty() && accumulated_content_ != full_content) {
        accumulated_content_ = full_content;
        // 重新提取 res 字段（以防有遗漏）
        std::string remaining_res = extractResDelta("");
        if (!remaining_res.empty()) {
            res_content_ += remaining_res;
            processBufferedText(remaining_res);
        }
    }

    // 发送剩余的缓存内容
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

    // 发送结束标记给 TTS
    publishToTTS("END");
}

void StreamTaskDispatcher::reset() {
    accumulated_content_.clear();
    res_content_.clear();
    parse_buffer_.clear();
    tts_buffer_.clear();
}

std::string StreamTaskDispatcher::extractResDelta(const std::string& delta) {
    // 流式提取 "res":"..." 中的内容
    // 累积到 parse_buffer_，查找 "res":" 模式，提取引号之间的内容
    
    parse_buffer_ += delta;
    
    // 查找 "res":"
    size_t res_start = parse_buffer_.find("\"res\":\"");
    if (res_start == std::string::npos) {
        return "";
    }
    
    // 找到开始位置（跳过 "res":"）
    size_t content_start = res_start + 7; // "res":" 的长度是 7
    
    // 查找结束引号（简单处理，不考虑转义）
    size_t quote_end = parse_buffer_.find('"', content_start);
    if (quote_end == std::string::npos) {
        // 字符串还未结束，提取当前所有内容
        std::string current_res = parse_buffer_.substr(content_start);
        // 计算新增部分：比较当前内容和已保存的内容
        size_t old_len = res_content_.length();
        if (current_res.length() > old_len) {
            // 确保 current_res 的前 old_len 个字符与 res_content_ 相同
            if (old_len == 0 || current_res.substr(0, old_len) == res_content_) {
                std::string result = current_res.substr(old_len);
                res_content_ = current_res;
                return result;
            } else {
                // 如果不匹配，说明可能有重复或错误，重新计算
                // 这种情况不应该发生，但为了安全起见
                std::string result = current_res;
                res_content_ = current_res;
                return result;
            }
        } else if (current_res.length() < old_len) {
            // 当前内容比已保存的短，说明可能是新的 JSON 开始，重置
            res_content_ = current_res;
            return current_res;
        }
        // 长度相等，没有新增
        return "";
    }
    
    // 字符串已完整，提取完整值
    std::string full_res = parse_buffer_.substr(content_start, quote_end - content_start);
    // 计算新增部分
    size_t old_len = res_content_.length();
    if (full_res.length() > old_len) {
        // 确保 full_res 的前 old_len 个字符与 res_content_ 相同
        if (old_len == 0 || full_res.substr(0, old_len) == res_content_) {
            std::string result = full_res.substr(old_len);
            res_content_ = full_res;
            // 清理已处理的部分
            parse_buffer_ = parse_buffer_.substr(quote_end + 1);
            return result;
        } else {
            // 如果不匹配，返回完整内容
            std::string result = full_res;
            res_content_ = full_res;
            parse_buffer_ = parse_buffer_.substr(quote_end + 1);
            return result;
        }
    } else if (full_res.length() < old_len) {
        // 完整内容比已保存的短，说明可能是新的 JSON，重置
        res_content_ = full_res;
        parse_buffer_ = parse_buffer_.substr(quote_end + 1);
        return full_res;
    }
    
    // 长度相等，没有新增，但需要清理缓冲区
    parse_buffer_ = parse_buffer_.substr(quote_end + 1);
    return "";
}

std::vector<std::string> StreamTaskDispatcher::parseFunctionCalls(const std::string& json_str) {
    std::vector<std::string> function_calls;
    
    json json_obj = json::parse(json_str);
        
    // 提取 "fc" 字段
    if (json_obj.contains("fc") && json_obj["fc"].is_array()) {
        for (const auto& fc_item : json_obj["fc"]) {
            if (fc_item.is_string()) {
                function_calls.push_back(fc_item.get<std::string>());
            }
        }
    }

    return function_calls;
}

bool StreamTaskDispatcher::isPunctuation(const std::string& text, size_t pos, size_t& len) {
    // 触发发送的标点符号：：。，、？！；
    // UTF-8 编码的中文标点符号（每个3字节）
    const std::vector<std::string> punctuations = {
        "：", "。", "，", "、", "？", "！", "；"
    };
    
    for (const auto& punct : punctuations) {
        if (pos + punct.length() <= text.length() && 
            text.substr(pos, punct.length()) == punct) {
            len = punct.length();
            return true;
        }
    }
    
    len = 0;
    return false;
}

void StreamTaskDispatcher::processBufferedText(const std::string& text) {
    if (text.empty()) {
        return;
    }
    
    std::string temp_buffer = tts_buffer_ + text;
    
    while (true) {
        size_t punct_pos = std::string::npos;
        size_t punct_len = 0;
        
        // UTF-8 安全遍历，查找第一个标点符号
        for (size_t i = 0; i < temp_buffer.length(); ) {
            size_t len = 0;
            if (isPunctuation(temp_buffer, i, len)) {
                punct_pos = i;
                punct_len = len;
                std::cout << "[StreamTaskDispatcher] 检测到标点符号，位置: " << i << ", 长度: " << len << std::endl;
                break;
            }
            
            // 移动到下一个 UTF-8 字符
            if ((temp_buffer[i] & 0x80) == 0) {
                i++; // ASCII 字符
            } else if ((temp_buffer[i] & 0xE0) == 0xC0) {
                i += 2; // 2字节 UTF-8
            } else if ((temp_buffer[i] & 0xF0) == 0xE0) {
                i += 3; // 3字节 UTF-8（中文）
            } else if ((temp_buffer[i] & 0xF8) == 0xF0) {
                i += 4; // 4字节 UTF-8
            } else {
                i++; // 无效字符，跳过
            }
        }
        
        if (punct_pos == std::string::npos) {
            // 没有找到标点符号，保留在缓存中
            tts_buffer_ = temp_buffer;
            break;
        }
        
        // 找到标点符号，提取标点符号之前的文本（去除标点）
        std::string text_before_punct = temp_buffer.substr(0, punct_pos);
        std::string text_after_punct = temp_buffer.substr(punct_pos + punct_len);
        
        // 发送标点符号之前的文本（去除标点）
        if (!text_before_punct.empty()) {
            std::cout << "[StreamTaskDispatcher] 准备发布给 TTS: \"" << text_before_punct << "\"" << std::endl;
            publishToTTS(text_before_punct);
        }
        
        // 继续处理标点符号之后的文本
        temp_buffer = text_after_punct;
    }
    
}

void StreamTaskDispatcher::flushTTSBuffer() {
    if (!tts_buffer_.empty()) {
        publishToTTS(tts_buffer_);
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


