/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, 2013, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Eitan Marder-Eppstein
 *         David V. Lu!!
 *********************************************************************/
#include <global_planner/planner_core.h>
#include <tf/transform_listener.h>
#include <costmap_2d/cost_values.h>
#include <costmap_2d/costmap_2d.h>

#include <global_planner/dijkstra.h>
#include <global_planner/astar.h>
#include <global_planner/grid_path.h>
#include <global_planner/gradient_path.h>
#include <global_planner/quadratic_calculator.h>

namespace global_planner {

void GlobalPlanner::outlineMap(unsigned char* costarr, int nx, int ny, unsigned char value) {
    unsigned char* pc = costarr;
    for (int i = 0; i < nx; i++)
        *pc++ = value;
    pc = costarr + (ny - 1) * nx;
    for (int i = 0; i < nx; i++)
        *pc++ = value;
    pc = costarr;
    for (int i = 0; i < ny; i++, pc += nx)
        *pc = value;
    pc = costarr + nx - 1;
    for (int i = 0; i < ny; i++, pc += nx)
        *pc = value;
}

GlobalPlanner::GlobalPlanner() :
        costmap_(NULL), path_costmap_(NULL), initialized_(false), allow_unknown_(true) {
}

GlobalPlanner::GlobalPlanner(std::string name, costmap_2d::Costmap2D* costmap, std::string frame_id) :
        costmap_(NULL), initialized_(false), allow_unknown_(true) {
    //initialize the planner
    initialize(name, costmap, costmap, frame_id);
}

GlobalPlanner::~GlobalPlanner() {
    if (p_calc_)
        delete p_calc_;
    if (planner_)
        delete planner_;
    if (path_maker_)
        delete path_maker_;
}

double GetNumberFromXMLRPC(XmlRpc::XmlRpcValue& value, const std::string& full_param_name) {
  // Make sure that the value we're looking at is either a double or an int.
  if (value.getType() != XmlRpc::XmlRpcValue::TypeInt &&
      value.getType() != XmlRpc::XmlRpcValue::TypeDouble) {
    std::string& value_string = value;
    ROS_FATAL("Values in the circle_center specification (param %s) must be numbers. Found value %s.",
              full_param_name.c_str(), value_string.c_str() );
    throw std::runtime_error("Values in the circle_center specification must be numbers");
  }
  return value.getType() == XmlRpc::XmlRpcValue::TypeInt ? static_cast<int>(value) : static_cast<double>(value);
}

void ReadCircleCenterFromXMLRPC(XmlRpc::XmlRpcValue& circle_center_xmlrpc, const std::string& full_param_name, std::vector<XYPoint>* points) {
  // Make sure we have an array of at least 3 elements.
  if (circle_center_xmlrpc.getType() != XmlRpc::XmlRpcValue::TypeArray || circle_center_xmlrpc.size() < 1) {
    ROS_FATAL("The circle_center must be specified as list of lists on the parameter server, %s was specified as %s",
              full_param_name.c_str(), std::string(circle_center_xmlrpc).c_str());
    throw std::runtime_error("The circle_center must be specified as list of lists on the parameter server with at least 1 points eg: [[x1, y1], [x2, y2], ..., [xn, yn]]");
  }

  XYPoint pt;

  for (int i = 0; i < circle_center_xmlrpc.size(); ++i) {
    // Make sure each element of the list is an array of size 2. (x and y coordinates)
    XmlRpc::XmlRpcValue point = circle_center_xmlrpc[ i ];
    if (point.getType() != XmlRpc::XmlRpcValue::TypeArray ||
        point.size() != 2) {
      ROS_FATAL("The circle_center (parameter %s) must be specified as list of lists on the parameter server eg: [[x1, y1], [x2, y2], ..., [xn, yn]], but this spec is not of that form.", full_param_name.c_str());
      throw std::runtime_error( "The circle_center must be specified as list of lists on the parameter server eg: [[x1, y1], [x2, y2], ..., [xn, yn]], but this spec is not of that form");
    }

    pt.x = GetNumberFromXMLRPC(point[0], full_param_name);
    pt.y = GetNumberFromXMLRPC(point[1], full_param_name);

    points->push_back(pt);
    GAUSSIAN_INFO("[Global Planner] circle_center[%d].x = %lf; .y = %lf", i, pt.x, pt.y);
  }
}

bool GlobalPlanner::ReadCircleCenterFromParams(ros::NodeHandle& nh, std::vector<XYPoint>* points) {
  std::string full_param_name;

  if (nh.searchParam("p7", full_param_name)) {
    XmlRpc::XmlRpcValue circle_center_xmlrpc;
    nh.getParam(full_param_name, circle_center_xmlrpc);
    if (circle_center_xmlrpc.getType() == XmlRpc::XmlRpcValue::TypeArray) {
      ReadCircleCenterFromXMLRPC(circle_center_xmlrpc, full_param_name, points);
      return true;
    } else {
      GAUSSIAN_ERROR("[Global Planner] circle_center param's type is not Array!");
      return false;
    }
  } else {
    GAUSSIAN_ERROR("[Global Planner] Cannot find circle_center param!");
    return false;
  }
}

void GlobalPlanner::initialize(std::string name, costmap_2d::Costmap2DROS* costmap_ros) {
// TODO(lizhen) getPathCostmap()
//    initialize(name, costmap_ros->getCostmap(), costmap_ros->getPathCostmap(), costmap_ros->getGlobalFrameID());
    initialize(name, costmap_ros->getCostmap(), costmap_ros->getPathCostmap(), costmap_ros->getGlobalFrameID());
    costmap_ros_ = costmap_ros;
}
void GlobalPlanner::initialize(std::string name, costmap_2d::Costmap2D* costmap, costmap_2d::Costmap2D* path_costmap, std::string frame_id) {
    if (!initialized_) {
        ros::NodeHandle private_nh("~/" + name);
        costmap_ = costmap;
        path_costmap_ = path_costmap;
        frame_id_ = frame_id;

        unsigned int cx = costmap->getSizeInCellsX(), cy = costmap->getSizeInCellsY();

        private_nh.param("old_navfn_behavior", old_navfn_behavior_, false);
        if(!old_navfn_behavior_)
            convert_offset_ = 0.5;
        else
            convert_offset_ = 0.0;

        bool use_quadratic;
        private_nh.param("use_quadratic", use_quadratic, true);
        if (use_quadratic)
            p_calc_ = new QuadraticCalculator(cx, cy);
        else
            p_calc_ = new PotentialCalculator(cx, cy);

        bool use_dijkstra;
        private_nh.param("p2", use_dijkstra, true);
        if (use_dijkstra)
        {
            DijkstraExpansion* de = new DijkstraExpansion(p_calc_, cx, cy);
            if(!old_navfn_behavior_)
                de->setPreciseStart(true);
            planner_ = de;
        } else {
          int path_cost, occ_dis_cost;
          private_nh.param("p3", path_cost, 50);
          private_nh.param("p4", occ_dis_cost, 10);
          // get circle_center
          std::vector<XYPoint> circle_center_point;
          if (!ReadCircleCenterFromParams(private_nh, &circle_center_point)) {
            GAUSSIAN_WARN("Cannot read circle centers from parametars, just plan unsing base_link origin point");
          } else {
            GAUSSIAN_INFO("[Global Planner] circle_center size = %zu", circle_center_point.size());
          }
          //planner_ = new AStarExpansion(p_calc_, cx, cy, path_cost, occ_dis_cost);
          planner_ = new AStarExpansion(p_calc_, cx, cy, path_cost, occ_dis_cost, circle_center_point, costmap_->getResolution());
        }
        bool use_grid_path;
        private_nh.param("p1", use_grid_path, false);
        if (use_grid_path)
            path_maker_ = new GridPath(p_calc_);
        else
            path_maker_ = new GradientPath(p_calc_);

        orientation_filter_ = new OrientationFilter();

        plan_pub_ = private_nh.advertise<nav_msgs::Path>("plan", 1);
        potential_pub_ = private_nh.advertise<nav_msgs::OccupancyGrid>("potential", 1);

        private_nh.param("p6", allow_unknown_, false);
        planner_->setHasUnknown(allow_unknown_);
        private_nh.param("planner_window_x", planner_window_x_, 0.0);
        private_nh.param("planner_window_y", planner_window_y_, 0.0);
        private_nh.param("default_tolerance", default_tolerance_, 0.0);
        private_nh.param("publish_scale", publish_scale_, 100);

        int lethal_cost, neutral_cost, orientation_mode;
        private_nh.param("lethal_cost", lethal_cost, 253);
        private_nh.param("p5", neutral_cost, 50);
        private_nh.param("orientation_mode", orientation_mode, 1);
        double cost_factor;
        private_nh.param("cost_factor", cost_factor, 3.0);
        bool publish_potential;
        private_nh.param("publish_potential", publish_potential, false);
        planner_->setLethalCost(lethal_cost);
        path_maker_->setLethalCost(lethal_cost);
        planner_->setNeutralCost(neutral_cost);
        planner_->setFactor(cost_factor);
        publish_potential_ = publish_potential;
        orientation_filter_->setMode(orientation_mode);
        double costmap_pub_freq;
        private_nh.param("planner_costmap_publish_frequency", costmap_pub_freq, 0.0);

        //get the tf prefix
        ros::NodeHandle prefix_nh;
        tf_prefix_ = tf::getPrefixParam(prefix_nh);

        make_plan_srv_ = private_nh.advertiseService("make_plan", &GlobalPlanner::makePlanService, this);

        initialized_ = true;
    } else {
        GAUSSIAN_WARN("This planner has already been initialized, you can't call it twice, doing nothing");
    }

}

void GlobalPlanner::setStaticCosmap(bool is_static) {
    if (!initialized_) {
        GAUSSIAN_ERROR(
                "This planner has not been initialized yet, but it is being used, please call initialize() before use");
        return;
    }
    //set current costmap_ as static
    if (is_static) {
      costmap_ = costmap_ros_->getStaticCostmap();
      GAUSSIAN_INFO("[GLOBAL PLANNER] take static costmap!");
    } else {
      costmap_ = costmap_ros_->getCostmap();
      GAUSSIAN_INFO("[GLOBAL PLANNER] take normal costmap!");
    }
}

void GlobalPlanner::clearRobotCell(const tf::Stamped<tf::Pose>& global_pose, unsigned int mx, unsigned int my) {
    if (!initialized_) {
        GAUSSIAN_ERROR(
                "This planner has not been initialized yet, but it is being used, please call initialize() before use");
        return;
    }

    //set the associated costs in the cost map to be free
    costmap_->setCost(mx, my, costmap_2d::FREE_SPACE);
}

bool GlobalPlanner::makePlanService(nav_msgs::GetPlan::Request& req, nav_msgs::GetPlan::Response& resp) {
    makePlan(req.start, req.goal, resp.plan.poses);

    resp.plan.header.stamp = ros::Time::now();
    resp.plan.header.frame_id = frame_id_;

    return true;
}

void GlobalPlanner::mapToWorld(double mx, double my, double& wx, double& wy) {
    wx = costmap_->getOriginX() + (mx+convert_offset_) * costmap_->getResolution();
    wy = costmap_->getOriginY() + (my+convert_offset_) * costmap_->getResolution();
}

bool GlobalPlanner::worldToMap(double wx, double wy, double& mx, double& my) {
    double origin_x = costmap_->getOriginX(), origin_y = costmap_->getOriginY();
    double resolution = costmap_->getResolution();

    if (wx < origin_x || wy < origin_y)
        return false;

    mx = (wx - origin_x) / resolution - convert_offset_;
    my = (wy - origin_y) / resolution - convert_offset_;

    if (mx < costmap_->getSizeInCellsX() && my < costmap_->getSizeInCellsY())
        return true;

    return false;
}

void GlobalPlanner::getExtendPoint(double& wx, double& wy) {
  int mx = planner_->min_cost_index_ % costmap_->getSizeInCellsX();
  int my = planner_->min_cost_index_ / costmap_->getSizeInCellsX();
  my = my >= costmap_->getSizeInCellsY() ? 0 : my;
  mapToWorld(mx, my, wx, wy);
}

bool GlobalPlanner::makePlan(const geometry_msgs::PoseStamped& start, const geometry_msgs::PoseStamped& goal,
                           std::vector<geometry_msgs::PoseStamped>& plan) {
    return makePlan(start, goal, default_tolerance_, plan);
}

bool GlobalPlanner::makePlan(const geometry_msgs::PoseStamped& start, const geometry_msgs::PoseStamped& goal,
                           double tolerance, std::vector<geometry_msgs::PoseStamped>& plan) {
    boost::mutex::scoped_lock lock(mutex_);
    if (!initialized_) {
        GAUSSIAN_ERROR(
                "This planner has not been initialized yet, but it is being used, please call initialize() before use");
        return false;
    }

    //clear the plan, just in case
    plan.clear();

    ros::NodeHandle n;
    std::string global_frame = frame_id_;

    //until tf can handle transforming things that are way in the past... we'll require the goal to be in our global frame
    if (tf::resolve(tf_prefix_, goal.header.frame_id) != tf::resolve(tf_prefix_, global_frame)) {
        GAUSSIAN_ERROR(
                "The goal pose passed to this planner must be in the %s frame.  It is instead in the %s frame.", tf::resolve(tf_prefix_, global_frame).c_str(), tf::resolve(tf_prefix_, goal.header.frame_id).c_str());
        return false;
    }

    if (tf::resolve(tf_prefix_, start.header.frame_id) != tf::resolve(tf_prefix_, global_frame)) {
        GAUSSIAN_ERROR(
                "The start pose passed to this planner must be in the %s frame.  It is instead in the %s frame.", tf::resolve(tf_prefix_, global_frame).c_str(), tf::resolve(tf_prefix_, start.header.frame_id).c_str());
        return false;
    }

    double wx = start.pose.position.x;
    double wy = start.pose.position.y;

    unsigned int start_x_i, start_y_i, goal_x_i, goal_y_i;
    double start_x, start_y, goal_x, goal_y;

    if (!costmap_->worldToMap(wx, wy, start_x_i, start_y_i)) {
        GAUSSIAN_WARN(
                "The robot's start position is off the global costmap. Planning will always fail, are you sure the robot has been properly localized?");
        return false;
    }
    if(old_navfn_behavior_){
        start_x = start_x_i;
        start_y = start_y_i;
    }else{
        worldToMap(wx, wy, start_x, start_y);
    }

    wx = goal.pose.position.x;
    wy = goal.pose.position.y;

    if (!costmap_->worldToMap(wx, wy, goal_x_i, goal_y_i)) {
        ROS_WARN_THROTTLE(1.0,
                "The goal sent to the global planner is off the global costmap. Planning will always fail to this goal.");
        return false;
    }
    if(old_navfn_behavior_){
        goal_x = goal_x_i;
        goal_y = goal_y_i;
    }else{
        worldToMap(wx, wy, goal_x, goal_y);
    }

    //clear the starting cell within the costmap because we know it can't be an obstacle
    tf::Stamped<tf::Pose> start_pose;
    tf::poseStampedMsgToTF(start, start_pose);
    clearRobotCell(start_pose, start_x_i, start_y_i);

    int nx = costmap_->getSizeInCellsX(), ny = costmap_->getSizeInCellsY();

    //make sure to resize the underlying array that Navfn uses
    p_calc_->setSize(nx, ny);
    planner_->setSize(nx, ny);
    path_maker_->setSize(nx, ny);
    potential_array_ = new float[nx * ny];

    outlineMap(costmap_->getCharMap(), nx, ny, costmap_2d::LETHAL_OBSTACLE);

    unsigned char* path_costs = NULL;
    if (path_costmap_ != NULL) {
      path_costs = path_costmap_->getCharMap();
    }
    bool found_legal = planner_->calculatePotentials(costmap_ros_, costmap_->getCharMap(), path_costs, start_x, start_y, goal_x, goal_y,
                                                    nx * ny * 2, potential_array_);

    if(!old_navfn_behavior_)
        planner_->clearEndpoint(costmap_->getCharMap(), potential_array_, goal_x_i, goal_y_i, 2);
    if(publish_potential_)
        publishPotential(potential_array_);

    if (found_legal) {
        //extract the plan
        if (getPlanFromPotential(start_x, start_y, goal_x, goal_y, goal, plan)) {
            //make sure the goal we push on has the same timestamp as the rest of the plan
            geometry_msgs::PoseStamped goal_copy = goal;
            goal_copy.header.stamp = ros::Time::now();
            plan.push_back(goal_copy);
        } else {
            GAUSSIAN_ERROR("Failed to get a plan from potential when a legal potential was found. This shouldn't happen.");
        }
    }else{
        GAUSSIAN_ERROR("Failed to get a global plan.");
    }

    // add orientations if needed
    orientation_filter_->processPath(start, plan);

    //publish the plan for visualization purposes
    publishPlan(plan);
    delete potential_array_;
    return !plan.empty();
}

void GlobalPlanner::publishPlan(const std::vector<geometry_msgs::PoseStamped>& path) {
    if (!initialized_) {
        GAUSSIAN_ERROR(
                "This planner has not been initialized yet, but it is being used, please call initialize() before use");
        return;
    }

    //create a message for the plan
    nav_msgs::Path gui_path;
    gui_path.poses.resize(path.size());

    if (!path.empty()) {
        gui_path.header.frame_id = path[0].header.frame_id;
        gui_path.header.stamp = path[0].header.stamp;
    }

    // Extract the plan in world co-ordinates, we assume the path is all in the same frame
    for (unsigned int i = 0; i < path.size(); i++) {
        gui_path.poses[i] = path[i];
    }

    plan_pub_.publish(gui_path);
}

bool GlobalPlanner::getPlanFromPotential(double start_x, double start_y, double goal_x, double goal_y,
                                      const geometry_msgs::PoseStamped& goal,
                                       std::vector<geometry_msgs::PoseStamped>& plan) {
    if (!initialized_) {
        GAUSSIAN_ERROR(
                "This planner has not been initialized yet, but it is being used, please call initialize() before use");
        return false;
    }

    std::string global_frame = frame_id_;

    //clear the plan, just in case
    plan.clear();

    std::vector<std::pair<float, float> > path;

    if (!path_maker_->getPath(potential_array_, start_x, start_y, goal_x, goal_y, path)) {
        GAUSSIAN_ERROR("NO PATH!");
        return false;
    }

    ros::Time plan_time = ros::Time::now();
    for (int i = path.size() -1; i>=0; i--) {
        std::pair<float, float> point = path[i];
        //convert the plan to world coordinates
        double world_x, world_y;
        mapToWorld(point.first, point.second, world_x, world_y);

        geometry_msgs::PoseStamped pose;
        pose.header.stamp = plan_time;
        pose.header.frame_id = global_frame;
        pose.pose.position.x = world_x;
        pose.pose.position.y = world_y;
        pose.pose.position.z = 0.0;
        pose.pose.orientation.x = 0.0;
        pose.pose.orientation.y = 0.0;
        pose.pose.orientation.z = 0.0;
        pose.pose.orientation.w = 1.0;
        plan.push_back(pose);
    }
    if(old_navfn_behavior_){
            plan.push_back(goal);
    }
    return !plan.empty();
}

void GlobalPlanner::publishPotential(float* potential)
{
    int nx = costmap_->getSizeInCellsX(), ny = costmap_->getSizeInCellsY();
    double resolution = costmap_->getResolution();
    nav_msgs::OccupancyGrid grid;
    // Publish Whole Grid
    grid.header.frame_id = frame_id_;
    grid.header.stamp = ros::Time::now();
    grid.info.resolution = resolution;

    grid.info.width = nx;
    grid.info.height = ny;

    double wx, wy;
    costmap_->mapToWorld(0, 0, wx, wy);
    grid.info.origin.position.x = wx - resolution / 2;
    grid.info.origin.position.y = wy - resolution / 2;
    grid.info.origin.position.z = 0.0;
    grid.info.origin.orientation.w = 1.0;

    grid.data.resize(nx * ny);

    float max = 0.0;
    for (unsigned int i = 0; i < grid.data.size(); i++) {
        float potential = potential_array_[i];
        if (potential < POT_HIGH) {
            if (potential > max) {
                max = potential;
            }
        }
    }

    for (unsigned int i = 0; i < grid.data.size(); i++) {
        if (potential_array_[i] >= POT_HIGH) {
            grid.data[i] = -1;
        } else
            grid.data[i] = potential_array_[i] * publish_scale_ / max;
    }
    potential_pub_.publish(grid);
}

} //end namespace global_planner

