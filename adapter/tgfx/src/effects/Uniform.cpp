#include "Uniform.h"

namespace motion {

size_t Uniform::size() const {
    return UniformFormatByteSize(_format);
}

}  // namespace motion
