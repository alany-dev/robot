import os
import sys

sys.path.append("/home/orangepi/.local/lib/python3.8/site-packages/")

import time
import cv2
import random
from random import randint
import signal
import numpy as np
import rospy
import threading
import platform
import subprocess
import re
import socket
from PIL import Image, ImageDraw, ImageFont
if platform.machine()=="x86_64":
    ARM_SYS = False
else:
    ARM_SYS = True

if ARM_SYS:
    from lcd import  Lcd
    

is_shutdown = False

def handler(signum, frame):
    print("receive a signal %d"%signum)
    global is_shutdown
    is_shutdown = True
    rospy.signal_shutdown("ctrl c shutdown")

signal.signal(signal.SIGINT, handler)

class Emobj:
    def __init__(self):
        self.cmd_str = None
        self.bgr_list = (255, 255, 255)

    def read_img_dict(self,root_path):
        emoji_states = \
            [
                "眨眼",
                #"左上看",
                #"右上看",
                "兴奋",
                "微笑",
                # "惊恐",
                # "不屑",
                # "愤怒",
                # "难过",
                "正常",
                "睡觉",
                "苏醒",
            ]
        imgs_dict = dict()
        for emoji_state in emoji_states:
            if emoji_state == "眨眼":
                paths = [
                    os.path.join(root_path, emoji_state, "单次眨眼偶发"),
                    os.path.join(root_path, emoji_state, "快速双眨眼偶发")
                ]
            elif emoji_state == "正常" or emoji_state == "睡觉" or emoji_state == "苏醒" or emoji_state == "微笑":
                paths = [
                    os.path.join(root_path, emoji_state)
                ]
            else:
                paths = [
                    os.path.join(root_path, emoji_state, emoji_state + "_1进入姿势"),
                    os.path.join(root_path, emoji_state, emoji_state + "_2可循环动作"),
                    os.path.join(root_path, emoji_state, emoji_state + "_3回正")
                ]

            for path in paths:
                print("path=",path)
                key = os.path.basename(path)  # 把文件夹名字作为key
                imgs_dict[key] = []

                for i in range(1, 200):
                    img_name = os.path.join(path, "%d.jpg" % (i))
                    if not os.path.exists(img_name):
                        continue

                    img = cv2.imread(img_name)
                    imgs_dict[key].append(img)

        # for key,val in imgs_dict.items():
        #     print(key,val)

        return imgs_dict


    def do_an_emoji(self,emoji_state):
        keys = []
        if emoji_state == "眨眼":
            keys.append((0, 0, "单次眨眼偶发"))
            keys.append((0, 0, "快速双眨眼偶发"))
            keys = [random.choice(keys)]  # 随机只挑一个
        elif emoji_state == "正常" or emoji_state == "睡觉" or emoji_state == "苏醒" or emoji_state == "微笑":
            print("执行微笑动作22222")
            keys.append((0, 0, emoji_state))
        else:
            #1.进入姿势
            if emoji_state == "左上看":
                keys.append((-2000, 2000, emoji_state + "_1进入姿势")) #前两个数字表示轮胎动作
            elif emoji_state == "右上看":
                keys.append((2000, -2000, emoji_state + "_1进入姿势"))
            else:
                keys.append((0, 0, emoji_state + "_1进入姿势"))

            #2.进入姿势后的循环动作
            loop_num = 1
            for l in range(loop_num):
                # if  emoji_state=="兴奋":
                #     if l%2==0:
                #         keys.append((1500, 1500, emoji_state+"_2可循环动作"))
                #     elif l%2==1:
                #         keys.append((-1500, -1500, emoji_state+"_2可循环动作"))
                # else:
                keys.append((0, 0, emoji_state + "_2可循环动作"))

            #3.循环动作后的回正
            if emoji_state == "左上看":
                keys.append((2000, -2000, emoji_state + "_3回正"))
            elif emoji_state == "右上看":
                keys.append((-2000, 2000, emoji_state + "_3回正"))
            else:
                keys.append((0, 0, emoji_state + "_3回正"))

        if emoji_state == "兴奋":
            action_div = 8
        elif emoji_state == "睡觉" or emoji_state == "苏醒":
            action_div = 1
        else:
            action_div = 3

        #开始执行动作
        all_cnt = 0
        for pwm1, pwm2, key in keys:  # 遍历所有key(进入，循环，回正)
            imgs = self.imgs_dict[key]

            # uart.set_pwm(pwm1,pwm2) #设置轮胎pwm

            for img in imgs: #遍历每个动作的图片
                if all_cnt % action_div == 0:  # 为加速播放，跳过动画中一部分的图像
                    start = time.time()

                    img_show = img.copy()

                    for i in range(3):
                        if self.bgr_list[i]!=255:
                            img_show[:, :, i] &= self.bgr_list[i]

                    if ARM_SYS:
                        if self.lcd:
                            self.lcd.display(img_show)  # 从字典读取表情并显示
                    else:
                        self.imshow_in_pc_screen(img_show)
                        

                    interval = time.time() - start

                    if (0.050 - interval) >= 0:
                        time.sleep(0.050 - interval) #控制时间间隔刚好为50ms
                    # else:
                    #     print("显示时间:%f ms, 超过了50ms!"%(interval*1000))
                all_cnt += 1

        # uart.set_pwm(0, 0)


    def set_display_cmd(self,cmd_str):
        self.cmd_str = cmd_str

    def set_display_picture(self,image):
        self.cmd_str = "show_pic"
        self.image = image #BGR

    def set_eye_color(self,bgr_list):
        self.bgr_list = bgr_list

    def get_eye_color(self):
        return self.bgr_list

    def get_wifi_info(self):
        """Get WiFi connection information"""
        try:
            # Get current connected WiFi information
            result = subprocess.run(['iwconfig'], capture_output=True, text=True)
            if result.returncode == 0:
                output = result.stdout
                # Parse WiFi information
                ssid_match = re.search(r'ESSID:"([^"]*)"', output)
                signal_match = re.search(r'Signal level=(-?\d+)', output)
                
                ssid = ssid_match.group(1) if ssid_match else "Not Connected"
                signal_level = signal_match.group(1) if signal_match else "Unknown"
                
                # 获取IP地址
                ip_address = self.get_ip_address()
                
                return {
                    'ssid': ssid,
                    'signal_level': signal_level,
                    'ip_address': ip_address,
                    'status': 'Connected' if ssid != 'Not Connected' else 'Not Connected'
                }
            else:
                return {
                    'ssid': 'Not Connected',
                    'signal_level': 'Unknown',
                    'ip_address': 'Not Obtained',
                    'status': 'Not Connected'
                }
        except Exception as e:
            print(f"Failed to get WiFi info: {e}")
            return {
                'ssid': 'Failed to Get',
                'signal_level': 'Unknown',
                'ip_address': 'Failed to Get',
                'status': 'Failed to Get'
            }

    def get_ip_address(self):
        """获取本机IP地址"""
        try:
            # 方法1: 通过socket连接获取外网IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            try:
                # 方法2: 通过hostname获取
                hostname = socket.gethostname()
                ip = socket.gethostbyname(hostname)
                return ip
            except Exception:
                try:
                    # 方法3: 通过ifconfig命令获取
                    result = subprocess.run(['ifconfig'], capture_output=True, text=True)
                    if result.returncode == 0:
                        # 查找wlan0接口的IP地址
                        wlan_match = re.search(r'wlan0.*?inet (\d+\.\d+\.\d+\.\d+)', result.stdout, re.DOTALL)
                        if wlan_match:
                            return wlan_match.group(1)
                        
                        # 如果没有wlan0，查找第一个inet地址
                        inet_match = re.search(r'inet (\d+\.\d+\.\d+\.\d+)', result.stdout)
                        if inet_match:
                            return inet_match.group(1)
                    
                    return "Not Obtained"
                except Exception as e:
                    print(f"Failed to get IP address: {e}")
                    return "Failed to Get"

    def create_wifi_display_image(self, wifi_info):
        """Create WiFi information display image"""
        # 创建一个黑色背景图像 (假设LCD尺寸为240x240)
        img = np.zeros((240, 240, 3), dtype=np.uint8)
        
        # 设置字体参数
        font = cv2.FONT_HERSHEY_SIMPLEX
        font_scale = 0.6
        color = (255, 255, 255)  # 白色文字
        thickness = 1
        
        # 计算所有文本的总高度，用于垂直居中
        line_height = 25  # 每行之间的间距
        total_lines = 6  # 标题 + 5行信息
        total_height = total_lines * line_height
        
        # 计算起始Y坐标，使整个信息块垂直居中
        start_y = (240 - total_height) // 2 + 20  # +20 是标题的基线偏移
        
        # 显示标题
        title = "WiFi Info"
        title_size = cv2.getTextSize(title, font, font_scale, thickness)[0]
        title_x = (240 - title_size[0]) // 2
        cv2.putText(img, title, (title_x, start_y), font, font_scale, color, thickness)
        
        # 显示SSID
        ssid_text = f"SSID: {wifi_info['ssid']}"
        ssid_size = cv2.getTextSize(ssid_text, font, 0.45, thickness)[0]
        ssid_x = (240 - ssid_size[0]) // 2
        cv2.putText(img, ssid_text, (ssid_x, start_y + line_height), font, 0.45, color, thickness)
        
        # 显示IP地址
        ip_text = f"IP: {wifi_info['ip_address']}"
        ip_size = cv2.getTextSize(ip_text, font, 0.45, thickness)[0]
        ip_x = (240 - ip_size[0]) // 2
        cv2.putText(img, ip_text, (ip_x, start_y + line_height * 2), font, 0.45, color, thickness)
        
        # 显示信号强度
        signal_text = f"Signal: {wifi_info['signal_level']} dBm"
        signal_size = cv2.getTextSize(signal_text, font, 0.45, thickness)[0]
        signal_x = (240 - signal_size[0]) // 2
        cv2.putText(img, signal_text, (signal_x, start_y + line_height * 3), font, 0.45, color, thickness)
        
        # 显示连接状态
        status_text = f"Status: {wifi_info['status']}"
        status_color = (0, 255, 0) if wifi_info['status'] == 'Connected' else (0, 0, 255)
        status_size = cv2.getTextSize(status_text, font, 0.45, thickness)[0]
        status_x = (240 - status_size[0]) // 2
        cv2.putText(img, status_text, (status_x, start_y + line_height * 4), font, 0.45, status_color, thickness)
        
        # 添加时间戳
        timestamp = time.strftime("%H:%M:%S")
        time_text = f"Time: {timestamp}"
        time_size = cv2.getTextSize(time_text, font, 0.4, thickness)[0]
        time_x = (240 - time_size[0]) // 2
        cv2.putText(img, time_text, (time_x, start_y + line_height * 5), font, 0.4, color, thickness)
        
        return img

    def set_display_wifi_info(self):
        """Set command to display WiFi information"""
        self.cmd_str = "show_wifi_info"

    def show_wifi_info(self):
        """Public interface to display WiFi information"""
        self.set_display_wifi_info()

    def imshow_in_pc_screen(self,image):
        # # 获取图像尺寸
        # image_height, image_width = image.shape[:2]

        # # 计算缩放比例
        # scale = min(self.screen_width / image_width, self.screen_height / image_height)

        # # 缩放图像
        # scaled_width = int(image_width * scale)
        # scaled_height = int(image_height * scale)
        # scaled_image = cv2.resize(image, (scaled_width, scaled_height))

        # # 创建一个黑色背景
        # full_image = np.zeros((self.screen_height, self.screen_width, 3), dtype=np.uint8)
        
        # # 将图像居中放置在黑色背景上
        # x_offset = (self.screen_width - scaled_width) // 2
        # y_offset = (self.screen_height - scaled_height) // 2
        # full_image[y_offset:y_offset+scaled_height, x_offset:x_offset+scaled_width] = scaled_image

        scaled_image = cv2.resize(image, dsize=(0,0), fx=3, fy=3)

        cv2.imshow('image', scaled_image)
        key = cv2.waitKey(1) & 0xFF  # 获取按下的键的ASCII码
        if key == 27 or key == ord('q'):  # 按下esc键或者q键
            rospy.signal_shutdown("shutdown")

    def open_lcd(self):
        if ARM_SYS:
            self.lcd = Lcd()
        else:
            import tkinter as tk
            root = tk.Tk()
            self.screen_width = root.winfo_screenwidth()
            self.screen_height = root.winfo_screenheight()
            root.destroy()

    def close_lcd(self):
        if ARM_SYS:
            del self.lcd
            self.lcd=None #防止报错：'Emobj' object has no attribute 'lcd'

    def loop(self):
        self.open_lcd()

        self.current_dir = os.path.dirname(os.path.realpath(__file__))  # 获取当前文件夹
        self.imgs_dict = self.read_img_dict(os.path.join(self.current_dir, "image")) #读取图片耗时较长！

        # x86系统创建一个全屏窗口
        # if not ARM_SYS:
        #     cv2.namedWindow('fullscreen', cv2.WND_PROP_FULLSCREEN)
        #     cv2.setWindowProperty('fullscreen', cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

        while True:
            num = random.uniform(10, 30)  # 10*0.1s~30*0.1s,也就是休眠1~3s
            for i in range(int(num)):
                time.sleep(0.1)  # 0.1s
                if self.cmd_str != None: #收到外部新的命令，则退出等待
                    break
                if is_shutdown:
                    return

            #print("cmd_str=",self.cmd_str)
            if self.cmd_str == "open_lcd":
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                self.open_lcd()
                self.do_an_emoji("正常")
            elif self.cmd_str == "兴奋":
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                self.do_an_emoji("兴奋")
            elif self.cmd_str == "smile":
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                print("执行微笑动作11111")
                self.do_an_emoji("微笑")
                while not rospy.is_shutdown(): #永远微笑
                    time.sleep(0.1)  # 0.1s
                    if self.cmd_str != None:  # 收到外部新的命令，则退出等待
                        break
                    if is_shutdown:
                        return
                # self.do_an_emoji("兴奋")
                self.do_an_emoji("正常") # 眼睛恢复正常

            elif self.cmd_str == "good_night":  # 一直持续睡觉，不用清空状态和恢复
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                self.do_an_emoji("睡觉")
                while not rospy.is_shutdown(): #永远睡觉
                    time.sleep(0.1)  # 0.1s
                    if self.cmd_str != None:  # 收到外部新的命令，则退出等待
                        break
                    if is_shutdown:
                        return
                self.do_an_emoji("苏醒")

            elif self.cmd_str == "show_pic":
                while not rospy.is_shutdown():
                    self.cmd_str = None  # 接收到命令立即清空命令状态为None
                    if ARM_SYS:
                        if self.lcd:
                            self.lcd.display(self.image) #显示图片
                    else:
                        self.imshow_in_pc_screen(self.image)

                    while not rospy.is_shutdown():  # 永远保持画画的画面，直到接收到新的命令
                        if self.cmd_str != None:  # 收到外部新的命令，则退出等待
                            break
                        if is_shutdown:
                            return
                        time.sleep(0.01)  #10ms，只等待10ms是为了循环显示图片更加流畅
                        
                    #如果切换成了其他命令而不再显示图像，则跳出显示图像的大循环
                    if self.cmd_str!="show_pic":
                        break

            elif self.cmd_str == "show_wifi_info":
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                print("Display WiFi info")
                
                # Get WiFi information
                wifi_info = self.get_wifi_info()
                print(f"WiFi info: {wifi_info}")
                
                # Create WiFi information display image
                wifi_image = self.create_wifi_display_image(wifi_info)
                
                # Display WiFi information
                if ARM_SYS:
                    if self.lcd:
                        self.lcd.display(wifi_image)
                else:
                    self.imshow_in_pc_screen(wifi_image)
                
                # Continue displaying WiFi information for a period (5 seconds)
                display_time = 15.0
                start_time = time.time()
                while time.time() - start_time < display_time:
                    if self.cmd_str != None:  # 收到外部新的命令，则退出等待
                        break
                    if is_shutdown:
                        return
                    time.sleep(0.1)
                
                # 显示完成后恢复正常状态
                self.do_an_emoji("正常")

            elif self.cmd_str == "exit":  # 退出命令
                print("收到退出命令，表情线程即将结束...")
                return
                
            elif self.cmd_str == None:  # None进行眨眼
                # emoji_state = random.choice(["眨眼","左上看","右上看","兴奋"])
                #music.play_music(os.path.join(self.current_dir, "sound", "du.wav"),background_playback=True)
                self.do_an_emoji("眨眼")

            else: #其他状态下，显示正常
                self.cmd_str = None  # 接收到命令立即清空命令状态为None
                #music.play_music(os.path.join(self.current_dir, "sound", "du.wav"), background_playback=True)
                self.do_an_emoji("正常")

            if is_shutdown:
                return

if __name__ == "__main__":
    emoji = Emobj()
    emoji.show_wifi_info()
    emoji.loop()
    # threading.Thread(target=emoji.loop).start() #表情显示线程
    
    # 使用示例：
    # emoji.show_wifi_info()  # Display WiFi information
    # emoji.set_display_cmd("兴奋")  # 显示兴奋表情
    # emoji.set_display_cmd("smile")  # 显示微笑表情






