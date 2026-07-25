{
  "version": "1.4.5",
  "vars": {
    "GITHUB_BASE_URL": "https://github.com"
  },
  "repos": {
    "common": [
      {
        "url": "${GITHUB_BASE_URL}/libpag/tgfx.git",
        "commit": "64c8597101809078bc71499c13fe850553fbfa1e",
        "dir": "third_party/tgfx"
      },
      {
        "url": "${GITHUB_BASE_URL}/google/googletest.git",
        "commit": "6910c9d9165801d8827d628cb72eb7ea9dd538c5",
        "dir": "third_party/googletest"
      },
      {
        "url": "${GITHUB_BASE_URL}/nlohmann/json.git",
        "commit": "9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03",
        "dir": "third_party/json"
      }
    ]
  },
  "linkfiles": {
    "common": [
      {
        "src": "git_hooks/pre-commit",
        "dest": ".git/hooks/pre-commit"
      }
    ]
  },
  "actions": {
    "common": [
      {
        "command": "depctl --clean",
        "dir": "third_party"
      },
      {
        "command": "python3 tgfx/third_party/shaderc/utils/git-sync-deps",
        "dir": "third_party"
      },
      {
        "command": "sh -c 'git reset --hard HEAD && git apply ../../../../patches/vendor_tools-maccatalyst-arm64.patch'",
        "dir": "third_party/tgfx/third_party/vendor_tools"
      },
      {
        "command": "sh -c 'git reset --hard HEAD && git apply ../../patches/tgfx-maccatalyst-arm64.patch'",
        "dir": "third_party/tgfx"
      }
    ]
  }
}
