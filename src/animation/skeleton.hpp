/*
 Implicit skinning
 Copyright (C) 2013 Rodolphe Vaillant, Loic Barthe, Florian Cannezin,
 Gael Guennebaud, Marie Paule Cani, Damien Rohmer, Brian Wyvill,
 Olivier Gourmel

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License 3 as published by
 the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>
 */
#ifndef SKELETON_HPP__
#define SKELETON_HPP__

#include <vector>

#include "bone.hpp"
#include "joint_type.hpp"
#include "transfo.hpp"
#include "blending_lib/controller.hpp"
#include "skeleton_env_type.hpp"

// Forward definitions ---------------------------------------------------------
namespace Loader {
struct Abs_skeleton;
}
// End Forward definitions  ----------------------------------------------------

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// -----------------------------------------------------------------------------

/**
 *  @class Skeleton
    @brief The class that defines an articulated skeleton.
    Skeleton is represented by a tree of bones with a single joint as root
    and others as node and leaves

    We refer to the rest position or bind pose of the skeleton
    to define the initial position (i.e the moment when the skeleton/graph is
    attached to the mesh). Transformed position of the skeleton and mesh is
    the animated position computed according to the rest/bind position.

    Each joint defines the next bone except for the leaves which are bones with
    a length equal to zero :
    @code

    joint_0  bone_0   joint_1   bone_1   joint_3
    +------------------+------------------+
                     joint_2
                        \
                         \ bone_2
                          \
                           + joint_4

    @endcode
    In this example we can see that joint_1 and joint_2 has the same position
    but not the same orientation. joint_3 and joint_4 are the leaves of the
    skeleton tree the corresponding  bone length cannot be defined.
    Note that a joint frame (fetched with 'joint_anim_frame()' for instance) is
    usually different from the bone frame ( bone_anim_frame() )

    One way to look up the skeleton's bones is to explore the bone array :
    @code
    Skeleton skel;
    ...
    for(int i = 0; i < skel.get_nb_joints(); i++){

        if( skel.is_leaf(i) )
        {
            // Do something or skip it
        }

        // Acces the ith bone :
        skel.get_bone(i)->a_bone_method();
        const Bone* b = skel.get_bones(i);

        ...
    }
    @endcode
 */
struct SkeletonJoint
{
    SkeletonJoint() { _anim_bone = NULL; _parent = -1; _hrbf_radius = 0; }

    Bone *_anim_bone;

    /// Bone in rest position.
    /// @warning a bone can have a orientation different from its associated joint
    /// Bones orientations are solely deduce from joint positions. _frame[i]
    /// is not necessarily equal to the _bones[i] frame.
    /// @see bone.hpp
    Bone_cu _bone;

    // List of children IDs for this bone.
    std::vector<Bone::Id> _children;

    // This joint's parent ID.
    Bone::Id _parent;

    /// List of transformations associated to each bone in order to deform a mesh.
    /// A point will follow rigidly the ith bone movements if it is transformed
    /// by bone_transformations[parents[ith]].
    Transfo _h_transfo;

    Skeleton_env::Joint_data _joint_data;

    /// hrbf radius to go from global to compact support
    float _hrbf_radius;

    // TODO: this might not be really needed as blending env already stores it
    /// shape of the controller associated to each joint
    /// for the gradient blending operators
    IBL::Ctrl_setup _controller;
};

struct Skeleton {
  friend struct SkeletonImpl;

  /// Build skeleton from the abstract representation
  Skeleton(const Loader::Abs_skeleton& skel);

  ~Skeleton();

  //----------------------------------------------------------------------------
  /// @name Setters
  //----------------------------------------------------------------------------

  /// Set the radius of the hrbf of bone i.
  /// The radius is used to transform hrbf from global support to
  /// compact support
  void set_bone_hrbf_radius(int i, float radius);

  void set_joint_controller(Blending_env::Ctrl_id i,
                            const IBL::Ctrl_setup& shape);

  /// @param type The joint type in the enum field in EJoint namespace
  void set_joint_blending(int i, EJoint::Joint_t type);

  /// @param m magnitude for the ith joint. range: [0 1]
  void set_joint_bulge_mag(int i, float m);

  /// Replace and delete the ith bone with the user allocated bone 'b'
  void set_bone(int i, Bone* b);

  /// Set the implicit cylinder radius of bone i
  void set_bone_radius(int i, float radius);

  //----------------------------------------------------------------------------
  /// @name Getter
  /// The difference between a joint and a bone must be clear in this section.
  /// A joint is between two bones except for the root joint. The joint frame
  /// used to compute skinning can be different from the bone frame.
  //----------------------------------------------------------------------------

  /// Get the number of joints in the skeleton
  int nb_joints() const { return (int) _joints.size(); }

  IBL::Ctrl_setup get_joint_controller(int i);

  /// Get the list of children for the ith bone
  const std::vector<int>& get_sons(int i) const { return _joints[i]._children;  }

  int parent(int i) const { return _joints[i]._parent; }

  // Return true if the joint represents a bone.
  //
  // Root joints don't create bones.
  bool is_bone(int i) const {
      return parent(i) != -1;
  }

  // If this joint represents a bone, return the transform used for the bone.  Otherwise,
  // return identity.
  //
  // This is the parent joint's transform.
  Transfo get_bone_transform(int i) const {
      if(!is_bone(i))
          return Transfo::identity();

      const SkeletonJoint &joint = _joints[i];
      return joint._parent == -1? Transfo::identity():_joints[joint._parent]._h_transfo;
  }

  bool is_leaf(int i) const { return _joints[i]._children.size() == 0; }

  /// Get the animated bones of the skeleton
  std::vector<Bone*> get_bones() const {
      std::vector<Bone*> bones(_joints.size());
      for(int i = 0; i < (int) _joints.size(); ++i)
          bones[i] = _joints[i]._anim_bone;
      return bones;
  }

  /// @note A bone is a part of the skeleton, as such you cannot change its
  /// properties outside the skeleton class. Changes are to be made with the
  /// dedicated setter 'set_bone()' by creating a new bone. The setter will
  /// ensure that the skeleton updates its state according to the bone properties.
  /// Do not even think about using the dreadfull const_cast<>(). <b> You've been
  /// warned. <\b>
  const Bone* get_bone(Bone::Id i) const{ return _joints[i]._anim_bone;  }

  Blending_env::Ctrl_id get_ctrl(int joint) const {
      return _joints[joint]._joint_data._ctrl_id;
  }

  float get_joints_bulge_magnitude(Bone::Id i) const {
      return _joints[i]._joint_data._bulge_strength;
  }

  Skeleton_env::DBone_id get_bone_didx(Bone::Id i) const;

  /// bone type (whether a primitive is attached to it)
  /// @see Bone Bone_hrbf Bone_ssd Bone_cylinder Bone_precomputed EBone
  EBone::Bone_t bone_type(Bone::Id id_bone) const {
      return _joints[id_bone]._anim_bone->get_type();
  }

  /// @return The joint type in the enum field in EJoint namespace
  EJoint::Joint_t joint_blending(Bone::Id i) const {
      return _joints[i]._joint_data._blend_type;
  }

  /// Get the animated joints global transformations expressed with matrices.
  /// These transformation can be used as is to deformed the mesh
  const Transfo& get_transfo(Bone::Id bone_id) const;

  /// @return the hrbf id associated to the bone or -1
  /// if the bone is not an HRBF
  int get_hrbf_id(Bone::Id bone_id) const;

  float get_hrbf_radius(Bone::Id bone_id) const;

  /// Get the id of the skeleton in the skeleton environment
  Skeleton_env::Skel_id get_skel_id() const { return _skel_id; }

  void set_transforms(const std::vector<Transfo> &transfos);

  /// Given a set of global transformation at each joints animate the skeleton.
  /// animated bones frames dual quaternions are updated as well as device
  /// memory
  void update_bones_pose();

private:

  /// Create and initilize a skeleton in the environment Skeleton_env
  void init_skel_env();

  /// updates 'hrbf_id_to_bone_id' attributes according to the bone array
  // TODO: to be deleted
//  void update_hrbf_id_to_bone_id();

  /// transform implicit surfaces pre computed in 3D grids
  /// @param global_transfos array of global transformations for each bone
  void transform_precomputed_prim();

  std::vector<Skeleton_env::Joint_data> get_joints_data() const;

  //----------------------------------------------------------------------------
  /// @name Attributes
  //----------------------------------------------------------------------------

  /// Id of the skeleton in the skeleton environment
  Skeleton_env::Skel_id _skel_id;

  std::vector<SkeletonJoint> _joints;

  /// hrbf_id_to_bone_id[hrbf_id] = bone_id
  // TODO: to be deleted
  //std::vector<int> _hrbf_id_to_bone_id;
};

#endif // SKELETON_HPP__
