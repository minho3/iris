#pragma once
#include "core/types.hpp"
#include "core/util.hpp"
#include "map/info.hpp"
#include "map/parameter.hpp"
#include <atomic>
#include <fstream>
#include <mutex>
#include <pcl/filters/crop_box.h>
#include <pcl/io/pcd_io.h>
#include <sstream>
#include <unordered_map>

namespace iris
{
namespace map
{

struct HashForPair {
  template <typename T1, typename T2>
  size_t operator()(const std::pair<T1, T2>& p) const
  {
    auto hash1 = std::hash<T1>{}(p.first);
    auto hash2 = std::hash<T2>{}(p.second);

    // https://stackoverflow.com/questions/4948780/magic-number-in-boosthash-combine
    size_t seed = 0;
    seed ^= hash1 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= hash2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

class Map
{
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  explicit Map(const Parameter& parameter);

  // If the map updates then return true.
  bool informCurrentPose(const Eigen::Matrix4f& T);

  // This informs viewer of whether local map updated or not
  Info getLocalmapInfo() const
  {
    std::lock_guard lock(info_mtx);
    return localmap_info;
  }

  // TODO: This function may have conflicts
  const pcXYZ::Ptr getTargetCloud() const
  {
    std::lock_guard lock(localmap_mtx);
    return local_target_cloud;
  }

  // TODO: This function may have conflicts
  const pcXYZ::Ptr getSparseCloud() const
  {
    std::lock_guard lock(localmap_mtx);
    return all_sparse_cloud;
  }

  // TODO: This function may have conflicts
  const pcNormal::Ptr getTargetNormals() const
  {
    std::lock_guard lock(localmap_mtx);
    return local_target_normals;
  }

private:
  const std::string cache_file;
  const Parameter parameter;
  const std::string cache_cloud_file = "vllm_cloud.pcd";
  const std::string cache_normals_file = "vllm_normals.pcd";
  const std::string cache_sparse_file = "vllm_sparse_cloud.pcd";

  // whole point cloud
  pcXYZ::Ptr all_target_cloud;
  pcNormal::Ptr all_target_normals;
  pcXYZ::Ptr all_sparse_cloud;

  // valid point cloud
  pcXYZ::Ptr local_target_cloud;
  pcNormal::Ptr local_target_normals;

  // divided point cloud
  std::unordered_map<std::pair<int, int>, pcXYZ, HashForPair> submap_cloud;
  std::unordered_map<std::pair<int, int>, pcNormal, HashForPair> submap_normals;

  // [x,y,theta]
  Eigen::Vector3f last_grid_center;
  Info localmap_info;

  mutable std::mutex localmap_mtx;
  mutable std::mutex info_mtx;

  bool isRecalculationNecessary() const;
  bool isUpdateNecessary(const Eigen::Matrix4f& T) const;
  void updateLocalmap(const Eigen::Matrix4f& T);

  // return [0,2*pi]
  float yawFromPose(const Eigen::Matrix4f& T) const;

  // return [0,pi]
  float subtractAngles(float a, float b) const
  {
    // a,b \in [0,2\pi]
    float d = std::fabs(a - b);
    if (d > 3.14159f)
      return 2.f * 3.14159f - d;
    return d;
  }
};
}  // namespace map
}  // namespace iris