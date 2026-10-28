"""Copies docs/ into a clone of the GitHub wiki, with links rewritten for the wiki.

The pages live in docs/ so every download and fork has them; the GitHub wiki is a separate
repository that a zip or a fork does not include. This script keeps the two in step. It only
writes files into the wiki clone; committing and pushing it is left to you.

    git clone https://github.com/<owner>/<repo>.wiki.git ../smalti.wiki   # once
    python tools/publish_wiki.py ../smalti.wiki

In docs/, pages link to each other as `Page-name.md`; on the wiki the same link is `Page-name`.
Links that leave docs/ (such as ../CONTRIBUTING.md) become links into the repository.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
REPO_URL = "https://github.com/DavidNyholm/Smalti/blob/main/"

LOCAL_PAGE_LINK = re.compile(r"\]\(([A-Za-z0-9-]+)\.md(#[^)]*)?\)")
PARENT_LINK = re.compile(r"\]\(\.\./([^)]+)\)")


def wiki_text(markdown: str) -> str:
    markdown = LOCAL_PAGE_LINK.sub(lambda m: f"]({m.group(1)}{m.group(2) or ''})", markdown)
    return PARENT_LINK.sub(lambda m: f"]({REPO_URL}{m.group(1)})", markdown)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wiki_clone", type=Path, help="a local clone of the repository's wiki")
    opts = parser.parse_args()
    wiki: Path = opts.wiki_clone
    if not (wiki / ".git").exists():
        print(f"{wiki} is not a git clone of the wiki; clone it first (see the docstring)")
        return 2
    copied = 0
    for page in sorted(DOCS.glob("*.md")):
        (wiki / page.name).write_text(
            wiki_text(page.read_text(encoding="utf-8")), encoding="utf-8", newline="\n"
        )
        copied += 1
    images = DOCS / "images"
    if images.is_dir():
        shutil.copytree(images, wiki / "images", dirs_exist_ok=True)
    print(f"copied {copied} pages into {wiki}; review with git diff there, then commit and push")
    return 0


if __name__ == "__main__":
    sys.exit(main())
