#pragma once

namespace motion {

// Type of a document-level asset. Only images are project assets; fonts use
// installed system families via TextContent::fontFamily.
enum class AssetType {
    Image,
};

}  // namespace motion
