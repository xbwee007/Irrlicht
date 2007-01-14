// Copyright (C) 2002-2006 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "CSceneManager.h"
#include "IVideoDriver.h"
#include "IFileSystem.h"
#include "IAnimatedMesh.h"
#include "CMeshCache.h"
#include "IWriteFile.h"
#include "IXMLWriter.h"
#include "ISceneUserDataSerializer.h"

#include "os.h"

#include "CGeometryCreator.h"

#include "CDefaultMeshFormatLoader.h"
#include "C3DSMeshFileLoader.h"
#include "CXMeshFileLoader.h"
#include "COCTLoader.h"
#include "CCSMLoader.h"
#include "CLMTSMeshFileLoader.h"
#include "CMY3DMeshFileLoader.h"
#include "CColladaFileLoader.h"
#include "CDMFLoader.h"
#include "COgreMeshFileLoader.h"
#include "COBJMeshFileLoader.h"

#include "CCubeSceneNode.h"
#include "CSphereSceneNode.h"
#include "CAnimatedMeshSceneNode.h"
#include "COctTreeSceneNode.h"
#include "CCameraSceneNode.h"
#include "CCameraMayaSceneNode.h"
#include "CCameraFPSSceneNode.h"
#include "CLightSceneNode.h"
#include "CBillboardSceneNode.h"
#include "CMeshSceneNode.h"
#include "CSkyBoxSceneNode.h"
#include "CSkyDomeSceneNode.h"
#include "CParticleSystemSceneNode.h"
#include "CDummyTransformationSceneNode.h"
#include "CWaterSurfaceSceneNode.h"
#include "CTerrainSceneNode.h"
#include "CEmptySceneNode.h"
#include "CTextSceneNode.h"
#include "CDefaultSceneNodeFactory.h"

#include "CSceneCollisionManager.h"
#include "CMeshManipulator.h"
#include "CTriangleSelector.h"
#include "COctTreeTriangleSelector.h"
#include "CTriangleBBSelector.h"
#include "CMetaTriangleSelector.h"
#include "CTerrainTriangleSelector.h"

#include "CSceneNodeAnimatorRotation.h"
#include "CSceneNodeAnimatorFlyCircle.h"
#include "CSceneNodeAnimatorFlyStraight.h"
#include "CSceneNodeAnimatorTexture.h"
#include "CSceneNodeAnimatorCollisionResponse.h"
#include "CSceneNodeAnimatorDelete.h"
#include "CSceneNodeAnimatorFollowSpline.h"
#include "CDefaultSceneNodeAnimatorFactory.h"

#include "CQuake3ShaderSceneNode.h"

//! Enable debug features
#define SCENEMANAGER_DEBUG

namespace irr
{
namespace scene
{

//! constructor
CSceneManager::CSceneManager(video::IVideoDriver* driver, io::IFileSystem* fs,
							 gui::ICursorControl* cursorControl, CMeshCache* cache,
							 gui::IGUIEnvironment * gui)
: ISceneNode(0, 0), Driver(driver), FileSystem(fs), ActiveCamera(0),
	CursorControl(cursorControl), CollisionManager(0),
	ShadowColor(150,0,0,0), MeshManipulator(0), CurrentRendertime(ESNRP_COUNT),
	MeshCache(cache), AmbientLight(0,0,0,0), GUIEnvironment ( gui ),
	IRR_XML_FORMAT_SCENE(L"irr_scene"), IRR_XML_FORMAT_NODE(L"node"), IRR_XML_FORMAT_NODE_ATTR_TYPE(L"type")
{
	#ifdef _DEBUG
	ISceneManager::setDebugName("CSceneManager ISceneManager");
	ISceneNode::setDebugName("CSceneManager ISceneNode");
	#endif

	if (Driver)
		Driver->grab();

	if (FileSystem)
		FileSystem->grab();

	if (CursorControl)
		CursorControl->grab();

	if ( GUIEnvironment )
		GUIEnvironment->grab ();

	// create mesh cache if not there already
	if (!MeshCache)
		MeshCache = new CMeshCache();
	else
		MeshCache->grab();

	// create collision manager
	CollisionManager = new CSceneCollisionManager(this, Driver);

	// create manipulator
	MeshManipulator = new CMeshManipulator();

	// add default format loader

	MeshLoaderList.push_back(new CDefaultMeshFormatLoader(FileSystem, Driver, this));
	MeshLoaderList.push_back(new C3DSMeshFileLoader(MeshManipulator,FileSystem, Driver));
	MeshLoaderList.push_back(new CXMeshFileLoader(MeshManipulator, Driver));
	MeshLoaderList.push_back(new COCTLoader(Driver));
	MeshLoaderList.push_back(new CCSMLoader(this, FileSystem));
	MeshLoaderList.push_back(new CLMTSMeshFileLoader(FileSystem, Driver, &Parameters));
	MeshLoaderList.push_back(new CMY3DMeshFileLoader(FileSystem, Driver, this));
	MeshLoaderList.push_back(new CColladaFileLoader(Driver, this, FileSystem));
	MeshLoaderList.push_back(new CDMFLoader(Driver, this));
	MeshLoaderList.push_back(new COgreMeshFileLoader(MeshManipulator, FileSystem, Driver));
	MeshLoaderList.push_back(new COBJMeshFileLoader(FileSystem, Driver));
	// factories

	ISceneNodeFactory* factory = new CDefaultSceneNodeFactory(this);
	registerSceneNodeFactory(factory);
	factory->drop();

	ISceneNodeAnimatorFactory* animatorFactory = new CDefaultSceneNodeAnimatorFactory(this);
	registerSceneNodeAnimatorFactory(animatorFactory);
	animatorFactory->drop();
}



//! destructor
CSceneManager::~CSceneManager()
{
	clearDeletionList();

	if (Driver)
		Driver->drop();

	if (FileSystem)
		FileSystem->drop();

	if (CursorControl)
		CursorControl->drop();

	if (CollisionManager)
		CollisionManager->drop();

	if (MeshManipulator)
		MeshManipulator->drop();

	if ( GUIEnvironment )
		GUIEnvironment->drop ();

	u32 i;

	for (i=0; i<MeshLoaderList.size(); ++i)
		MeshLoaderList[i]->drop();

	if (ActiveCamera)
		ActiveCamera->drop();

	if (MeshCache)
		MeshCache->drop();

	for (i=0; i<SceneNodeFactoryList.size(); ++i)
		SceneNodeFactoryList[i]->drop();

	for (i=0; i<SceneNodeAnimatorFactoryList.size(); ++i)
		SceneNodeAnimatorFactoryList[i]->drop();
}


//! gets an animateable mesh. loads it if needed. returned pointer must not be dropped.
IAnimatedMesh* CSceneManager::getMesh(const c8* filename)
{
	IAnimatedMesh* msh = 0;

	core::stringc name = filename;
	name.make_lower();

	msh = MeshCache->findMesh(name.c_str());
	if (msh)
		return msh;

	io::IReadFile* file = FileSystem->createAndOpenFile(filename);
	if (!file)
	{
		os::Printer::log("Could not load mesh, because file could not be opened.", filename, ELL_ERROR);
		return 0;
	}

	s32 count = MeshLoaderList.size();
	for (s32 i=count-1; i>=0; --i)
		if (MeshLoaderList[i]->isALoadableFileExtension(name.c_str()))
		{
			// reset file to avoid side effects of previous calls to createMesh
			file->seek(0);
			msh = MeshLoaderList[i]->createMesh(file);
			if (msh)
			{
				MeshCache->addMesh(filename, msh);
				msh->drop();
				break;
			}
		}

	file->drop();

	if (!msh)
		os::Printer::log("Could not load mesh, file format seems to be unsupported", filename, ELL_ERROR);
	else
		os::Printer::log("Loaded mesh", filename, ELL_INFORMATION);

	return msh;
}


//! returns the video driver
video::IVideoDriver* CSceneManager::getVideoDriver()
{
	return Driver;
}

//! returns the GUI Environment
gui::IGUIEnvironment* CSceneManager::getGUIEnvironment ()
{
	return GUIEnvironment;
}

//! Adds a text scene node, which is able to display
//! 2d text at a position in three dimensional space
ITextSceneNode* CSceneManager::addTextSceneNode(gui::IGUIFont* font, const wchar_t* text,
	video::SColor color,
	ISceneNode* parent,	const core::vector3df& position,
	s32 id)
{
	if (!font)
		return 0;

	if (!parent)
		parent = this;

	ITextSceneNode* t = new CTextSceneNode(parent, this, id, font,
		getSceneCollisionManager(), position, text, color);
	t->drop();

	return t;
}

//! Adds a text scene node, which uses billboards
ITextSceneNode* CSceneManager::addTextSceneNode2(gui::IGUIFontASCII* font, const wchar_t* text,
			ISceneNode* parent,
			const core::dimension2d<f32>& size,f32 kerning,
			const core::vector3df& position, s32 id,
			video::SColor shade_top, video::SColor shade_down)
{
	if (!font)
		return 0;

	if (!parent)
		parent = this;

	ITextSceneNode* node = new CTextSceneNode2(parent, this, id, font, text,position, size,kerning,
		shade_top, shade_down);
	node->drop();

	return node;

}


//! Adds a text scene node, which uses billboards
ISceneNode* CSceneManager::addQuake3SceneNode(	IMeshBuffer* meshBuffer,
												const quake3::SShader * shader,
												ISceneNode* parent,
												s32 id
											)

{
	if ( 0 == shader )
		return 0;

	if (!parent)
		parent = this;

	CQuake3ShaderSceneNode* node = new CQuake3ShaderSceneNode ( parent, this, id, FileSystem, meshBuffer, shader );
	node->drop();

	return node;

}


//! adds a test scene node for test purposes to the scene. It is a simple cube of (1,1,1) size.
//! the returned pointer must not be dropped.
ISceneNode* CSceneManager::addCubeSceneNode(f32 size, ISceneNode* parent, s32 id,
	const core::vector3df& position, const core::vector3df& rotation, const core::vector3df& scale)
{
	if (!parent)
		parent = this;

	ISceneNode* node = new CCubeSceneNode(size, parent, this, id, position, rotation, scale);
	node->drop();

	return node;
}

//! Adds a sphere scene node for test purposes to the scene.
ISceneNode* CSceneManager::addSphereSceneNode(f32 radius, s32 polyCount, ISceneNode* parent, s32 id,
	const core::vector3df& position,
	const core::vector3df& rotation,
	const core::vector3df& scale)
{
	if (!parent)
		parent = this;

	ISceneNode* node = new CSphereSceneNode(radius, polyCount, parent, this, id, position, rotation, scale);
	node->drop();

	return node;
}


//! adds a scene node for rendering a static mesh
//! the returned pointer must not be dropped.
IMeshSceneNode* CSceneManager::addMeshSceneNode(IMesh* mesh, ISceneNode* parent, s32 id,
	const core::vector3df& position, const core::vector3df& rotation,
	const core::vector3df& scale, bool alsoAddIfMeshPointerZero)
{
	if (!alsoAddIfMeshPointerZero && !mesh)
		return 0;

	if (!parent)
		parent = this;

	IMeshSceneNode* node = new CMeshSceneNode(mesh, parent, this, id, position, rotation, scale);
	node->drop();

	return node;
}


//! Adds a scene node for rendering a animated water surface mesh.
ISceneNode* CSceneManager::addWaterSurfaceSceneNode(IMesh* mesh, f32 waveHeight, f32 waveSpeed, f32 waveLength,
	ISceneNode* parent, s32 id, const core::vector3df& position,
	const core::vector3df& rotation, const core::vector3df& scale)
{
	if (!mesh)
		return 0;

	if (!parent)
		parent = this;

	ISceneNode* node = new CWaterSurfaceSceneNode(waveHeight, waveSpeed, waveLength,
		mesh, parent, this, id, position, rotation, scale);

	node->drop();

	return node;
}



//! adds a scene node for rendering an animated mesh model
IAnimatedMeshSceneNode* CSceneManager::addAnimatedMeshSceneNode(IAnimatedMesh* mesh, ISceneNode* parent, s32 id,
	const core::vector3df& position, const core::vector3df& rotation,
	const core::vector3df& scale, bool alsoAddIfMeshPointerZero)
{
	if (!alsoAddIfMeshPointerZero && !mesh)
		return 0;

	if (!parent)
		parent = this;

	IAnimatedMeshSceneNode* node =
		new CAnimatedMeshSceneNode(mesh, parent, this, id, position, rotation, scale);
	node->drop();

	return node;
}


//! Adds a scene node for rendering using a octtree to the scene graph. This a good method for rendering
//! scenes with lots of geometry. The Octree is built on the fly from the mesh, much
//! faster then a bsp tree.
ISceneNode* CSceneManager::addOctTreeSceneNode(IAnimatedMesh* mesh, ISceneNode* parent,
			s32 id, s32 minimalPolysPerNode, bool alsoAddIfMeshPointerZero)
{
	if (!alsoAddIfMeshPointerZero && (!mesh || !mesh->getFrameCount()))
		return 0;

	return addOctTreeSceneNode(mesh ? mesh->getMesh(0) : 0,
							   parent, id, minimalPolysPerNode,
							   alsoAddIfMeshPointerZero);
}



//! Adss a scene node for rendering using a octtree. This a good method for rendering
//! scenes with lots of geometry. The Octree is built on the fly from the mesh, much
//! faster then a bsp tree.
ISceneNode* CSceneManager::addOctTreeSceneNode(IMesh* mesh, ISceneNode* parent,
											   s32 id, s32 minimalPolysPerNode,
											   bool alsoAddIfMeshPointerZero)
{
	if (!alsoAddIfMeshPointerZero && !mesh)
		return 0;

	if (!parent)
		parent = this;

	COctTreeSceneNode* node = new COctTreeSceneNode(parent, this, id, minimalPolysPerNode);

	if (mesh)
		node->createTree(mesh);

	node->drop();

	return node;
}


//! Adds a camera scene node to the tree and sets it as active camera.
//! \param position: Position of the space relative to its parent where the camera will be placed.
//! \param lookat: Position where the camera will look at. Also known as target.
//! \param parent: Parent scene node of the camera. Can be null. If the parent moves,
//! the camera will move too.
//! \return Returns pointer to interface to camera
ICameraSceneNode* CSceneManager::addCameraSceneNode(ISceneNode* parent,
	const core::vector3df& position, const core::vector3df& lookat, s32 id)
{
	if (!parent)
		parent = this;

	ICameraSceneNode* node = new CCameraSceneNode(parent, this, id, position, lookat);
	node->drop();

	setActiveCamera(node);

	return node;
}



//! Adds a camera scene node which is able to be controlle with the mouse similar
//! like in the 3D Software Maya by Alias Wavefront.
//! The returned pointer must not be dropped.
ICameraSceneNode* CSceneManager::addCameraSceneNodeMaya(ISceneNode* parent,
	f32 rotateSpeed, f32 zoomSpeed, f32 translationSpeed, s32 id)
{
	if (!parent)
		parent = this;

	ICameraSceneNode* node = new CCameraMayaSceneNode(parent, this, id, rotateSpeed,
		zoomSpeed, translationSpeed);
	node->drop();

	setActiveCamera(node);

	return node;
}



//! Adds a camera scene node which is able to be controled with the mouse and keys
//! like in most first person shooters (FPS):
ICameraSceneNode* CSceneManager::addCameraSceneNodeFPS(ISceneNode* parent,
	f32 rotateSpeed, f32 moveSpeed, s32 id,
	SKeyMap* keyMapArray, s32 keyMapSize, bool noVerticalMovement,f32 jumpSpeed)
{
	if (!parent)
		parent = this;

	ICameraSceneNode* node = new CCameraFPSSceneNode(parent, this, CursorControl,
		id, rotateSpeed, moveSpeed, jumpSpeed, keyMapArray, keyMapSize, noVerticalMovement);
	node->drop();

	setActiveCamera(node);

	return node;
}



//! Adds a dynamic light scene node. The light will cast dynamic light on all
//! other scene nodes in the scene, which have the material flag video::MTF_LIGHTING
//! turned on. (This is the default setting in most scene nodes).
ILightSceneNode* CSceneManager::addLightSceneNode(ISceneNode* parent,
	const core::vector3df& position, video::SColorf color, f32 range, s32 id)
{
	if (!parent)
		parent = this;

	ILightSceneNode* node = new CLightSceneNode(parent, this, id, position, color, range);
	node->drop();

	return node;
}



//! Adds a billboard scene node to the scene. A billboard is like a 3d sprite: A 2d element,
//! which always looks to the camera. It is usually used for things like explosions, fire,
//! lensflares and things like that.
IBillboardSceneNode* CSceneManager::addBillboardSceneNode(ISceneNode* parent,
	const core::dimension2d<f32>& size, const core::vector3df& position, s32 id,
	video::SColor shade_top, video::SColor shade_down
	)
{
	if (!parent)
		parent = this;

	IBillboardSceneNode* node = new CBillboardSceneNode(parent, this, id, position, size,
		shade_top, shade_down);
	node->drop();

	return node;
}



//! Adds a skybox scene node. A skybox is a big cube with 6 textures on it and
//! is drawn around the camera position.
ISceneNode* CSceneManager::addSkyBoxSceneNode(video::ITexture* top, video::ITexture* bottom,
	video::ITexture* left, video::ITexture* right, video::ITexture* front,
	video::ITexture* back, ISceneNode* parent, s32 id)
{
	if (!parent)
		parent = this;

	ISceneNode* node = new CSkyBoxSceneNode(top, bottom, left, right,
			front, back, parent, this, id);

	node->drop();
	return node;
}


//! Adds a skydome scene node. A skydome is a large (half-) sphere with a
//! panoramic texture on it and is drawn around the camera position.
ISceneNode* CSceneManager::addSkyDomeSceneNode(video::ITexture* texture,
	u32 horiRes, u32 vertRes, f64 texturePercentage,
	f64 spherePercentage, ISceneNode* parent, s32 id)
{
	if (!parent)
		parent = this;

	ISceneNode* node = new CSkyDomeSceneNode(texture, horiRes, vertRes,
		texturePercentage, spherePercentage, parent, this, id);

	node->drop();
	return node;
}


//! Adds a particle system scene node.
IParticleSystemSceneNode* CSceneManager::addParticleSystemSceneNode(
	bool withDefaultEmitter, ISceneNode* parent, s32 id,
	const core::vector3df& position, const core::vector3df& rotation,
	const core::vector3df& scale)
{
	if (!parent)
		parent = this;

	IParticleSystemSceneNode* node = new CParticleSystemSceneNode(withDefaultEmitter,
		parent, this, id, position, rotation, scale);
	node->drop();

	return node;
}


//! Adds a terrain scene node to the scene graph.
ITerrainSceneNode* CSceneManager::addTerrainSceneNode(
	const char* heightMapFileName,
	ISceneNode* parent, s32 id,
	const core::vector3df& position,
	const core::vector3df& rotation,
	const core::vector3df& scale,
	video::SColor vertexColor,
	s32 maxLOD, E_TERRAIN_PATCH_SIZE patchSize, s32 smoothFactor)
{
	io::IReadFile* file = FileSystem->createAndOpenFile(heightMapFileName);
	if (!file)
	{
		os::Printer::log("Could not load terrain, because file could not be opened.",
			heightMapFileName, ELL_ERROR);
		return 0;
	}

	ITerrainSceneNode* terrain = addTerrainSceneNode(file, parent, id,
		position, rotation, scale, vertexColor, maxLOD, patchSize, smoothFactor);
	file->drop();

	return terrain;
}

//! Adds a terrain scene node to the scene graph.
ITerrainSceneNode* CSceneManager::addTerrainSceneNode(
	io::IReadFile* heightMapFile,
	ISceneNode* parent, s32 id,
	const core::vector3df& position,
	const core::vector3df& rotation,
	const core::vector3df& scale,
	video::SColor vertexColor,
	s32 maxLOD, E_TERRAIN_PATCH_SIZE patchSize,
	s32 smoothFactor)
{
	if (!parent)
		parent = this;

	CTerrainSceneNode* node = new CTerrainSceneNode(parent, this, id,
		maxLOD, patchSize, position, rotation, scale);

	if (!node->loadHeightMap(heightMapFile, vertexColor, smoothFactor))
	{
		node->remove();
		node->drop();
		return 0;
	}

	node->drop();
	return node;
}


//! Adds an empty scene node.
ISceneNode* CSceneManager::addEmptySceneNode(ISceneNode* parent, s32 id)
{
	if (!parent)
		parent = this;

	ISceneNode* node = new CEmptySceneNode(parent, this, id);
	node->drop();

	return node;
}


//! Adds a dummy transformation scene node to the scene graph.
IDummyTransformationSceneNode* CSceneManager::addDummyTransformationSceneNode(
	ISceneNode* parent, s32 id)
{
	if (!parent)
		parent = this;

	IDummyTransformationSceneNode* node = new CDummyTransformationSceneNode(
		parent, this, id);
	node->drop();

	return node;
}

//! Adds a Hill Plane mesh to the mesh pool. The mesh is generated on the fly
//! and looks like a plane with some hills on it. It is uses mostly for quick
//! tests of the engine only. You can specify how many hills there should be
//! on the plane and how high they should be. Also you must specify a name for
//! the mesh, because the mesh is added to the mesh pool, and can be retieved
//! again using ISceneManager::getMesh with the name as parameter.
IAnimatedMesh* CSceneManager::addHillPlaneMesh(const c8* name,
	const core::dimension2d<f32>& tileSize, const core::dimension2d<s32>& tileCount,
	video::SMaterial* material,	f32 hillHeight, const core::dimension2d<f32>& countHills,
	const core::dimension2d<f32>& textureRepeatCount)
{
	if (!name || MeshCache->isMeshLoaded(name))
		return 0;

	IAnimatedMesh* animatedMesh = CGeometryCreator::createHillPlaneMesh(tileSize,
		tileCount, material, hillHeight, countHills, textureRepeatCount);

	MeshCache->addMesh(name, animatedMesh);

	animatedMesh->drop();

	return animatedMesh;
}


//! Adds a terrain mesh to the mesh pool.
IAnimatedMesh* CSceneManager::addTerrainMesh(const c8* name,
	video::IImage* texture, video::IImage* heightmap,
	const core::dimension2d<f32>& stretchSize,
	f32 maxHeight,
	const core::dimension2d<s32>& defaultVertexBlockSize)
{
	if (!name || MeshCache->isMeshLoaded(name))
		return 0;

	IAnimatedMesh* animatedMesh = CGeometryCreator::createTerrainMesh(texture,
		heightmap, stretchSize, maxHeight, getVideoDriver(), defaultVertexBlockSize);

	if (!animatedMesh)
		return 0;

	MeshCache->addMesh(name, animatedMesh);

	animatedMesh->drop();

	return animatedMesh;
}

//! Adds an arrow mesh to the mesh pool.
IAnimatedMesh* CSceneManager::addArrowMesh(const c8* name, u32 tesselation, f32 width, f32 height, video::SColor vtxColor)
{
	if (!name || MeshCache->isMeshLoaded(name))
		return 0;

	IAnimatedMesh* animatedMesh = CGeometryCreator::createArrowMesh( tesselation, width, height, vtxColor );

	if (!animatedMesh)
		return 0;

	MeshCache->addMesh(name, animatedMesh);

	animatedMesh->drop();

	return animatedMesh;
}



//! Returns the root scene node. This is the scene node wich is parent
//! of all scene nodes. The root scene node is a special scene node which
//! only exists to manage all scene nodes. It is not rendered and cannot
//! be removed from the scene.
//! \return Returns a pointer to the root scene node.
ISceneNode* CSceneManager::getRootSceneNode()
{
	return this;
}



//! Returns the current active camera.
//! \return The active camera is returned. Note that this can be NULL, if there
//! was no camera created yet.
ICameraSceneNode* CSceneManager::getActiveCamera()
{
	return ActiveCamera;
}



//! Sets the active camera. The previous active camera will be deactivated.
//! \param camera: The new camera which should be active.
void CSceneManager::setActiveCamera(ICameraSceneNode* camera)
{
	if (ActiveCamera)
		ActiveCamera->drop();

	ActiveCamera = camera;

	if (ActiveCamera)
		ActiveCamera->grab();
}




//! renders the node.
void CSceneManager::render()
{
}


//! returns the axis aligned bounding box of this node
const core::aabbox3d<f32>& CSceneManager::getBoundingBox() const
{
	_IRR_DEBUG_BREAK_IF(true) // Bounding Box of Scene Manager wanted.

	// should never be used.
	return *((core::aabbox3d<f32>*)0);
}



//! returns if node is culled
bool CSceneManager::isCulled(ISceneNode* node)
{
	ICameraSceneNode* cam = getActiveCamera();
	if (!cam)
		return false;


	switch ( node->getAutomaticCulling() )
	{
		// can be seen by a bounding box ?
		case scene::EAC_BOX:
		{
			core::aabbox3d<f32> tbox = node->getBoundingBox();
			node->getAbsoluteTransformation().transformBox(tbox);
			return !(tbox.intersectsWithBox(cam->getViewFrustum()->boundingBox));
		} break;

		// can be seen by a bounding sphere
		case scene::EAC_FRUSTUM_SPHERE:
		{
		} break;

		// can be seen by cam pyramid planes ?
		case scene::EAC_FRUSTUM_BOX:
		{

			SViewFrustum frust = *cam->getViewFrustum();

			//transform the frustum to the node's current absolute transformation
			core::matrix4 invTrans(node->getAbsoluteTransformation());
			invTrans.makeInverse();
			frust.transform(invTrans);

			core::vector3df edges[8];
			node->getBoundingBox().getEdges(edges);

			bool visible = true;

			s32 i;
			for (i=0; i<scene::SViewFrustum::VF_PLANE_COUNT; ++i)
			{
				bool boxInFrustum = false;

				for (int j=0; j<8; ++j)
				{
					if (frust.planes[i].isFrontFacing(edges[j]) )
					{
						boxInFrustum = true;
						break;
					}
				}

				if (!boxInFrustum)
				{
					visible = false;
					break;
				}

			}

			return !visible;

		} break;
	}

	return false;
}




//! registers a node for rendering it at a specific time.
u32 CSceneManager::registerNodeForRendering(ISceneNode* node, E_SCENE_NODE_RENDER_PASS time)
{
	u32 taken = 0;

	switch(time)
	{
		// take camera if it doesn't exists
		case ESNRP_CAMERA:
		{
			taken = 1;
			for ( u32 i = 0; i != CameraList.size(); ++i )
			{
				if ( CameraList[i] == node )
				{
					taken = 0;
					break;
				}
			}
			if ( taken )
			{
				CameraList.push_back(node);
			}
		}break;

	case ESNRP_LIGHT:
		// TODO: Point Light culling..
		// Lighting modell in irrlicht has to be redone..
		//if (!isCulled(node))
		{
			LightList.push_back(DistanceNodeEntry(node, camWorldPos));
			taken = 1;
		}
		break;

	case ESNRP_SKY_BOX:
		SkyBoxList.push_back(node);
		taken = 1;
		break;
	case ESNRP_SOLID:
		if (!isCulled(node))
		{
			SolidNodeList.push_back( node );
			taken = 1;
		}
		break;
	case ESNRP_TRANSPARENT:
		if (!isCulled(node))
		{
			TransparentNodeList.push_back(TransparentNodeEntry(node, camWorldPos));
			taken = 1;
		}
		break;
	case ESNRP_AUTOMATIC:
		if (!isCulled(node))
		{
			u32 count = node->getMaterialCount();

			taken = 0;
			for (u32 i=0; i<count; ++i)
			{
				video::IMaterialRenderer* rnd =
					Driver->getMaterialRenderer(node->getMaterial(i).MaterialType);
				if (rnd && rnd->isTransparent())
				{
					// register as transparent node
					TransparentNodeEntry e(node, camWorldPos);
					TransparentNodeList.push_back(e);
					taken = 1;
					break;
				}
			}

			// not transparent, register as solid
			if ( 0 == taken )
			{
				SolidNodeList.push_back( node );
				taken = 1;
			}
		}
		break;
	case ESNRP_SHADOW:
		if (!isCulled(node))
		{
			ShadowNodeList.push_back(node);
			taken = 1;
		}
		break;
	case ESNRP_SHADER_0:
	case ESNRP_SHADER_1:
	case ESNRP_SHADER_2:
	case ESNRP_SHADER_3:
	case ESNRP_SHADER_4:
	case ESNRP_SHADER_5:
	case ESNRP_SHADER_6:
	case ESNRP_SHADER_7:
	case ESNRP_SHADER_8:
	case ESNRP_SHADER_9:
	case ESNRP_SHADER_10:
		if ( !isCulled(node) )
		{
			ShaderNodeList[ time - ESNRP_SHADER_0].push_back( ShaderNodeEntry ( node,time - ESNRP_SHADER_0 ));
			taken = 1;
		} break;

	case ESNRP_COUNT: // ignore this one
		break;
	}

#ifdef SCENEMANAGER_DEBUG
	s32 index = Parameters.findAttribute ( "calls" );
	Parameters.setAttribute ( index, Parameters.getAttributeAsInt ( index ) + 1 );

	if ( 0 == taken )
	{
		index = Parameters.findAttribute ( "culled" );
		Parameters.setAttribute ( index, Parameters.getAttributeAsInt ( index ) + 1 );
	}
#endif

	return taken;
}

//! This method is called just before the rendering process of the whole scene.
//! draws all scene nodes
void CSceneManager::drawAll()
{
	if (!Driver)
		return;

	// reset attributes
	Parameters.setAttribute ( "culled", 0 );
	Parameters.setAttribute ( "calls", 0 );
	Parameters.setAttribute ( "drawn", 0 );


	// reset all transforms
	video::IVideoDriver* driver = getVideoDriver();
	if ( driver )
	{
		core::matrix4 identity;
		driver->setTransform ( video::ETS_PROJECTION, identity );
		driver->setTransform ( video::ETS_VIEW, identity );
		driver->setTransform ( video::ETS_WORLD, identity );
		driver->setTransform ( video::ETS_TEXTURE_0, identity );
		driver->setTransform ( video::ETS_TEXTURE_1, identity );
	}

	// do animations and other stuff.
	OnPostRender(os::Timer::getTime());


	/*!
		First Scene Node for prerendering should be the active camera
		consisten Camera is needed for culling
	*/
	camWorldPos.set(0,0,0);
	if ( ActiveCamera )
	{
		ActiveCamera->OnPreRender();
		camWorldPos = ActiveCamera->getAbsolutePosition();
	}

	// let all nodes register themselfes
	OnPreRender();

	u32 i; // new ISO for scoping problem in some compilers

	//render camera scenes
	{
		CurrentRendertime = ESNRP_CAMERA;
		for (i=0; i<CameraList.size(); ++i)
			CameraList[i]->render();

		CameraList.clear();
	}

	//render lights scenes
	{
		CurrentRendertime = ESNRP_LIGHT;

		Driver->deleteAllDynamicLights();

		Driver->setAmbientLight(AmbientLight);

		LightList.sort ();		// on distance to camera

		u32 maxLights = irr::core::min_ ( Driver->getMaximalDynamicLightAmount (), LightList.size () );
		for (i=0; i< maxLights; ++i)
			LightList[i].node->render();

		LightList.clear();
	}

	// render skyboxes
	{
		CurrentRendertime = ESNRP_SKY_BOX;

		for (i=0; i<SkyBoxList.size(); ++i)
			SkyBoxList[i]->render();

		SkyBoxList.clear();
	}


	// render default objects
	{
		CurrentRendertime = ESNRP_SOLID;
		SolidNodeList.sort(); // sort by textures

		for (i=0; i<SolidNodeList.size(); ++i)
			SolidNodeList[i].node->render();

		Parameters.setAttribute ( "drawn", (s32) SolidNodeList.size () );

		SolidNodeList.clear();
	}

	// render shadows
	{
		CurrentRendertime = ESNRP_SHADOW;
		for (i=0; i<ShadowNodeList.size(); ++i)
			ShadowNodeList[i]->render();

		if (!ShadowNodeList.empty())
			Driver->drawStencilShadow(true,ShadowColor, ShadowColor,
				ShadowColor, ShadowColor);

		ShadowNodeList.clear();
	}

	// render transparent objects.
	{
		CurrentRendertime = ESNRP_TRANSPARENT;
		TransparentNodeList.sort(); // sort by distance from camera

		for (i=0; i<TransparentNodeList.size(); ++i)
			TransparentNodeList[i].node->render();

		TransparentNodeList.clear();
	}


	// render shader objects.
	{
		for ( u32 g = 0; g!= ESNRP_SHADER_10 - ESNRP_SHADER_0 + 1; ++g )
		{
			CurrentRendertime = (scene::E_SCENE_NODE_RENDER_PASS) (ESNRP_SHADER_0 + g);

			const u32 size = ShaderNodeList[g].size ();
			if ( 0 == size )
				continue;

			ShaderNodeList[g].sort(); // sort by textures
			for (i=0; i< size; ++i)
				ShaderNodeList[g][i].node->render();

			ShaderNodeList[g].clear();
		}
	}


	// do animations and other stuff.
	//OnPostRender(os::Timer::getTime());

	clearDeletionList();

	CurrentRendertime = ESNRP_COUNT;
}


//! Sets the color of stencil buffers shadows drawn by the scene manager.
void CSceneManager::setShadowColor(video::SColor color)
{
	ShadowColor = color;
}


//! Returns the current color of shadows.
video::SColor CSceneManager::getShadowColor() const
{
	return ShadowColor;
}



//! creates a rotation animator, which rotates the attached scene node around itself.
ISceneNodeAnimator* CSceneManager::createRotationAnimator(const core::vector3df& rotationPerSecond)
{
	ISceneNodeAnimator* anim = new CSceneNodeAnimatorRotation(os::Timer::getTime(),
		rotationPerSecond);

	return anim;
}



//! creates a fly circle animator, which lets the attached scene node fly around a center.
ISceneNodeAnimator* CSceneManager::createFlyCircleAnimator(const core::vector3df& normal,
														   f32 radius, f32 speed,
														   const core::vector3df& direction
														   )
{
	ISceneNodeAnimator* anim = new CSceneNodeAnimatorFlyCircle(os::Timer::getTime(), normal,
			radius, speed, direction);
	return anim;
}


//! Creates a fly straight animator, which lets the attached scene node
//! fly or move along a line between two points.
ISceneNodeAnimator* CSceneManager::createFlyStraightAnimator(const core::vector3df& startPoint,
					const core::vector3df& endPoint, u32 timeForWay, bool loop)
{
	ISceneNodeAnimator* anim = new CSceneNodeAnimatorFlyStraight(startPoint,
		endPoint, timeForWay, loop, os::Timer::getTime());

	return anim;
}


//! Creates a texture animator, which switches the textures of the target scene
//! node based on a list of textures.
ISceneNodeAnimator* CSceneManager::createTextureAnimator(const core::array<video::ITexture*>& textures,
	s32 timePerFrame, bool loop)
{
	ISceneNodeAnimator* anim = new CSceneNodeAnimatorTexture(textures,
		timePerFrame, loop, os::Timer::getTime());

	return anim;
}


//! Creates a scene node animator, which deletes the scene node after
//! some time automaticly.
ISceneNodeAnimator* CSceneManager::createDeleteAnimator(u32 when)
{
	return new CSceneNodeAnimatorDelete(this, os::Timer::getTime() + when);
}




//! Creates a special scene node animator for doing automatic collision detection
//! and response.
ISceneNodeAnimatorCollisionResponse* CSceneManager::createCollisionResponseAnimator(
	ITriangleSelector* world, ISceneNode* sceneNode, const core::vector3df& ellipsoidRadius,
	const core::vector3df& gravityPerSecond,
	const core::vector3df& ellipsoidTranslation, f32 slidingValue)
{
	ISceneNodeAnimatorCollisionResponse* anim = new
		CSceneNodeAnimatorCollisionResponse(this, world, sceneNode,
			ellipsoidRadius, gravityPerSecond,
			ellipsoidTranslation, slidingValue);

	return anim;
}


//! Creates a follow spline animator.
ISceneNodeAnimator* CSceneManager::createFollowSplineAnimator(s32 startTime,
	const core::array< core::vector3df >& points,
	f32 speed, f32 tightness)
{
	ISceneNodeAnimator* a = new CSceneNodeAnimatorFollowSpline(startTime, points,
		speed, tightness);
	return a;
}



//! Adds an external mesh loader.
void CSceneManager::addExternalMeshLoader(IMeshLoader* externalLoader)
{
	if (!externalLoader)
		return;

	externalLoader->grab();
	MeshLoaderList.push_back(externalLoader);
}



//! Returns a pointer to the scene collision manager.
ISceneCollisionManager* CSceneManager::getSceneCollisionManager()
{
	return CollisionManager;
}


//! Returns a pointer to the mesh manipulator.
IMeshManipulator* CSceneManager::getMeshManipulator()
{
	return MeshManipulator;
}


//! Creates a simple ITriangleSelector, based on a mesh.
ITriangleSelector* CSceneManager::createTriangleSelector(IMesh* mesh, ISceneNode* node)
{
	if (!mesh || !node)
		return 0;

	return new CTriangleSelector(mesh, node);
}


//! Creates a simple dynamic ITriangleSelector, based on a axis aligned bounding box.
ITriangleSelector* CSceneManager::createTriangleSelectorFromBoundingBox(ISceneNode* node)
{
	if (!node)
		return 0;

	return new CTriangleBBSelector(node);
}


//! Creates a simple ITriangleSelector, based on a mesh.
ITriangleSelector* CSceneManager::createOctTreeTriangleSelector(IMesh* mesh,
																ISceneNode* node,
																s32 minimalPolysPerNode)
{
	if (!mesh || !node)
		return 0;

	return new COctTreeTriangleSelector(mesh, node, minimalPolysPerNode);
}



//! Creates a meta triangle selector.
IMetaTriangleSelector* CSceneManager::createMetaTriangleSelector()
{
	return new CMetaTriangleSelector();
}



//! Creates a triangle selector which can select triangles from a terrain scene node
ITriangleSelector* CSceneManager::createTerrainTriangleSelector(
	ITerrainSceneNode* node, s32 LOD)
{
	return new CTerrainTriangleSelector(node, LOD);
}



//! Adds a scene node to the deletion queue.
void CSceneManager::addToDeletionQueue(ISceneNode* node)
{
	if (!node)
		return;

	node->grab();
	DeletionList.push_back(node);
}


//! clears the deletion list
void CSceneManager::clearDeletionList()
{
	if (DeletionList.empty())
		return;

	for (s32 i=0; i<(s32)DeletionList.size(); ++i)
	{
		DeletionList[i]->remove();
		DeletionList[i]->drop();
	}

	DeletionList.clear();
}


//! Returns the first scene node with the specified name.
ISceneNode* CSceneManager::getSceneNodeFromName(const char* name, ISceneNode* start)
{
	if (start == 0)
		start = getRootSceneNode();

	if (!strcmp(start->getName(),name))
		return start;

	ISceneNode* node = 0;

	const core::list<ISceneNode*>& list = start->getChildren();
	core::list<ISceneNode*>::Iterator it = list.begin();
	for (; it!=list.end(); ++it)
	{
		node = getSceneNodeFromName(name, *it);
		if (node)
			return node;
	}

	return 0;
}


//! Returns the first scene node with the specified id.
ISceneNode* CSceneManager::getSceneNodeFromId(s32 id, ISceneNode* start)
{
	if (start == 0)
		start = getRootSceneNode();

	if (start->getID() == id)
		return start;

	ISceneNode* node = 0;

	const core::list<ISceneNode*>& list = start->getChildren();
	core::list<ISceneNode*>::Iterator it = list.begin();
	for (; it!=list.end(); ++it)
	{
		node = getSceneNodeFromId(id, *it);
		if (node)
			return node;
	}

	return 0;
}


//! Posts an input event to the environment. Usually you do not have to
//! use this method, it is used by the internal engine.
bool CSceneManager::postEventFromUser(SEvent event)
{
	bool ret = false;
	ICameraSceneNode* cam = getActiveCamera();
	if (cam)
		ret = cam->OnEvent(event);

	_IRR_IMPLEMENT_MANAGED_MARSHALLING_BUGFIX;
	return ret;
}


//! Removes all children of this scene node
void CSceneManager::removeAll()
{
	ISceneNode::removeAll();
	setActiveCamera(0);
}


//! Clears the whole scene. All scene nodes are removed.
void CSceneManager::clear()
{
	removeAll();
}


//! Returns interface to the parameters set in this scene.
io::IAttributes* CSceneManager::getParameters()
{
	return &Parameters;
}


//! Returns current render pass.
E_SCENE_NODE_RENDER_PASS CSceneManager::getSceneNodeRenderPass()
{
	return CurrentRendertime;
}



//! Returns an interface to the mesh cache which is shared beween all existing scene managers.
IMeshCache* CSceneManager::getMeshCache()
{
	return MeshCache;
}


//! Creates a new scene manager.
ISceneManager* CSceneManager::createNewSceneManager()
{
	return new CSceneManager(Driver, FileSystem, CursorControl, MeshCache);
}


//! Returns the default scene node factory which can create all built in scene nodes
ISceneNodeFactory* CSceneManager::getDefaultSceneNodeFactory()
{
	return getSceneNodeFactory(0);
}


//! Adds a scene node factory to the scene manager.
void CSceneManager::registerSceneNodeFactory(ISceneNodeFactory* factoryToAdd)
{
	if (factoryToAdd)
	{
		factoryToAdd->grab();
		SceneNodeFactoryList.push_back(factoryToAdd);
	}
}


//! Returns amount of registered scene node factories.
s32 CSceneManager::getRegisteredSceneNodeFactoryCount()
{
	return SceneNodeFactoryList.size();
}


//! Returns a scene node factory by index
ISceneNodeFactory* CSceneManager::getSceneNodeFactory(s32 index)
{
	if (index>=0 && index<(int)SceneNodeFactoryList.size())
		return SceneNodeFactoryList[index];

	return 0;
}


//! Returns the default scene node animator factory which can create all built-in scene node animators
ISceneNodeAnimatorFactory* CSceneManager::getDefaultSceneNodeAnimatorFactory()
{
	return getSceneNodeAnimatorFactory(0);
}

//! Adds a scene node animator factory to the scene manager.
void CSceneManager::registerSceneNodeAnimatorFactory(ISceneNodeAnimatorFactory* factoryToAdd)
{
	if (factoryToAdd)
	{
		factoryToAdd->grab();
		SceneNodeAnimatorFactoryList.push_back(factoryToAdd);
	}
}


//! Returns amount of registered scene node animator factories.
s32 CSceneManager::getRegisteredSceneNodeAnimatorFactoryCount()
{
	return SceneNodeAnimatorFactoryList.size();
}


//! Returns a scene node animator factory by index
ISceneNodeAnimatorFactory* CSceneManager::getSceneNodeAnimatorFactory(s32 index)
{
	if (index>=0 && index<(int)SceneNodeAnimatorFactoryList.size())
		return SceneNodeAnimatorFactoryList[index];

	return 0;
}


//! Saves the current scene into a file.
//! \param filename: Name of the file .
bool CSceneManager::saveScene(const c8* filename, ISceneUserDataSerializer* userDataSerializer)
{
	io::IWriteFile* file = FileSystem->createAndWriteFile(filename);
	if (!file)
		return false;

	bool ret = saveScene(file, userDataSerializer);
	file->drop();
	return ret;
}


//! Saves the current scene into a file.
bool CSceneManager::saveScene(io::IWriteFile* file, ISceneUserDataSerializer* userDataSerializer)
{
	if (!file)
		return false;

	io::IXMLWriter* writer = FileSystem->createXMLWriter(file);
	if (!writer)
		return false;

	writer->writeXMLHeader();
    writeSceneNode(writer, this, userDataSerializer);
	writer->drop();

	return true;
}


//! Loads a scene. Note that the current scene is not cleared before.
//! \param filename: Name of the file .
bool CSceneManager::loadScene(const c8* filename, ISceneUserDataSerializer* userDataSerializer)
{
	io::IReadFile* read = FileSystem->createAndOpenFile(filename);
	if (!read)
	{
	    os::Printer::log("Unable to open scene file", filename, ELL_ERROR);
		return false;
	}

	bool ret = loadScene(read, userDataSerializer);
	read->drop();

	return ret;
}


//! Loads a scene. Note that the current scene is not cleared before.
bool CSceneManager::loadScene(io::IReadFile* file, ISceneUserDataSerializer* userDataSerializer)
{
	if (!file)
	{
	    os::Printer::log("Unable to open scene file", ELL_ERROR);
		return false;
	}

	io::IXMLReader* reader = FileSystem->createXMLReader(file);
	if (!reader)
	{
        os::Printer::log("Scene is not a valid XML file", file->getFileName(), ELL_ERROR);
		return false;
	}
	
	// for mesh loading, set collada loading attributes

	bool bOldColladaSingleMesh = getParameters()->getAttributeAsBool(COLLADA_CREATE_SCENE_INSTANCES);
	getParameters()->setAttribute(COLLADA_CREATE_SCENE_INSTANCES, false);

	// read file

	while(reader->read())
	{
		readSceneNode(reader, 0, userDataSerializer);
	}

	// restore old collada parameters

	getParameters()->setAttribute(COLLADA_CREATE_SCENE_INSTANCES, bOldColladaSingleMesh);

	// finish up

	reader->drop();
	return true;

}


//! reads a scene node
void CSceneManager::readSceneNode(io::IXMLReader* reader, ISceneNode* parent, ISceneUserDataSerializer* userDataSerializer)
{
	if (!reader)
		return;

	scene::ISceneNode* node = 0;

	if ((!parent && IRR_XML_FORMAT_SCENE==reader->getNodeName()) ||
		( parent && IRR_XML_FORMAT_NODE==reader->getNodeName()))
	{
		if (parent)
		{
			// find node type and create it
			core::stringc attrName = reader->getAttributeValue(IRR_XML_FORMAT_NODE_ATTR_TYPE.c_str());

			for (int i=0; i<(int)SceneNodeFactoryList.size() && !node; ++i)
				node = SceneNodeFactoryList[i]->addSceneNode(attrName.c_str(), parent);

			if (!node)
				os::Printer::log("Could not create scene node of unknown type", attrName.c_str());
		}
		else
			node = this; // root
	}

    // read attributes

	while(reader->read())
	{
		bool endreached = false;

		switch (reader->getNodeType())
		{
		case io::EXN_ELEMENT_END:
			if ((IRR_XML_FORMAT_NODE==reader->getNodeName()) ||
				(IRR_XML_FORMAT_SCENE==reader->getNodeName()))
			{
				endreached = true;
			}
			break;
		case io::EXN_ELEMENT:
			if (core::stringw(L"attributes")==reader->getNodeName())
			{
				// read attributes
				io::IAttributes* attr = FileSystem->createEmptyAttributes(Driver);
				attr->read(reader, true);

				if (node)
					node->deserializeAttributes(attr);

				attr->drop();
			}
			else
			if (core::stringw(L"materials")==reader->getNodeName())
				readMaterials(reader, node);
			else
			if (core::stringw(L"animators")==reader->getNodeName())
				readAnimators(reader, node);
			else
			if (core::stringw(L"userData")==reader->getNodeName())
				readUserData(reader, node, userDataSerializer);
			else
			if ((IRR_XML_FORMAT_NODE==reader->getNodeName()) ||
				(IRR_XML_FORMAT_SCENE==reader->getNodeName()))
			{
				readSceneNode(reader, node, userDataSerializer);
			}
			else
			{
				os::Printer::log("Found unknown element in irrlicht scene file",
								 core::stringc(reader->getNodeName()).c_str());
			}

			break;
		}

		if (endreached)
			break;
	}
}


//! reads materials of a node
void CSceneManager::readMaterials(io::IXMLReader* reader, ISceneNode* node)
{
	u32 nr = 0;

	while(reader->read())
	{
		const wchar_t* name = reader->getNodeName();

		switch(reader->getNodeType())
		{
		case io::EXN_ELEMENT_END:
			if (core::stringw(L"materials")==name)
				return;
			break;
		case io::EXN_ELEMENT:
			if (core::stringw(L"attributes")==name)
			{
				// read materials from attribute list
				io::IAttributes* attr = FileSystem->createEmptyAttributes(Driver);
				attr->read(reader);

				if (node && node->getMaterialCount() > nr)
				{
					getVideoDriver()->fillMaterialStructureFromAttributes(
						node->getMaterial(nr), attr);
				}

				attr->drop();
				++nr;
			}
			break;
		}
	}
}


//! reads animators of a node
void CSceneManager::readAnimators(io::IXMLReader* reader, ISceneNode* node)
{
	s32 cnt = 0;

	while(reader->read())
	{
		const wchar_t* name = reader->getNodeName();

		switch(reader->getNodeType())
		{
		case io::EXN_ELEMENT_END:
			if (core::stringw(L"animators")==name)
				return;
			break;
		case io::EXN_ELEMENT:
			if (core::stringw(L"attributes")==name)
			{
				// read animator data from attribute list
				io::IAttributes* attr = FileSystem->createEmptyAttributes(Driver);
				attr->read(reader);

				if (node)
				{
					core::stringc typeName = attr->getAttributeAsString("Type");
					ISceneNodeAnimator* anim = 0;

					for (int i=0; i<(int)SceneNodeAnimatorFactoryList.size() && !anim; ++i)
						anim = SceneNodeAnimatorFactoryList[i]->createSceneNodeAnimator(typeName.c_str(), node);

					if (anim)
					{
						anim->deserializeAttributes(attr);
						anim->drop();
					}
				}

				attr->drop();
				++cnt;
			}
			break;
		}
	}
}


//! reads user data of a node
void CSceneManager::readUserData(io::IXMLReader* reader, ISceneNode* node, ISceneUserDataSerializer* userDataSerializer)
{
	while(reader->read())
	{
		const wchar_t* name = reader->getNodeName();

		switch(reader->getNodeType())
		{
		case io::EXN_ELEMENT_END:
			if (core::stringw(L"userData")==name)
				return;
			break;
		case io::EXN_ELEMENT:
			if (core::stringw(L"attributes")==name)
			{
				// read user data from attribute list
				io::IAttributes* attr = FileSystem->createEmptyAttributes(Driver);
				attr->read(reader);

				if (node && userDataSerializer)
				{
					userDataSerializer->OnReadUserData(node, attr);
				}

				attr->drop();
			}
			break;
		}
	}
}


//! writes a scene node
void CSceneManager::writeSceneNode(io::IXMLWriter* writer, ISceneNode* node, ISceneUserDataSerializer* userDataSerializer)
{
	if (!writer || !node || node->isDebugObject())
		return;

	const wchar_t* name = 0;

	if (node == this)
	{
		name = IRR_XML_FORMAT_SCENE.c_str();
		writer->writeElement(name, false);
	}
	else
	{
		name = IRR_XML_FORMAT_NODE.c_str();
		writer->writeElement(name, false, IRR_XML_FORMAT_NODE_ATTR_TYPE.c_str(),
			core::stringw(getSceneNodeTypeName(node->getType())).c_str());
	}

	writer->writeLineBreak();
	writer->writeLineBreak();

	// write properties

	io::IAttributes* attr = FileSystem->createEmptyAttributes(Driver);
	node->serializeAttributes(attr);

	if (attr->getAttributeCount() > 0)
	{
		attr->write(writer);
		writer->writeLineBreak();
	}

	// write materials

	if (node->getMaterialCount() && getVideoDriver())
	{
		const wchar_t* materialElement = L"materials";

		writer->writeElement(materialElement);
		writer->writeLineBreak();

		for (u32 i=0; i < node->getMaterialCount(); ++i)
		{
			io::IAttributes* attr =
				getVideoDriver()->createAttributesFromMaterial(node->getMaterial(i));
			attr->write(writer);
			attr->drop();
		}

		writer->writeClosingTag(materialElement);
		writer->writeLineBreak();
	}

	// write animators

	if (!node->getAnimators().empty())
	{
		const wchar_t* animatorElement = L"animators";
		writer->writeElement(animatorElement);
		writer->writeLineBreak();

		core::list<ISceneNodeAnimator*>::Iterator it = node->getAnimators().begin();
		for (; it != node->getAnimators().end(); ++it)
		{
			attr->clear();
			attr->addString("Type", getAnimatorTypeName((*it)->getType()));

			(*it)->serializeAttributes(attr);

			attr->write(writer);
		}

		writer->writeClosingTag(animatorElement);
		writer->writeLineBreak();
	}

	// write possible user data

	if ( userDataSerializer )
	{
		io::IAttributes* userData = userDataSerializer->createUserData(node);
		if (userData)
		{
			const wchar_t* userDataElement = L"userData";

			writer->writeLineBreak();
			writer->writeElement(userDataElement);
			writer->writeLineBreak();

			userData->write(writer);

			writer->writeClosingTag(userDataElement);
			writer->writeLineBreak();
			writer->writeLineBreak();

			userData->drop();
		}
	}

	// write children

	core::list<ISceneNode*>::Iterator it = node->getChildren().begin();
	for (; it != node->getChildren().end(); ++it)
		writeSceneNode(writer, (*it), userDataSerializer);

	attr->drop();

	writer->writeClosingTag(name);
	writer->writeLineBreak();
	writer->writeLineBreak();
}


//! Returns a typename from a scene node type or null if not found
const c8* CSceneManager::getSceneNodeTypeName(ESCENE_NODE_TYPE type)
{
	const char* name = 0;

	for (int i=0; !name && i<(int)SceneNodeFactoryList.size(); ++i)
		name = SceneNodeFactoryList[i]->getCreateableSceneNodeTypeName(type);

	return name;
}

//! Returns a typename from a scene node animator type or null if not found
const c8* CSceneManager::getAnimatorTypeName(ESCENE_NODE_ANIMATOR_TYPE type)
{
	const char* name = 0;

	for (int i=0; !name && i<(int)SceneNodeAnimatorFactoryList.size(); ++i)
		name = SceneNodeAnimatorFactoryList[i]->getCreateableSceneNodeAnimatorTypeName(type);

	return name;
}
//! Writes attributes of the scene node.
void CSceneManager::serializeAttributes(io::IAttributes* out, io::SAttributeReadWriteOptions* options)
{
	out->addString	("Name", Name.c_str());
	out->addInt		("Id", ID );
	out->addColorf	("AmbientLight", AmbientLight);
}

//! Reads attributes of the scene node.
void CSceneManager::deserializeAttributes(io::IAttributes* in, io::SAttributeReadWriteOptions* options)
{
	Name =			in->getAttributeAsString("Name");
	ID =			in->getAttributeAsInt("Id");
	AmbientLight =	in->getAttributeAsColorf("AmbientLight");

	RelativeTranslation.set(0,0,0);
	RelativeRotation.set(0,0,0);
	RelativeScale.set(1,1,1);
	IsVisible = true;
	AutomaticCullingState = scene::EAC_BOX;
	DebugDataVisible = scene::EDS_OFF;
	IsDebugObject = false;

	updateAbsolutePosition();
}


//! Sets ambient color of the scene
void CSceneManager::setAmbientLight(const video::SColorf &ambientColor)
{
	AmbientLight = ambientColor;
}


//! Returns ambient color of the scene
video::SColorf CSceneManager::getAmbientLight()
{
	return AmbientLight;
}


// creates a scenemanager
ISceneManager* createSceneManager(video::IVideoDriver* driver, io::IFileSystem* fs,
								  gui::ICursorControl* cursorcontrol, 
								  gui::IGUIEnvironment *guiEnvironment )
{
	return new CSceneManager(driver, fs, cursorcontrol, 0, guiEnvironment );
}



} // end namespace scene
} // end namespace irr

