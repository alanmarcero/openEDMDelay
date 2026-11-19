"""Write a GitHub release body for the current tag.

Uses the matching section of CHANGELOG.md (or CHANGELOG) when it exists.
Otherwise uses the tag message: the annotation for an annotated tag, or the
commit message when the tag is lightweight. The editor screenshot stays at
the bottom. GitHub's generated compare link is appended by the release action.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

TRAILER = re.compile(
    r"^(Co-authored-by|Signed-off-by|Reviewed-by|Acked-by|Tested-by|"
    r"Helped-by|Reported-by|Suggested-by): "
)
CHANGELOG_NAMES = ("CHANGELOG.md", "CHANGELOG", "changelog.md")


def version_token(tag):
    return tag[1:] if tag.startswith("v") else tag


def heading_matches(title, version):
    return re.search(
        rf"(?<![0-9A-Za-z.])v?{re.escape(version)}(?![0-9A-Za-z.-])",
        title,
    ) is not None


def changelog_section(root, version):
    for name in CHANGELOG_NAMES:
        path = root / name
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        headings = list(re.finditer(r"^(#{1,6})[ \t]+(.+?)\s*$", text, re.M))
        for index, match in enumerate(headings):
            if not heading_matches(match.group(2).strip(), version):
                continue
            level = len(match.group(1))
            end = len(text)
            for later in headings[index + 1:]:
                if len(later.group(1)) <= level:
                    end = later.start()
                    break
            body = text[match.end():end].strip()
            if body:
                return body
    return ""


def strip_trailers(message):
    lines = message.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    while lines and (not lines[-1].strip() or TRAILER.match(lines[-1])):
        lines.pop()
    return "\n".join(lines).strip()


def tag_message(root, tag):
    result = subprocess.run(
        ["git", "-C", str(root), "for-each-ref", f"refs/tags/{tag}", "--format=%(contents)"],
        check=True, capture_output=True, text=True,
    )
    return strip_trailers(result.stdout)


def screenshot(repository, tag):
    return (
        "![openEDMDelay editor](https://raw.githubusercontent.com/"
        f"{repository}/{tag}/docs/screenshots/preview.png)"
    )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: release_body.py OUTPUT")
    tag = os.environ.get("GITHUB_REF_NAME", "").strip()
    repository = os.environ.get("GITHUB_REPOSITORY", "").strip()
    if not tag or not repository:
        raise SystemExit("GITHUB_REF_NAME and GITHUB_REPOSITORY are required")
    root = Path(__file__).resolve().parent.parent
    notes = changelog_section(root, version_token(tag))
    source = "CHANGELOG"
    if not notes:
        notes = tag_message(root, tag)
        source = "tag message" if notes else "none"
    if not notes:
        print(
            f"warning: no CHANGELOG section or tag message for {tag}; "
            "release body is the screenshot only",
            file=sys.stderr,
        )
    image = screenshot(repository, tag)
    parts = [notes] if notes else []
    if image not in notes:
        parts.append(image)
    Path(sys.argv[1]).write_text("\n\n".join(parts) + "\n", encoding="utf-8")
    print(f"release notes source: {source}")


if __name__ == "__main__":
    main()
