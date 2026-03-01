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
        output = self.config['output']
        self.output_file = output.get('wiki', output.get('file', '/app/data/corpus.ndjson'))
        
        self.visited: Set[str] = set()
        self.visited_titles: Set[str] = set()  # дедупликация по каноническому заголовку (редиректы РФ→Россия)
        self.queue: deque = deque()
        self.document_count = 0
        self.extractor = ContentExtractor()
        
        self.excluded_patterns = self.settings.get('excluded_patterns', [])
        self.min_paragraphs = self.settings.get('min_paragraphs', 3)
        self.min_word_count = self.settings.get('min_word_count', 1000)
    
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
        
        if paragraph_count < self.min_paragraphs:
            return None
        
        title = self.extractor.extract_title(html)
        if not title:
            return None
        
        word_count = len(text.split())
        if word_count < self.min_word_count:
            return None
        
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
            
            if article and article.title not in self.visited_titles:
                self.visited_titles.add(article.title)
                await self.save_article(article)
                self.document_count += 1
                
                if self.document_count % 100 == 0:
                    logger.info(f"Progress: {self.document_count}/{self.settings['max_documents']} documents")
            
            if depth < self.settings['max_depth']:
                links = self.extractor.extract_links(html, url)
                for link in links[:150]:  # больше ссылок со страниц категорий
                    if self.is_valid_url(link):
                        link_normalized = self.normalize_url(link)
                        if link_normalized not in self.visited:
                            self.queue.append((link, depth + 1))
    
    async def run(self):
        logger.info("Starting scraper...")
        logger.info(f"Target: {self.settings['max_documents']} documents")
        logger.info(f"Min paragraphs: {self.min_paragraphs}")
        logger.info(f"Min word count: {self.min_word_count}")
        
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


class CyberLeninkaScraper:

    def __init__(self, config_path: str):
        with open(config_path, 'r', encoding='utf-8') as f:
            self.config = yaml.safe_load(f)

        self.settings = self.config['cyberleninka']
        output = self.config['output']
        self.output_file = output.get('cyberleninka', '/app/data/corpus2.ndjson')

        self.visited: Set[str] = set()
        self.document_count = 0
        self.min_word_count = self.settings.get('min_word_count', 1000)

    async def fetch_page(self, session: aiohttp.ClientSession, url: str) -> Optional[str]:
        headers = {
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
            'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
            'Accept-Language': 'ru-RU,ru;q=0.9,en;q=0.8',
            'Referer': 'https://cyberleninka.ru/',
        }
        try:
            async with session.get(url, headers=headers, timeout=aiohttp.ClientTimeout(total=self.settings['timeout_seconds'])) as response:
                if response.status == 200:
                    return await response.text()
                logger.warning(f"Fetch {url}: HTTP {response.status}")
        except Exception as e:
            logger.warning(f"Failed to fetch {url}: {e}")
        return None

    def extract_article_links(self, html: str) -> List[str]:
        soup = BeautifulSoup(html, 'lxml')
        links = []
        for a in soup.find_all('a', href=True):
            href = a['href']
            if href.startswith('/article/n/'):
                links.append('https://cyberleninka.ru' + href)
        return links

    def extract_article(self, html: str, url: str) -> Optional[Article]:
        soup = BeautifulSoup(html, 'lxml')

        title_tag = soup.find('h1')
        if not title_tag:
            return None
        raw_title = title_tag.get_text(strip=True)
        title = re.sub(r'\s*Текст научной статьи по специальности.*$', '', raw_title)

        text_header = None
        for h2 in soup.find_all(['h2', 'h3']):
            if 'Текст научной работы' in h2.get_text():
                text_header = h2
                break

        if not text_header:
            return None

        text_parts = []
        for sibling in text_header.find_next_siblings():
            tag_name = sibling.name
            if tag_name in ('h1', 'h2', 'h3'):
                heading_text = sibling.get_text(strip=True)
                if heading_text and 'Список литературы' in heading_text:
                    break
                if heading_text and heading_text.startswith('Похожие тем'):
                    break
                continue
            text = sibling.get_text(separator=' ', strip=True)
            text = re.sub(r'\s+', ' ', text)
            text = re.sub(r'\[\d+\]', '', text)
            if len(text) > 30:
                text_parts.append(text)

        full_text = '\n'.join(text_parts)
        word_count = len(full_text.split())

        if word_count < self.min_word_count:
            return None

        paragraphs = [p for p in text_parts if len(p) > 50]
        return Article(
            url=url,
            title=title,
            text=full_text,
            html='',
            word_count=word_count,
            paragraph_count=len(paragraphs)
        )

    async def save_article(self, article: Article):
        data = {
            'url': article.url,
            'title': article.title,
            'text': article.text,
            'word_count': article.word_count,
            'paragraph_count': article.paragraph_count
        }
        async with aiofiles.open(self.output_file, 'a', encoding='utf-8') as f:
            await f.write(json.dumps(data, ensure_ascii=False) + '\n')

    async def run(self):
        logger.info("Starting CyberLeninka scraper...")
        logger.info(f"Target: {self.settings['max_documents']} articles")
        logger.info(f"Min word count: {self.min_word_count}")

        async with aiofiles.open(self.output_file, 'w', encoding='utf-8') as f:
            pass

        connector = aiohttp.TCPConnector(limit=2)
        delay = self.settings['request_delay_ms'] / 1000

        async with aiohttp.ClientSession(connector=connector) as session:
            for base_url in self.settings['category_urls']:
                if self.document_count >= self.settings['max_documents']:
                    break

                for page in range(1, self.settings['max_pages'] + 1):
                    if self.document_count >= self.settings['max_documents']:
                        break

                    page_url = base_url if page == 1 else f"{base_url}/{page}"
                    logger.info(f"Загрузка страницы каталога {page}: {page_url}")
                    page_html = None
                    for attempt in range(3):
                        await asyncio.sleep(delay * (attempt + 1))
                        page_html = await self.fetch_page(session, page_url)
                        if page_html:
                            break
                        logger.warning(f"Retry {attempt + 1}/3 for page {page}")
                        await asyncio.sleep(2)

                    if not page_html:
                        logger.warning(f"Failed to load listing page {page} after retries, stopping")
                        break

                    article_links = self.extract_article_links(page_html)
                    if not article_links:
                        logger.info(f"No more articles on page {page}, stopping")
                        break

                    for link in article_links:
                        if self.document_count >= self.settings['max_documents']:
                            break
                        if link in self.visited:
                            continue
                        self.visited.add(link)
                        await asyncio.sleep(delay)
                        await self.process_one(session, link)

                    if self.document_count % 100 == 0 or page % 10 == 0:
                        logger.info(f"Progress: {self.document_count}/{self.settings['max_documents']} docs, page {page}")

                    await asyncio.sleep(1)

        logger.info(f"CyberLeninka scraping completed. Total: {self.document_count}")

    async def process_one(self, session, url):
        if self.document_count >= self.settings['max_documents']:
            return
        html = await self.fetch_page(session, url)
        if not html:
            return
        article = self.extract_article(html, url)
        if article:
            await self.save_article(article)
            self.document_count += 1
            title_short = article.title[:60] + '...' if len(article.title) > 60 else article.title
            logger.info(f"[{self.document_count}] {title_short} ({article.word_count} слов)")


async def main():
    import sys
    mode = sys.argv[1] if len(sys.argv) > 1 else 'wiki'
    config_path = '/app/config.yaml'

    if mode == 'cyberleninka':
        scraper = CyberLeninkaScraper(config_path)
    else:
        scraper = WikiScraper(config_path)

    await scraper.run()


if __name__ == '__main__':
    asyncio.run(main())
