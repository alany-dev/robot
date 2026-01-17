#include "common.h"
#include "arg.h"
#include "console.h"

#include "server-context.h"
#include "server-task.h"
#include "StreamTaskDispatcher.h"

#include <atomic>
#include <thread>
#include <signal.h>

#include <ros/ros.h>
#include <std_msgs/String.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>
#endif

static std::atomic<bool> g_is_interrupted = false;
static bool should_stop() {
    return g_is_interrupted.load();
}

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__)) || defined (_WIN32)
static void signal_handler(int) {
    if (g_is_interrupted.load()) {
        fprintf(stdout, "\033[0m\n");
        fflush(stdout);
        std::exit(130);
    }
    g_is_interrupted.store(true);
}
#endif

static ros::Publisher* g_tts_pub = nullptr;  
static ros::Publisher* g_fc_pub = nullptr;   
static StreamTaskDispatcher* g_dispatcher = nullptr;  

void handleFunctionCalls(const std::vector<std::string>& func_calls) {
    console::log("[Function Calls] ");
    for (size_t i = 0; i < func_calls.size(); i++) {
        console::log("%s", func_calls[i].c_str());
        if (i < func_calls.size() - 1) {
            console::log(", ");
        }
    }
    console::log("\n");
    
    if (g_fc_pub && ros::ok() && !func_calls.empty()) {
        for (const auto& fc : func_calls) {
            std_msgs::String msg;
            msg.data = fc;
            std::cout << "Published function call: " << fc << std::endl;
            g_fc_pub->publish(msg);
        }
    }
}

struct cli_context {
    server_context ctx_server;
    json messages = json::array();
    task_params defaults;

    cli_context(const common_params & params) {
        defaults.sampling    = params.sampling;
        defaults.speculative = params.speculative;
        defaults.n_keep      = params.n_keep;
        defaults.n_predict   = params.n_predict;
        defaults.antiprompt  = params.antiprompt;
        defaults.stream = true; // 流式输出
    }

    std::string generate_completion() {
        server_response_reader rd = ctx_server.get_response_reader();
        
        // 创建推理任务
        server_task task = server_task(SERVER_TASK_TYPE_COMPLETION);
        task.id = rd.get_new_id();
        task.index = 0;
        task.params = defaults;
        task.cli_input = messages;
        rd.post_task({std::move(task)});

        // 等待第一个结果
        server_task_result_ptr result = rd.next(should_stop);
        
        std::string curr_content;

        // 流式处理结果
        while (result) {
            if (should_stop()) {
                break;
            }
            
            if (result->is_error()) {
                json err_data = result->to_json();
                if (err_data.contains("message")) {
                    console::error("Error: %s\n", err_data["message"].get<std::string>().c_str());
                } else {
                    console::error("Error: %s\n", err_data.dump().c_str());
                }
                return curr_content;
            }
            
            // 处理部分结果（流式输出）
            auto res_partial = dynamic_cast<server_task_result_cmpl_partial *>(result.get());
            if (res_partial) {
                for (const auto & diff : res_partial->oaicompat_msg_diffs) {
                    if (!diff.content_delta.empty()) {
                        curr_content += diff.content_delta;
                        // 实时输出内容
                        // printf("%s", diff.content_delta.c_str());
                        // fflush(stdout);
                        
                        // 使用 StreamTaskDispatcher 处理流式内容
                        if (g_dispatcher) {
                            g_dispatcher->processDelta(diff.content_delta);
                        }
                    }
                }
            }
            
            auto res_final = dynamic_cast<server_task_result_cmpl_final *>(result.get());
            if (res_final) {
                break;
            }
            
            ros::spinOnce();
            
            result = rd.next(should_stop);
        }
        
        g_is_interrupted.store(false);
        return curr_content;
    }
};

static cli_context* g_ctx_cli = nullptr;

void asrTextCallback(const std_msgs::String::ConstPtr& msg) {
    
    if (!g_ctx_cli) {
        std::cout << "[asrTextCallback] Error: g_ctx_cli is null" << std::endl;
        return;
    }

    std::string asr_text = msg->data;
    if (asr_text.empty()) {
        std::cout << "[asrTextCallback] Warning: Empty message received" << std::endl;
        return;
    }

    std::cout << "[asrTextCallback] Processing ASR text: " << asr_text << std::endl;

    json system_msg;
    bool has_system = false;
    if (!g_ctx_cli->messages.empty() && 
        g_ctx_cli->messages[0].contains("role") && 
        g_ctx_cli->messages[0]["role"] == "system") {
        system_msg = g_ctx_cli->messages[0];
        has_system = true;
    }

    g_ctx_cli->messages.clear();
    if (has_system) {
        g_ctx_cli->messages.push_back(system_msg);
    }

    g_ctx_cli->messages.push_back({
        {"role", "user"},
        {"content", asr_text}
    });

    if (g_dispatcher) {
        g_dispatcher->reset();
    }

    // 流式生成回复
    console::log("[LLM] ");
    std::string assistant_content = g_ctx_cli->generate_completion();
    printf("\n");

    if (g_dispatcher) {
        g_dispatcher->processComplete(assistant_content);
    }

}

int main(int argc, char ** argv) {
    ros::init(argc, argv, "llamacpp_ros");
    ros::NodeHandle nh;
    common_params params;
    params.verbosity = LOG_LEVEL_ERROR;

    if (!common_params_parse(argc, argv, params, LLAMA_EXAMPLE_CLI)) {
        return 1;
    }

    common_init();
    cli_context ctx_cli(params);
    g_ctx_cli = &ctx_cli;  

    llama_backend_init();
    llama_numa_init(params.numa);

    console::init(params.simple_io, params.use_color);
    atexit([]() { console::cleanup(); });

    // 信号处理
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    struct sigaction sigint_action;
    sigint_action.sa_handler = signal_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);
    sigaction(SIGTERM, &sigint_action, NULL);
#elif defined (_WIN32)
    auto console_ctrl_handler = +[](DWORD ctrl_type) -> BOOL {
        return (ctrl_type == CTRL_C_EVENT) ? (signal_handler(SIGINT), true) : false;
    };
    SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(console_ctrl_handler), true);
#endif

    // 加载模型
    console::log("Loading model...\n");
    if (!ctx_cli.ctx_server.load_model(params)) {
        console::error("Failed to load the model\n");
        return 1;
    }

    std::thread inference_thread([&ctx_cli]() {
        ctx_cli.ctx_server.start_loop();
    });

    if (!params.system_prompt.empty()) {
        ctx_cli.messages.push_back({
            {"role", "system"},
            {"content", params.system_prompt}
        });
    }

    ros::Subscriber asr_sub = nh.subscribe("/llm_request", 10, asrTextCallback);
    
    ros::Publisher tts_pub = nh.advertise<std_msgs::String>("/tts/text_input", 10);
    g_tts_pub = &tts_pub;
    
    ros::Publisher fc_pub = nh.advertise<std_msgs::String>("/llm/function_call", 10);
    g_fc_pub = &fc_pub;
    
    StreamTaskDispatcher dispatcher(&tts_pub, handleFunctionCalls);
    g_dispatcher = &dispatcher;
    
    console::log("\n=== LLM ROS Node Started ===\n");
    console::log("Subscribed to /llm_request topic, waiting for ASR text...\n");
    console::log("Publishing to /tts/text_input topic for TTS synthesis...\n");
    console::log("Publishing to /llm/function_call topic for function calls...\n");
    console::log("StreamTaskDispatcher initialized for JSON parsing and task dispatching...\n\n");

    ros::spin();
    
    console::log("\nExiting...\n");
    ctx_cli.ctx_server.terminate();
    inference_thread.join();
    ros::shutdown();

    return 0;
}
