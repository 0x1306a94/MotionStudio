#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FollowPath.h"
#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/model/Transform.h"

namespace motion {

class Document;

struct Mask {
    Animatable<VectorNetwork> path;
    MaskMode mode = MaskMode::Add;
    Animatable<float> opacity{1.0f};
    bool inverted = false;
    Animatable<float> feather{0.0f};
    Animatable<float> expansion{0.0f};
};

class Layer {
  public:
    // Constructs a layer with the matching LayerContent subtype.
    // type: content variant to create.
    explicit Layer(LayerType type);
    ~Layer();

    // Returns the content variant tag of this layer.
    LayerType type() const;

    // Sets the parent layer. Detects cycles along the parent chain; returns
    // false without modification if a cycle would form or the target does not
    // exist, true on success.
    // newParentId: id of the new parent (invalid id clears the parent).
    // document: used to look up the target parent entity.
    bool setParent(EntityId newParentId, const Document &document);

    // Returns the local transform matrix at the given time.
    Mat3 localTransform(FrameTime time) const;
    // Returns the world transform matrix (accumulated through the parent chain).
    // time: evaluation time in frames.
    // document: used to resolve parent layers.
    Mat3 worldTransform(FrameTime time, const Document &document) const;

    // True when this layer and every ancestor is visible. Missing parents are
    // treated as visible. document: used to walk the parent chain.
    bool isEffectivelyVisible(const Document &document) const;
    // True when this layer or any ancestor is locked. Missing parents are
    // treated as unlocked. document: used to walk the parent chain.
    bool isEffectivelyLocked(const Document &document) const;

    EntityId id = EntityId::Generate();
    std::string name;

    // Timing
    FrameTime inPoint = 0;    // start frame on the host composition timeline
    FrameTime outPoint = 0;   // end frame (exclusive)
    FrameTime startTime = 0;  // source time offset (precomp sampling origin)
    double timeStretch = 1.0;

    bool visible = true;
    bool locked = false;

    EntityId parentId;  // invalid id = no parent
    Transform transform;
    std::unique_ptr<LayerContent> content;

    BlendMode blendMode = BlendMode::Normal;
    std::vector<Mask> masks;
    EntityId trackMatteLayerId;
    TrackMatteType trackMatteType = TrackMatteType::None;
    FollowPath followPath;
    std::vector<std::unique_ptr<LayerStyle>> styles;
    std::vector<std::unique_ptr<LayerEffect>> effects;
    std::vector<std::unique_ptr<LayerFx>> layerStyles;

  private:
    Mat3 worldTransform(FrameTime time, const Document &document, int depth) const;

    LayerType type_;
};

}  // namespace motion
