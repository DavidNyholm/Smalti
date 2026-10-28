"""Privacy check: refuses to commit or push anything that should never be public.

Runs as three git hooks, so a problem is caught when it is committed rather than when it is
about to be published:

    pre-commit   the staged changes: added lines, file names, sizes, and who is committing
    commit-msg   the commit message
    pre-push     every commit about to leave this machine: messages, authors, names, added lines

It refuses local paths, home directories, the current login name as a path segment, e-mail
addresses other than GitHub's noreply one, token-shaped strings, private keys, credential
assignments, assistant and agent files, large files, and any file the repository's ignore rules
say should not be tracked. That last check includes the local-only .git/info/exclude, so files
listed there are refused even when added with `git add -f`. It is a heuristic over the text git
shows as added and over file names (binaries are only size-checked), not a proof. Nothing here
knows your name or paths: it reads them from the environment at run time, so the script itself
contains nothing personal.

    python tools/privacy_check.py --install      # install the three hooks (once per clone)
    python tools/privacy_check.py --staged       # check the staged changes by hand
    python tools/privacy_check.py --all          # scan every commit on every branch
    python tools/privacy_check.py --range origin/main..main   # scan a git range by hand
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ZERO_SHA = "0" * 40
LARGE_FILE_BYTES = 1_000_000  # the bundled font files are well under this; nothing else should come close

# Addresses that are fine to publish: GitHub's privacy relay, the model attribution, and the
# address in GitHub's SSH URLs.
ALLOWED_EMAIL_SUFFIXES = ("@users.noreply.github.com", "noreply@anthropic.com")
ALLOWED_EMAILS = {"git@github.com"}

# Files that are personal tooling, secrets or generated output, never part of the project.
# Files that only this machine produces belong in .git/info/exclude instead, which is enforced too.
BLOCKED_FILE_PATTERNS = [
    r"(^|/)AGENTS\.md$",
    r"(^|/)CLAUDE(\.local)?\.md$",
    r"(^|/)GEMINI\.md$",
    r"(^|/)\.claude(/|$)",
    r"(^|/)\.codex(/|$)",
    r"(^|/)\.gemini(/|$)",
    r"(^|/)\.cursor(/|$)",
    r"(^|/)\.cursorrules$",
    r"(^|/)\.aider",
    r"(^|/)CMakeUserPresets\.json$",
    r"\.log$",
    r"\.jsonl$",
    r"(^|/)\.env($|\.)",
    r"\.pem$",
    r"\.key$",
    r"\.pfx$",
    r"\.p12$",
]

# Content that must not appear on an added line or in a commit message. (name, regex)
BLOCKED_TEXT_PATTERNS = [
    # A drive letter, a colon and a slash (a Windows absolute path); a URL scheme does not
    # match because the letter must not follow another letter and the slash is not doubled.
    ("drive-letter path", r"(?<![A-Za-z0-9])[A-Za-z]:[\\/](?![\\/])\S"),
    (
        "home directory",
        r"(/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[\\/]Users[\\/][A-Za-z0-9._-]+[\\/])",
    ),
    ("GitHub token", r"\b(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{20,}\b|\bgithub_pat_[A-Za-z0-9_]{20,}\b"),
    ("OpenAI-style key", r"\bsk-[A-Za-z0-9_-]{20,}\b"),
    ("AWS access key", r"\bAKIA[0-9A-Z]{16}\b"),
    ("Slack token", r"\bxox[abpr]-[A-Za-z0-9-]{10,}\b"),
    ("Google API key", r"\bAIza[0-9A-Za-z_-]{35}\b"),
    ("private key block", r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
    (
        "credential assignment",
        r"(?i)\b(api[_-]?key|secret|token|password|passwd)\b\s*[:=]\s*[\"'][^\"']{8,}[\"']",
    ),
]
EMAIL_PATTERN = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
IDENT_EMAIL_PATTERN = re.compile(r"<([^>]*)>")


def git(*args: str, stdin: str | None = None, ok_codes: tuple[int, ...] = (0,)) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        input=stdin,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode not in ok_codes:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def login_name() -> str:
    for key in ("USERNAME", "USER", "LOGNAME"):
        if os.environ.get(key):
            return os.environ[key]
    return ""


def personal_patterns() -> list[tuple[str, re.Pattern[str]]]:
    """Patterns built from this machine's identity, never written into the script."""
    patterns = []
    name = login_name()
    if name:
        # Only as a path segment: the name may legitimately appear as an author credit.
        patterns.append(("login name in a path", re.compile(r"[\\/]" + re.escape(name) + r"[\\/]", re.I)))
    home = str(Path.home())
    if len(home) > 3:
        patterns.append(("home directory path", re.compile(re.escape(home), re.I)))
        patterns.append(("home directory path", re.compile(re.escape(home.replace("\\", "/")), re.I)))
    return patterns


def email_allowed(address: str) -> bool:
    return address in ALLOWED_EMAILS or address.endswith(ALLOWED_EMAIL_SUFFIXES)


class Findings:
    def __init__(self) -> None:
        self.items: list[str] = []

    def add(self, where: str, what: str, sample: str) -> None:
        sample = sample.strip()
        if len(sample) > 100:
            sample = sample[:100] + "..."
        self.items.append(f"  {where}: {what}: {sample}")


def check_text(findings: Findings, where: str, text: str, extra: list[tuple[str, re.Pattern[str]]]) -> None:
    for name, pattern in BLOCKED_TEXT_PATTERNS:
        for match in re.finditer(pattern, text):
            findings.add(where, name, match.group(0))
    for name, pattern in extra:
        for match in pattern.finditer(text):
            findings.add(where, name, match.group(0))
    for match in EMAIL_PATTERN.finditer(text):
        if not email_allowed(match.group(0)):
            findings.add(where, "e-mail address", match.group(0))


def check_file_names(
    findings: Findings, prefix: str, paths: list[str], extra: list[tuple[str, re.Pattern[str]]]
) -> None:
    """File names can leak too; blocked names and anything the ignore rules cover are refused."""
    for path in paths:
        check_text(findings, f"{prefix}file name", path, extra)
        for pattern in BLOCKED_FILE_PATTERNS:
            if re.search(pattern, path):
                findings.add(f"{prefix}{path}", "file that must stay local", path)
                break
    if paths:
        # --no-index asks whether the rules would ignore the path, tracked or not. This is what
        # catches `git add -f` on something listed in .gitignore or .git/info/exclude.
        # Exit code 1 means none of the paths is ignored. NUL-separated (-z) because text-mode
        # stdin on Windows turns "\n" into "\r\n", and git would then match "PLAN.md\r".
        ignored = git(
            "check-ignore", "--no-index", "--stdin", "-z", stdin="\0".join(paths) + "\0", ok_codes=(0, 1)
        )
        for path in filter(None, ignored.split("\0")):
            findings.add(f"{prefix}{path}", "file the ignore rules exclude", path)


def check_added_lines(
    findings: Findings, prefix: str, diff: str, extra: list[tuple[str, re.Pattern[str]]]
) -> None:
    current_file = ""
    for raw in diff.splitlines():
        if raw.startswith("+++ "):
            current_file = raw[4:].removeprefix("b/")
        elif raw.startswith("+") and not raw.startswith("+++"):
            check_text(findings, f"{prefix}{current_file}", raw[1:], extra)


def check_size(findings: Findings, where: str, blob: str) -> None:
    try:
        size = int(git("cat-file", "-s", blob))
    except RuntimeError:
        return  # deleted
    if size > LARGE_FILE_BYTES:
        findings.add(where, "large file", f"{size} bytes")


def check_range(findings: Findings, rev_range: str, extra: list[tuple[str, re.Pattern[str]]]) -> int:
    """Checks every commit in the range. Returns how many commits were looked at."""
    commits = git("rev-list", "--reverse", rev_range).split()
    for sha in commits:
        short = sha[:7]
        message = git("log", "-1", "--format=%B", sha)
        check_text(findings, f"{short} commit message", message, extra)
        for address in git("log", "-1", "--format=%ae %ce", sha).split():
            if not email_allowed(address):
                findings.add(f"{short} author/committer", "e-mail address", address)
        names = git(
            "diff-tree", "--no-commit-id", "-r", "--name-only", "--root", "-m", "--first-parent", sha
        ).splitlines()
        check_file_names(findings, f"{short} ", names, extra)
        for name in names:
            check_size(findings, f"{short} {name}", f"{sha}:{name}")
        diff = git(
            "show", "--format=", "--unified=0", "--no-color", "--no-ext-diff", "-m", "--first-parent", sha
        )
        check_added_lines(findings, f"{short} ", diff, extra)
    return len(commits)


def check_staged(findings: Findings, extra: list[tuple[str, re.Pattern[str]]]) -> None:
    """What pre-commit runs: the identity about to be recorded, and everything staged."""
    for var in ("GIT_AUTHOR_IDENT", "GIT_COMMITTER_IDENT"):
        match = IDENT_EMAIL_PATTERN.search(git("var", var))
        address = match.group(1) if match else ""
        if not email_allowed(address):
            findings.add(var.lower().replace("_", " "), "e-mail address", address or "(none)")
    names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "--no-renames").splitlines()
    check_file_names(findings, "staged ", names, extra)
    for name in names:
        check_size(findings, f"staged {name}", f":{name}")
    diff = git("diff", "--cached", "--unified=0", "--no-color", "--no-ext-diff", "--no-renames")
    check_added_lines(findings, "staged ", diff, extra)


def check_message_file(findings: Findings, path: str, extra: list[tuple[str, re.Pattern[str]]]) -> None:
    """What commit-msg runs. Lines git strips (comments starting with #) are skipped."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    kept = "\n".join(line for line in text.splitlines() if not line.startswith("#"))
    check_text(findings, "commit message", kept, extra)


def ranges_from_hook_stdin() -> list[str]:
    """pre-push gives one line per ref: <local ref> <local sha> <remote ref> <remote sha>."""
    ranges = []
    for line in sys.stdin.read().splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        _, local_sha, _, remote_sha = parts
        if local_sha == ZERO_SHA:
            continue  # a deletion pushes nothing
        if remote_sha == ZERO_SHA:
            ranges.append(local_sha)  # new branch: everything reachable
        else:
            ranges.append(f"{remote_sha}..{local_sha}")
    return ranges


HOOKS = {
    "pre-commit": "--staged",
    "commit-msg": '--message "$1"',
    "pre-push": '"$@"',
}


def install_hooks() -> int:
    hooks = Path(git("rev-parse", "--git-path", "hooks").strip())
    if not hooks.is_absolute():
        hooks = ROOT / hooks
    hooks.mkdir(parents=True, exist_ok=True)
    for hook, args in HOOKS.items():
        path = hooks / hook
        # `python` on Windows, `python3` where only that exists (Ubuntu without python-is-python3).
        path.write_text(
            "#!/bin/sh\n"
            "# Installed by tools/privacy_check.py: refuses local paths, secrets and local files.\n"
            'py=python; command -v "$py" >/dev/null 2>&1 || py=python3\n'
            f'exec "$py" -I "$(git rev-parse --show-toplevel)/tools/privacy_check.py" {args}\n',
            encoding="utf-8",
            newline="\n",
        )
        path.chmod(0o755)
        print(f"installed {path}")
    return 0


def report(findings: Findings, what: str) -> int:
    if findings.items:
        print(f"privacy_check: REFUSED, {len(findings.items)} finding(s) in {what}:")
        print("\n".join(findings.items))
        print("fix it (unstage, edit, or amend/rebase the commits), then try again")
        return 1
    print(f"privacy_check: {what} checked, nothing that must stay local")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--install", action="store_true", help="install the pre-commit, commit-msg and pre-push hooks"
    )
    parser.add_argument(
        "--staged", action="store_true", help="check the staged changes and the committing identity"
    )
    parser.add_argument("--message", metavar="FILE", help="check a commit message file")
    parser.add_argument("--all", action="store_true", help="scan every commit on every branch")
    parser.add_argument("--range", help="a git range to scan by hand, e.g. origin/main..main")
    # git runs a pre-push hook as "pre-push <remote name> <remote url>"; both are ignored here,
    # the refs come on stdin.
    parser.add_argument("hook_args", nargs="*", help=argparse.SUPPRESS)
    opts = parser.parse_args()
    if opts.install:
        return install_hooks()

    extra = personal_patterns()
    findings = Findings()
    if opts.staged:
        check_staged(findings, extra)
        return report(findings, "the staged changes")
    if opts.message:
        check_message_file(findings, opts.message, extra)
        return report(findings, "the commit message")

    if opts.all:
        ranges = ["--all"]
    elif opts.range:
        ranges = [opts.range]
    else:
        ranges = ranges_from_hook_stdin()
    if not ranges:
        print("privacy_check: nothing to check")
        return 0
    looked_at = sum(check_range(findings, rev_range, extra) for rev_range in ranges)
    return report(findings, f"{looked_at} commit(s)")


if __name__ == "__main__":
    sys.exit(main())
