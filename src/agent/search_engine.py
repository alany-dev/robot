#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
搜索引擎模块
提供网络搜索功能，支持多种搜索引擎
"""

import requests
import json
import time
from typing import List, Dict, Any, Optional
import urllib.parse
from bs4 import BeautifulSoup
import re

class SearchEngine:
    """搜索引擎类"""
    
    def __init__(self):
        """初始化搜索引擎"""
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'
        })
        
    def search_baidu(self, query: str, max_results: int = 5) -> List[Dict[str, str]]:
        """
        使用百度搜索
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            List[Dict]: 搜索结果列表
        """
        try:
            # 百度搜索URL
            search_url = "https://www.baidu.com/s"
            params = {
                'wd': query,
                'rn': max_results
            }
            
            response = self.session.get(search_url, params=params, timeout=10)
            response.raise_for_status()
            
            soup = BeautifulSoup(response.text, 'html.parser')
            results = []
            
            # 解析搜索结果
            for item in soup.find_all('div', class_='result'):
                try:
                    title_elem = item.find('h3')
                    link_elem = item.find('a')
                    content_elem = item.find('span', class_='content-right_8Zs40')
                    
                    if title_elem and link_elem:
                        title = title_elem.get_text(strip=True)
                        link = link_elem.get('href', '')
                        content = content_elem.get_text(strip=True) if content_elem else ""
                        
                        results.append({
                            'title': title,
                            'url': link,
                            'content': content[:200] + '...' if len(content) > 200 else content
                        })
                except Exception as e:
                    continue
            
            return results[:max_results]
            
        except Exception as e:
            print(f"百度搜索出错: {e}")
            return []
    
    def search_bing(self, query: str, max_results: int = 5) -> List[Dict[str, str]]:
        """
        使用必应搜索
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            List[Dict]: 搜索结果列表
        """
        try:
            # 必应搜索URL
            search_url = "https://www.bing.com/search"
            params = {
                'q': query,
                'count': max_results
            }
            
            response = self.session.get(search_url, params=params, timeout=10)
            response.raise_for_status()
            
            soup = BeautifulSoup(response.text, 'html.parser')
            results = []
            
            # 解析搜索结果
            for item in soup.find_all('li', class_='b_algo'):
                try:
                    title_elem = item.find('h2')
                    link_elem = item.find('a')
                    content_elem = item.find('p')
                    
                    if title_elem and link_elem:
                        title = title_elem.get_text(strip=True)
                        link = link_elem.get('href', '')
                        content = content_elem.get_text(strip=True) if content_elem else ""
                        
                        results.append({
                            'title': title,
                            'url': link,
                            'content': content[:200] + '...' if len(content) > 200 else content
                        })
                except Exception as e:
                    continue
            
            return results[:max_results]
            
        except Exception as e:
            print(f"必应搜索出错: {e}")
            return []
    
    def search_duckduckgo(self, query: str, max_results: int = 5) -> List[Dict[str, str]]:
        """
        使用DuckDuckGo搜索
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            List[Dict]: 搜索结果列表
        """
        try:
            # DuckDuckGo搜索URL
            search_url = "https://html.duckduckgo.com/html/"
            params = {
                'q': query
            }
            
            response = self.session.post(search_url, data=params, timeout=10)
            response.raise_for_status()
            
            soup = BeautifulSoup(response.text, 'html.parser')
            results = []
            
            # 解析搜索结果
            for item in soup.find_all('div', class_='result'):
                try:
                    title_elem = item.find('a', class_='result__a')
                    content_elem = item.find('a', class_='result__snippet')
                    
                    if title_elem:
                        title = title_elem.get_text(strip=True)
                        link = title_elem.get('href', '')
                        content = content_elem.get_text(strip=True) if content_elem else ""
                        
                        results.append({
                            'title': title,
                            'url': link,
                            'content': content[:200] + '...' if len(content) > 200 else content
                        })
                except Exception as e:
                    continue
            
            return results[:max_results]
            
        except Exception as e:
            print(f"DuckDuckGo搜索出错: {e}")
            return []
    
    def search_web(self, query: str, max_results: int = 5) -> List[Dict[str, str]]:
        """
        网络搜索接口（使用Bing）
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            List[Dict]: 搜索结果列表
        """
        print(f" 正在搜索: {query} (使用Bing)")
        return self.search_bing(query, max_results)
    
    def search_and_summarize(self, query: str, max_results: int = 3) -> str:
        """
        搜索并生成摘要（使用Bing）
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            str: 搜索结果的摘要
        """
        results = self.search_web(query, max_results)
        
        if not results:
            return f"抱歉，未能找到关于'{query}'的相关信息。"
        
        # 生成摘要
        summary_parts = [f"关于'{query}'的搜索结果：\n"]
        
        for i, result in enumerate(results, 1):
            summary_parts.append(f"{i}. {result['title']}")
            if result['content']:
                summary_parts.append(f"   {result['content']}")
            summary_parts.append("")  # 空行分隔
        
        return "\n".join(summary_parts)
    
    def search_news(self, query: str, max_results: int = 3) -> List[Dict[str, str]]:
        """
        搜索新闻
        
        Args:
            query: 搜索关键词
            max_results: 最大结果数量
            
        Returns:
            List[Dict]: 新闻搜索结果列表
        """
        news_query = f"{query} 新闻"
        return self.search_web(news_query, max_results)
    
    def search_wikipedia(self, query: str) -> Optional[str]:
        """
        搜索维基百科
        
        Args:
            query: 搜索关键词
            
        Returns:
            Optional[str]: 维基百科内容摘要
        """
        try:
            # 使用中文维基百科
            wiki_url = "https://zh.wikipedia.org/w/api.php"
            params = {
                'action': 'query',
                'format': 'json',
                'list': 'search',
                'srsearch': query,
                'srlimit': 1
            }
            
            response = self.session.get(wiki_url, params=params, timeout=10)
            response.raise_for_status()
            
            data = response.json()
            
            if 'query' in data and 'search' in data['query'] and data['query']['search']:
                page_title = data['query']['search'][0]['title']
                
                # 获取页面内容
                content_params = {
                    'action': 'query',
                    'format': 'json',
                    'prop': 'extracts',
                    'exintro': True,
                    'explaintext': True,
                    'titles': page_title
                }
                
                content_response = self.session.get(wiki_url, params=content_params, timeout=10)
                content_response.raise_for_status()
                
                content_data = content_response.json()
                
                if 'query' in content_data and 'pages' in content_data['query']:
                    pages = content_data['query']['pages']
                    for page_id, page_data in pages.items():
                        if 'extract' in page_data:
                            return f"维基百科 - {page_title}:\n{page_data['extract'][:500]}..."
            
            return None
            
        except Exception as e:
            print(f"维基百科搜索出错: {e}")
            return None


# 全局搜索引擎实例
_global_search_engine = None

def get_global_search_engine() -> SearchEngine:
    """获取全局搜索引擎实例"""
    global _global_search_engine
    if _global_search_engine is None:
        _global_search_engine = SearchEngine()
    return _global_search_engine

def search_web(query: str, max_results: int = 5) -> List[Dict[str, str]]:
    """便捷函数：网络搜索（使用Bing）"""
    return get_global_search_engine().search_web(query, max_results)

def search_and_summarize(query: str, max_results: int = 3) -> str:
    """便捷函数：搜索并生成摘要（使用Bing）"""
    return get_global_search_engine().search_and_summarize(query, max_results)

def search_news(query: str, max_results: int = 3) -> List[Dict[str, str]]:
    """便捷函数：搜索新闻"""
    return get_global_search_engine().search_news(query, max_results)

def search_wikipedia(query: str) -> Optional[str]:
    """便捷函数：搜索维基百科"""
    return get_global_search_engine().search_wikipedia(query)


# 使用示例
if __name__ == "__main__":
    print(" 测试搜索引擎功能")
    print("=" * 50)
    
    # 创建搜索引擎实例
    se = SearchEngine()
    
    # 测试网络搜索
    print("\n1. 测试网络搜索")
    results = se.search_web("户晨风", 3)
    for i, result in enumerate(results, 1):
        print(f"{i}. {result['title']}")
        print(f"   {result['content']}")
        print(f"   URL: {result['url']}")
        print()
    
        # 测试搜索摘要
        print("\n2. 测试搜索摘要")
        summary = se.search_and_summarize("人工智能发展", 2)
    print(summary)
    
    # 测试新闻搜索
    print("\n3. 测试新闻搜索")
    news_results = se.search_news("科技新闻", 2)
    for i, result in enumerate(news_results, 1):
        print(f"{i}. {result['title']}")
        print(f"   {result['content']}")
        print()
    
    print(" 搜索引擎测试完成")
