import json
import os
import sys
import urllib.request

# Print "webui" when the PR only touches the Cockpit UI, so the matrix
# shrinks to that suite. On any doubt, print nothing (full matrix).
UI_PREFIX = "src/cockpit/389-console/"
PER_PAGE = 100
MAX_PAGES = 3


def changed_files(repo, pr_number, token):
    files = []
    for page in range(1, MAX_PAGES + 1):
        url = (f"https://api.github.com/repos/{repo}/pulls/{pr_number}/files"
               f"?per_page={PER_PAGE}&page={page}")
        request = urllib.request.Request(url)
        request.add_header("Accept", "application/vnd.github+json")
        if token:
            request.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(request, timeout=30) as response:
            batch = json.load(response)
        for entry in batch:
            files.append(entry["filename"])
            # A rename lists only its new path in "filename"; the old
            # path must count too, or a move out of another tree would
            # pass as UI-only.
            previous = entry.get("previous_filename")
            if previous:
                files.append(previous)
        if len(batch) < PER_PAGE:
            return files
    return None


def main():
    repo = os.environ.get("GITHUB_REPOSITORY")
    token = os.environ.get("GITHUB_TOKEN")
    if len(sys.argv) != 2 or not repo:
        return 0
    try:
        files = changed_files(repo, sys.argv[1], token)
    except Exception:
        return 0
    if files and all(name.startswith(UI_PREFIX) for name in files):
        print("webui")
    return 0


if __name__ == "__main__":
    sys.exit(main())
