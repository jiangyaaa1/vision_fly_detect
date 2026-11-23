#include "debug_frame.hpp"

namespace tools
{
cv::Mat DebugFrame::render()
{
  DebugView view(image, feature_panels, rgb, true);
  view.search_roi(inference_roi);
  for (const auto & detection : detections) view.detection(detection);
  for (const auto & item : items) {
    view.features(item.features);
    if (item.corners_valid) view.corners(item.target, false);
  }
  if (selected_valid) view.corners(selected, true);
  view.status(status, selected_valid ? &selected : nullptr, process_fps, inference_ms);
  return feature_panels ? view.sheet() : view.overlay();
}
}  // namespace tools
