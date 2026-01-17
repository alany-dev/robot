#include "TTSModel.h"
#include "MessageQueue.h"
#include "AudioPlayer.h"
#include "TextProcessor.h"
#include "Utils.h"
#include "ZmqServer.h"


#include <ros/ros.h>
#include <std_msgs/String.h>

#include <thread>
#include <iostream>
#include <atomic>
#include <memory>

ros::Subscriber text_sub;
zmq_component::ZmqServer status_server("tcp://*:6677");

AudioPlayer player;
DoubleMessageQueue message_queue;

std::atomic<bool> first_msg(true);

void synthesis_worker(DoubleMessageQueue &queue, TTSModel &model) {

    while (true) {
        std::string text = queue.pop_text();
        if (text.empty()) continue;

        if (text.find("END") != std::string::npos) {
            first_msg = true;  
            text = "";
            
        }

        int32_t audio_len = 0;
        if (!text.empty()) {
            int16_t* wavData = model.infer(text, audio_len);
            
            if (wavData && audio_len > 0) {
                auto audio_data = std::make_unique<int16_t[]>(audio_len);
                memcpy(audio_data.get(), wavData, audio_len * sizeof(int16_t));
                queue.push_audio(std::move(audio_data), audio_len, first_msg);
                model.free_data(wavData);
            }
        }else {
            auto empty_audio = std::make_unique<int16_t[]>(0);
            queue.push_audio(std::move(empty_audio), 0, first_msg);
        }
        
    }
}

void playback_worker(DoubleMessageQueue &queue, AudioPlayer &player) {
      while (true) {
        auto msg = queue.pop_audio();
        if (msg.data == nullptr) continue;
        
        player.play(msg.data.get(), msg.length * sizeof(int16_t), 1.0f);
        
        if (msg.is_last) {
           
            status_server.send("[tts -> voice]play end success");
        }
    }
}

void textCallback(const std_msgs::String::ConstPtr& msg) {
     if (first_msg.load()) {
        std::string req = status_server.receive();
        std::cout << "[voice -> tts] received: " << req << std::endl;
        first_msg = false;
    }
    std::string text = msg->data;
    std::cout << "[llm -> tts] received: " << text << std::endl;
    
    if (!text.empty()) {       
        message_queue.push_text(text);
    }
}


int main(int argc, char **argv) {
    ros::init(argc, argv, "tts_server");
    ros::NodeHandle nh;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model_path>" << std::endl;
        return 1;
    }

    
    text_sub = nh.subscribe("/tts/text_input", 10, textCallback);
    
    TTSModel model(argv[1]);
   

    std::thread synthesis_thread = std::thread(synthesis_worker, std::ref(message_queue), std::ref(model));
    std::thread playback_thread = std::thread(playback_worker, std::ref(message_queue), std::ref(player));    

    ros::Rate rate(10); // 10 Hz
    while (ros::ok()) {

        ros::spinOnce();
        rate.sleep();
    }

    message_queue.stop();

    synthesis_thread.join();
    playback_thread.join();
    return 0;
    
}