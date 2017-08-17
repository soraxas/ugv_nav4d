#include "FrontierGenerator.hpp"
#include <vizkit3d_debug_drawings/DebugDrawing.h>
#include <vizkit3d_debug_drawings/DebugDrawingColors.h>
#include "TravMapBfsVisitor.hpp"
#include "CollisionCheck.hpp"
#include "Dijkstra.hpp"
#include <Eigen/Geometry>
#include "PathStatistics.hpp"


#define SHOW(x) std::cout << #x": "<< (x) << std::endl

using base::samples::RigidBodyState;
using maps::grid::TraversabilityNodeBase;
namespace ugv_nav4d 
{

struct NodeWithOrientation
{
    const TravGenNode* node;
    double orientationZ;
};

struct NodeWithOrientationAndCost
{
    const TravGenNode* node;
    double orientationZ;
    double cost;
};

    
    
FrontierGenerator::FrontierGenerator(const TraversabilityConfig& travConf,
                                     const CostFunctionParameters& costParams) :
    costParams(costParams), travConf(travConf), travGen(travConf),
    robotPos(0, 0, 0), goalPos(0, 0, 0)
{
}

void FrontierGenerator::setInitialPatch(const Eigen::Affine3d& body2Mls, double patchRadius)
{
    travGen.setInitialPatch(body2Mls, patchRadius);
}

void FrontierGenerator::updateGoalPos(const base::Vector3d& _goalPos)
{
    goalPos = _goalPos;
    CLEAR_DRAWING("goalPos");
    DRAW_ARROW("goalPos", _goalPos, base::Quaterniond(Eigen::AngleAxisd(M_PI, base::Vector3d::UnitX())),
               base::Vector3d(1,1,1), vizkit3dDebugDrawings::Color::yellow);
    
    CLEAR_DRAWING("robotToGoal");
    DRAW_LINE("robotToGoal", robotPos, goalPos, vizkit3dDebugDrawings::Color::magenta);
    
}


void FrontierGenerator::updateRobotPos(const base::Vector3d& _robotPos)
{
    robotPos = _robotPos;
    CLEAR_DRAWING("RobotPos");
    DRAW_ARROW("RobotPos", _robotPos, base::Quaterniond(Eigen::AngleAxisd(M_PI, base::Vector3d::UnitX())),
               base::Vector3d(1,1,1), vizkit3dDebugDrawings::Color::blue);
    
    CLEAR_DRAWING("robotToGoal");
    DRAW_LINE("robotToGoal", robotPos, goalPos, vizkit3dDebugDrawings::Color::magenta);
}


base::Vector3d FrontierGenerator::nodeCenterPos(const TravGenNode* node) const
{
    Eigen::Vector3d pos;
    travGen.getTraversabilityMap().fromGrid(node->getIndex(), pos, node->getHeight(), false);
    return pos;
}

    
std::vector<RigidBodyState> FrontierGenerator::getNextFrontiers()
{
    CLEAR_DRAWING("visitable");
    
    std::vector<RigidBodyState> result;
    
    TravGenNode* startNode = travGen.generateStartNode(robotPos);
    travGen.expandAll(startNode);
    
    std::cout << "find frontiers" << std::endl;
    const std::vector<const TravGenNode*> frontier(getFrontierPatches());
    const std::vector<NodeWithOrientation> frontierWithOrientation(getFrontierOrientation(frontier));
    std::cout << "found frontiers: " << frontierWithOrientation.size() << std::endl;
    
    std::cout << "find candidates" << std::endl;
    const std::vector<NodeWithOrientation> candidatesWithOrientation(getCandidatesFromFrontierPatches(frontierWithOrientation));
    if(candidatesWithOrientation.size() == 0)
    {
        std::cout << "No candidates found" << std::endl;
        return result;
    }
    else
    {
        std::cout << "found candidates: " << candidatesWithOrientation.size() << std::endl;
    }
    
    std::cout << "Finding collision free neighbors" << std::endl;
    const std::vector<NodeWithOrientation> collisionFreeNeighbors(getCollisionFreeNeighbor(candidatesWithOrientation));
    std::cout << "Neighbors: " << collisionFreeNeighbors.size() << std::endl;
    
    std::cout << "Removing duplicates" << std::endl;
    const std::vector<NodeWithOrientation> nodesWithoutDuplicates(removeDuplicates(collisionFreeNeighbors));
    std::cout << "frontiers without duplicates: " << nodesWithoutDuplicates.size() << std::endl;
    
    std::cout << "calculating costs" << std::endl;
    const std::vector<NodeWithOrientationAndCost> nodesWithCost = calculateCost(startNode, goalPos, nodesWithoutDuplicates);
    std::cout << "calculated costs: " << nodesWithCost.size() << std::endl;
    
    std::cout << "sorting ndoes" << std::endl;
    const std::vector<NodeWithOrientationAndCost> sortedNodes(sortNodes(nodesWithCost));
    std::cout << "sorted nodes: " << sortedNodes.size() << std::endl;
    
    std::cout << "Converting to poses" << std::endl;
    result = getPositions(sortedNodes);
    
    std::cout << "Done. position count: " << result.size() << std::endl;
    
    //test code:
    
    
    COMPLEX_DRAWING(
        CLEAR_DRAWING("candidates");
        for(const auto& node : candidatesWithOrientation)
        {
            base::Vector3d pos(node.node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x(), 
                               node.node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y(),
                               node.node->getHeight());
            pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
            DRAW_CYLINDER("candidates", pos + base::Vector3d(travGen.getTraversabilityMap().getResolution().x() / 2.0, travGen.getTraversabilityMap().getResolution().y() / 2.0, travGen.getTraversabilityMap().getResolution().x() / 2.0), base::Vector3d(0.05, 0.05, 2), vizkit3dDebugDrawings::Color::blue);
        }
    );
    
      COMPLEX_DRAWING(
        double maxCost = 0;
        double costSum = 0;
        CLEAR_DRAWING("explorable");  
        for(const auto& node : sortedNodes)
        {
            costSum += node.cost;
            if(node.cost > maxCost) 
                maxCost = node.cost;
        }
        for(const auto& node : sortedNodes)
        {
            const double value = (node.cost);// / maxCost) * 2;
            base::Vector3d pos(nodeCenterPos(node.node));
            pos.z() += value / 2.0;
            
            DRAW_CYLINDER("explorable", pos,  base::Vector3d(0.03, 0.03, value), vizkit3dDebugDrawings::Color::green);
        }
      );
    
    
//     COMPLEX_DRAWING(
//         CLEAR_DRAWING("frontierWithOrientation");
//         for(const NodeWithOrientation& node : frontierWithOrientation)
//         {
//             Eigen::Vector3d pos(node.node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x() + travGen.getTraversabilityMap().getResolution().x() / 2.0, node.node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y() + travGen.getTraversabilityMap().getResolution().y() / 2.0, node.node->getHeight());
//             pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
//             pos.z() += 0.02;
//             const double radius = travGen.getTraversabilityMap().getResolution().x() / 2.0;
//             DRAW_RING("frontierWithOrientation", pos, radius, 0.01, 0.01, vizkit3dDebugDrawings::Color::blue);
//             const Eigen::Rotation2Dd rot(node.orientationZ);
//             Eigen::Vector2d rotVec(travGen.getTraversabilityMap().getResolution().x() / 2.0, 0);
//             rotVec = rot * rotVec;
//             Eigen::Vector3d to(pos);
//             to.topRows(2) += rotVec;
//             DRAW_LINE("frontierWithOrientation", pos, to, vizkit3dDebugDrawings::Color::cyan);
//         }
//      );
     
//     COMPLEX_DRAWING(
//         CLEAR_DRAWING("nodesWithoutCollisions");
//         for(const NodeWithOrientation& node : nodesWithoutCollisions)
//         {
//             Eigen::Vector3d pos(node.node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x() + travGen.getTraversabilityMap().getResolution().x() / 2.0, node.node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y() + travGen.getTraversabilityMap().getResolution().y() / 2.0, node.node->getHeight());
//             pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
//             pos.z() += 0.02;
//             const double radius = travGen.getTraversabilityMap().getResolution().x() / 2.0;
//             DRAW_RING("nodesWithoutCollisions", pos, radius, 0.01, 0.01, vizkit3dDebugDrawings::Color::green);
//             const Eigen::Rotation2Dd rot(node.orientationZ);
//             Eigen::Vector2d rotVec(travGen.getTraversabilityMap().getResolution().x() / 2.0, 0);
//             rotVec = rot * rotVec;
//             Eigen::Vector3d to(pos);
//             to.topRows(2) += rotVec;
//             DRAW_LINE("nodesWithoutCollisions", pos, to, vizkit3dDebugDrawings::Color::green);
//         }
//      );
    
//     COMPLEX_DRAWING(
//         CLEAR_DRAWING("sortedNodes");
//         for(size_t i = 0; i < sortedNodes.size(); ++i)
//         {
//             const NodeWithOrientationAndCost& node = sortedNodes[i];
//             Eigen::Vector3d pos(node.node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x() + travGen.getTraversabilityMap().getResolution().x() / 2.0, node.node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y() + travGen.getTraversabilityMap().getResolution().y() / 2.0, node.node->getHeight());
//             pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
//             pos.z() += 0.02;
//             
//             DRAW_TEXT("sortedNodes", pos, std::to_string(i), 0.3, vizkit3dDebugDrawings::Color::magenta);
//         }
//     );
     
    

    return std::move(result);
}




 
std::vector<const TravGenNode*> FrontierGenerator::getFrontierPatches() const
{
    std::vector<const TravGenNode*> frontier;
    for(const maps::grid::LevelList<TravGenNode*>& list : travGen.getTraversabilityMap())
    {
        for(const TravGenNode* node : list)
        {
            if(node->getType() == TraversabilityNodeBase::FRONTIER)
            {
                frontier.push_back(node);
            }
        }
    }  
    return std::move(frontier);    
}

std::vector<NodeWithOrientation> FrontierGenerator::getCandidatesFromFrontierPatches(const std::vector<NodeWithOrientation> &frontiers) const
{
    std::vector<NodeWithOrientation> candidates;
    
    for(const NodeWithOrientation& node : frontiers)
    {
        for(const TraversabilityNodeBase *connected : node.node->getConnections())
        {
            if(connected->getType() == TraversabilityNodeBase::TRAVERSABLE)
            {
                candidates.push_back(NodeWithOrientation{reinterpret_cast<const TravGenNode *>(connected), node.orientationZ});
            }
        }
    }
    return std::move(candidates); 
}


std::vector<NodeWithOrientation> FrontierGenerator::getFrontierOrientation(const std::vector<const TravGenNode*>& frontier) const
{
    //sobel filter is used to get an estimate of the edge direction
    const int yOp[3][3] = {{1,0,-1},
                           {2,0,-2},
                           {1,0,-1}};
    const int xOp[3][3] = {{1,2,1},
                           {0,0,0},
                           {-1,-2,-1}};
    
    CLEAR_DRAWING("edge direction");

    std::vector<NodeWithOrientation> frontierWithOrientation;
    for(const TravGenNode* frontierPatch : frontier)
    {
        int xSum = 0;
        int ySum = 0;
        const maps::grid::Index i = frontierPatch->getIndex();
        
        for(int x = -1; x < 2; ++x)
        {
            for(int y = -1; y < 2; ++y)
            {
                const maps::grid::Index neighborIndex(x + i.x(), y + i.y());
                if(travGen.getTraversabilityMap().inGrid(neighborIndex))
                {
                    const TravGenNode* neighbor = frontierPatch->getConnectedNode(neighborIndex);
                    if(neighbor != nullptr && neighbor->getType() != TraversabilityNodeBase::UNKNOWN &&
                        neighbor->getType() != TraversabilityNodeBase::UNSET)
                    {
                        xSum += xOp[x + 1][y + 1];
                        ySum += yOp[x + 1][y + 1];
                    }
                }
            }
        } 
        
        base::Angle orientation = base::Angle::fromRad(atan2(ySum, xSum));
        //check if frontier is allowed, if not use the closest allowed frontier
        bool orientationAllowed = false;
        for(const base::AngleSegment& allowedOrientation : frontierPatch->getUserData().allowedOrientations)
        {
            if(allowedOrientation.isInside(orientation))
            {
                orientationAllowed = true;
                break;
            }
        }
        if(!orientationAllowed)
        {
            const base::AngleSegment& firstSegment = frontierPatch->getUserData().allowedOrientations[0];
            orientation = firstSegment.getStart();
            orientation += base::Angle::fromRad(firstSegment.getWidth() / 2.0);
        }
        frontierWithOrientation.push_back(NodeWithOrientation{frontierPatch, orientation.getRad()});

        COMPLEX_DRAWING(
        
            Eigen::Vector3d start(i.x() * travConf.gridResolution + travConf.gridResolution / 2.0, i.y() * travConf.gridResolution + travConf.gridResolution / 2.0, frontierPatch->getHeight());
            start = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * start;
            Eigen::Vector3d end((i.x() + xSum) * travConf.gridResolution + travConf.gridResolution / 2.0, (i.y() + ySum) * travConf.gridResolution + travConf.gridResolution / 2.0, frontierPatch->getHeight());
            end = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * end;
            DRAW_LINE("edge direction", start, end, vizkit3dDebugDrawings::Color::red);
        );
    }
    return std::move(frontierWithOrientation);
}


std::vector<NodeWithOrientation> FrontierGenerator::getNodesWithoutCollision(const std::vector<NodeWithOrientation>& nodes) const
{
    std::vector<NodeWithOrientation> result;
    const base::Vector3d robotHalfSize(travConf.robotSizeX / 2, travConf.robotSizeY / 2, travConf.robotHeight / 2);
    
    CLEAR_DRAWING("removed_due_to_collision");
    
    for(const NodeWithOrientation& node : nodes)
    {
        if(CollisionCheck::checkCollision(node.node, node.orientationZ, mlsMap, robotHalfSize, travGen))
        {
            result.push_back(node);
        }
        else
        {
            COMPLEX_DRAWING(
            base::Vector3d pos(node.node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x(), 
                               node.node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y(),
                               node.node->getHeight());
            pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
            DRAW_CYLINDER("removed_due_to_collision", pos + base::Vector3d(travGen.getTraversabilityMap().getResolution().x() / 2.0, travGen.getTraversabilityMap().getResolution().y() / 2.0, travGen.getTraversabilityMap().getResolution().x() / 2.0), base::Vector3d(0.05, 0.05, 2), vizkit3dDebugDrawings::Color::magenta);
            );

        }
    }   
    
    return std::move(result);
}

std::vector<NodeWithOrientation> FrontierGenerator::getCollisionFreeNeighbor(const std::vector<NodeWithOrientation>& nodes) const
{
    std::vector<NodeWithOrientation> result;
    
    //FIXME the collisions are drawn inside CollisionCheck::checkCollision().
    //      If they are not cleared we will run out of memory after some time.

    
    for(const NodeWithOrientation& node : nodes)
    {
        
        maps::grid::Vector3d nodePos;
        travGen.getTraversabilityMap().fromGrid(node.node->getIndex(), nodePos, node.node->getHeight(), false);
        
        const TravGenNode* traversableNeighbor = nullptr;
        TravMapBfsVisitor::visit(node.node, 
            [this, &traversableNeighbor, &nodePos, &node]
            (const TravGenNode* currentNode, bool& visitChildren, bool& abort, std::size_t distToRoot)
            {
                maps::grid::Vector3d neighborPos;
                travGen.getTraversabilityMap().fromGrid(currentNode->getIndex(), neighborPos, currentNode->getHeight(), false);
                
                if(currentNode->getType() == maps::grid::TraversabilityNodeBase::TRAVERSABLE)
                {
                    const base::Pose2D pose(neighborPos.topRows(2), node.orientationZ);
                    PathStatistic stats(travConf);
                    std::vector<const TravGenNode*> path;
                    path.push_back(currentNode);
                    std::vector<base::Pose2D> poses;
                    poses.push_back(pose);
                    stats.calculateStatistics(path, poses, travGen.getTraversabilityMap());
                    
                    if(stats.getRobotStats().getNumObstacles() == 0)
                    {
                        //found a patch that the robot can stand on without collision.
                        traversableNeighbor = currentNode;
                        abort = true;
                    }
                    else
                    {
                        abort = false;
                    }
                }
                else
                {
                    abort = false;
                }
                
                if(!abort)
                {
                    DRAW_CYLINDER("neighBorobstacleCheck", neighborPos, base::Vector3d(0.05, 0.05, 2), vizkit3dDebugDrawings::Color::red);
                    
                    const double dist = (nodePos - neighborPos).norm();
                    if(dist < maxNeighborDistance)
                        visitChildren = true;
                    else
                        visitChildren = false;
                }
                
            });
                
        if(traversableNeighbor != nullptr)
            result.push_back({traversableNeighbor, node.orientationZ});
    }

    return std::move(result);
}


std::vector<NodeWithOrientation> FrontierGenerator::removeDuplicates(const std::vector<NodeWithOrientation>& nodes) const
{
    //FIXME probably performance could be improved a lot
    std::vector<NodeWithOrientation> result;
    std::unordered_set<const TravGenNode*> set;
    for(const NodeWithOrientation& node : nodes)
    {
        if(set.find(node.node) == set.end())
        {
            set.insert(node.node);
            result.push_back(node);
        }
    }
    return result;
}


std::vector<NodeWithOrientationAndCost> FrontierGenerator::sortNodes(const std::vector<NodeWithOrientationAndCost>& nodes) const
{
    std::vector<NodeWithOrientationAndCost> result(nodes);
    std::sort(result.begin(), result.end(), 
        [](const NodeWithOrientationAndCost& a, const NodeWithOrientationAndCost& b)
        {
            return a.cost < b.cost;
        });
    return std::move(result);
}

std::vector<RigidBodyState> FrontierGenerator::getPositions(const std::vector<NodeWithOrientationAndCost>& nodes) const
{
    std::vector<RigidBodyState> result;
    const maps::grid::TraversabilityMap3d<TravGenNode *> &map(travGen.getTraversabilityMap());
    for(const NodeWithOrientationAndCost& node : nodes)
    {
        RigidBodyState rbs;
        Eigen::Vector3d pos;
        map.fromGrid(node.node->getIndex(), pos, node.node->getHeight(), false);
        rbs.position = pos;
        rbs.orientation = Eigen::AngleAxisd(node.orientationZ, Eigen::Vector3d::UnitZ());
        result.push_back(rbs);
    }
    return std::move(result);
}

std::vector<NodeWithOrientationAndCost> FrontierGenerator::calculateCost(const TravGenNode* startNode,
                                                                         const base::Vector3d& goalPos,
                                                                         const std::vector<NodeWithOrientation>& nodes) const
{
    //calc travel distances on map
    std::vector<NodeWithOrientationAndCost> result;
    std::unordered_map<const maps::grid::TraversabilityNodeBase*, double> distancesOnMap;
    Dijkstra::computeCost(startNode, distancesOnMap, travConf);
    
    //find max distances for normalization
    double maxDistFromStart = 0;
    double maxDistToGoal = 0;
    for(const NodeWithOrientation& node : nodes)
    {
        if(distancesOnMap.find(node.node) == distancesOnMap.end())
        {
            //this means there is no traversable connection to the node.
            continue;
        }
        
        const double distToGoal = distToPoint(node.node, goalPos);
        const double distFromStart = distancesOnMap[node.node];

        //0.0 is a dummy cost here, it is updated in the next loop
        result.push_back({node.node, node.orientationZ, 0.0});

        maxDistToGoal = std::max(maxDistToGoal, distToGoal);
        maxDistFromStart = std::max(maxDistFromStart, distFromStart);
    }

    //calc cost
    for(NodeWithOrientationAndCost& node : result)
    {
        if(distancesOnMap.find(node.node) == distancesOnMap.end())
        {
            throw std::runtime_error("Internal Error, list contains non reachable nodes");
        }

        const double distFromStart = distancesOnMap[node.node];
        const double distToGoal = distToPoint(node.node, goalPos) / maxDistToGoal; //range 0..1
        const double explorableFactor = calcExplorablePatches(node.node); //range: 0.. 1
        const double travelDist = distFromStart / maxDistFromStart; //range: 0..1
        
        assert(distToGoal >= 0 && distToGoal <= 1);
        assert(explorableFactor >= 0 && explorableFactor <= 1);
        assert(travelDist >= 0 && travelDist <= 1);
        
        const double cost = costParams.distToGoalFactor * distToGoal +
                            costParams.explorableFactor * explorableFactor +
                            costParams.distFromStartFactor * travelDist;
        
        node.cost = cost;
    }
    return result;
}


double FrontierGenerator::distToPoint(const TravGenNode* node, const base::Vector3d& p) const
{
    const base::Vector3d nodePos(nodeCenterPos(node));
    return (nodePos - p).norm();
}



double FrontierGenerator::calcExplorablePatches(const TravGenNode* node) const
{
    std::size_t visited = 0;
    const size_t visitRadius = 3; //FIXME should be parameter
    /* Since the grid is a square we can calculate the number of visitable nodes using odd square*/
    const std::size_t maxVisitable = std::pow(2 * visitRadius + 1, 2);
    TravMapBfsVisitor::visit(node, 
        [&visited] (const TravGenNode* currentNode, bool& visitChildren, bool& abort, std::size_t distToRoot)
        {
            ++visited;
            abort = false;
            if(distToRoot >= visitRadius)
                visitChildren = false;
            else
                visitChildren = true;
        });
    
    assert(visited <= maxVisitable);
    
    const double explorablePatches = (visited / (double)maxVisitable);
    
    COMPLEX_DRAWING(
        Eigen::Vector3d pos(node->getIndex().x() * travGen.getTraversabilityMap().getResolution().x() + travGen.getTraversabilityMap().getResolution().x() / 2.0, node->getIndex().y() * travGen.getTraversabilityMap().getResolution().y() + travGen.getTraversabilityMap().getResolution().y() / 2.0, node->getHeight());
        pos = travGen.getTraversabilityMap().getLocalFrame().inverse(Eigen::Isometry) * pos;
        pos.z() += 0.02;
        
        DRAW_TEXT("visitable", pos, std::to_string(maxVisitable - visited), 0.3, vizkit3dDebugDrawings::Color::magenta);
    );
    
    
    return explorablePatches;
}

void FrontierGenerator::updateCostParameters(const CostFunctionParameters& params)
{
    costParams = params;
}

maps::grid::TraversabilityMap3d< TraversabilityNodeBase* > FrontierGenerator::getTraversabilityBaseMap() const
{
    return travGen.getTraversabilityBaseMap();
}

const TraversabilityConfig& FrontierGenerator::getConfig() const
{
    return travConf;
}



}