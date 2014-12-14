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
#include "fbx_loader.hpp"

// -----------------------------------------------------------------------------

#include "fbx_utils.hpp"
#include "std_utils.hpp"

// -----------------------------------------------------------------------------

#include <stdio.h>
#include <iostream>
#include <cassert>


// =============================================================================
namespace Fbx_loader {
// =============================================================================

/// global variables for the FbxSdk resources manager
KFbxSdkManager* g_FBXSdkManager = 0;

// -----------------------------------------------------------------------------

void init()
{
    // init fbx
    g_FBXSdkManager = KFbxSdkManager::Create();

    // create an IOSettings object
    KFbxIOSettings * ios = KFbxIOSettings::Create(g_FBXSdkManager, IOSROOT );
    g_FBXSdkManager->SetIOSettings(ios);

    // Load plugins from the executable directory
    KString lPath = KFbxGetApplicationDirectory();
#if defined(KARCH_ENV_WIN)
    KString lExtension = "dll";
#elif defined(KARCH_ENV_MACOSX)
    KString lExtension = "dylib";
#elif defined(KARCH_ENV_LINUX)
    KString lExtension = "so";
#endif
    g_FBXSdkManager->LoadPluginsDirectory(lPath.Buffer(), lExtension.Buffer());
}

// -----------------------------------------------------------------------------

void clean()
{
    if ( g_FBXSdkManager != 0 ) g_FBXSdkManager->Destroy();
    g_FBXSdkManager = 0;
}


//// Mesh import utilities =====================================================

void importNormals_byControlPoint( KFbxMesh* fbx_mesh,
                                   KFbxGeometryElementNormal* elt_normal,
                                   Loader::Abs_mesh& mesh,
                                   std::map< std::pair<int, int>, int>& idx_normals,
                                   int v_size)
{
    KFbxVector4 n;
    int n_size = mesh._normals.size();
    mesh._normals.resize( n_size + elt_normal->GetDirectArray().GetCount() );
    if( elt_normal->GetReferenceMode() == KFbxGeometryElement::eDIRECT )
    {
        for(int i = 0; i < fbx_mesh->GetControlPointsCount(); ++i)
        {
            n = elt_normal->GetDirectArray().GetAt(i);
            mesh._normals[ n_size + i ] = Fbx_utils::to_lnormal( n );
            idx_normals[ std::pair<int, int>( v_size + i, -1 ) ] = n_size + i;
        }
    }
    else
    {
        for(int i = 0; i < fbx_mesh->GetControlPointsCount(); ++i)
        {
            n = elt_normal->GetDirectArray().GetAt( elt_normal->GetIndexArray().GetAt(i) );
            mesh._normals[ n_size + i ] = Fbx_utils::to_lnormal( n );
            idx_normals[ std::pair<int, int>( v_size + i, -1 ) ] = n_size + i;
        }
    }
}

// -----------------------------------------------------------------------------

void importNormals_byPolygonVertex( KFbxMesh* fbx_mesh,
                                    KFbxGeometryElementNormal* elt_normal,
                                    Loader::Abs_mesh& mesh,
                                    std::map< std::pair<int, int>, int>& idx_normals,
                                    int v_size )
{
    KFbxVector4 n;
    // map of indices of normals, in order to quickly know if already seen
    std::map<int, int> seenNormals;
    std::map<int, int>::iterator it;
    int lIndexByPolygonVertex = 0;

    // Lookup polygons
    const int nb_polygons = fbx_mesh->GetPolygonCount();
    for(int p = 0; p < nb_polygons; p++)
    {
        // Lookup polygon vertices
        int lPolygonSize = fbx_mesh->GetPolygonSize(p);
        for(int i = 0; i < lPolygonSize; i++)
        {
            int lNormalIndex = 0;
            if( elt_normal->GetReferenceMode() == KFbxGeometryElement::eDIRECT )
                lNormalIndex = lIndexByPolygonVertex;
            if(elt_normal->GetReferenceMode() == KFbxGeometryElement::eINDEX_TO_DIRECT)
                lNormalIndex = elt_normal->GetIndexArray().GetAt(lIndexByPolygonVertex);
            // record the normal if not already seen
            it = seenNormals.find(lNormalIndex);
            if( it == seenNormals.end() )
            {
                n = elt_normal->GetDirectArray().GetAt(lNormalIndex);
                mesh._normals.push_back( Fbx_utils::to_lnormal(n) );
                seenNormals[lNormalIndex] = mesh._normals.size()-1;
                // record vertice to normal mapping
                idx_normals[ std::pair<int, int>( v_size + fbx_mesh->GetPolygonVertex(p, i), p ) ] = mesh._normals.size()-1;
            }else
                // record vertice to normal mapping
                idx_normals[ std::pair<int, int>( v_size + fbx_mesh->GetPolygonVertex(p, i), p ) ] = it->second;

            lIndexByPolygonVertex++;
        }
    }
}

// -----------------------------------------------------------------------------

void importTexCoords_byControlPoint( KFbxMesh* lMesh,
                                     KFbxGeometryElementUV* elt_UV,
                                     Loader::Abs_mesh& mesh,
                                     std::map< std::pair<int, int>, int>& idx_UV,
                                     int v_size )
{
    KFbxVector2 uv;
    int nb_uv = mesh._texCoords.size();
    mesh._texCoords.resize( nb_uv + elt_UV->GetDirectArray().GetCount() );
    if (elt_UV->GetReferenceMode() == KFbxGeometryElement::eDIRECT) {
        for (int i=0; i<lMesh->GetControlPointsCount(); ++i){
            uv = elt_UV->GetDirectArray().GetAt(i);
            mesh._texCoords[ nb_uv + i ] = Fbx_utils::to_ltexcoord( uv );
            idx_UV[ std::pair<int, int>( v_size + i, -1 ) ] = nb_uv + i;
        }
    } else {
        for (int i=0; i<lMesh->GetControlPointsCount(); ++i){
            uv = elt_UV->GetDirectArray().GetAt( elt_UV->GetIndexArray().GetAt(i) );
            mesh._texCoords[ nb_uv + i ] = Fbx_utils::to_ltexcoord( uv );
            idx_UV[ std::pair<int, int>( v_size + i, -1 ) ] = nb_uv + i;
        }
    }
}

// -----------------------------------------------------------------------------

void importTexCoords_byPolygonVertex( KFbxMesh* lMesh,
                                      KFbxGeometryElementUV* elt_UV,
                                      Loader::Abs_mesh& mesh,
                                      std::map< std::pair<int, int>, int>& idx_UV,
                                      int v_size )
{
    KFbxVector2 uv;
    // map of indices of normals, in order to quickly know if already seen
    std::map<int, int> seenCoords;
    std::map<int, int>::iterator it;
    int lIndexByPolygonVertex = 0;
    for(int p = 0; p < lMesh->GetPolygonCount(); p++)
    {
        int lPolygonSize = lMesh->GetPolygonSize(p);
        for(int i = 0; i < lPolygonSize; i++)
        {
            int lTexCoordIndex = 0;
            if( elt_UV->GetReferenceMode() == KFbxGeometryElement::eDIRECT )
                lTexCoordIndex = lIndexByPolygonVertex;
            if(elt_UV->GetReferenceMode() == KFbxGeometryElement::eINDEX_TO_DIRECT)
                lTexCoordIndex = elt_UV->GetIndexArray().GetAt(lIndexByPolygonVertex);

            // record the normal if not already seen
            it = seenCoords.find(lTexCoordIndex);
            if( it == seenCoords.end() )
            {
                uv = elt_UV->GetDirectArray().GetAt(lTexCoordIndex);
                mesh._texCoords.push_back ( Fbx_utils::to_ltexcoord( uv ) );
                seenCoords[lTexCoordIndex] = mesh._texCoords.size()-1;
                // record vertice to normal mapping
                idx_UV[ std::pair<int, int>( v_size + lMesh->GetPolygonVertex(p, i), p ) ] = mesh._texCoords.size()-1;
            }else
                idx_UV[ std::pair<int, int>( v_size + lMesh->GetPolygonVertex(p, i), p ) ] = it->second;

            lIndexByPolygonVertex++;
        }
    }
}

// -----------------------------------------------------------------------------

void fill_material(KFbxNode* node, Loader::Abs_mesh& mesh)
{
    for (int i = 0; i < node->GetMaterialCount(); ++i )
    {
        Loader::Material m;
        // get material infos
        KFbxSurfaceMaterial* fbx_mat = node->GetMaterial(i);

        const KFbxImplementation* implem = GetImplementation(fbx_mat, ImplementationHLSL);
        if( implem == 0 )         implem = GetImplementation(fbx_mat, ImplementationCGFX);

        if(implem != 0)
            std::cerr << "Material implementation not supported : CGFX/HLSL.\n";
        else if(fbx_mat->GetClassId().Is(KFbxSurfacePhong::ClassId))
            Fbx_utils::copy(m, (KFbxSurfacePhong*)fbx_mat);
        else if(fbx_mat->GetClassId().Is(KFbxSurfaceLambert::ClassId) )
            Fbx_utils::copy(m, (KFbxSurfaceLambert*)fbx_mat);
        else
        {
            std::cerr << "ERROR FBX : unhandle material type we won't load it";
            std::cerr << std::endl;
        }

        // add material
        if(implem == 0) mesh._materials.push_back ( m );

    }// for materialCount
}


// -----------------------------------------------------------------------------

void fill_mesh(KFbxMesh* fbx_mesh, KFbxNode* node, Loader::Abs_mesh& mesh)
{
    // deal with non triangular mesh
    if ( !fbx_mesh->IsTriangleMesh() )
    {
        KFbxGeometryConverter triangulator(g_FBXSdkManager);
        fbx_mesh = triangulator.TriangulateMesh(fbx_mesh);
    }

    // vertices ################################################################
    KFbxVector4* v = fbx_mesh->GetControlPoints();
    int nb_verts = fbx_mesh->GetControlPointsCount();
    int v_size = mesh._vertices.size();
    mesh._vertices.resize(v_size + nb_verts);
    for (int i = 0; i < nb_verts; ++i)
        mesh._vertices[ v_size+i ] = Fbx_utils::to_lvertex( v[i] );


    // normals #################################################################

    // map the indice of face and vertice to indice of normal
    std::map< std::pair<int, int>, int> idx_normals;
    KFbxGeometryElementNormal* elt_normal;
    KFbxGeometryElement::EMappingMode type;
    bool isNormalByControlPoint = true;

    int nb_elt_normal = fbx_mesh->GetElementNormalCount();

    if( nb_elt_normal > 1){
        std::cerr << "WARNING FBX : there is more than one layer for normals";
        std::cerr << "We only handle the first layer" << std::endl;
    }

    if( nb_elt_normal > 0)
    {
        // Fetch first element
        elt_normal = fbx_mesh->GetElementNormal();
        type = elt_normal->GetMappingMode();

        if( type == KFbxGeometryElement::eBY_CONTROL_POINT )
        {
            importNormals_byControlPoint( fbx_mesh, elt_normal, mesh, idx_normals, v_size );
        }
        else if( type == KFbxGeometryElement::eBY_POLYGON_VERTEX )
        {
            isNormalByControlPoint = false;
            importNormals_byPolygonVertex( fbx_mesh, elt_normal, mesh, idx_normals, v_size );
        }
        else
        {
            std::cerr << "ERROR FBX: mapping mode'" << Fbx_utils::to_string(type);
            std::cerr << "'for normals is not handled" << std::endl;
        }
    }

    // texCoords ###############################################################
    // map the indice of face and vertice to indice of normal
    std::map< std::pair<int, int>, int> idx_uv;
    KFbxGeometryElementUV* elt_uv;
    bool isUVByControlPoint = true;

    int nb_elt_uv = fbx_mesh->GetElementUVCount();

    if( nb_elt_uv > 1){
        std::cerr << "WARNING FBX : there is more than one layer for texture coordinates";
        std::cerr << "We only handle the first layer" << std::endl;
    }

    if( nb_elt_uv > 0 )
    {
        // Fetch first element
        elt_uv = fbx_mesh->GetElementUV();
        type = elt_uv->GetMappingMode();

        if( type == KFbxGeometryElement::eBY_CONTROL_POINT )
        {
            importTexCoords_byControlPoint( fbx_mesh, elt_uv, mesh, idx_uv, v_size );
        }
        else if( type == KFbxGeometryElement::eBY_POLYGON_VERTEX )
        {
            isUVByControlPoint = false;
            importTexCoords_byPolygonVertex( fbx_mesh, elt_uv, mesh, idx_uv, v_size );
        }
        else
        {
            std::cerr << "ERROR FBX: mapping mode'" << Fbx_utils::to_string(type);
            std::cerr << "'for tex coords is not handled" << std::endl;
        }
    }

    // triangles ###############################################################
    int f_size = mesh._triangles.size();
    mesh._triangles.resize( f_size + fbx_mesh->GetPolygonCount() );
    for (int faceIndex=0; faceIndex<fbx_mesh->GetPolygonCount(); ++faceIndex){
        Loader::Tri_face f;
        for (int verticeIndex = 0; verticeIndex <fbx_mesh->GetPolygonSize(faceIndex); ++verticeIndex){// here size = 3
            // register the vertice indice
            f.v[verticeIndex] = v_size + fbx_mesh->GetPolygonVertex( faceIndex, verticeIndex );
            // register the vertice's normal indice
            if (fbx_mesh->GetElementNormalCount() )
                f.n[verticeIndex] = idx_normals[ std::pair<int, int>(f.v[verticeIndex], isNormalByControlPoint?-1:faceIndex) ];
            // register the vertice's texcoords indice
            if ( fbx_mesh->GetElementUVCount() )
                f.t[verticeIndex] = idx_uv[ std::pair<int, int>(f.v[verticeIndex], isUVByControlPoint?-1:faceIndex) ];
        }
        mesh._triangles[ f_size + faceIndex] = f;
    }


    // materials ###############################################################
    int m_size = mesh._materials.size();
    fill_material(node, mesh);

    // material groups & groups ################################################
    Loader::Group g;
    g._start_face = f_size;
    g._end_face = mesh._triangles.size();
    int nb_elementMaterial = fbx_mesh->GetElementMaterialCount();
    KFbxGeometryElementMaterial* lMaterialElement;
    if (nb_elementMaterial == 1) {
        lMaterialElement = fbx_mesh->GetElementMaterial();
        if( lMaterialElement->GetMappingMode() == KFbxGeometryElement::eALL_SAME) {
            Loader::MaterialGroup mg;
            mg._material_idx = m_size + lMaterialElement->GetIndexArray().GetAt(0);
            mg._start_face = f_size;
            mg._end_face = mesh._triangles.size();
            g._assigned_mats.push_back ( mg );
        } else {
            for (int i=0; i<fbx_mesh->GetPolygonCount(); ++i){
                // register all material groups
                Loader::MaterialGroup mg;
                mg._material_idx = m_size + lMaterialElement->GetIndexArray().GetAt(i);
                mg._start_face = f_size + i;
                for (; i<fbx_mesh->GetPolygonCount() && (m_size + lMaterialElement->GetIndexArray().GetAt(i) == (int)mg._material_idx); ++i);
                mg._end_face = f_size + i;
                g._assigned_mats.push_back ( mg );
                --i;
            }
        }
        mesh._groups.push_back ( g );
    } else if (nb_elementMaterial>1) {
        int i=0;
        for (int i=0; i<nb_elementMaterial; ++i){
            if ( fbx_mesh->GetElementMaterial( i )->GetMappingMode() == KFbxGeometryElement::eBY_POLYGON )
                break;
        }
        if (i>=nb_elementMaterial){
            lMaterialElement = fbx_mesh->GetElementMaterial();
            Loader::MaterialGroup mg;
            mg._material_idx = m_size + lMaterialElement->GetIndexArray().GetAt(0);
            mg._start_face = f_size;
            mg._end_face = mesh._triangles.size();
            g._assigned_mats.push_back ( mg );
        } else {
            lMaterialElement = fbx_mesh->GetElementMaterial( i );
            for (int j=0; j<fbx_mesh->GetPolygonCount(); ++j){
                // register all material groups
                Loader::MaterialGroup mg;
                mg._material_idx = m_size + lMaterialElement->GetIndexArray().GetAt(j);
                mg._start_face = f_size + j;
                for (; j<fbx_mesh->GetPolygonCount() && (m_size + lMaterialElement->GetIndexArray().GetAt(j) == (int)mg._material_idx); ++j);
                mg._end_face = f_size + j;
                g._assigned_mats.push_back ( mg );
                --j;
            }
        }
        mesh._groups.push_back ( g );

    }
    /////////////////////////////////////////////
}

//------------------------------------------------------------------------------

void Fbx_file::compute_size_mesh()
{
    _offset_verts.clear();
    int acc = 0;
    for(int i = 0; i < _fbx_scene->GetNodeCount(); i++)
    {
        KFbxNode*          node = _fbx_scene->GetNode(i);
        KFbxNodeAttribute* attr = node->GetNodeAttribute();

        if(attr != 0 && attr->GetAttributeType() == KFbxNodeAttribute::eMESH)
        {
            _offset_verts[node] = acc;
            acc += ((KFbxMesh*)attr)->GetControlPointsCount();
        }
    }
    _size_mesh = acc;
}

//------------------------------------------------------------------------------

bool Fbx_file::load_file(const std::string& filename)
{
    assert(g_FBXSdkManager != 0);
    Base_loader::load_file(filename);
    free_mem();
    // Create the entity that will hold the scene.
    _fbx_scene = KFbxScene::Create(g_FBXSdkManager,"");

    bool state = Fbx_utils::load_scene(filename, _fbx_scene, g_FBXSdkManager);
    if(state) compute_size_mesh();
    return state;
}

// -----------------------------------------------------------------------------

void Fbx_file::get_mesh(Loader::Abs_mesh& mesh)
{
    _offset_verts.clear();
    // transform into intermediary structure
    for(int i = 0; i < _fbx_scene->GetNodeCount(); i++)
    {
        KFbxNode* node = _fbx_scene->GetNode(i);

        // upgrade structure from node content
        KFbxNodeAttribute* attr = node->GetNodeAttribute();
        if(attr != 0)
        {
            switch (attr->GetAttributeType())
            {
            // TODO: extend NodeAttributes handling
            case KFbxNodeAttribute::eSKELETON: break;
            case KFbxNodeAttribute::eMESH:
                _offset_verts[node] = mesh._vertices.size();
                fill_mesh( (KFbxMesh*)attr, node, mesh);
                break;
            default:
                break;
            }
        }
        //... TODO: deal with transforms, properties ...
    }

    _size_mesh = mesh._vertices.size();

    for(unsigned i = 0; i < mesh._materials.size(); i++)
        mesh._materials[i].set_relative_paths(_path);
 }

// -----------------------------------------------------------------------------


/// Fill skell with the FBx skeleton hierachy. Only links between parents and
/// nodes are stored.
static int fill_skeleton(Loader::Abs_skeleton& skel,
                         std::map<KFbxNode*, int>& ptr_to_idx,
                         int bone_parent,
                         KFbxNode* node)
{
    if(bone_parent < 0) skel._root = 0; // First node in srd::vector is root

    const KFbxNodeAttribute* attr = node->GetNodeAttribute();
    assert( attr->GetAttributeType() == KFbxNodeAttribute::eSKELETON ||
            attr->GetAttributeType() == KFbxNodeAttribute::eNULL );

    const KFbxSkeleton* skel_attr = (const KFbxSkeleton*)attr;
    std::string name( skel_attr->GetNameOnly().Buffer() );

    //KFbxAnimEvaluator::GetNodeGlobalTransformFast()
    //Transfo tr = Fbx_utils::to_transfo( Fbx_utils::geometry_transfo(node) );
    Transfo tr = Fbx_utils::to_transfo( node->EvaluateGlobalTransform() );
    Loader::Abs_bone bone = {0.f, tr, name };
    skel._bones.  push_back( bone               );
    skel._parents.push_back( bone_parent        );
    skel._sons.   push_back( std::vector<int>() );
    int bone_idx = skel._bones.size() - 1;
    ptr_to_idx[node] = bone_idx;

    unsigned nb_children = node->GetChildCount();
    std::vector<int> sons(nb_children);
    for(unsigned c = 0; c < nb_children; c++)
    {
        KFbxNode* child = node->GetChild(c);
        // Recursive lookup
        int cidx = fill_skeleton(skel, ptr_to_idx, bone_idx, child);
        sons[c] = cidx;
    }
    skel._sons [bone_idx] = sons;
    return bone_idx;
}

// -----------------------------------------------------------------------------

typedef std::map<KFbxNode*, int> Node_map;

void set_skel_frame(Loader::Abs_skeleton& skel,
                    KFbxNode* node,
                    const std::map<KFbxNode*, int>& ptr_to_idx,
                    const KFbxMatrix& mat)
{
    const Transfo tr = Fbx_utils::to_transfo( mat );
    Node_map::const_iterator it = ptr_to_idx.find( node );
    if( it != ptr_to_idx.end() )
        skel._bones[ it->second ]._frame = tr;
    else
    {
        std::cerr << "WARNING FBX: unkonwn node reference in bones list '";
        std::cerr << node->GetName() << "'" << std::endl;
    }
}

// -----------------------------------------------------------------------------

/// Compute the frame of every bone from the bind pose
/// @return if we succeded to compute every frames
void compute_bones_bind_frame(Loader::Abs_skeleton& skel,
                              KFbxPose* pose,
                              const std::map<KFbxNode*, int>& ptr_to_idx)
{
    for(int i = 0; i < pose->GetCount(); i++)
    {
        KFbxNode* node = pose->GetNode(i);
        const KFbxMatrix mat = pose->GetMatrix(i);
        set_skel_frame(skel, node, ptr_to_idx, mat);
    }
}

// -----------------------------------------------------------------------------

/// @return the bind frame
KFbxXMatrix compute_cluster_bind_frame(KFbxGeometry* geom,
                                       KFbxCluster* cluster)
{
    KFbxCluster::ELinkMode clus_mode = cluster->GetLinkMode();

    if (clus_mode == KFbxCluster::eADDITIVE && cluster->GetAssociateModel())
    {
        std::cerr << "WARNING FBX: cluster is eADDITIVE we don't handle";
        std::cerr << "this type of skinning" << std::endl;
    }

    KFbxXMatrix clus_transfo;
    // TransformMatrix refers to the global initial transform of the geometry
    // node that contains the link node. (i.e global transfo of 'geom')
    cluster->GetTransformMatrix(clus_transfo);

    KFbxXMatrix geom_transfo = Fbx_utils::geometry_transfo(geom->GetNode());

    KFbxXMatrix clus_link_transfo;
    // TransformLink refers to global initial transform of the link node (
    // (i.e global transfo of the bone associated to this cluster).
    cluster->GetTransformLinkMatrix(clus_link_transfo);

    //return lClusterRelativeCurrentPositionInverse * lClusterRelativeInitPosition;
    return (clus_transfo * geom_transfo).Inverse() * clus_link_transfo;
}

// -----------------------------------------------------------------------------

void compute_bones_bind_frame(Loader::Abs_skeleton& skel,
                              std::map<KFbxNode*, KFbxNode*>& done,
                              KFbxSkin* skin,
                              KFbxGeometry* geom,
                              const std::map<KFbxNode*, int>& ptr_to_idx)
{
    done.clear();
    for(int i = 0; i < skin->GetClusterCount(); i++)
    {
        KFbxCluster* cluster = skin->GetCluster(i);
        KFbxNode*    cl_node = cluster->GetLink();

        if( cluster->GetLink() == 0) continue;

        done[ cl_node ] = cl_node;

        KFbxXMatrix mat = compute_cluster_bind_frame( geom, cluster);
        set_skel_frame(skel, cl_node, ptr_to_idx, KFbxMatrix( mat ) );
    }
}

// -----------------------------------------------------------------------------

void fill_bones_weights(Loader::Abs_skeleton& skel,
                        KFbxSkin* skin,
                        const std::map<KFbxNode*, int>& ptr_to_idx,
                        int off,
                        int size_mesh)
{
    skel._weights.resize( size_mesh );
    for(int i = 0; i < skin->GetClusterCount(); i++)
    {
        KFbxCluster* cluster = skin->GetCluster(i);
        KFbxNode*    cl_node = cluster->GetLink();

        if( cl_node == 0 ) continue;

        const int bone_id = Std_utils::find( ptr_to_idx, cl_node);

        const int size_cluster = cluster->GetControlPointIndicesCount();
        for(int c = 0; c < size_cluster; c++)
        {
            const int   fbx_idx    =        cluster->GetControlPointIndices()[c];
            const float fbx_weight = (float)cluster->GetControlPointWeights()[c];

            std::pair<int, float> p = std::make_pair( bone_id, fbx_weight );

            skel._weights[ fbx_idx + off].push_back( p );
        }
    }
}

// -----------------------------------------------------------------------------

/// Extract bind pose using KFBxPose but For some files this won't work
void fill_bind_pose(KFbxScene* scene,
                    Loader::Abs_skeleton& skel,
                    std::map<KFbxNode*, int> ptr_to_idx)
{
    int nb_poses = scene->GetPoseCount();
    int nb_bind_poses = 0;
    for(int i = 0; i < nb_poses; i++)
    {
        KFbxPose* pose = scene->GetPose(i);
        if( pose->IsBindPose() )
        {
            compute_bones_bind_frame(skel, pose, ptr_to_idx);
            nb_bind_poses++;
        }
    }

    if(nb_bind_poses > 1)
    {
        std::cerr << "WARNING FBX: there is more than one bind pose!";
        std::cerr << std::endl;
    }
    else if( nb_bind_poses == 0 )
    {
        std::cerr << "WARNING FBX: there is no bind pose!\n";
        std::cerr << "We try to compute it with LinkMatrix";

    }
}

// -----------------------------------------------------------------------------

/// compute bind pose for nodes without clusters
/// @param done: list of nodes pointer which bind pose are correct and
/// associated to a cluster
void fill_bind_pose_cluster_less_nodes(
        KFbxScene* scene,
        Loader::Abs_skeleton& skel,
        const std::map<KFbxNode*, KFbxNode*>& done,
        const std::map<KFbxNode*, int>& ptr_to_idx)
{
    for(int i = 0; i < scene->GetNodeCount(); i++)
    {
        KFbxNode* node = scene->GetNode(i);

        KFbxNodeAttribute* attr = node->GetNodeAttribute();
        // If not in the map 'done' means the bind pose is not computed
        if( attr != 0 &&
            attr->GetAttributeType() == KFbxNodeAttribute::eSKELETON &&
            !Std_utils::exists(done, node) )
        {
            KFbxNode* parent = node->GetParent();

            // Retreive bind pose of the parent bone
            const int* pid = 0;
            Transfo bind_pose_prt = Transfo::identity();
            if( Std_utils::find(ptr_to_idx, parent, pid) )
                bind_pose_prt = skel._bones[*pid]._frame;

            // Retreive local transformation of the bone
            int id = Std_utils::find(ptr_to_idx, node );
            Transfo lc = Fbx_utils::to_transfo( node->EvaluateLocalTransform() );
            Transfo tr = bind_pose_prt * lc;
            skel._bones[id]._frame = tr;
        }
    }
}

// -----------------------------------------------------------------------------

/// Build the map that associates a KFbxNode pointer to its index in the
/// structure Loader::Abs_skeleton._bones[index]
void get_nodes_ptr_to_bone_id(const KFbxScene* scene,
                              std::map<KFbxNode*, int>& ptr_to_idx)
{
    ptr_to_idx.clear();
    KFbxNode* root = scene->GetRootNode();
    root = Fbx_utils::find_root(root, KFbxNodeAttribute::eSKELETON);
    if( root == 0 ) return;
    Loader::Abs_skeleton skel;
    fill_skeleton(skel, ptr_to_idx, -1, root);
}

// -----------------------------------------------------------------------------

/// Build the vector that associates an index in the
/// structure Loader::Abs_skeleton._bones[index] to a KFbxNode pointer
void get_bone_id_to_nodes_ptr(const KFbxScene* scene,
                              std::vector<KFbxNode*>& idx_to_ptr)
{
    idx_to_ptr.clear();
    std::map<KFbxNode*, int> ptr_to_idx;
    get_nodes_ptr_to_bone_id(scene, ptr_to_idx );

    idx_to_ptr.resize( ptr_to_idx.size() );
    std::map<KFbxNode*, int>::const_iterator it;
    for(it = ptr_to_idx.begin(); it != ptr_to_idx.end(); ++it)
        idx_to_ptr[it->second] = it->first;
}

// -----------------------------------------------------------------------------

void Fbx_file::get_skeleton(Loader::Abs_skeleton& skel) const
{
    KFbxNode* root = _fbx_scene->GetRootNode();
    root = Fbx_utils::find_root(root, KFbxNodeAttribute::eSKELETON);

    if( root == 0) return;
    //Fbx_utils::print_hierarchy(_fbx_scene);

    std::map<KFbxNode*, int> ptr_to_idx;
    // Building the skeleton hierarchy
    fill_skeleton(skel, ptr_to_idx, -1, root);

#if 0
    // Extract bind pose using KFBxPose but For some files this won't work
    fill_bind_pose(_fbx_scene, skel, ptr_to_idx);
#else

    // List of bone/node whose bind pose are computed
    std::map<KFbxNode*, KFbxNode*> done;
    for(int i = 0; i < _fbx_scene->GetGeometryCount(); i++)
    {
        KFbxGeometry* geom = _fbx_scene->GetGeometry(i);
        const KFbxNode* node = geom->GetNode();
        if( geom != 0 && geom->GetAttributeType() == KFbxNodeAttribute::eMESH)
        {
            if( geom->GetDeformerCount(KFbxDeformer::eSKIN) > 1){
                std::cerr << "WARNING FBX: there is more than one skin deformer";
                std::cerr << "associated to the geometry: " << geom->GetName();
                std::cerr << std::endl;
            }

            KFbxDeformer* dfm = geom->GetDeformer(0, KFbxDeformer::eSKIN);

            if( dfm == 0){
                std::cerr << "WARNING FBX: there is no deformer associated to";
                std::cerr << "the geometry: " << geom->GetName() << std::endl;
                continue;
            }

            // Extract bind pose using clusters
            compute_bones_bind_frame(skel, done, (KFbxSkin*)dfm, geom, ptr_to_idx);

            const int offset = Std_utils::find( _offset_verts, node);
            // Extract bones weights
            fill_bones_weights(skel, (KFbxSkin*)dfm, ptr_to_idx, offset, _size_mesh);
        }
    }

    // Some bind pose can't be calculated with clusters because nodes does not
    // influence the mesh. This little trick tries to compute them
    fill_bind_pose_cluster_less_nodes(_fbx_scene, skel, done, ptr_to_idx);

#endif
    Loader::compute_bone_lengths( skel );
}

// -----------------------------------------------------------------------------

/// Sample the animation 'anim_stack' and store it in 'abs_anim'
/// @param abs_anim: animation evaluator to fill
/// @param anim_stack: animation used to fill 'abs_anim'
/// @param scene: FBX scene needed to evaluate the animation
/// @param skel: we need the bind pose matrices to convert animation matrices
/// from Global to local space of the bones
bool fill_anim(Loader::Sampled_anim_eval* abs_anim,
               KFbxAnimStack* anim_stack,
               KFbxScene* scene,
               const std::vector<KFbxNode*>& idx_to_ptr,
               const Loader::Abs_skeleton& skel)
{
    if (anim_stack == 0)
        return false;

    // we assume that the first animation layer connected to the animation
    // stack is the base layer (this is the assumption made in the FBXSDK)
    //KFbxAnimLayer* mCurrentAnimLayer = anim_stack->GetMember(FBX_TYPE(KFbxAnimLayer), 0);

    scene->GetEvaluator()->SetContext(anim_stack);

    KTime frame_inter, start, stop;
    KTime::ETimeMode time_mode = scene->GetGlobalSettings().GetTimeMode();
    const double fps = KTime::GetFrameRate(time_mode);
    frame_inter.SetSecondDouble( 1. / fps );

    KFbxTakeInfo* take_info = scene->GetTakeInfo( anim_stack->GetName() );
    if (take_info)
    {
        start = take_info->mLocalTimeSpan.GetStart();
        stop  = take_info->mLocalTimeSpan.GetStop ();
    }
    else
    {
        // Take the time line value
        KTimeSpan lTimeLineTimeSpan;
        scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(lTimeLineTimeSpan);

        start = lTimeLineTimeSpan.GetStart();
        stop  = lTimeLineTimeSpan.GetStop ();
    }

    if( fps <= 0. ) {
        std::cerr << "FBX ERROR: frame rate is not set properly" << std::endl;
        return false;
    }

    // Sampling matrices for every frames
    int nb_frame_approx = (int)((stop-start).GetSecondDouble() * fps) + 1;
    abs_anim->_lcl_frames.reserve( nb_frame_approx );
    abs_anim->_frame_rate = (float)fps;
    for(KTime t = start; t < stop; t += frame_inter )
    {
        int nb_bones = idx_to_ptr.size();
        std::vector<Transfo> pose_t(nb_bones);
        for(int i = 0; i < nb_bones; i++)
        {
            KFbxNode* node = idx_to_ptr[i];
            Transfo tr = Fbx_utils::to_transfo(node->EvaluateGlobalTransform(t));

            Transfo joint_inv = skel._bones[i]._frame.fast_invert();

            pose_t[i] = joint_inv * tr;
        }
        abs_anim->_lcl_frames.push_back( pose_t );
    }

    return true;
}

// -----------------------------------------------------------------------------

void Fbx_file::get_animations(std::vector<Loader::Base_anim_eval*>& anims) const
{
    Loader::Abs_skeleton skel;
    get_skeleton(skel);

    std::vector<KFbxNode*> idx_to_ptr;
    get_bone_id_to_nodes_ptr(_fbx_scene, idx_to_ptr);

    KFbxScene* scene = _fbx_scene;

    /*
    KFbxNode* root = scene->GetRootNode();
    const float framerate = static_cast<float>(KTime::GetFrameRate(scene->GetGlobalSettings().GetTimeMode()));
    root->ResetPivotSetAndConvertAnimation( framerate, false, false );
    */

    int nb_stacks = _fbx_scene->GetSrcObjectCount(FBX_TYPE(KFbxAnimStack));
    for(int i = 0; i < nb_stacks; i++)
    {
        // Extract the ith animation
        KFbxObject* obj = scene->GetSrcObject(FBX_TYPE(KFbxAnimStack), i);
        KFbxAnimStack* stack = KFbxCast<KFbxAnimStack>( obj );
        std::string str( stack->GetName() );
        Loader::Sampled_anim_eval* abs_anim = new Loader::Sampled_anim_eval(str);
        if( fill_anim(abs_anim, stack, scene, idx_to_ptr, skel) )
            anims.push_back( abs_anim );
    }
}

// -----------------------------------------------------------------------------

void Fbx_file::set_mesh(const Loader::Abs_mesh& mesh)
{


}

// -----------------------------------------------------------------------------

#if 0
bool SaveScene(KFbxDocument* fbx_scene, const char* pFilename, int pFileFormat, bool pEmbedMedia)
{
    int lMajor, lMinor, lRevision;
    bool lStatus = true;

    // Create an exporter.
    KFbxExporter* lExporter = KFbxExporter::Create(g_FBXSdkManager, "");

    if( pFileFormat < 0 || pFileFormat >= g_FBXSdkManager->GetIOPluginRegistry()->GetWriterFormatCount() )
    {
        // Write in fall back format if pEmbedMedia is true
        pFileFormat = g_FBXSdkManager->GetIOPluginRegistry()->GetNativeWriterFormat();

        if (!pEmbedMedia)
        {
            //Try to export in ASCII if possible
            int lFormatIndex, lFormatCount = g_FBXSdkManager->GetIOPluginRegistry()->GetWriterFormatCount();

            for (lFormatIndex=0; lFormatIndex<lFormatCount; lFormatIndex++)
            {
                if (g_FBXSdkManager->GetIOPluginRegistry()->WriterIsFBX(lFormatIndex))
                {
                    KString lDesc =g_FBXSdkManager->GetIOPluginRegistry()->GetWriterFormatDescription(lFormatIndex);
                    char *lASCII = "ascii";
                    if (lDesc.Find(lASCII)>=0)
                    {
                        pFileFormat = lFormatIndex;
                        break;
                    }
                }
            }
        }
    }

    // Set the export states. By default, the export states are always set to
    // true except for the option eEXPORT_TEXTURE_AS_EMBEDDED. The code below
    // shows how to change these states.

    IOS_REF.SetBoolProp(EXP_FBX_MATERIAL,        true);
    IOS_REF.SetBoolProp(EXP_FBX_TEXTURE,         true);
    IOS_REF.SetBoolProp(EXP_FBX_EMBEDDED,        pEmbedMedia);
    IOS_REF.SetBoolProp(EXP_FBX_SHAPE,           true);
    IOS_REF.SetBoolProp(EXP_FBX_GOBO,            true);
    IOS_REF.SetBoolProp(EXP_FBX_ANIMATION,       true);
    IOS_REF.SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

    // Initialize the exporter by providing a filename.
    if(lExporter->Initialize(pFilename, pFileFormat, g_FBXSdkManager->GetIOSettings()) == false)
    {
        printf("Call to KFbxExporter::Initialize() failed.\n");
        printf("Error returned: %s\n\n", lExporter->GetLastErrorString());
        return false;
    }

    KFbxSdkManager::GetFileFormatVersion(lMajor, lMinor, lRevision);
    printf("FBX version number for this version of the FBX SDK is %d.%d.%d\n\n", lMajor, lMinor, lRevision);

    // Export the scene.
    lStatus = lExporter->Export(fbx_scene);

    // Destroy the exporter.
    lExporter->Destroy();
    return lStatus;
}
#endif

// -----------------------------------------------------------------------------

void Fbx_file::free_mem()
{
    if( _fbx_scene != 0)
    {
        _fbx_scene->Destroy();
        _fbx_scene = 0;
    }
}

}// END FBX_LOADER =============================================================