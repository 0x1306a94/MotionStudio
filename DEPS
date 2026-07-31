{
  "version": "1.4.5",
  "vars": {
    "GITHUB_BASE_URL": "https://github.com"
  },
  "repos": {
    "common": [
      {
        "url": "${GITHUB_BASE_URL}/Tencent/libpag.git",
        "commit": "05aebeb2ea694e4df839861a197501b98cb927ac",
        "dir": "third_party/libpag"
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
        "command": "python3 libpag/third_party/tgfx/third_party/shaderc/utils/git-sync-deps",
        "dir": "third_party"
      },
      {
        "command": "sh -c 'git reset --hard HEAD && git apply ../../../../../../patches/libpag-tgfx-vendor_tools-maccatalyst-arm64.patch'",
        "dir": "third_party/libpag/third_party/tgfx/third_party/vendor_tools"
      },
      {
        "command": "sh -c 'git reset --hard HEAD && git apply ../../../../patches/libpag-tgfx-maccatalyst-arm64.patch'",
        "dir": "third_party/libpag/third_party/tgfx"
      }
    ]
  }
}
