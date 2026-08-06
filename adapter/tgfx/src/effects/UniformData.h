#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Uniform.h"

#include <tgfx/core/Color.h>
#include <tgfx/core/Matrix.h>

namespace motion {

/**
 * Manages uniform buffer layout and writes.
 * Modeled after tgfx's UniformData for GPU uniform buffer packing and updates.
 *
 * Capabilities:
 * - Computes memory layout and alignment from a Uniform list
 * - Type-safe setData() for writing uniform values
 * - Converts Matrix to a 3x3 std140-friendly layout
 * - Binds an external buffer (e.g. mapped GPU UBO memory)
 */
class UniformData {
  public:
    /**
     * Builds the memory layout from the given uniform list.
     * @param uniforms Uniform variable list
     */
    explicit UniformData(std::vector<Uniform> uniforms);

    /**
     * Sets uniform data (generic template for trivially copyable values).
     * @param name Uniform variable name
     * @param value Data value
     */
    template <typename T>
    std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                         !std::is_same_v<std::decay_t<T>, tgfx::Matrix>,
                     void>
    setData(const std::string &name, const T &value) const {
        onSetData(name, &value, sizeof(value));
    }

    void setData(const std::string &name, const void *value, size_t size) const {
        onSetData(name, value, size);
    }
    /**
     * Sets array uniform data.
     * @param name Uniform variable name
     * @param data Pointer to packed array elements
     * @param count Number of elements to write
     */
    void setArrayData(const std::string &name, const void *data, int count) const;

    /**
     * Sets a single element of an array uniform.
     * @param name Uniform variable name
     * @param index Element index
     * @param data Pointer to the element value
     */
    void setArrayElement(const std::string &name, int index, const void *data) const;

    /**
     * Sets Matrix data (converted to a 3x3 column-major layout with padding).
     * @param name Uniform variable name
     * @param matrix 2D transform matrix
     */
    template <typename T>
    std::enable_if_t<std::is_same_v<std::decay_t<T>, tgfx::Matrix>, void> setData(
        const std::string &name, const T &matrix) const {
        float values[6] = {};
        matrix.get6(values);

        // Convert to 3x3 (column-major, 4 floats per column for std140).
        const float data[] = {
            values[0], values[3], 0, 0,  // col0: scaleX, skewY, 0, 0
            values[1], values[4], 0, 0,  // col1: skewX, scaleY, 0, 0
            values[2], values[5], 1, 0,  // col2: translateX, translateY, 1, 0
        };
        onSetData(name, data, sizeof(data));
    }

    void setData(const std::string &name, const tgfx::Color &color) const {
        float values[4] = {
            color.red,
            color.green,
            color.blue,
            color.alpha,
        };

        onSetData(name, values, sizeof(values));
    }

    void setData(const std::string &name, const tgfx::PMColor &color) const {
        float values[4] = {
            color.red,
            color.green,
            color.blue,
            color.alpha,
        };

        onSetData(name, values, sizeof(values));
    }

    /**
     * Binds an external memory buffer.
     * @param buffer Buffer pointer (typically mapped GPU UBO memory)
     */
    void setBuffer(void *buffer);

    /**
     * Returns the total uniform data size in bytes.
     */
    size_t size() const {
        return _bufferSize;
    }

    /**
     * Returns the uniform list.
     */
    const std::vector<Uniform> &uniforms() const {
        return _uniforms;
    }

  private:
    struct Field {
        std::string name = "";
        UniformFormat format = UniformFormat::Float;
        int count = 1;      // Array element count
        size_t offset = 0;  // Byte offset in the buffer
        size_t size = 0;    // Data size in bytes
        size_t align = 0;   // Alignment requirement in bytes
    };

    struct Entry {
        size_t size;   // Data size
        size_t align;  // Alignment requirement
    };

    uint8_t *_buffer = nullptr;
    size_t _bufferSize = 0;
    std::vector<Uniform> _uniforms = {};
    std::unordered_map<std::string, Field> _fieldMap = {};
    size_t _cursor = 0;  // Current layout cursor

    void onSetData(const std::string &name, const void *data, size_t size) const;

    const Field *findField(const std::string &name) const;

    size_t alignCursor(size_t alignment) const;

    static Entry EntryOf(UniformFormat format);
};

}  // namespace motion
