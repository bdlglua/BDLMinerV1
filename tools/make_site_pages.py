"""Render separate public pages from a shared layout and small page templates."""
import html
import json
from pathlib import Path
import re


PAGES = {
    'home': ('index.html', 'Solo Bitcoin mining on ESP32', 'Minage Bitcoin solo sur ESP32'),
    'start': ('start.html', 'Get started', 'Bien démarrer'),
    'boards': ('boards.html', 'Supported boards', 'Cartes compatibles'),
    'flash': ('flash.html', 'Flash your board', 'Flasher ta carte'),
    'demo': ('demo.html', 'Interactive demos', 'Démos interactives'),
    'faq': ('faq.html', 'Troubleshooting', 'Dépannage'),
    'releases': ('releases.html', 'Releases', 'Nouveautés'),
    'stats': ('stats.html', 'Live statistics', 'Statistiques en direct'),
    'gallery': ('gallery.html', 'Community', 'Communauté'),
    'support': ('support.html', 'Support BDLMiner', 'Soutenir BDLMiner'),
}

LEGACY_SECTIONS = {
    'top': 'index.html', 'discover': 'start.html', 'boards': 'boards.html',
    'install': 'flash.html', 'demo': 'demo.html', 'help': 'faq.html',
    'news': 'releases.html', 'stats': 'stats.html', 'gallery': 'gallery.html',
    'community': 'gallery.html', 'support': 'support.html',
}


def build_pages(root, output, version, boards, content):
    root, output = Path(root), Path(output)
    layout = (root / 'installer/index.html').read_text(encoding='utf-8')
    titles = {key: {'en': page[1], 'fr': page[2]} for key, page in PAGES.items()}
    for key, (filename, title, _) in PAGES.items():
        body = (root / 'installer/pages' / (key + '.html')).read_text(encoding='utf-8')
        data = {**content, 'page': key, 'page_titles': titles, 'legacy_sections': LEGACY_SECTIONS}
        page = layout.replace('__PAGE_CONTENT__', body).replace('__PAGE_KEY__', key)
        page = page.replace('__PAGE_TITLE__', html.escape('BDLMiner V2: ' + title))
        page = page.replace('__BOARDS__', json.dumps(boards)).replace('__VERSION__', version)
        page = page.replace('__SITE_DATA__', json.dumps(data, ensure_ascii=False).replace('<', '\\u003c'))
        page = re.sub(r'href="#([a-z-]+)"', lambda match: 'href="' + LEGACY_SECTIONS.get(match[1], '#' + match[1]) + '"', page)
        if key != 'flash':
            page = re.sub(r'<script type="module" src="https://unpkg.com/esp-web-tools[^>]+></script>\n?', '', page)
        (output / filename).write_text(page, encoding='utf-8')
