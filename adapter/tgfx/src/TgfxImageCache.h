#pragma once

#include <list>
#include <string>
#include <unordered_map>

#include <tgfx/core/Image.h>

namespace motion {

// Path → tgfx::Image LRU used by TgfxCanvasAdapter::drawImage.
class TgfxImageCache {
  public:
    explicit TgfxImageCache(size_t capacity = 64);

    std::shared_ptr<tgfx::Image> GetOrLoad(const std::string &absolutePath);
    void Clear();

  private:
    size_t capacity_;
    std::list<std::string> order_;
    std::unordered_map<std::string,
                       std::pair<std::list<std::string>::iterator, std::shared_ptr<tgfx::Image>>>
        entries_;
};

}  // namespace motion
