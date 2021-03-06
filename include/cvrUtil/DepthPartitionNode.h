/* -*-c++-*- 
 *
 *  OpenSceneGraph example, osgdepthpartion.
 *
 */

#ifndef _OF_DEPTHPARTITIONNODE_
#define _OF_DEPTHPARTITIONNODE_

#include <cvrUtil/Export.h>
#include <cvrUtil/DistanceAccumulator.h>

#include <osg/Camera>
#include <OpenThreads/Mutex>

#include <cstdio>

/**********************************************************
 * Ravi Mathur
 * OpenFrames API, class DepthPartitionNode
 * A type of osg::Group that analyzes a scene, then partitions it into
 * several segments that can be rendered separately. Each segment
 * is small enough in the z-direction to avoid depth buffer problems
 * for very large scenes.
 **********************************************************/
class CVRUTIL_EXPORT DepthPartitionNode : public osg::Group
{
    public:
        DepthPartitionNode();
        DepthPartitionNode(const DepthPartitionNode& dpn,
                const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY);

        META_Node( OpenFrames, DepthPartitionNode ); // Common Node functions

        /** Set the active state. If not active, this node will simply add the
         specified scene as it's child, without analyzing it at all. */
        void setActive(bool active);
        inline bool getActive() const
        {
            return _active;
        }

        /** Specify whether the color buffer should be cleared before the first
         Camera draws it's scene. */
        void setClearColorBuffer(bool clear);
        inline bool getClearColorBuffer() const
        {
            return _clearColorBuffer;
        }

        /** Set/get the maximum depth that the scene will be traversed to.
         Defaults to UINT_MAX. */
        void setMaxTraversalDepth(unsigned int depth)
        {
            printf("SETMAX\n");
        } //_distAccumulator->setMaxDepth(depth);

        inline unsigned int getMaxTraversalDepth() const
        {
            printf("GETMAX\n");
            return 0;
        } //_distAccumulator->getMaxDepth();

        /** Override update and cull traversals */
        virtual void traverse(osg::NodeVisitor &nv);

        /** Catch child management functions so the Cameras can be informed
         of added or removed children. */
        virtual bool addChild(osg::Node *child);
        virtual bool insertChild(unsigned int index, osg::Node *child);
        virtual bool removeChildren(unsigned int pos,
                unsigned int numRemove = 1);
        virtual bool setChild(unsigned int i, osg::Node *node);

        void setForwardOtherTraversals(bool b);
        bool getForwardOtherTraversals();

        void removeNodesFromCameras();

        protected:
        typedef std::vector<osg::ref_ptr<osg::Camera> > CameraList;

        ~DepthPartitionNode();

        void init();

        // Creates a new Camera object with default settings
        osg::Camera* createOrReuseCamera(const osg::Matrix& proj, double znear,
                double zfar, const unsigned int &camNum, int context, osg::Camera * rootCam,const int numCameras);

        bool _active;// Whether partitioning is active on the scene

        // The NodeVisitor that computes cameras for the scene
        osg::ref_ptr<DistanceAccumulator> _distAccumulator;

        bool _clearColorBuffer;

        // Cameras that should be used to draw the scene.  These cameras
        // will be reused on every frame in order to save time and memory.
        std::map<int,CameraList> _cameraList;
        unsigned int _numCameras;// Number of Cameras actually being used

        std::map<int,osg::ref_ptr<DistanceAccumulator> > _daMap;

        bool _forwardOtherTraversals;
        OpenThreads::Mutex _lock;
    };

#endif
