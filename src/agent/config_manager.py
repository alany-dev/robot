#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
配置文件管理模块
用于统一管理 API key 等配置信息
"""

import os
import json
from typing import Dict, Any, Optional

class ConfigManager:
    """配置管理器类"""
    
    def __init__(self, config_file: str = None):
        """
        初始化配置管理器
        
        Args:
            config_file: 配置文件路径，默认为当前目录下的 config.json
        """
        if config_file is None:
            # 默认配置文件路径
            current_dir = os.path.dirname(os.path.abspath(__file__))
            config_file = os.path.join(current_dir, "config.json")
        
        self.config_file = config_file
        self.config = self.load_config()
    
    def load_config(self) -> Dict[str, Any]:
        """
        从文件加载配置
        
        Returns:
            配置字典
        """
        if not os.path.exists(self.config_file):
            # 如果配置文件不存在，返回默认配置
            return self._get_default_config()
        
        try:
            with open(self.config_file, "r", encoding="utf-8") as f:
                content = f.read().strip()
                if not content:
                    return self._get_default_config()
                return json.loads(content)
        except Exception as e:
            print(f"加载配置文件失败: {e}")
            return self._get_default_config()
    
    def save_config(self, config: Dict[str, Any] = None) -> bool:
        """
        保存配置到文件
        
        Args:
            config: 配置字典，如果为None则保存当前配置
            
        Returns:
            是否成功
        """
        if config is not None:
            self.config = config
        
        try:
            # 确保目录存在
            os.makedirs(os.path.dirname(self.config_file), exist_ok=True)
            
            # 使用临时文件避免写入过程中的数据损坏
            temp_file = f"{self.config_file}.tmp"
            with open(temp_file, "w", encoding="utf-8") as f:
                json.dump(self.config, f, ensure_ascii=False, indent=2)
            
            # 原子性替换原文件
            import shutil
            shutil.move(temp_file, self.config_file)
            print(f"配置已保存到: {self.config_file}")
            return True
        except Exception as e:
            print(f"保存配置文件失败: {e}")
            # 清理临时文件
            if os.path.exists(temp_file):
                try:
                    os.remove(temp_file)
                except:
                    pass
            return False
    
    def get(self, key: str, default: Any = None) -> Any:
        """
        获取配置项
        
        Args:
            key: 配置键名
            default: 默认值
            
        Returns:
            配置值
        """
        return self.config.get(key, default)
    
    def set(self, key: str, value: Any) -> bool:
        """
        设置配置项
        
        Args:
            key: 配置键名
            value: 配置值
            
        Returns:
            是否成功
        """
        self.config[key] = value
        return self.save_config()
    
    def update(self, updates: Dict[str, Any]) -> bool:
        """
        批量更新配置
        
        Args:
            updates: 要更新的配置字典
            
        Returns:
            是否成功
        """
        self.config.update(updates)
        return self.save_config()
    
    def _get_default_config(self) -> Dict[str, Any]:
        """
        获取默认配置
        
        Returns:
            默认配置字典
        """
        return {
            "api_key": "",  # 空字符串表示未配置
            "model": "qwen-plus",
            "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
            "tts_model": "cosyvoice-v2",
            "tts_voice": "longfeifei_v2",
            "enable_tts": True,
            "wake_words": ["小沫小沫", "小莫小莫", "小墨小墨"]
        }
    
    def get_api_key(self) -> str:
        """
        获取 API key
        
        Returns:
            API key 字符串
        """
        return self.get("api_key", "")
    
    def set_api_key(self, api_key: str) -> bool:
        """
        设置 API key
        
        Args:
            api_key: API key 字符串
            
        Returns:
            是否成功
        """
        return self.set("api_key", api_key)
    
    def is_api_key_configured(self) -> bool:
        """
        检查 API key 是否已配置
        
        Returns:
            是否已配置
        """
        api_key = self.get_api_key()
        return api_key is not None and api_key.strip() != ""
    
    def get_all_config(self) -> Dict[str, Any]:
        """
        获取所有配置（隐藏敏感信息）
        
        Returns:
            配置字典
        """
        config_copy = self.config.copy()
        # 隐藏 API key，只显示前4位和后4位
        if "api_key" in config_copy and config_copy["api_key"]:
            api_key = config_copy["api_key"]
            if len(api_key) > 8:
                config_copy["api_key_masked"] = f"{api_key[:4]}{'*' * (len(api_key) - 8)}{api_key[-4:]}"
                config_copy["api_key_configured"] = True
            else:
                config_copy["api_key_masked"] = "****"
                config_copy["api_key_configured"] = True
            del config_copy["api_key"]  # 不返回完整的 API key
        else:
            config_copy["api_key_configured"] = False
            config_copy["api_key_masked"] = ""
        
        return config_copy


# 全局配置管理器实例
_global_config_manager = None

def get_global_config_manager() -> ConfigManager:
    """
    获取全局配置管理器实例
    
    Returns:
        ConfigManager: 全局配置管理器实例
    """
    global _global_config_manager
    if _global_config_manager is None:
        _global_config_manager = ConfigManager()
    return _global_config_manager

def get_api_key() -> str:
    """
    便捷函数：获取 API key
    
    Returns:
        API key 字符串
    """
    return get_global_config_manager().get_api_key()

def set_api_key(api_key: str) -> bool:
    """
    便捷函数：设置 API key
    
    Args:
        api_key: API key 字符串
        
    Returns:
        是否成功
    """
    return get_global_config_manager().set_api_key(api_key)


# 使用示例
if __name__ == "__main__":
    config_mgr = ConfigManager()
    
    print("当前配置:")
    print(json.dumps(config_mgr.get_all_config(), ensure_ascii=False, indent=2))
    
    # 测试设置 API key
    test_key = "sk-test123456789"
    print(f"\n设置 API key: {test_key}")
    config_mgr.set_api_key(test_key)
    
    print("\n更新后的配置:")
    print(json.dumps(config_mgr.get_all_config(), ensure_ascii=False, indent=2))
    
    print(f"\n获取 API key: {config_mgr.get_api_key()}")
    print(f"API key 是否已配置: {config_mgr.is_api_key_configured()}")

