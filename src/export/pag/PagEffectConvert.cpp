#include "PagEffectConvert.h"

#include "PagAnimatableConvert.h"

namespace motion {
namespace pag_export {

pag::Effect *ToPagEffect(const LayerEffect &effect, std::vector<PagExportWarning> *warnings,
                         EntityId entityId) {
    switch (effect.type()) {
        case LayerEffectType::BrightnessContrast: {
            const auto &source = static_cast<const BrightnessContrastEffect &>(effect);
            auto *converted = new pag::BrightnessContrastEffect();
            converted->brightness = ConvertFloat(source.brightness, warnings, entityId);
            converted->contrast = ConvertFloat(source.contrast, warnings, entityId);
            converted->useOldVersion = new pag::Property<bool>(false);
            return converted;
        }
        case LayerEffectType::GaussianBlur: {
            const auto &source = static_cast<const GaussianBlurEffect &>(effect);
            auto *converted = new pag::FastBlurEffect();
            converted->blurriness = ConvertFloat(source.blurriness, warnings, entityId);
            converted->blurDimensions = new pag::Property<pag::BlurDimensionsDirection>(
                pag::BlurDimensionsDirection::All);
            converted->repeatEdgePixels = new pag::Property<bool>(source.repeatEdgePixels);
            return converted;
        }
    }
    return nullptr;
}

}  // namespace pag_export
}  // namespace motion
