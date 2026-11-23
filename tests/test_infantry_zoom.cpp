#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

#include "tasks/target_position/infantry_zoom.hpp"
#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/solver.hpp"
#include "tools/debug_frame.hpp"

namespace
{
void check(bool ok, const char * message)
{
  if (!ok) throw std::runtime_error(message);
}

tools::Config fixture()
{
  tools::Config config;
  config.model_type = "szurpv_infantry_v8n";
  config.class_id = 0;
  config.corner_mode = "keypoints";
  config.keypoint_indices = {0, 3, 2, 1};
  config.infantry_zoom_enabled = true;
  config.calibration_valid = config.extrinsics_valid = true;
  config.camera_matrix = {553.467824, 0, 308.72496, 0, 551.812757, 245.07661, 0, 0, 1};
  config.target_width = 0.135;
  config.target_height = 0.056;
  config.min_quad_area = 15;
  config.min_corner_edge = 3;
  config.pnp_min_confidence = 0.6f;
  return config;
}
}  // namespace

int main()
{
  try {
    auto config = fixture();
    const cv::Rect full(0, 0, 640, 480);
    target_position::InfantryZoom search(config);
    check(search.next_roi() == full, "first frame must cover full image");
    cv::Mat coverage = cv::Mat::zeros(480, 640, CV_8UC1);
    std::vector<cv::Rect> tiles;
    for (int i = 0; i < 4; ++i) {
      const auto roi = search.next_roi();
      check(roi.width == 384 && roi.height == 288, "search size");
      coverage(roi).setTo(1);
      tiles.push_back(roi);
      search.update({});
    }
    check(cv::countNonZero(coverage) == 640 * 480, "search must cover all image pixels");
    check(search.next_roi() == full, "full image patrol interval");
    // 中线附近的小目标必须完整位于至少一个窗口，不能仅有像素覆盖而目标被切开。
    for (int x = 0; x < 620; x += 13) {
      for (int y = 0; y < 468; y += 11) {
        const cv::Rect armor(x, y, 20, 12);
        bool complete = false;
        for (const auto & tile : tiles) complete = complete || ((tile & armor) == armor);
        check(complete, "target at seam has no complete tile");
      }
    }
    target_position::Detection armor;
    armor.score = 0.95f;
    armor.box = {300, 230, 20, 10};
    armor.keypoints = {{300, 230}, {300, 240}, {320, 240}, {320, 230}};
    search.update({armor});
    const auto tracked = search.next_roi();
    check((tracked & armor.box) == armor.box, "tracking crop clipped armor");
    search.update({});
    search.next_roi(); search.update({});
    check(search.next_roi() == tiles[0], "two crop misses must restart tile search");
    // 低于PnP门限的偶然检测不能锁住局部窗口，阻碍其它区域搜索。
    target_position::InfantryZoom weak(config);
    weak.next_roi(); armor.score = 0.4f; weak.update({armor});
    check(weak.next_roi() == tiles[0], "weak detection must not lock tracker");
    target_position::InfantryZoom big(config);
    big.next_roi();
    auto large = armor; large.score = .95f; large.box = {100, 100, 300, 200};
    big.update({large});
    check(big.next_roi() == full, "large target must keep all corners");
    big.update({}); big.next_roi(); big.update({});
    check(big.next_roi() == tiles[0], "lost large target must not lock full image forever");
    config.infantry_zoom_enabled = false;
    target_position::InfantryZoom disabled(config);
    for (int i = 0; i < 20; ++i) check(disabled.next_roi() == full, "disabled must stay full");
    config.infantry_zoom_enabled = true;
    // 将6米、稍偏心的真实矩形投影到原图，再模拟切片缩放，验证还原后PnP没有尺度误差。
    const cv::Vec3d expected(0.10, -0.05, 6.0);
    const std::vector<cv::Point3d> physical{{-.0675, -.028, 0}, {-.0675, .028, 0},
                                          {.0675, .028, 0}, {.0675, -.028, 0}};
    target_position::Detection original;
    original.score = 0.95f;
    original.class_id = 0;
    for (const auto & point : physical) {
      original.keypoints.emplace_back(
        config.camera_matrix(0, 0) * (point.x + expected[0]) / expected[2] + config.camera_matrix(0, 2),
        config.camera_matrix(1, 1) * (point.y + expected[1]) / expected[2] + config.camera_matrix(1, 2));
    }
    original.box = cv::boundingRect(original.keypoints);
    const auto image = cv::Mat::zeros(480, 640, CV_8UC3);
    for (const auto & roi : tiles) {
      auto restored = original;
      for (auto & point : restored.keypoints) {
        point.x = static_cast<float>((point.x - roi.x + .5) * 640 / roi.width - .5);
        point.y = static_cast<float>((point.y - roi.y + .5) * 480 / roi.height - .5);
      }
      target_position::InfantryZoom::restore(restored, roi);
      for (int i = 0; i < 4; ++i)
        check(cv::norm(restored.keypoints[i] - original.keypoints[i]) < 1e-4, "pixel mapping");
      target_position::Target target;
      std::string reason;
      check(target_position::Detector(config).detect(image, restored, target, reason), "restored corners");
      check(target_position::Solver(config).solve(target, reason), "restored PnP");
      check(cv::norm(target.xyz_in_camera - expected) < .005, "crop introduced incorrect depth");
    }
    // 3像素短边可按配置通过，4像素门限仍会拒绝；折中门限不允许绕过质量检查。
    auto tiny = original;
    tiny.keypoints = {{300, 230}, {300, 233.2f}, {310, 233.2f}, {310, 230}};
    tiny.box = cv::boundingRect(tiny.keypoints);
    target_position::Target target;
    std::string reason;
    check(target_position::Detector(config).detect(image, tiny, target, reason), "configured 3px edge");
    config.min_corner_edge = 4;
    check(!target_position::Detector(config).detect(image, tiny, target, reason) &&
          reason == "corner_edge_too_short", "4px quality gate");
    tiny.keypoints[1] = tiny.keypoints[0];
    check(!target_position::Detector(config).detect(image, tiny, target, reason), "duplicate corner");
    // 灰色切片框也必须只在显示副本绘制，不污染下一次推理。
    cv::Mat source(480, 640, CV_8UC3, cv::Scalar(10, 20, 30));
    const auto before = source.clone();
    tools::DebugView view(source, false);
    view.search_roi(tiles[0]);
    check(cv::norm(source, before, cv::NORM_INF) == 0, "search overlay modified inference pixels");
    tools::DebugFrame job;
    job.image = source.clone(); job.feature_panels = false; job.inference_roi = tiles[0];
    tools::DebugView reference(source, false);
    reference.search_roi(tiles[0]); reference.status("", nullptr);
    check(cv::norm(reference.overlay(), job.render(), cv::NORM_INF) == 0, "async search overlay");
    std::cout << "infantry zoom checks passed\n";
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
