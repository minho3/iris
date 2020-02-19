#include "aligner.hpp"
#include "types_icp.hpp"

#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/core/solver.h>
#include <g2o/core/sparse_optimizer.h>
#include <g2o/solvers/dense/linear_solver_dense.h>
#include <g2o/stuff/sampler.h>
#include <g2o/types/sim3/types_seven_dof_expmap.h>

namespace vllm
{
Eigen::Matrix4f Aligner::execute(g2o::SparseOptimizer& optimizer)
{
  optimizer.setVerbose(false);
  optimizer.initializeOptimization();
  optimizer.computeActiveErrors();
  optimizer.optimize(10);

  // construct output matrix
  g2o::VertexSim3Expmap* optimized = dynamic_cast<g2o::VertexSim3Expmap*>(optimizer.vertices().find(0)->second);
  float scale = static_cast<float>(optimized->estimate().scale());
  Eigen::Matrix3f R = optimized->estimate().rotation().matrix().cast<float>();
  Eigen::Vector3f t = optimized->estimate().translation().cast<float>();

  Eigen::Matrix4f T = Eigen::Matrix4f::Identity();
  T.topLeftCorner(3, 3) = scale * R;
  T.topRightCorner(3, 1) = t;
  return T;
}

Eigen::Matrix4f Aligner::estimate(
    const pcl::PointCloud<pcl::PointXYZ>& source,
    const pcl::PointCloud<pcl::PointXYZ>& target,
    const pcl::Correspondences& correspondances)
{
  // TODO: I don't know I should initialize optimizer in every estimate
  g2o::SparseOptimizer optimizer;
  g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(
      g2o::make_unique<g2o::BlockSolverX>(g2o::make_unique<g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>>()));
  optimizer.setAlgorithm(solver);

  setVertexSim3(optimizer);
  setEdgeGICP(optimizer, source, target, correspondances);
  return execute(optimizer);
}

Eigen::Matrix4f Aligner::estimate(
    const pcl::PointCloud<pcl::PointXYZ>& source,
    const pcl::PointCloud<pcl::PointXYZ>& target,
    const pcl::Correspondences& correspondances,
    const pcl::PointCloud<pcl::Normal>& normals)
{
  // TODO: I don't know I should initialize optimizer in every estimate
  g2o::SparseOptimizer optimizer;
  g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(
      g2o::make_unique<g2o::BlockSolverX>(g2o::make_unique<g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>>()));
  optimizer.setAlgorithm(solver);

  setVertexSim3(optimizer);
  setEdgeGICP(optimizer, source, target, correspondances, normals);
  return execute(optimizer);
}

void Aligner::setVertexSim3(g2o::SparseOptimizer& optimizer)
{
  // set up rotation and translation for this node
  Eigen::Vector3d t(0, 0, 0);
  Eigen::Quaterniond q;
  q.setIdentity();

  double r = 1.0;
  g2o::Sim3 sim3(q, t, r);

  // set up initial parameter
  g2o::VertexSim3Expmap* vc = new g2o::VertexSim3Expmap();
  vc->setEstimate(sim3);
  vc->setId(0);

  // add to optimizer
  optimizer.addVertex(vc);
}

void Aligner::setEdgeGICP(
    g2o::SparseOptimizer& optimizer,
    const pcl::PointCloud<pcl::PointXYZ>& source,
    const pcl::PointCloud<pcl::PointXYZ>& target,
    const pcl::Correspondences& correspondances)
{
  for (const pcl::Correspondence& cor : correspondances) {
    // get Vertex
    g2o::VertexSim3Expmap* vp0 = dynamic_cast<g2o::VertexSim3Expmap*>(optimizer.vertices().find(0)->second);

    // new edge with correct cohort for caching
    vllm::Edge_Sim3_GICP* e = new vllm::Edge_Sim3_GICP();
    e->setVertex(0, vp0);  // set viewpoint

    // calculate the relative 3D position of the point
    Eigen::Vector3f pt0, pt1;
    pt0 = target.at(cor.index_match).getArray3fMap();
    pt1 = source.at(cor.index_query).getArray3fMap();

    vllm::EdgeGICP meas;
    meas.pos0 = pt0.cast<double>();
    meas.pos1 = pt1.cast<double>();

    e->setMeasurement(meas);
    e->information().setIdentity();  // use this for point-point

    // set Huber kernel (default delta = 1.0)
    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
    e->setRobustKernel(rk);
    optimizer.addEdge(e);
  }
}

void Aligner::setEdgeGICP(
    g2o::SparseOptimizer& optimizer,
    const pcl::PointCloud<pcl::PointXYZ>& source,
    const pcl::PointCloud<pcl::PointXYZ>& target,
    const pcl::Correspondences& correspondances,
    const pcl::PointCloud<pcl::Normal>& normals)
{
  for (const pcl::Correspondence& cor : correspondances) {
    // get Vertex
    g2o::VertexSim3Expmap* vp0 = dynamic_cast<g2o::VertexSim3Expmap*>(optimizer.vertices().find(0)->second);

    // new edge with correct cohort for caching
    vllm::Edge_Sim3_GICP* e = new vllm::Edge_Sim3_GICP();
    e->setVertex(0, vp0);  // set viewpoint

    // calculate the relative 3D position of the point
    Eigen::Vector3f pt0, pt1;
    pt0 = target.at(cor.index_match).getArray3fMap();
    pt1 = source.at(cor.index_query).getArray3fMap();

    vllm::EdgeGICP meas;
    meas.pos0 = pt0.cast<double>();
    meas.pos1 = pt1.cast<double>();

    Eigen::Vector3f n = normals.at(cor.index_match).getNormalVector3fMap();
    e->information().setIdentity();
    if (std::isfinite(n.x())) {  // sometime n has NaN
      meas.normal0 = n.cast<double>();
      e->information() = meas.prec0(0.01f);
    }
    e->setMeasurement(meas);

    // set Huber kernel (default delta = 1.0)
    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
    e->setRobustKernel(rk);
    optimizer.addEdge(e);
  }
}
}  // namespace vllm
