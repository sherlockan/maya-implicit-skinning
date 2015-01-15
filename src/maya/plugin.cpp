// Don't include CUDA headers in this file.  CUDA and Maya headers are incompatible due to
// namespace conflicts.
#define NO_CUDA

#include "plugin.hpp"
#include <string.h>
#include <maya/MIOStream.h>
#include <math.h>
#include <assert.h>

#include <maya/MGlobal.h> 
#include <maya/MPxDeformerNode.h> 
#include <maya/MPxCommand.h>
#include <maya/MPxData.h>
#include <maya/MPxTransform.h>
#include <maya/MItGeometry.h>
#include <maya/MItMeshVertex.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMatrixData.h>

#include <maya/MFnDagNode.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnTransform.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnGeometryFilter.h>
#include <maya/MFnPluginData.h>
#include <maya/MFnMeshData.h>

#include <maya/MArgList.h>
#include <maya/MTypeId.h> 
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MFnMesh.h>

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>

#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MVector.h>
#include <maya/MVectorArray.h>
#include <maya/MMatrix.h>

#include <maya/MDagModifier.h>

#include "maya/maya_helpers.hpp"
#include "maya/maya_data.hpp"

#include "loader_mesh.hpp"
#include "loader_skel.hpp"
#include "skeleton.hpp"

#include "loader_mesh.hpp"
#include "sample_set.hpp"
#include "animated_mesh_ctrl.hpp"
#include "cuda_ctrl.hpp"
#include "skeleton_ctrl.hpp"
#include "marching_cubes/marching_cubes.hpp"
#include "vert_to_bone_info.hpp"

// #include "animesh.hpp"

#include <algorithm>
#include <map>
using namespace std;

// XXX: http://help.autodesk.com/view/MAYAUL/2015/ENU/?guid=__cpp_ref_class_m_type_id_html says that
// ADN assigns public blocks of IDs, but nothing says how to request a block without paying for
// a commercial ADN account.  Let's use a value in the devkit sample range, so it's unlikely to conflict,
// and it does, it won't conflict with somebody's internal-use IDs (0-0x7ffff).  At worst, we'll collide
// with a sample or somebody else doing the same thing.
const MTypeId ImplicitSkinDeformer::id(0xEA115);
MObject ImplicitSkinDeformer::basePotentialAttr;
MObject ImplicitSkinDeformer::baseGradientAttr;
MObject ImplicitSkinDeformer::influenceJointsAttr;
MObject ImplicitSkinDeformer::parentJointAttr;
MObject ImplicitSkinDeformer::influenceBindMatrixAttr;
MObject ImplicitSkinDeformer::influenceMatrixAttr;
MObject ImplicitSkinDeformer::sampleSetUpdateAttr;
MObject ImplicitSkinDeformer::skeletonUpdateAttr;
MObject ImplicitSkinDeformer::meshUpdateAttr;
MObject ImplicitSkinDeformer::hrbfRadiusAttr;
MObject ImplicitSkinDeformer::samplePointAttr;
MObject ImplicitSkinDeformer::sampleNormalAttr;
MObject ImplicitSkinDeformer::visualizationGeomUpdateAttr;
MObject ImplicitSkinDeformer::visualizationGeomAttr;

static void loadDependency(MObject obj, MObject attr, MStatus *status)
{
    if(*status != MStatus::kSuccess)
        return;

    MPlug updatePlug(obj, attr);

    bool unused;
    *status = updatePlug.getValue(unused);
}

MStatus ImplicitSkinDeformer::test()
{
    /*
    Skeleton *skel = skeleton.skel;
    vector<const Bone *> bones = skel->get_bones();
    if(bones.size() < 1)
        return MStatus::kSuccess;
    Bone *bone = bones[1];
    if(bone->get_type() != EBone::Bone_t::HRBF)
        return MStatus::kSuccess;

    Bone_hrbf *b = (Bone_hrbf *) bone;
    HermiteRBF &hrbf = b->get_hrbf();
    vector<Vec3_cu> samples;
    hrbf.get_samples(samples);
*/
    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::initialize()
{
    MStatus status = MStatus::kSuccess;

    DagHelpers::MayaDependencies dep;

    // XXX
    // MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer blendNode weights;");

    MFnMatrixAttribute mAttr;
    MFnNumericAttribute numAttr;
    MFnCompoundAttribute cmpAttr;
    MFnTypedAttribute typeAttr;

    influenceBindMatrixAttr = mAttr.create("influenceBindMatrix", "ibm");
    addAttribute(influenceBindMatrixAttr);

    parentJointAttr = numAttr.create("parentIdx", "parentIdx", MFnNumericData::Type::kInt, -1, &status);
    addAttribute(parentJointAttr);

    // The joint's output matrix.
    influenceMatrixAttr = mAttr.create("matrix", "ma");
    addAttribute(influenceMatrixAttr);

    // SampleSet:
    samplePointAttr = numAttr.create("point", "p", MFnNumericData::Type::k3Float, 0, &status);
    numAttr.setArray(true);
    addAttribute(samplePointAttr);

    sampleNormalAttr = numAttr.create("normal", "n", MFnNumericData::Type::k3Float, 0, &status);
    numAttr.setArray(true);
    addAttribute(sampleNormalAttr);

    hrbfRadiusAttr = numAttr.create("hrbfRadius", "hrbfRadius", MFnNumericData::Type::kFloat, 0, &status);
    addAttribute(hrbfRadiusAttr);
    
    // The main joint array:
    influenceJointsAttr = cmpAttr.create("joints", "jt", &status);
    cmpAttr.setArray(true);
    cmpAttr.addChild(influenceBindMatrixAttr);
    cmpAttr.addChild(parentJointAttr);
    cmpAttr.addChild(influenceMatrixAttr);
    cmpAttr.addChild(samplePointAttr);
    cmpAttr.addChild(sampleNormalAttr);
    cmpAttr.addChild(hrbfRadiusAttr);
    addAttribute(influenceJointsAttr);

    skeletonUpdateAttr = numAttr.create("skeletonUpdate", "skeletonUpdate", MFnNumericData::Type::kInt, 0, &status);
    numAttr.setStorable(false);
    numAttr.setHidden(true);
    addAttribute(skeletonUpdateAttr);
    if(status != MS::kSuccess) return status;
    dep.add(ImplicitSkinDeformer::parentJointAttr, ImplicitSkinDeformer::skeletonUpdateAttr);
    dep.add(ImplicitSkinDeformer::influenceBindMatrixAttr, ImplicitSkinDeformer::skeletonUpdateAttr);

    meshUpdateAttr = numAttr.create("meshUpdate", "meshUpdate", MFnNumericData::Type::kInt, 0, &status);
    numAttr.setStorable(false);
    numAttr.setHidden(true);
    addAttribute(meshUpdateAttr);
    dep.add(ImplicitSkinDeformer::skeletonUpdateAttr, ImplicitSkinDeformer::meshUpdateAttr);
    dep.add(ImplicitSkinDeformer::basePotentialAttr, ImplicitSkinDeformer::meshUpdateAttr);
    dep.add(ImplicitSkinDeformer::baseGradientAttr, ImplicitSkinDeformer::meshUpdateAttr);
    dep.add(ImplicitSkinDeformer::input, ImplicitSkinDeformer::meshUpdateAttr);
    dep.add(ImplicitSkinDeformer::inputGeom, ImplicitSkinDeformer::meshUpdateAttr);

    sampleSetUpdateAttr = numAttr.create("sampleSetUpdate", "sampleSetUpdate", MFnNumericData::Type::kInt, 0, &status);
    numAttr.setStorable(false);
    numAttr.setHidden(true);
    addAttribute(sampleSetUpdateAttr);

    dep.add(ImplicitSkinDeformer::influenceBindMatrixAttr, ImplicitSkinDeformer::sampleSetUpdateAttr);
    dep.add(ImplicitSkinDeformer::samplePointAttr, ImplicitSkinDeformer::sampleSetUpdateAttr);
    dep.add(ImplicitSkinDeformer::sampleNormalAttr, ImplicitSkinDeformer::sampleSetUpdateAttr);
    dep.add(ImplicitSkinDeformer::hrbfRadiusAttr, ImplicitSkinDeformer::sampleSetUpdateAttr);
    dep.add(ImplicitSkinDeformer::skeletonUpdateAttr, ImplicitSkinDeformer::sampleSetUpdateAttr);

    // The base potential of the mesh.
    basePotentialAttr = numAttr.create("basePotential", "bp", MFnNumericData::Type::kFloat, 0, &status);
    numAttr.setArray(true);
    addAttribute(basePotentialAttr);

    baseGradientAttr = numAttr.create("baseGradient", "bg", MFnNumericData::Type::k3Float, 0, &status);
    numAttr.setArray(true);
    addAttribute(baseGradientAttr);

    // All of the dependency nodes are required by the output geometry.
    dep.add(ImplicitSkinDeformer::influenceMatrixAttr, ImplicitSkinDeformer::outputGeom);
    dep.add(ImplicitSkinDeformer::skeletonUpdateAttr, ImplicitSkinDeformer::outputGeom);
    dep.add(ImplicitSkinDeformer::meshUpdateAttr, ImplicitSkinDeformer::outputGeom);

    visualizationGeomUpdateAttr = numAttr.create("visualizationGeomUpdate", "visualizationGeomUpdate", MFnNumericData::Type::kInt, 0, &status);
    addAttribute(visualizationGeomUpdateAttr);
    numAttr.setHidden(true);
    numAttr.setStorable(false);
    dep.add(ImplicitSkinDeformer::sampleSetUpdateAttr, ImplicitSkinDeformer::visualizationGeomUpdateAttr);

    visualizationGeomAttr = typeAttr.create("visualizationGeom", "visualizationGeom", MFnData::Type::kMesh, MObject::kNullObj, &status);
    addAttribute(visualizationGeomAttr);
    dep.add(visualizationGeomUpdateAttr, visualizationGeomAttr);

    status = dep.apply();
    if(status != MS::kSuccess) return status;

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::compute(const MPlug& plug, MDataBlock& dataBlock)
{
    MStatus status = MStatus::kSuccess;

    // If we're calculating the output geometry, use the default implementation, which will
    // call deform().
    printf("Compute: %s\n", plug.name().asChar());
    if(plug.attribute() == outputGeom) return MPxDeformerNode::compute(plug, dataBlock);
    else if(plug.attribute() == sampleSetUpdateAttr) return load_sampleset(dataBlock);
    else if(plug.attribute() == skeletonUpdateAttr) return load_skeleton(dataBlock);
    else if(plug.attribute() == meshUpdateAttr) return load_mesh(dataBlock);

    else if(plug.attribute() == visualizationGeomUpdateAttr) return load_visualization_geom_data(dataBlock);
    else if(plug.attribute() == visualizationGeomAttr) return load_visualization_geom(dataBlock);
    
    else return MStatus::kUnknownParameter;
}

MStatus ImplicitSkinDeformer::deform(MDataBlock &dataBlock, MItGeometry &geomIter, const MMatrix &mat, unsigned int multiIndex)
{
    // We only support a single input, like skinCluster.
    if(multiIndex > 0)
        return MStatus::kSuccess;

    MStatus status = MStatus::kSuccess;

    // Read the dependency attributes that represent data we need.  We don't actually use the
    // results of inputvalue(); this is triggering updates for cudaCtrl data.
    dataBlock.inputValue(ImplicitSkinDeformer::skeletonUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    dataBlock.inputValue(ImplicitSkinDeformer::meshUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    // If we don't have a mesh or skeleton to work with yet, stop.
    if(mesh.get() == NULL || !skeleton.is_loaded())
        return MStatus::kSuccess;

    // Get the joint array.
    MArrayDataHandle influenceJointsHandle = dataBlock.inputArrayValue(influenceJointsAttr, &status);
    if(status != MS::kSuccess) return status;

    // Update the skeleton.
    map<Bone::Id,Transfo> bone_transforms;

    for(int i = 0; i < (int) influenceJointsHandle.elementCount(); ++i)
    {
        // If the user created more entries than there are bones (this is easy to do accidentally),
        // ignore them.
        if(i >= boneSet.bones.size())
            break;

        status = influenceJointsHandle.jumpToElement(i);
        if(status != MS::kSuccess) return status;

        int logicalIndex = influenceJointsHandle.elementIndex(&status);
        if(status != MS::kSuccess) return status;

        // The world transform the joint has now:
        MDataHandle matrixHandle = influenceJointsHandle.inputValue(&status).child(influenceMatrixAttr);
        if(status != MS::kSuccess) return status;
        
        // The world transform the joint had at bind time:
        MDataHandle bindMatrixHandle = influenceJointsHandle.inputValue(&status).child(influenceBindMatrixAttr);
        if(status != MS::kSuccess) return status;

        MMatrix bindPreMatrixWorldSpace = DagHelpers::readHandle<MMatrix>(bindMatrixHandle, &status); // original inverted world space transform
        MMatrix jointTransformWorldSpace = DagHelpers::readHandle<MMatrix>(matrixHandle, &status); // current world space transform

        MMatrix changeToTransform = bindPreMatrixWorldSpace * jointTransformWorldSpace; // joint transform relative to bind pose in world space
        
        const Bone *bone = boneSet.get_bone_by_idx(logicalIndex);
        bone_transforms[bone->get_bone_id()] = DagHelpers::MMatrixToTransfo(changeToTransform);
    }

    // Update the skeleton transforms.
    boneSet.set_transforms(bone_transforms);
    skeleton.skel->update_bones_data();

    // Update the vertex data.  We read all geometry, not just the set (if any) that we're being
    // applied to, so the algorithm can see the whole mesh.
    {
        // Get input.
        MArrayDataHandle inputArray = dataBlock.inputArrayValue(input, &status);
        if(status != MS::kSuccess) return status;

        // Get input[multiIndex].
        MDataHandle inputGeomData = DagHelpers::readArrayHandleLogicalIndex<MDataHandle>(inputArray, multiIndex, &status);
        if(status != MS::kSuccess) return status;

        // Get input[multiIndex].inputGeometry.
        MDataHandle inputGeomDataHandle = inputGeomData.child(inputGeom);

        // Load the vertex positions into animMesh.
        status = setGeometry(inputGeomDataHandle, mat);
        if(status != MS::kSuccess) return status;
    }

    // Run the algorithm.  XXX: If we're being applied to a set, use init_vert_to_fit to only
    // process the vertices we need to.
    animMesh->set_do_smoothing(true);
    animMesh->deform_mesh();

    vector<Point_cu> result_verts;
    animMesh->get_anim_vertices_aifo(result_verts);

    // Copy out the vertices that we were actually asked to process.
    MMatrix invMat = mat.inverse();
    for ( ; !geomIter.isDone(); geomIter.next()) {
        int vertex_index = geomIter.index();

        Point_cu v = result_verts[vertex_index];
        MPoint pt = MPoint(v.x, v.y, v.z) * invMat;
        status = geomIter.setPosition(pt, MSpace::kObject);
        if(status != MS::kSuccess) return status;
    }

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::load_skeleton(MDataBlock &dataBlock)
{
    MStatus status = MStatus::kSuccess;

    // Load the skeleton from the node.
    Loader::Abs_skeleton newSkeleton;
    status = createSkeleton(dataBlock, newSkeleton);
    if(status != MS::kSuccess) return status;

    // Load the BoneSet.  This will create the actual bones.
    boneSet.load(newSkeleton);

    // Load the skeleton.
    vector<Bone*> &bones = boneSet.all_bones();
    vector<const Bone*> const_bones(bones.begin(), bones.end());
    skeleton.skel.reset(new Skeleton(const_bones, newSkeleton._parents));
    if(mesh.get() != NULL && skeleton.is_loaded())
        animMesh.reset(new Animated_mesh_ctrl(mesh.get(), skeleton.skel.get()));

    return MStatus::kSuccess;
}

/*
 * Create an Abs_skeleton for the bind pose skeleton described by our attributes.
 */
MStatus ImplicitSkinDeformer::createSkeleton(MDataBlock &dataBlock, Loader::Abs_skeleton &skeleton)
{
    MStatus status = MStatus::kSuccess;

    // Create the bones.
    MArrayDataHandle influenceJointsHandle = dataBlock.inputArrayValue(ImplicitSkinDeformer::influenceJointsAttr, &status);
    if(status != MS::kSuccess) return status;

    for(int i = 0; i < (int) influenceJointsHandle.elementCount(); ++i)
    {
        status = influenceJointsHandle.jumpToElement(i);
        if(status != MS::kSuccess) return status;

        int logicalIndex = influenceJointsHandle.elementIndex(&status);
        if(status != MS::kSuccess) return status;

        // Get bind positions from the bindPreMatrix.
        MMatrix jointWorldMat;

        {
            MDataHandle influenceBindMatrixHandle = influenceJointsHandle.inputValue(&status).child(ImplicitSkinDeformer::influenceBindMatrixAttr);
            if(status != MS::kSuccess) return status;

            MMatrix influenceBindMatrix = DagHelpers::readHandle<MMatrix>(influenceBindMatrixHandle, &status);
            if(status != MS::kSuccess) return status;
            
            // This is the inverse matrix.  We want the forwards matrix, so un-invert it.
            jointWorldMat = influenceBindMatrix.inverse();
        }

        // Make space for the item, if needed.
        if(skeleton._parents.size() <= logicalIndex)
            skeleton._parents.resize(logicalIndex+1);
        if(skeleton._bones.size() <= logicalIndex)
            skeleton._bones.resize(logicalIndex+1);

        // Add the bone.
        skeleton._bones[logicalIndex] = DagHelpers::MMatrixToTransfo(jointWorldMat);

        // Read this joint's parent joint index.
        MDataHandle parentJointHandle = influenceJointsHandle.inputValue(&status).child(ImplicitSkinDeformer::parentJointAttr);
        if(status != MS::kSuccess) return status;

        int parentIdx = DagHelpers::readHandle<int>(parentJointHandle, &status);
        if(status != MS::kSuccess) return status;

        skeleton._parents[logicalIndex] = parentIdx;

        printf("Idx #%i, logical %i, parent %i\n", i, logicalIndex, parentIdx);
    }

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::save_sampleset(const SampleSet::SampleSet &samples)
{
    MStatus status = MStatus::kSuccess;

    MPlug jointArrayPlug(thisMObject(), ImplicitSkinDeformer::influenceJointsAttr);
    
    for(int i = 0; i < (int) boneSet.bones.size(); ++i)
    {
        const Bone *bone = boneSet.get_bone_by_idx(i);

        // Get the samples for this bone, or an empty SampleSet if there's no entry for it.
        bool has_samples = (samples._samples.find(bone->get_bone_id()) != samples._samples.end());
        SampleSet::InputSample emptySample;
        const SampleSet::InputSample &inputSample = has_samples? samples._samples.at(bone->get_bone_id()):emptySample;

        MPlug jointPlug = jointArrayPlug.elementByLogicalIndex(i, &status);
        if(status != MS::kSuccess) return status;

        // Save the samples.
        MPlug samplePointPlug = jointPlug.child(ImplicitSkinDeformer::samplePointAttr, &status);
        if(status != MS::kSuccess) return status;

        MPlug sampleNormalPlug = jointPlug.child(ImplicitSkinDeformer::sampleNormalAttr, &status);
        if(status != MS::kSuccess) return status;
        for(int sampleIdx = 0; sampleIdx < inputSample.nodes.size(); ++sampleIdx)
        {
            MPlug samplePlug = samplePointPlug.elementByLogicalIndex(sampleIdx, &status);
            if(status != MS::kSuccess) return status;

            status = DagHelpers::setPlugValue(samplePlug,
                inputSample.nodes[sampleIdx].x,
                inputSample.nodes[sampleIdx].y,
                inputSample.nodes[sampleIdx].z);
            if(status != MStatus::kSuccess) return status;

            MPlug normalPlug = sampleNormalPlug.elementByLogicalIndex(sampleIdx, &status);
            if(status != MS::kSuccess) return status;

            status = DagHelpers::setPlugValue(normalPlug,
                inputSample.n_nodes[sampleIdx].x,
                inputSample.n_nodes[sampleIdx].y,
                inputSample.n_nodes[sampleIdx].z);
            if(status != MStatus::kSuccess) return status;
        }
    }

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::load_sampleset(MDataBlock &dataBlock)
{
    MStatus status = MStatus::kSuccess;

    // Update Skeleton.
    dataBlock.inputValue(ImplicitSkinDeformer::skeletonUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    MArrayDataHandle influenceJointsHandle = dataBlock.inputArrayValue(ImplicitSkinDeformer::influenceJointsAttr, &status);
    if(status != MS::kSuccess) return status;

    // If the skeleton isn't attached, do nothing.
    if(skeleton.skel.get() == NULL)
        return MStatus::kSuccess;

    // Create a new SampleSet, and load its values from the node.
    SampleSet::SampleSet samples;
    /*
    if(skeleton.skel->nb_joints() != influenceJointsHandle.elementCount())
    {
        // We don't have the same number of joints loaded as we have .joints elements.  XXX: This happened
        // after creating a bad connection between skinCluster.outputGeom and unskinnedGeom
        int a = skeleton.skel->nb_joints();
        int b = influenceJointsHandle.elementCount();
        assert(0);
    }*/

    for(int i = 0; i < (int) influenceJointsHandle.elementCount(); ++i)
    {
        Bone *bone = boneSet.get_bone_by_idx(i);
        SampleSet::InputSample &inputSample = samples._samples[bone->get_bone_id()];

        status = influenceJointsHandle.jumpToElement(i);
        if(status != MS::kSuccess) return status;

        // Load the samples.
        MArrayDataHandle samplePointHandle = influenceJointsHandle.inputValue(&status).child(ImplicitSkinDeformer::samplePointAttr);
        if(status != MS::kSuccess) return status;

        MArrayDataHandle sampleNormalHandle = influenceJointsHandle.inputValue(&status).child(ImplicitSkinDeformer::sampleNormalAttr);
        if(status != MS::kSuccess) return status;

        // Load the HRBF radius.  This isn't really part of the sample set.
        {
            MDataHandle hrbfRadiusHandle = influenceJointsHandle.inputValue(&status).child(ImplicitSkinDeformer::hrbfRadiusAttr);
            if(status != MS::kSuccess) return status;
            float hrbfRadius = hrbfRadiusHandle.asFloat();
            bone->set_hrbf_radius(hrbfRadius);
        }

        if(samplePointHandle.elementCount() != sampleNormalHandle.elementCount())
            return MStatus::kFailure;

        for(int sampleIdx = 0; sampleIdx < (int) samplePointHandle.elementCount(); ++sampleIdx)
        {
            status = samplePointHandle.jumpToElement(sampleIdx);
            if(status != MS::kSuccess) return status;

            status = sampleNormalHandle.jumpToElement(sampleIdx);
            if(status != MS::kSuccess) return status;

            DagHelpers::simpleFloat3 samplePoint = DagHelpers::readArrayHandle<DagHelpers::simpleFloat3>(samplePointHandle, &status);
            if(status != MS::kSuccess) return status;

            DagHelpers::simpleFloat3 sampleNormal = DagHelpers::readArrayHandle<DagHelpers::simpleFloat3>(sampleNormalHandle, &status);
            if(status != MS::kSuccess) return status;

            inputSample.nodes.push_back(Vec3_cu(samplePoint.x, samplePoint.y, samplePoint.z));

            inputSample.n_nodes.push_back(Vec3_cu(sampleNormal.x, sampleNormal.y, sampleNormal.z));
        }
    }

    // Load the SampleSet into _anim_mesh.
    boneSet.load_sampleset(samples);
    boneSet.precompute_all_bones();
    if(skeleton.skel != NULL)
        skeleton.skel->update_bones_data();

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::load_mesh(MDataBlock &dataBlock)
{
    MStatus status = MStatus::kSuccess;

    // Always load the skeleton before the mesh.
    // XXX: This is only so Animesh can always be created, to ensure we can always load base
    // potential.  However, we don't actually need the Skeleton to store that.  The base potential
    // and other data in Animesh that has no dependency on the skeleton should be separated.
    dataBlock.inputValue(skeletonUpdateAttr, &status);
    if(status != MS::kSuccess) return status;
    if(skeleton.skel.get() == NULL)
        return MStatus::kSuccess;

    // Get input.
    MArrayDataHandle inputArray = dataBlock.inputArrayValue(input, &status);
    if(status != MS::kSuccess) return status;

    // Get input[multiIndex].
    MDataHandle inputGeomData = DagHelpers::readArrayHandleLogicalIndex<MDataHandle>(inputArray, 0, &status);
    if(status != MS::kSuccess) return status;

    // Get input[multiIndex].inputGeometry.
    MDataHandle geomHandle = inputGeomData.child(inputGeom);

    MObject geom = geomHandle.asMesh();
    if(!geom.hasFn(MFn::kMesh)) {
        // XXX: only meshes are supported
        return MStatus::kFailure;
    }

    // Hack: We calculate a bunch of properties from the mesh, such as the nearest joint to each
    // vertex.  We don't want to recalculate that every time our input (skinned) geometry changes.
    // Maya only tells us that the input data has changed, not how.  For now, if we already have
    // geometry loaded and it has the same number of vertices, assume that we already have the correct
    // mesh loaded.  This will handle the mesh being disconnected, etc.  It'll fail on the edge case
    // of switching out the geometry with another mesh that has the same number of vertices but a
    // completely different topology.  XXX
    if(mesh.get() != NULL)
    {
        int loadedVertices = mesh.get()->get_nb_vertices();
        MItMeshVertex meshIt(geom, &status);
        if(status != MS::kSuccess) return status;
        int meshVertices = meshIt.count();

        if(loadedVertices == meshVertices)
            return MStatus::kSuccess;
    }

    // Load the input mesh from the unskinned geometry.
    Loader::Abs_mesh loaderMesh;
    status = MayaData::load_mesh(geom, loaderMesh);
    if(status != MS::kSuccess) return status;

    // Abs_mesh is a simple representation that doesn't touch CUDA.  Load it into Mesh.
    Mesh *newMesh = new Mesh(loaderMesh);
    newMesh->check_integrity();

    // If we have a Animated_mesh_ctrl, delete it.
    animMesh.release();

    // Store the mesh.
    mesh.reset(newMesh);

    if(skeleton.is_loaded())
        animMesh.reset(new Animated_mesh_ctrl(mesh.get(), skeleton.skel.get()));

    // Load base potential.
    status = load_base_potential(dataBlock);
    if(status != MS::kSuccess) return status;

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::setGeometry(MDataHandle &inputGeomDataHandle, const MMatrix &objectToWorldSpace)
{
    MStatus status = MStatus::kSuccess;
    MItGeometry allGeomIter(inputGeomDataHandle, true);

    MPointArray points;
    status = allGeomIter.allPositions(points, MSpace::kObject);
    if(status != MS::kSuccess) return status;

    // If the geometry doesn't have the same number of vertices, we can't use it.  This can be
    // caused by a deformer like deleteVertices being added between us and the skinCluster, and the
    // user should bake it (delete non-deformer history).
    // XXX: Is there a way we can tell the user about this?
    // XXX: Will the algorithm allow us to support this, if we give it a whole new mesh with similar
    // topology and call update_base_potential?
    if(points.length() != animMesh->get_nb_vertices())
        return MStatus::kSuccess;

    // Set the deformed vertex data.
    vector<Vec3_cu> input_verts;
    input_verts.reserve(points.length());
    for(int i = 0; i < (int) points.length(); ++i)
    {
        MPoint point = points[i] * objectToWorldSpace;
        input_verts.push_back(Vec3_cu((float) point.x, (float) point.y, (float) point.z));
    }
        
    animMesh->copy_vertices(input_verts);

    return MStatus::kSuccess;
}

// Update the base potential for the current mesh and samples.  This requires loading the unskinned geometry.
MStatus ImplicitSkinDeformer::calculate_base_potential()
{
    MDataBlock dataBlock = this->forceCache();
    MStatus status = MStatus::kSuccess;

    // Make sure our dependencies are up to date.
    dataBlock.inputValue(ImplicitSkinDeformer::sampleSetUpdateAttr, &status);
    if(status != MS::kSuccess) return status;
    dataBlock.inputValue(ImplicitSkinDeformer::meshUpdateAttr, &status);
    if(status != MS::kSuccess) return status;
    dataBlock.inputValue(ImplicitSkinDeformer::skeletonUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    // If we don't have a mesh yet, don't do anything.
    if(animMesh.get() == NULL)
        return MStatus::kSuccess;

    // Update base potential.
    animMesh->update_base_potential();

    // Read the result.
    vector<float> pot;
    vector<Vec3_cu> grad;
    animMesh->get_base_potential(pot, grad);

    // Save the base potential to basePotentialAttr and baseGradientAttr.
    MPlug basePotentialPlug(thisMObject(), ImplicitSkinDeformer::basePotentialAttr);
    MPlug baseGradientPlug(thisMObject(), ImplicitSkinDeformer::baseGradientAttr);
    for(int i = 0; i < (int) pot.size(); ++i)
    {
        MPlug basePotentialItemPlug = basePotentialPlug.elementByLogicalIndex(i, &status);
        if(status != MS::kSuccess) return status;

        status = basePotentialItemPlug.setValue(pot[i]);
        if(status != MStatus::kSuccess) return status;

        MPlug baseGradientItemPlug = baseGradientPlug.elementByLogicalIndex(i, &status);
        if(status != MS::kSuccess) return status;

        status = DagHelpers::setPlugValue(baseGradientItemPlug, grad[i]);
        if(status != MStatus::kSuccess) return status;
    }

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::load_base_potential(MDataBlock &dataBlock)
{
    MStatus status = MStatus::kSuccess;
    MArrayDataHandle basePotentialHandle = dataBlock.inputArrayValue(ImplicitSkinDeformer::basePotentialAttr, &status);
    if(status != MS::kSuccess) return status;
    MArrayDataHandle baseGradientHandle = dataBlock.inputArrayValue(ImplicitSkinDeformer::baseGradientAttr, &status);
    if(status != MS::kSuccess) return status;

    // If we don't have the animMesh to load into yet, stop.  We'll come back here when it's
    // available due to the meshUpdateAttr and skeletonUpdateAttr dependency.
    if(animMesh.get() == NULL)
        return MS::kSuccess;

    // We can't have the animMesh but no mesh.
    assert(mesh.get() != NULL);

    vector<float> pot(basePotentialHandle.elementCount());
    vector<Vec3_cu> grad(baseGradientHandle.elementCount());

    // If the inputs have a different number of entries, we're out of sync.
    // Clear the base potential.
    if(mesh->get_nb_vertices() != basePotentialHandle.elementCount() ||
        baseGradientHandle.elementCount() != basePotentialHandle.elementCount() )
    {
        pot.clear();
        pot.resize(mesh->get_nb_vertices());
        grad.clear();
        grad.resize(mesh->get_nb_vertices());
    }
    else
    {
        for(int i = 0; i < (int) basePotentialHandle.elementCount(); ++i)
        {
            status = basePotentialHandle.jumpToElement(i);
            if(status != MS::kSuccess) return status;
            status = baseGradientHandle.jumpToElement(i);
            if(status != MS::kSuccess) return status;
        
            MDataHandle potentialItem = basePotentialHandle.inputValue(&status);
            pot[i] = potentialItem.asFloat();

            MDataHandle gradientItem = baseGradientHandle.inputValue(&status);
            const float3 &gradient = gradientItem.asFloat3();
            grad[i] = Vec3_cu(gradient[0], gradient[1], gradient[2]);
        }
    }

    // Set the base potential that we loaded.
    animMesh->set_base_potential(pot, grad);

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::load_visualization_geom_data(MDataBlock &dataBlock)
{
    MStatus status;

    previewMeshGeometry.vertices.clear();
    previewMeshGeometry.indices.clear();

    // Load dependencies:
    dataBlock.inputValue(ImplicitSkinDeformer::sampleSetUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    vector<Bone *> bones = boneSet.all_bones();
    for(int i = 0; i < bones.size(); ++i)
    {
        const Bone *bone = bones[i];
        if(bone->get_type() != EBone::Bone_t::HRBF)
            continue;
        MarchingCubes::compute_surface(previewMeshGeometry, bone);
    }

    return MStatus::kSuccess;
}



MStatus ImplicitSkinDeformer::load_visualization_geom(MDataBlock &dataBlock)
{
    MStatus status = MStatus::kSuccess;

    dataBlock.inputValue(visualizationGeomUpdateAttr, &status);
    if(status != MS::kSuccess) return status;
        
    MDataHandle fnMeshHandle = dataBlock.outputValue(visualizationGeomAttr, &status);
    if(status != MS::kSuccess) return status;

    MObject mesh = MarchingCubes::create_visualization_geom(previewMeshGeometry, &status);
    if(status != MS::kSuccess) return status;
    fnMeshHandle.set(mesh);

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::get_default_hrbf_radius(std::map<Bone::Id,float> &hrbf_radius)
{
    MStatus status = MStatus::kSuccess;

    // If the mesh or skeleton aren't up to date, update them.
    MDataBlock dataBlock = forceCache();
    dataBlock.inputValue(ImplicitSkinDeformer::skeletonUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    dataBlock.inputValue(ImplicitSkinDeformer::meshUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    assert(mesh.get() != NULL);
    assert(skeleton.skel.get() != NULL);

    VertToBoneInfo vertToBoneInfo(skeleton.skel.get(), mesh.get());
    vertToBoneInfo.get_default_hrbf_radius(skeleton.skel.get(), mesh.get(), hrbf_radius);
    return MStatus::kSuccess;
}

class ImplicitCommand : public MPxCommand
{
public:
    virtual ~ImplicitCommand() { }
    MStatus doIt( const MArgList& );
//    MStatus redoIt();
//    MStatus undoIt();

    MStatus init(MString nodeName);
    MStatus sampleAll(MString nodeName); // runs all sampling

    ImplicitSkinDeformer *getDeformerByName(MString nodeName, MStatus *status);

    bool isUndoable() const { return false; }
    static void *creator() { return new ImplicitCommand(); }
    MStatus test(MString nodeName);

private:
    MStatus getOnePlugByName(MString nodeName, MPlug &plug);
};

MStatus ImplicitCommand::getOnePlugByName(MString nodeName, MPlug &plug)
{
    MSelectionList slist;
    MStatus status = slist.add(nodeName);
    if(status != MS::kSuccess) return status;

    int matches = slist.length(&status);
    if(status != MS::kSuccess) return status;

    if(matches > 1)
    {
        displayError("Multiple nodes found: " + nodeName);
        return MS::kFailure;
    }

    MPlug implicitPlug;
    return slist.getPlug(0, plug);
}

MStatus ImplicitSkinDeformer::sample_all_joints()
{
    MStatus status = MStatus::kSuccess;

    // Force skeletonUpdateAttr to be updated.
    loadDependency(thisMObject(), ImplicitSkinDeformer::skeletonUpdateAttr, &status);
    loadDependency(thisMObject(), ImplicitSkinDeformer::meshUpdateAttr, &status);
    if(status != MS::kSuccess) return status;

    // If we don't have a mesh yet, don't do anything.
    if(animMesh.get() == NULL)
        return MStatus::kSuccess;

    // Run the initial sampling.  Skip bone 0, which is a dummy parent bone.
    SampleSet::SampleSet samples;

    VertToBoneInfo vertToBoneInfo(skeleton.skel.get(), mesh.get());
    
    SampleSet::SampleSetSettings sampleSettings;
    // Get the default junction radius.
    vertToBoneInfo.get_default_junction_radius(skeleton.skel.get(), mesh.get(), sampleSettings.junction_radius);

    for(Bone::Id bone_id: skeleton.skel->get_bone_ids())
        samples.choose_hrbf_samples(mesh.get(), skeleton.skel.get(), vertToBoneInfo, sampleSettings, bone_id);

    // Save the new SampleSet.
    status = save_sampleset(samples);
    if(status != MS::kSuccess) return status;

    // XXX don't do this here
    status = calculate_base_potential();
    if(status != MS::kSuccess) return status;

    return MStatus::kSuccess;
}

ImplicitSkinDeformer *ImplicitSkinDeformer::deformerFromPlug(MObject node, MStatus *status)
{
    MFnDependencyNode plugDep(node, status);
    if(*status != MS::kSuccess) return NULL;

    // Verify that this is one of our nodes.
    MTypeId type = plugDep.typeId(status);
    if(*status != MS::kSuccess) return NULL;

    if(type != ImplicitSkinDeformer::id)
    {
        *status = MStatus::kFailure;
        status->perror("Node not an implicitDeformer");
        return NULL;
    }

    ImplicitSkinDeformer *deformer = (ImplicitSkinDeformer *) plugDep.userNode(status);
    if(*status != MS::kSuccess) return NULL;
    return deformer;
}

ImplicitSkinDeformer *ImplicitCommand::getDeformerByName(MString nodeName, MStatus *status)
{
    // Get the MPlug for the selected node.
    MPlug implicitPlug;
    *status = getOnePlugByName(nodeName, implicitPlug);
    if(*status != MS::kSuccess) return NULL;

    return ImplicitSkinDeformer::deformerFromPlug(implicitPlug.node(), status);
}

MStatus ImplicitCommand::init(MString nodeName)
{
    MStatus status;
    ImplicitSkinDeformer *deformer = getDeformerByName(nodeName, &status);
    if(status != MS::kSuccess) return status;

    // Find the skinCluster deformer node above the deformer.
    MObject skinClusterNode;
    status = DagHelpers::findAncestorDeformer(deformer->thisMObject(), MFn::kSkinClusterFilter, skinClusterNode);
    if(status != MS::kSuccess)
    {
        printf("Couldn't find a skinCluster deformer.  Is the node skinned?\n");
        return status;
    }

    MFnDependencyNode skinClusterDep(skinClusterNode, &status);
    if(status != MS::kSuccess) return status;

    // For each influence going into the skinCluster's .matrix array, connect it to our .matrix array
    // as well.
    MPlug jointArrayPlug(deformer->thisMObject(), ImplicitSkinDeformer::influenceJointsAttr);

    {
        MPlug skinClusterMatrixArray = skinClusterDep.findPlug("matrix", &status);
        if(status != MS::kSuccess) return status;

        skinClusterMatrixArray.evaluateNumElements(&status);
        if(status != MS::kSuccess) return status;

        MDGModifier dgModifier;

        for(int i = 0; i < (int) skinClusterMatrixArray.numElements(); ++i)
        {
            MPlug skinClusterMatrixElementPlug = skinClusterMatrixArray.elementByPhysicalIndex(i, &status);
            if(status != MS::kSuccess) return status;

            // XXX: test this if a skinCluster has deleted influences
            MPlugArray plugArray;
            skinClusterMatrixElementPlug.connectedTo(plugArray, true /* asDst */, false /* asSrc */, &status);
            if(status != MS::kSuccess) return status;

            if(plugArray.length() == 0)
                continue;

            // The joint's worldMatrix plug, which is connected to the skinCluster's matrix[n] plug.
            MPlug connectionPlug = plugArray[0];


            // Get the logical index on the skinCluster.matrix array, which we'll mirror.
            int elementLogicalIndex = skinClusterMatrixElementPlug.logicalIndex(&status);
            if(status != MS::kSuccess) return status;

            MPlug jointPlug = jointArrayPlug.elementByLogicalIndex(elementLogicalIndex, &status);
            if(status != MS::kSuccess) return status;

            MPlug matrixElementPlug = jointPlug.child(ImplicitSkinDeformer::influenceMatrixAttr, &status);
            if(status != MS::kSuccess) return status;

            status = dgModifier.connect(connectionPlug, matrixElementPlug);
            if(status != MS::kSuccess) return status;
        }

        dgModifier.doIt();
    }

    {
        vector<int> parentIndexes;
        status = MayaData::loadSkeletonHierarchyFromSkinCluster(skinClusterNode, parentIndexes);
        if(status != MS::kSuccess) return status;

        // Copy bindPreMatrix from the skinCluster to influenceBindMatrix.  This stores the transform for
        // each influence at the time setup was done.
        MPlug bindPreMatrixArray = skinClusterDep.findPlug("bindPreMatrix", &status);
        if(status != MS::kSuccess) return status;

        bindPreMatrixArray.evaluateNumElements(&status);
        if(status != MS::kSuccess) return status;

        for(int i = 0; i < (int) bindPreMatrixArray.numElements(); ++i)
        {
            MPlug bindPreMatrix = bindPreMatrixArray.elementByPhysicalIndex(i, &status);
            if(status != MS::kSuccess) return status;

            int elementLogicalIndex = bindPreMatrix.logicalIndex(&status);
            if(status != MS::kSuccess) return status;

            MPlug jointPlug = jointArrayPlug.elementByLogicalIndex(elementLogicalIndex, &status);
            if(status != MS::kSuccess) return status;

            MPlug item = jointPlug.child(ImplicitSkinDeformer::influenceBindMatrixAttr, &status);
            if(status != MS::kSuccess) return status;

            MMatrix bindPreMatrixWorldSpace = DagHelpers::getMatrixFromPlug(bindPreMatrix, &status);
            if(status != MS::kSuccess) return status;

            status = DagHelpers::setPlug(item, bindPreMatrixWorldSpace);
            if(status != MS::kSuccess) return status;

            item = jointPlug.child(ImplicitSkinDeformer::parentJointAttr, &status);
            if(status != MS::kSuccess) return status;

            int parentId = elementLogicalIndex < parentIndexes.size()? parentIndexes[elementLogicalIndex]:-1;
            status = item.setValue(parentId);
            if(status != MS::kSuccess) return status;
        }
    }

    // Store the default HRBF radius for the bones we set up.
    {
        std::map<Bone::Id,float> hrbf_radius;
        status = deformer->get_default_hrbf_radius(hrbf_radius);
        if(status != MS::kSuccess) return status;

        for(int i = 0; i < (int) deformer->boneSet.bones.size(); ++i)
        {
            MPlug jointPlug = jointArrayPlug.elementByLogicalIndex(i, &status);
            if(status != MS::kSuccess) return status;

            MPlug item = jointPlug.child(ImplicitSkinDeformer::hrbfRadiusAttr, &status);
            if(status != MS::kSuccess) return status;

            const Bone *bone = deformer->boneSet.get_bone_by_idx(i);
            status = item.setValue(hrbf_radius[bone->get_bone_id()]);
            if(status != MS::kSuccess) return status;
        }
    }

    return MStatus::kSuccess;
}

MStatus ImplicitCommand::sampleAll(MString nodeName)
{
    MStatus status;
    ImplicitSkinDeformer *deformer = getDeformerByName(nodeName, &status);
    if(status != MS::kSuccess) return status;

    return deformer->sample_all_joints();
}

MStatus ImplicitCommand::test(MString nodeName)
{
    MStatus status;
    ImplicitSkinDeformer *deformer = getDeformerByName(nodeName, &status);
    if(status != MS::kSuccess) return status;

    deformer->test();

    return MS::kSuccess;
}

MStatus ImplicitCommand::doIt(const MArgList &args)
{
    MStatus status;
    for(int i = 0; i < (int) args.length(); ++i)
    {
        if(args.asString(i, &status) == MString("-init") && MS::kSuccess == status)
        {
            ++i;
            MString nodeName = args.asString(i, &status);
            if(status != MS::kSuccess) return status;

            status = init(nodeName);

            if(status != MS::kSuccess) {
                displayError(status.errorString());
                return status;
            }
        }
        else if(args.asString(i, &status) == MString("-sampleAll") && MS::kSuccess == status)
        {
            ++i;
            MString nodeName = args.asString(i, &status);
            if(status != MS::kSuccess) return status;

            status = sampleAll(nodeName);

            if(status != MS::kSuccess) {
                displayError(status.errorString());
                return status;
            }
        }
        else if(args.asString(i, &status) == MString("-test") && MS::kSuccess == status)
        {
            ++i;
            MString nodeName = args.asString(i, &status);
            if(status != MS::kSuccess) return status;

            status = test(nodeName);

            if(status != MS::kSuccess) {
                displayError(status.errorString());
                return status;
            }
        }
    }
    return MS::kSuccess;
}

MStatus initializePlugin(MObject obj)
{
    MStatus status;

    std::vector<Blending_env::Op_t> op;
    op.push_back( Blending_env::B_D  );
    op.push_back( Blending_env::U_OH );
    op.push_back( Blending_env::C_D  );

    Cuda_ctrl::cuda_start(op);

    // XXX "HACK: Because blending_env initialize to elbow too ..." What?
    IBL::Ctrl_setup shape = IBL::Shape::elbow();

    MFnPlugin plugin(obj, "", "1.0", "Any");

    status = plugin.registerNode("implicitSkin", ImplicitSkinDeformer::id, ImplicitSkinDeformer::creator, ImplicitSkinDeformer::initialize, MPxNode::kDeformerNode);
    if(status != MS::kSuccess) return status;

    status = plugin.registerCommand("implicitSkin", ImplicitCommand::creator);
    if(status != MS::kSuccess) return status;

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus status;

    Cuda_ctrl::cleanup();

    MFnPlugin plugin(obj);

    status = plugin.deregisterNode(ImplicitSkinDeformer::id);
    if(status != MS::kSuccess) return status;

    status = plugin.deregisterCommand("implicitSkin");
    if(status != MS::kSuccess) return status;

    return MS::kSuccess;
}
