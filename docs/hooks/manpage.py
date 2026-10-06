import re
from pathlib import Path

MANUAL = "shmscope.1.md"
HEADING = re.compile(r"^# (.+)$", re.MULTILINE)
STRONG = re.compile(r"\*\*(.+?)\*\*")
MANREF = re.compile(r"\*\*([\w.-]+)\*\*\((\d)\)")
MAN7 = "https://man7.org/linux/man-pages/man{1}/{0}.{1}.html"

version = ""
repo = ""


def on_config(config):
    global version, repo
    path = Path(config.config_file_path).parent / "VERSION"
    version = path.read_text().strip() if path.exists() else ""
    repo = (config.repo_url or "").rstrip("/")
    return config


def slug(title):
    return title.lower().replace(" ", "-")


def section(title, body):
    name = slug(title)
    if name == "keys":
        body = STRONG.sub(r"<kbd>\1</kbd>", body)
    if name == "layouts":
        body = STRONG.sub(lambda m: m[1] if m[1] == "shmscope" else f"`{m[1]}`", body)
        body += "\n\nThe [layout guide](layouts.md) covers each feature with examples.\n"
    if name == "see-also":
        body = MANREF.sub(
            lambda m: f"[`{m[1]}({m[2]})`]({MAN7.format(m[1], m[2])})", body
        )
        body = "\n".join(
            line for line in body.splitlines() if not line.startswith("Project site:")
        )
    if name == "notes":
        body = '!!! warning ""\n' + "\n".join(
            f"    {line}" if line else "" for line in body.splitlines()
        )
    return (
        f'<div class="man-section man-{name}" markdown>\n\n'
        f"## {title.capitalize()}\n\n{body.strip()}\n\n</div>\n"
    )


def hero(summary):
    tool = Path(MANUAL).stem.rsplit(".", 1)[0]
    release = ""
    if version and repo:
        url = f"{repo}/releases/tag/v{version}"
        release = f'<p class="man-meta"><a href="{url}">v{version}</a></p>\n\n'
    return f"{release}# {tool}\n\n" f'<p class="man-lead">{summary}</p>\n'


def on_page_markdown(markdown, page, **_):
    if page.file.src_uri != MANUAL:
        return markdown

    page.meta["title"] = page.title

    parts = HEADING.split(markdown)
    sections = list(zip(parts[1::2], parts[2::2]))

    summary = ""
    out = []
    for title, body in sections:
        if title == "NAME":
            summary = body.strip().split(" - ", 1)[-1]
            summary = summary[:1].upper() + summary[1:]
            continue
        out.append(section(title, body))

    return hero(summary) + "\n" + "\n".join(out)
