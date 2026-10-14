#!/usr/bin/env python3
import hashlib
import io
import json
import sys
import tarfile
import urllib.request
from pathlib import Path

TARBALL = "https://codeload.github.com/{repo}/tar.gz/{sha}"
MIRROR = "https://ghfast.top/"

GROUPS = {
    "lkm": ("inforcqb/susfs4ksu-lkm", "85ef920ccf27682a2a9089b4676547f691b92828"),
    "module": ("sidex15/susfs4ksu-module", "c6a1c065efff9eaead049acf529a10bc93d34e75"),
    "webui": ("sidex15/susfs4ksu-module", "b2d5e037b2370d5ae13a15eab0d7d1c29cbe80b0"),
    "ksu_susfs": ("sidex15/susfs4ksu-binaries", "895afae32602e11418e7665a625b9ce32f389bcb"),
    "cjson": ("DaveGamble/cJSON", "c859b25da02955fef659d658b8f324b5cde87be3"),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_lock(root):
    lock = json.loads((root / "SOURCE_LOCK.json").read_text(encoding="utf-8"))
    expected = {}
    for entry in lock["files"]:
        path = entry["path"]
        if path.startswith("sources/"):
            expected[path] = entry["sha256"].lower()
    return expected


def manifest_matches(root):
    text = (root / "manifest.yaml").read_text(encoding="utf-8")
    missing = [sha for _, sha in GROUPS.values() if sha not in text]
    if missing:
        raise SystemExit("manifest.yaml does not pin: " + ", ".join(missing))


def download(repo, sha):
    url = TARBALL.format(repo=repo, sha=sha)
    last = None
    for candidate in (url, MIRROR + url):
        try:
            request = urllib.request.Request(candidate, headers={"User-Agent": "susfs4lkm-fetch"})
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except Exception as error:
            last = error
    raise SystemExit(f"download failed for {repo}@{sha}: {last}")


def extract(payload):
    files = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            handle = archive.extractfile(member)
            if handle is None:
                continue
            relative = member.name.split("/", 1)
            if len(relative) < 2:
                continue
            files[relative[1]] = handle.read()
    return files


def verify_tree(root, expected):
    actual = {}
    for path in sorted((root / "sources").rglob("*")):
        if path.is_file():
            actual[path.relative_to(root).as_posix()] = sha256(path.read_bytes())
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    wrong = sorted(name for name, digest in actual.items() if expected.get(name) != digest)
    return missing, extra, wrong


def fetch(root, target, expected):
    for group, (repo, sha) in GROUPS.items():
        prefix = f"sources/{group}/"
        wanted = {name: digest for name, digest in expected.items() if name.startswith(prefix)}
        if not wanted:
            raise SystemExit(f"lock has no files for {prefix}")
        current = {name[len(prefix) :]: digest for name, digest in wanted.items()}
        payload = download(repo, sha)
        files = extract(payload)
        for relative, digest in current.items():
            data = files.get(relative)
            if data is None:
                raise SystemExit(f"{repo}@{sha[:12]} has no {relative}")
            if sha256(data) != digest:
                raise SystemExit(f"hash mismatch after download: {prefix}{relative}")
            destination = target / group / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        print(f"{group}: {len(current)} files from {repo}@{sha[:12]}")


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
    force = "--force" in sys.argv
    manifest_matches(root)
    expected = load_lock(root)
    target = root / "sources"
    missing, extra, wrong = verify_tree(root, expected)
    if not force and not (missing or extra or wrong):
        print("SOURCES_OK " + str(len(expected)))
        return
    fetch(root, target, expected)
    missing, extra, wrong = verify_tree(root, expected)
    if missing or extra or wrong:
        raise SystemExit(
            "sources verification failed: missing=%s extra=%s wrong=%s"
            % (missing[:5], extra[:5], wrong[:5])
        )
    print("SOURCES_FETCHED " + str(len(expected)))


if __name__ == "__main__":
    main()
