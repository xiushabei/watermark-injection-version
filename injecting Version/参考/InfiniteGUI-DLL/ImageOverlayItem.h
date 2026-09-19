#pragma once
#include "Item.h"
#include "ImageOverlay.h"
#include <vector>
#include <memory>

class ImageOverlayItem : public Item {
public:
    ImageOverlayItem();
    static ImageOverlayItem& Instance();

    std::vector<std::shared_ptr<ImageOverlay>>& GetOverlays() { return overlays; }
    const std::vector<std::shared_ptr<ImageOverlay>>& GetOverlays() const { return overlays; }

    std::shared_ptr<ImageOverlay> AddOverlay(const std::string& fileName);
    bool RemoveOverlay(ImageOverlay* overlay);
    ImageOverlay* GetOverlayAt(float x, float y);
    ImageOverlay* GetOverlayAtCorner(float x, float y);
    void SetOverlayZOrder(ImageOverlay* overlay, int z);
    void TryLoadTextures(ImageOverlay* overlay);

    void Toggle() override;
    void Reset() override;
    void Load(const nlohmann::json& j) override;
    void Save(nlohmann::json& j) const override;
    void DrawSettings(const float& bigPadding, const float& centerX, const float& itemWidth) override;

private:
    std::vector<std::shared_ptr<ImageOverlay>> overlays;
    int nextZOrder = 0;
};
