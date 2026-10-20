import pathlib
import posixpath
import re
import subprocess
import sys

VENDOR = ("port/third_party/", "tools/vtable/d3d8.h")
EXEMPT = set(VENDOR)


ALLOWED = {
    "255338f698c8270b537f0a91a13f795f4f988250": "d3d8to9, the vendored translation layer",
    "b073859675060c9211fcbccfd90e4e87520dc2c2": "Wine 10.0, the pinned vtable order",
    "2e9cb99369e5ead9d7fe9836dcd43c05e42890bc": "Wine 10.0 tag object",
    "b0738596": "the Wine pin's short form",
    "005088b7": "a virtual address in the generated census table",
}

COMMIT = re.compile(r"(?<![0-9A-Za-z_])[0-9a-f]{7,40}(?![0-9A-Za-z_])")
DEAD = re.compile(r"(?<![\w.])(evidence|build|game|intake)[/\\]")
REMOVED = ("port/HANDOFF.md", "docs/ENGINEERING_PRINCIPLES.md")
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$")
SCHEME = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:")


def tracked(root):
    try:
        listed = subprocess.run(
            ["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True
        ).stdout
        return [name for name in listed.decode("utf-8", "replace").split("\0") if name]
    except (OSError, subprocess.CalledProcessError):
        return sorted(
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file() and ".git" not in path.parts
        )


def heading_slugs(text):
    slugs = set()
    for line in text.splitlines():
        found = HEADING.match(line)
        if not found:
            continue
        title = re.sub(r"[`*]", "", found.group(1)).lower()
        slugs.add(re.sub(r"[^\w\- ]", "", title).replace(" ", "-"))
    return slugs


def links(text):
    fenced = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        for found in LINK.finditer(line):
            yield number, found.group(1)


def scan(root):
    root = pathlib.Path(root).resolve()
    files = tracked(root)
    present = set(files)
    failures = []
    for name in files:
        if name in EXEMPT or name.startswith(VENDOR[0]):
            continue
        try:
            text = (root / name).read_text(encoding="utf-8")
        except (UnicodeDecodeError, FileNotFoundError, IsADirectoryError, OSError):
            continue
        for number, line in enumerate(text.splitlines(), 1):
            for token in COMMIT.findall(line):
                if re.search("[a-f]", token) and token not in ALLOWED:
                    failures.append(
                        f"{name}:{number}: commit: {token} names history this release does not carry"
                    )
            if not name.endswith(".md"):
                continue
            for found in DEAD.finditer(line):
                failures.append(f"{name}:{number}: path: {found.group(0)} is not in the release")
            for removed in REMOVED:
                if removed in line:
                    failures.append(f"{name}:{number}: path: {removed} was removed")
        if not name.endswith(".md"):
            continue
        for number, target in links(text):
            if target.startswith("#"):
                path, fragment = "", target[1:]
            elif SCHEME.match(target) or target.startswith("//"):
                continue
            else:
                path, _, fragment = target.partition("#")
            resolved = (
                posixpath.normpath(pathlib.PurePosixPath(name).parent / path) if path else name
            )
            if resolved not in present:
                failures.append(f"{name}:{number}: link: {target} is not a file in the tree")
            elif fragment and resolved.endswith(".md") and not resolved.startswith(VENDOR[0]):
                slugs = heading_slugs((root / resolved).read_text(encoding="utf-8"))
                if fragment.lower() not in slugs:
                    failures.append(f"{name}:{number}: link: {resolved} has no heading #{fragment}")
    return failures


def main(argv):
    root = argv[1] if len(argv) > 1 else pathlib.Path(__file__).resolve().parent.parent
    failures = scan(root)
    for failure in failures:
        print(failure)
    print(f"{len(failures)} failures in {root}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
