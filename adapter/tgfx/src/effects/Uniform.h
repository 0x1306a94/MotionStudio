#pragma once

#include "MotionStudio/common/UniformFormat.h"
#include <string>

namespace motion {

/**
 * Represents a uniform variable in a GPU program.
 */
class Uniform {
  public:
    /**
     * Default constructor for Uniform.
     */
    Uniform() = default;

    /**
     * Creates a uniform variable with the specified name and format.
     * @param name Uniform variable name
     * @param format Uniform variable format
     * @param count Array element count; defaults to 1 (non-array)
     */
    Uniform(std::string name, UniformFormat format, int count = 1)
        : _name(std::move(name))
        , _format(format)
        , _count(count) {
    }

    /**
     * Returns true if the uniform variable is empty.
     */
    bool empty() const {
        return _name.empty();
    }

    /**
     * The name of the uniform variable.
     */
    const std::string &name() const {
        return _name;
    }

    /**
     * The format of the uniform variable.
     */
    UniformFormat format() const {
        return _format;
    }

    /**
     * The array count of the uniform variable.
     * Returns 1 for non-array uniforms.
     */
    int count() const {
        return _count;
    }

    /**
     * Returns true if this is an array uniform.
     */
    bool isArray() const {
        return _count > 1;
    }

    /**
     * Returns the size of the uniform variable in bytes.
     */
    size_t size() const;

  private:
    std::string _name = {};
    UniformFormat _format = UniformFormat::Float;
    int _count = 1;  // Array element count; defaults to 1 (non-array)
};

};  // namespace motion
