#pragma once

#include "DiscreteTheta.hpp"
#include <limits>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <base/Pose.hpp>
#include <maps/grid/Index.hpp>
#include <motion_planning_libraries/sbpl/SbplMotionPrimitives.hpp>
#include <motion_planning_libraries/sbpl/SbplSplineMotionPrimitives.hpp>

namespace motion_planning_libraries
{
    class MotionPrimitivesConfig;
};

namespace ugv_nav4d
{

class RobotModel
{
public:
    RobotModel(double tr, double rv);
    
    ///in m per sec
    double translationalVelocity;
    ///in rad per sec
    double rotationalVelocity;
};
  
struct PoseWithCell
{
    base::Pose2D pose;
    maps::grid::Index cell;
};

struct Motion
{
    enum Type {
        MOV_FORWARD,
        MOV_BACKWARD,
        MOV_POINTTURN,
        MOV_LATERAL,
    };

    Motion(unsigned int numAngles) : endTheta(0, numAngles),startTheta(0, numAngles), baseCost(0), id(std::numeric_limits<size_t>::max()) {};
    
    int xDiff;
    int yDiff;
    DiscreteTheta endTheta;
    DiscreteTheta startTheta;
    
    double speed;
    
    Type type;
    
    /**the intermediate poses are not discrete.
        * They are relative to the starting cell*/
    std::vector<PoseWithCell> intermediateSteps;
    
    int baseCost;
    
    int costMultiplier;
    
    size_t id;
    
};

class PreComputedMotions
{
    //indexed by discrete start theta
    std::vector<std::vector<Motion> > thetaToMotion;
    std::vector<Motion> idToMotion;
    motion_planning_libraries::SbplMotionPrimitives primitives;
     
public:
    PreComputedMotions(const motion_planning_libraries::MotionPrimitivesConfig& primitiveConfig, const RobotModel &model);
    
    /**Initialize using spline based primitives */
    PreComputedMotions(const motion_planning_libraries::SplinePrimitivesConfig& primitiveConfig,
                       const RobotModel &model,
                       const motion_planning_libraries::Mobility& mobilityConfig);
    
    void readMotionPrimitives(const motion_planning_libraries::SbplMotionPrimitives& primGen, const RobotModel& model);
    
    void readMotionPrimitives(const motion_planning_libraries::SbplSplineMotionPrimitives& primGen,
                              const RobotModel& model,
                              const motion_planning_libraries::Mobility& mobilityConfig);
    
    void setMotionForTheta(const Motion &motion, const DiscreteTheta &theta);
    
    void preComputeCost(Motion &motion, const RobotModel &model);
    
    const std::vector<Motion> &getMotionForStartTheta(const DiscreteTheta &theta) const
    {
        if(theta.getTheta() >= (int)thetaToMotion.size())
        {
            std::cout << "Input theta is " << theta.getTheta();
            throw std::runtime_error("Internal error, motion for requested theta ist not available");
        }
        return thetaToMotion.at(theta.getTheta());
    };
    
    const Motion &getMotion(std::size_t id) const; 
    
    const motion_planning_libraries::SbplMotionPrimitives& getPrimitives() const;
    
private:
    
    void computeSplinePrimCost(const motion_planning_libraries::SplinePrimitive& prim,
                               const RobotModel &model,
                               Motion& outMotion) const;
    
};

}