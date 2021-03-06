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
#ifndef _ASTAR_H
#define _ASTAR_H

#include <costmap_2d/costmap_2d.h>
#include <global_planner/planner_core.h>
#include <global_planner/expander.h>
#include <gslib/gaussian_debug.h>
#include <vector>
#include <algorithm>

namespace global_planner {
class Index {
    public:
        Index(int a, float b) {
            i = a;
            cost = b;
        }
        int i;
        float cost;
};

struct greater1 {
        bool operator()(const Index& a, const Index& b) const {
            return a.cost > b.cost;
        }
};

class AStarExpansion : public Expander {
    public:
        AStarExpansion(PotentialCalculator* p_calc, int nx, int ny);
        AStarExpansion(PotentialCalculator* p_calc, int xs, int ys, unsigned char path_cost, unsigned char occ_dis_cost);
        AStarExpansion(PotentialCalculator* p_calc, int xs, int ys, unsigned char path_cost, unsigned char occ_dis_cost, const std::vector<XYPoint>& circle_center_point, double resolution);
        bool calculatePotentials(costmap_2d::Costmap2DROS* costmap_ros, unsigned char* costs, unsigned char* path_costs,
                                 double start_x, double start_y, double end_x, double end_y, int cycles, float* potential);
        int min_cost_index_;
    private:
        void add(costmap_2d::Costmap2DROS* costmap_ros, unsigned char* costs, unsigned char* path_costs, float* potential,
                float prev_potential, int current_i, int next_i, int end_x, int end_y);
        unsigned int GetCircleCenterLargestCost(unsigned char* costs, std::vector<XYPoint> circle_center, int current_i, int next_i);
        std::vector<Index> queue_;
        unsigned char path_cost_;
        unsigned char occ_dis_cost_;
        std::vector<XYPoint> circle_center_point_;
        bool use_circle_center_;
        double resolution_;
        int min_cost_; 
};

} //end namespace global_planner
#endif

