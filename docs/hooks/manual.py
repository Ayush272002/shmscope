import re
from pathlib import Path

MANUAL = "manual.md"
SECTION = re.compile(r"^## (.+)$", re.MULTILINE)

version = ""
repo = ""


def on_config(config):
    global version, repo
    path = Path(config.config_file_path).parent / "VERSION"
    version = path.read_text().strip() if path.exists() else ""
    repo = (config.repo_url or "").rstrip("/")
    return config


def section(title, body):
    name = title.lower().replace(" ", "-")
    return (
        f'<div class="man-section man-{name}" markdown>\n\n'
        f"## {title}\n\n{body.strip()}\n\n</div>\n"
    )


def release():
    if not (version and repo):
        return ""
    url = f"{repo}/releases/tag/v{version}"
    return f'<p class="man-meta"><a href="{url}">v{version}</a></p>\n\n'


def on_page_markdown(markdown, page, **_):
    if page.file.src_uri != MANUAL:
        return markdown

    parts = SECTION.split(markdown)
    sections = zip(parts[1::2], parts[2::2])
    return release() + parts[0] + "\n" + "\n".join(section(t, b) for t, b in sections)
