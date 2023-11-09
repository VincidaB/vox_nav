// Copyright (c) 2023 Norwegian University of Life Sciences, Fetullah Atas
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VOX_NAV_CONTROL__PLAN_REFINER_PLUGINS__TRAVERSABILITY_BASED_PLAN_REFINER_HPP_
#define VOX_NAV_CONTROL__PLAN_REFINER_PLUGINS__TRAVERSABILITY_BASED_PLAN_REFINER_HPP_
#pragma once

#include <string>
#include <memory>
#include <vector>
#include <algorithm>

#include <fcl/config.h>
#include <fcl/geometry/octree/octree.h>
#include <fcl/math/constants.h>
#include <fcl/narrowphase/collision.h>
#include <fcl/narrowphase/collision_object.h>

#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/buffer.h>
#include <pcl_ros/transforms.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <nav_msgs/msg/path.hpp>
#include <vision_msgs/msg/detection3_d.hpp>

#include "vox_nav_control/common.hpp"
#include "vox_nav_control/plan_refiner_core.hpp"
#include "vox_nav_utilities/pcl_helpers.hpp"
#include "vox_nav_utilities/planner_helpers.hpp"
#include "vox_nav_utilities/map_manager_helpers.hpp"
#include "vox_nav_utilities/boost_graph_utils.hpp"

namespace vox_nav_control
{

/**
 * @brief This class is used to refine the global plan based on the traversability of the terrain in the vicinity of the
 * robot It subscribes to a Traverasblity map generated by a travsability estimator. The algorithm finds a local goal
 * based on global plan and traversability map. Typically the local goal is the first point on the global plan that is
 * not in the bounds of the traversability map. We then use super voxel clustering and boost Graph library to find the
 * optimal path from current robot pose to the local goal.
 *
 */
class TraversabilityBasedPlanRefiner : public vox_nav_control::PlanRefinerCore
{
public:
  /**
   * @brief Construct a new Traversability Based Plan Refiner object
   *
   */
  TraversabilityBasedPlanRefiner();

  /**
   * @brief Destroy the Traversability Based Plan Refiner object
   *
   */
  ~TraversabilityBasedPlanRefiner();

  /**
   * @brief
   *
   * @param parent rclcpp node
   * @param plugin_name refiner plugin name
   */
  void initialize(rclcpp::Node* parent, const std::string& plugin_name) override;

  /**
   * @brief Refine the plan locally, only in the vicinity of the robot
   *
   * @param curr_pose
   * @param plan
   * @return std::vector<geometry_msgs::msg::PoseStamped>
   */
  bool refinePlan(const geometry_msgs::msg::PoseStamped& curr_pose, nav_msgs::msg::Path& plan_to_refine) override;

  /**
   * @brief Subscribes to traversability map topic and stores it in a member variable
   *        It transforms the traversability map to the "map" frame.
   *        It creates a superxoel adjacency graph from the traversability map. And stores it in a member variable #g_.
   *
   * @param msg
   */
  void traversabilityMapCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

private:
  // Pointer to the node that owns this plugin
  rclcpp::Node* node_;
  // Name of this path refiner plugin
  std::string plugin_name_;
  // Mutex to lock #g_ when it is being updated
  std::mutex global_mutex_;
  // Traversability map topic name to subscribe to
  std::string map_topic_;
  // is this plugin enabled ?
  bool is_enabled_;
  float local_goal_max_nn_dist_;

  // Subscribe to traversability map topic
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr traversability_map_subscriber_;
  // Publish local goal as PoseStamped
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr local_goal_publisher_;
  // Publish local optimal path as PointCloud2 for visualization
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr local_optimal_path_publisher_;
  // Publish minmal bounding box of the local optimal path as Detection3D for visualization
  rclcpp::Publisher<vision_msgs::msg::Detection3D>::SharedPtr traversability_map_bbox_publisher_;
  // Keep a copy of the traversability map, it is used by /link vox_nav_utilities::getTraversabilityMap
  sensor_msgs::msg::PointCloud2::SharedPtr traversability_map_;

  // For transforming traversability map to the "map" frame
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_ptr_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_ptr_;

  // SUpervxel graph and its weightmap based on traversability map
  vox_nav_utilities::GraphT g_;
  vox_nav_utilities::WeightMap weightmap_;

  // Visualize supervoxel graph related topics
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr supervoxel_graph_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr supervoxel_clusters_publisher_;

  // typedefs for supervoxel clustering
  typedef std::map<std::uint32_t, pcl::Supervoxel<pcl::PointXYZRGBA>::Ptr> SuperVoxelClusters;
  SuperVoxelClusters supervoxel_clusters_;
  // Supervoxel parameters, e.g. resolution, seed resolution, color importance, spatial importance, normal importance
  bool supervoxel_disable_transform_;
  float supervoxel_resolution_;
  float supervoxel_seed_resolution_;
  float supervoxel_color_importance_;
  float supervoxel_spatial_importance_;
  float supervoxel_normal_importance_;
  float supervoxel_dist_;
  float supervoxel_cost_;
  /**
   * @brief Find the shortest path from #start_vertex to #goal_vertex in the boost graph #g
   *        append the shortest path to #shortest_path and report whether the path was found or not
   *
   * @param g
   * @param weightmap
   * @param start_vertex
   * @param goal_vertex
   * @param shortest_path
   * @return true
   * @return false
   */
  bool find_astar_path(const vox_nav_utilities::GraphT& g, const vox_nav_utilities::WeightMap& weightmap,
                       const vox_nav_utilities::vertex_descriptor start_vertex,
                       const vox_nav_utilities::vertex_descriptor goal_vertex,
                       std::list<vox_nav_utilities::vertex_descriptor>& shortest_path)
  {
    std::vector<vox_nav_utilities::vertex_descriptor> p(boost::num_vertices(g));
    std::vector<vox_nav_utilities::Cost> d(boost::num_vertices(g));
    std::vector<geometry_msgs::msg::PoseStamped> plan_poses;
    int num_visited_nodes = 0;
    try
    {
      if (supervoxel_clusters_.empty())
      {
        RCLCPP_WARN(node_->get_logger(), "Empty supervoxel clusters! failed to find a valid path!");
        return false;
      }
      auto heuristic =
          vox_nav_utilities::distance_heuristic<vox_nav_utilities::GraphT, vox_nav_utilities::Cost,
                                                SuperVoxelClusters*>(&supervoxel_clusters_, goal_vertex, g);
      auto c_visitor =
          vox_nav_utilities::custom_goal_visitor<vox_nav_utilities::vertex_descriptor>(goal_vertex, &num_visited_nodes);
      // astar
      boost::astar_search_tree(g, start_vertex, heuristic /*only difference*/,
                               boost::predecessor_map(&p[0]).distance_map(&d[0]).visitor(c_visitor));
      // If a path found exception will be thrown and code block here
      // Should not be eecuted. If code executed up until here,
      // A path was NOT found. Warn user about it
      RCLCPP_WARN(node_->get_logger(), "A* search failed to find a valid path!");
      return false;
    }
    catch (vox_nav_utilities::FoundGoal found_goal)
    {
      // Found a path to the goal, catch the exception
      for (vox_nav_utilities::vertex_descriptor v = goal_vertex;; v = p[v])
      {
        shortest_path.push_front(v);
        if (p[v] == v)
        {
          break;
        }
      }
      return true;
    }
  }

  /**
   * @brief Given a PCL #point find the nearest vertex to it on the boost graph #g
   *
   * @param g
   * @param point
   * @return vox_nav_utilities::vertex_descriptor
   */
  vox_nav_utilities::vertex_descriptor get_nearest_vertex(const vox_nav_utilities::GraphT& g,
                                                          const pcl::PointXYZRGBA& point)
  {
    double dist_min = INFINITY;
    vox_nav_utilities::vertex_descriptor nn_vertex;
    for (auto vd : boost::make_iterator_range(vertices(g)))
    {
      auto voxel_centroid = g[vd].point;
      auto dist_to_crr_voxel_centroid = vox_nav_utilities::PCLPointEuclideanDist<>(point, voxel_centroid);

      if (dist_to_crr_voxel_centroid < dist_min)
      {
        dist_min = dist_to_crr_voxel_centroid;
        nn_vertex = vd;
      }
    }
    return nn_vertex;
  }

  /**
   * @brief Fill a PCL pointcloud from a boost graph
   *
   * @param g
   * @param cloud
   */
  void fillCloudfromGraph(const vox_nav_utilities::GraphT& g, pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud)
  {
    for (auto vd : boost::make_iterator_range(vertices(g)))
    {
      auto voxel_centroid = g[vd].point;
      pcl::PointXYZRGBA point;
      point.x = voxel_centroid.x;
      point.y = voxel_centroid.y;
      point.z = voxel_centroid.z;
      cloud->points.push_back(point);
    }
  }

  /**
   * @brief Given a PCL #point, compute the average traversability of the points within radius #r based on the PCL
   * #cloud
   *
   * @param cloud
   * @param point
   * @param radius
   * @return int
   */
  double computeAverageTraversability(const pcl::PointCloud<pcl::PointXYZRGBA>::Ptr& cloud,
                                      const pcl::PointXYZRGBA& point)
  {
    float min = 0.0;
    float max = 0.6;
    std::vector<std::tuple<int, int, int>> colors;
    colors.push_back(std::make_tuple(0, 0, 255));  // blue
    colors.push_back(std::make_tuple(0, 255, 0));  // green
    colors.push_back(std::make_tuple(255, 0, 0));  // red

    double sum_traversability = 0.0;
    int num_points = 0;
    for (size_t i = 0; i < cloud->points.size(); i++)
    {
      auto traversability = vox_nav_utilities::convert_to_value(
          std::make_tuple(cloud->points[i].r, cloud->points[i].g, cloud->points[i].b), min, max, colors);
      sum_traversability += traversability;
      num_points++;
    }
    if (num_points == 0)
    {
      return 0;
    }
    return sum_traversability / static_cast<double>(num_points);
  }
};

}  // namespace vox_nav_control

#endif  // VOX_NAV_CONTROL__PLAN_REFINER_PLUGINS__TRAVERSABILITY_BASED_PLAN_REFINER_HPP_
