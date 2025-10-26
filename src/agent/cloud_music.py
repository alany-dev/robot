import requests
import json
import os
import subprocess
import time
import threading
import signal
import psutil

# 全局音乐控制变量
current_music_process = None
current_song_name = ""


class MusicController:
    """音乐控制器类"""
    
    def __init__(self):
        self.current_process = None
        self.current_song = ""
        self.song_queue = []
        self.is_playing = False
    
    def stop_music(self):
        print("停止音乐")
        """停止当前播放的音乐"""
        try:
            print("正在停止所有音乐播放...")
            
            # 直接使用pkill -9强制杀死所有ffplay进程
            try:
                result = subprocess.run(["pkill", "-9", "ffplay"], 
                                      stdout=subprocess.DEVNULL, 
                                      stderr=subprocess.DEVNULL, 
                                      timeout=3)
                print("已强制停止所有ffplay进程")
            except subprocess.TimeoutExpired:
                print("pkill命令超时，但继续执行")
            except Exception as e:
                print(f"pkill命令执行出错: {e}")
            
            # 重置状态
            self.current_process = None
            self.is_playing = False
            print("音乐已停止")
            return True
            
        except Exception as e:
            print(f"停止音乐时出错: {e}")
            self.is_playing = False
            return False
    
    def play_music(self, song_name):
        """播放音乐"""
        print(f"播放音乐: {song_name}")
        # 先停止当前音乐
        self.stop_music()
        
        # 等待一下确保进程完全停止
        time.sleep(0.5)
        
        # 搜索并播放新音乐
        print(f"开始播放: {song_name}")
        result = play_cloud_music(song_name)
        if result == 0:
            self.current_song = song_name
            self.is_playing = True
            print(f"成功开始播放: {song_name}")
            return True
        else:
            print(f"播放失败: {song_name}")
            return False
    
    def next_song(self):
        """切到下一首歌"""
        if self.song_queue:
            next_song = self.song_queue.pop(0)
            print(f"从队列播放下一首: {next_song}")
            return self.play_music(next_song)
        else:
            # 如果没有队列，尝试播放推荐歌曲
            print("队列为空，尝试播放推荐歌曲...")
            if self.current_song:
                recommended_songs = self.get_recommended_songs(self.current_song)
                print(f"基于当前歌曲 '{self.current_song}' 推荐: {recommended_songs}")
            else:
                recommended_songs = ["小幸运", "青花瓷", "稻香", "夜曲", "告白气球"]
                print(f"播放热门歌曲: {recommended_songs}")
            
            for song in recommended_songs:
                print(f"尝试播放: {song}")
                if self.play_music(song):
                    print(f"qqq播放成功: {song}")
                    return True
            
            print("无法找到可播放的歌曲")
            return False
    
    def add_to_queue(self, song_name):
        """添加歌曲到播放队列"""
        self.song_queue.append(song_name)
        print(f"已添加 {song_name} 到播放队列")
    
    def get_recommended_songs(self, current_song):
        """根据当前歌曲获取推荐歌曲"""
        # 简单的推荐逻辑，可以根据歌曲名称推荐相似歌曲
        recommendations = {
            "小幸运": ["青花瓷", "稻香", "夜曲", "告白气球"],
            "青花瓷": ["小幸运", "稻香", "夜曲", "东风破"],
            "稻香": ["小幸运", "青花瓷", "夜曲", "听妈妈的话"],
            "夜曲": ["小幸运", "青花瓷", "稻香", "七里香"],
            "告白气球": ["小幸运", "青花瓷", "稻香", "等你下课"],
            "东风破": ["青花瓷", "小幸运", "稻香", "夜曲"],
            "七里香": ["夜曲", "小幸运", "青花瓷", "稻香"],
            "听妈妈的话": ["稻香", "小幸运", "青花瓷", "夜曲"],
            "等你下课": ["告白气球", "小幸运", "青花瓷", "稻香"]
        }
        
        # 返回推荐歌曲，排除当前歌曲
        recommended = recommendations.get(current_song, ["小幸运", "青花瓷", "稻香", "夜曲"])
        return [song for song in recommended if song != current_song]
    
    def get_status(self):
        """获取播放状态"""
        return {
            "is_playing": self.is_playing,
            "current_song": self.current_song,
            "queue": self.song_queue.copy()
        }


# 全局音乐控制器实例
music_controller = MusicController()


def check_url(url):
    try:
        response = requests.head(url, timeout=(2,2)) #重定向之后会返回302
        if response.status_code == 200 or response.status_code == 301 or response.status_code == 302:
            #print(f"可以访问: {url}")
            url_location = response.headers.get('Location', '')
            #print("重新定向地址:",url_location)
            if url_location == 'http://music.163.com/404' or url_location == "":
                print("链接无效，返回404网页")
                return False
            else:
                print("链接有效，重定向成功")
                return True
        else:
            print(f"链接无法访问，状态码: {response.status_code}")
            return False

    except requests.exceptions.RequestException as e:
        print(f"请求错误: {e}")
        return False


def play_cloud_music(song_name):
    """播放云音乐（原始函数，保持兼容性）"""
    result = subprocess.run(["pkill", "-9", "ffplay"], 
                                      stdout=subprocess.DEVNULL, 
                                      stderr=subprocess.DEVNULL, 
                                      timeout=3)
    print("已强制停止所有ffplay进程")
    
    search_url = "https://music.163.com/api/search/get/web"
    params = {
        "csrf_token": "",
        "hlpretag": "",
        "hlposttag": "",
        "s": song_name,
        "type": 1,
        "offset": 0,
        "total": "true",
        "limit": 10
    }

    print("搜索歌曲:", song_name)
    
    try:
        response = requests.get(search_url, params=params, timeout=(2,2))
        response.raise_for_status()
    except requests.exceptions.RequestException as e:
        print(f"search_url请求错误: {e}")
        return -1

    if response.status_code != 200:
        print("请求失败")
        return -1

    data = response.json()

    if not data.get('result') or not data['result'].get('songs'):
        print("请求错误")
        return -1

    song_ids = [song['id'] for song in data['result']['songs']]

    print("搜索完成:", song_ids)

    for song_id in song_ids:
        song_url = "https://music.163.com/song/media/outer/url?id=%s.mp3" % (song_id)
        print("检查歌曲:", song_id)
        if check_url(song_url) == False:  # 如果不能播放
            continue  # 遍历下一个ID看看能不能播放
        
        print("检查完成，开始播放")
        
        # 使用subprocess.Popen启动播放，但不等待进程结束
        try:
            process = subprocess.Popen(
                ["ffplay", "-nodisp", "-autoexit", song_url],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            
            # 更新音乐控制器状态
            music_controller.current_process = process
            music_controller.current_song = song_name
            music_controller.is_playing = True
            
            print(f"成功启动播放: {song_name}")
            return 0  # 成功播放返回0
        except Exception as e:
            print(f"播放音乐时出错: {e}")
            continue

    return -1  # 播放失败返回-1


def stop_music():
    """停止当前播放的音乐"""
    return music_controller.stop_music()


def next_song():
    """切到下一首歌"""
    return music_controller.next_song()


def add_to_queue(song_name):
    """添加歌曲到播放队列"""
    music_controller.add_to_queue(song_name)


def get_music_status():
    """获取音乐播放状态"""
    return music_controller.get_status()


def play_music_with_control(song_name):
    """使用音乐控制器播放音乐"""
    return music_controller.play_music(song_name)


def get_recommended_songs(song_name=None):
    """获取推荐歌曲列表"""
    if song_name:
        return music_controller.get_recommended_songs(song_name)
    else:
        return music_controller.get_recommended_songs(music_controller.current_song)


# https://music.163.com/song/media/outer/url?id=423997333.mp3
# [423997333, 467953710, 2092339903, 1409118269, 1498103330, 2047739094, 2122671513, 2055147655, 2062567757, 2058309266]

if __name__ == "__main__":
    play_cloud_music("稻香")


