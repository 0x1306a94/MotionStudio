#include "MotionStudio/serialization/SchemaMigrator.h"

#include <nlohmann/json.hpp>

namespace motion {

Expected<std::string> SchemaMigrator::migrate(const std::string &jsonText) {
    // Disable nlohmann parse exceptions; errors returned as discarded value.
    const nlohmann::json data = nlohmann::json::parse(jsonText, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return Error("failed to parse the document JSON");
    }
    const auto it = data.find("schemaVersion");
    if (it == data.end() || !it->is_number_integer()) {
        return Error("missing a valid schemaVersion field");
    }
    const int fromVersion = it->get<int>();
    if (fromVersion < 1 || fromVersion > currentVersion()) {
        return Error("unsupported schemaVersion: " + std::to_string(fromVersion));
    }
    // Future migration steps go here: if (fromVersion == 1) data = migrateV1ToV2(data); ...
    return data.dump();
}

}  // namespace motion
