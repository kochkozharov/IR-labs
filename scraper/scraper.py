import asyncio
import aiohttp
import aiofiles
import yaml
import json
import re
import hashlib
from urllib.parse import urljoin, urlparse, unquote
from bs4 import BeautifulSoup
from collections import deque
from dataclasses import dataclass, asdict
from typing import Set, List, Optional
import logging
import time

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


@dataclass
class Article:
    url: str
    title: str
    text: str
    html: str
    word_count: int
    paragraph_count: int


class ContentExtractor:
    
    @staticmethod
    def extract_text(html: str) -> tuple[str, int]:
        soup = BeautifulSoup(html, 'lxml')
        
        for tag in soup.find_all(['script', 'style', 'nav', 'footer', 'header']):
            tag.decompose()
        
        for cls in ['navbox', 'infobox', 'sidebar', 'mw-editsection', 
                    'reference', 'reflist', 'toc', 'catlinks', 'mw-jump-link']:
            for elem in soup.find_all(class_=re.compile(cls)):
                elem.decompose()
        
        for elem in soup.find_all(id=re.compile('(toc|catlinks|mw-navigation)')):
            elem.decompose()
        
        content = soup.find('div', {'id': 'mw-content-text'})
        if not content:
            content = soup.find('div', {'class': 'mw-parser-output'})
        if not content:
            content = soup
        
        paragraphs = content.find_all('p')
        paragraph_count = len([p for p in paragraphs if len(p.get_text(strip=True)) > 50])
        
        text_parts = []
        for p in paragraphs:
            text = p.get_text(separator=' ', strip=True)
            text = re.sub(r'\s+', ' ', text)
            text = re.sub(r'\[\d+\]', '', text)
            if len(text) > 30:
                text_parts.append(text)
        
        return '\n'.join(text_parts), paragraph_count
    
    @staticmethod
    def extract_title(html: str) -> str:
        soup = BeautifulSoup(html, 'lxml')
        title_tag = soup.find('h1', {'id': 'firstHeading'})
        if title_tag:
            return title_tag.get_text(strip=True)
        title_tag = soup.find('title')
        if title_tag:
            title = title_tag.get_text(strip=True)
            return title.replace(' — Википедия', '').strip()
        return ''
    
    @staticmethod
    def extract_links(html: str, base_url: str) -> List[str]:
        soup = BeautifulSoup(html, 'lxml')
        links = []
        
        content = soup.find('div', {'id': 'mw-content-text'})
        if not content:
            content = soup
        
        for a in content.find_all('a', href=True):
            href = a['href']
            if href.startswith('/wiki/'):
                full_url = urljoin(base_url, href)
                links.append(full_url)
        
        return links


class WikiScraper:
    
    def __init__(self, config_path: str):
        with open(config_path, 'r', encoding='utf-8') as f:
            self.config = yaml.safe_load(f)
        
        self.settings = self.config['scraper']
        self.output_file = self.config['output']['file']
        
        self.visited: Set[str] = set()
        self.queue: deque = deque()
        self.document_count = 0
        self.extractor = ContentExtractor()
        
        self.excluded_patterns = self.settings.get('excluded_patterns', [])
        self.min_text_length = self.settings.get('min_text_length', 1500)
        self.min_paragraphs = self.settings.get('min_paragraphs', 3)
    
    def normalize_url(self, url: str) -> str:
        parsed = urlparse(url)
        path = unquote(parsed.path)
        return f"{parsed.scheme}://{parsed.netloc}{path}"
    
    def is_valid_url(self, url: str) -> bool:
        if not url.startswith('http'):
            return False
        
        if 'wikipedia.org/wiki/' not in url:
            return False
        
        for pattern in self.excluded_patterns:
            if pattern in url:
                return False
        
        return True
    
    def url_hash(self, url: str) -> str:
        return hashlib.md5(url.encode()).hexdigest()
    
    async def fetch_page(self, session: aiohttp.ClientSession, url: str) -> Optional[str]:
        try:
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=self.settings['timeout_seconds'])) as response:
                if response.status == 200:
                    return await response.text()
        except Exception as e:
            logger.debug(f"Failed to fetch {url}: {e}")
        return None
    
    async def process_article(self, html: str, url: str) -> Optional[Article]:
        text, paragraph_count = self.extractor.extract_text(html)
        
        if len(text) < self.min_text_length:
            return None
        
        if paragraph_count < self.min_paragraphs:
            return None
        
        title = self.extractor.extract_title(html)
        if not title:
            return None
        
        word_count = len(text.split())
        
        return Article(
            url=url,
            title=title,
            text=text,
            html=html,
            word_count=word_count,
            paragraph_count=paragraph_count
        )
    
    async def save_article(self, article: Article):
        async with aiofiles.open(self.output_file, 'a', encoding='utf-8') as f:
            data = asdict(article)
            await f.write(json.dumps(data, ensure_ascii=False) + '\n')
    
    async def worker(self, session: aiohttp.ClientSession, semaphore: asyncio.Semaphore):
        while True:
            if self.document_count >= self.settings['max_documents']:
                return
            
            try:
                url, depth = self.queue.popleft()
            except IndexError:
                await asyncio.sleep(0.5)
                if not self.queue:
                    return
                continue
            
            normalized = self.normalize_url(url)
            if normalized in self.visited:
                continue
            
            self.visited.add(normalized)
            
            async with semaphore:
                await asyncio.sleep(self.settings['request_delay_ms'] / 1000)
                html = await self.fetch_page(session, url)
            
            if not html:
                continue
            
            article = await self.process_article(html, url)
            
            if article:
                await self.save_article(article)
                self.document_count += 1
                
                if self.document_count % 100 == 0:
                    logger.info(f"Progress: {self.document_count}/{self.settings['max_documents']} documents")
            
            if depth < self.settings['max_depth']:
                links = self.extractor.extract_links(html, url)
                for link in links[:50]:
                    if self.is_valid_url(link):
                        link_normalized = self.normalize_url(link)
                        if link_normalized not in self.visited:
                            self.queue.append((link, depth + 1))
    
    async def run(self):
        logger.info("Starting scraper...")
        logger.info(f"Target: {self.settings['max_documents']} documents")
        logger.info(f"Min text length: {self.min_text_length} chars")
        logger.info(f"Min paragraphs: {self.min_paragraphs}")
        
        async with aiofiles.open(self.output_file, 'w', encoding='utf-8') as f:
            pass
        
        for url in self.settings['start_urls']:
            self.queue.append((url, 0))
        
        connector = aiohttp.TCPConnector(limit=self.settings['concurrent_requests'])
        headers = {'User-Agent': self.settings['user_agent']}
        
        async with aiohttp.ClientSession(connector=connector, headers=headers) as session:
            semaphore = asyncio.Semaphore(self.settings['concurrent_requests'])
            
            workers = [
                asyncio.create_task(self.worker(session, semaphore))
                for _ in range(self.settings['concurrent_requests'])
            ]
            
            await asyncio.gather(*workers)
        
        logger.info(f"Scraping completed. Total documents: {self.document_count}")


async def main():
    scraper = WikiScraper('/app/config.yaml')
    await scraper.run()


if __name__ == '__main__':
    asyncio.run(main())
