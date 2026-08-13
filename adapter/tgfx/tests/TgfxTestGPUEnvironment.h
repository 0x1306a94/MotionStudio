#pragma once

#include "RenderCache.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <tgfx/core/Color.h>
#include <tgfx/core/Image.h>
#include <tgfx/core/Surface.h>
#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/Device.h>

namespace tgfx_test {

class TgfxTestGPUEnvironment {
  public:
    static std::unique_ptr<TgfxTestGPUEnvironment> Make(int width, int height);

    tgfx::Context *lockContext();
    void unlockContext();
    tgfx::Surface *surface();
    motion::RenderCache *renderCache();

  private:
    TgfxTestGPUEnvironment() = default;

    std::shared_ptr<tgfx::Device> device_ = nullptr;
    std::shared_ptr<tgfx::Surface> surface_ = nullptr;
};

struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y);
int ChannelDelta(uint8_t a, uint8_t b);
std::shared_ptr<tgfx::Image> MakeSolidImage(tgfx::Context *context, int size, tgfx::Color color);
bool ReadCenter(tgfx::Surface *surface, int size, Pixel *out);
bool SaveWebp(const std::vector<uint8_t> &rgba, int width, int height, const std::string &path);
std::string OutputPath(const std::string &fileName);

}  // namespace tgfx_test
