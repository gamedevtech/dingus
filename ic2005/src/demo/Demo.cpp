#include "stdafx.h"

// Demo application.
// Most of stuff is just kept in global variables :)

#include "Demo.h"
#include "DemoResources.h"
#include "Entity.h"
#include "MeshEntity.h"
#include "ComplexStuffEntity.h"
#include "ControllableCharacter.h"
#include "ThirdPersonCamera.h"

#include <dingus/gfx/DebugRenderer.h>
#include <dingus/gfx/GfxUtils.h>
#include <dingus/utils/Random.h>
#include <dingus/gfx/gui/Gui.h>
#include <dingus/math/MathUtils.h>
#include <dingus/math/Interpolation.h>
#include <dingus/resource/MeshCreator.h>
#include <dingus/gfx/geometry/DynamicVBManager.h>
#include <dingus/gfx/geometry/DynamicIBManager.h>
#include "PostProcess.h"

#include <dingus/utils/FixedRateProcess.h>


#include "wallz/WallPieces.h"
#include "wallz/WallFracturer.h"
#include "wallz/WallPhysics.h"
#include "wallz/FractureScenario.h"


// --------------------------------------------------------------------------
// Demo variables, constants, etc.

const char* RMODE_PREFIX[RMCOUNT] = {
	"normal/",
	"reflected/",
	"caster/",
};

CDebugRenderer*	gDebugRenderer;

int			gGlobalCullMode;	// global cull mode
int			gGlobalFillMode;	// global fill mode
SVector4	gScreenFixUVs;		// UV fixes for fullscreen quads
float		gTimeParam;			// time parameter for effects

bool	gNoPixelShaders = false;

bool	gFinished = false;
bool	gShowStats = false;


// --------------------------------------------------------------------------

CDemo::CDemo()
{
}

bool CDemo::checkDevice( const CD3DDeviceCaps& caps, CD3DDeviceCaps::eVertexProcessing vproc, CD3DEnumErrors& errors )
{
	bool ok = true;
	if( caps.getVShaderVersion() < CD3DDeviceCaps::VS_1_1 ) {
		if( vproc != CD3DDeviceCaps::VP_SW )
			ok = false;
	}
	// need float textures...
	//if( !caps.hasFloatTextures() ) {
	//	errors.addError( "Floating point rendertargets are required" );
	//	ok = false;
	//}
	
	return ok;
}

bool CDemo::shouldFinish()
{
	return gFinished;
}

bool CDemo::shouldShowStats()
{
	return gShowStats;
}


// --------------------------------------------------------------------------
//  GUI

CUIDialog*		gUIDlg;

CUIStatic*		gUILabFPS;


void CALLBACK gUICallback( UINT evt, int ctrlID, CUIControl* ctrl )
{
}

const int UIHLAB = 14;

static void	gSetupGUI()
{
	gUIDlg->addStatic( 0, "", 3, 480-UIHLAB-1, 500, UIHLAB, false, &gUILabFPS );
	gUIDlg->enableNonUserEvents( true );
}



// --------------------------------------------------------------------------
// Demo variables


CCameraEntity		gCamera;
CAnimationBunch*	gCameraAnim;
double				gCameraAnimStartTime;
double				gCameraAnimDuration;
CAnimationBunch::TVector3Animation*	gCameraAnimPos;
CAnimationBunch::TQuatAnimation*	gCameraAnimRot;
CAnimationBunch::TVector3Animation*	gCameraAnimParams;

double			gAnimFrameCount;
double			gCurrAnimFrame;
double			gCurrAnimAlpha;

bool			gInteractiveMode;

CComplexStuffEntity*	gBicas;
CControllableCharacter*	gBicasUser;
int						gBicasSpineBone;


float		gMouseX; // from -1 to 1
float		gMouseY; // from -1 to 1
SVector3	gMouseRay;

CThirdPersonCameraController*	gCameraController;


const float PHYS_UPDATE_FREQ = 60.0f;
const float PHYS_UPDATE_DT = 1.0f / PHYS_UPDATE_FREQ;



static const SVector3 ROOM_MIN = SVector3( -4.907f, 0.000f, -3.096f );
static const SVector3 ROOM_MAX = SVector3(  4.820f, 3.979f,  4.726f );
static const SVector3 ROOM_MID = (ROOM_MIN + ROOM_MAX)*0.5f;
static const SVector3 ROOM_SIZE = (ROOM_MAX - ROOM_MIN);
static const SVector3 ROOM_HSIZE = ROOM_SIZE*0.5f;

const char*		WALL_TEXS[CFACE_COUNT] = { RT_REFL_PX, RT_REFL_NX, RT_REFL_PY, RT_REFL_NY, RT_REFL_PZ, RT_REFL_NZ };

CCameraEntity	gWallCamera;
SMatrix4x4		gCameraViewProjMatrix;
SMatrix4x4		gViewTexProjMatrix;
CPostProcess*	gPPReflBlur;



CWall3D*			gWalls[CFACE_COUNT];
std::vector<int>	gMousePieces[CFACE_COUNT];

int		gWallVertCount, gWallTriCount;


fastvector<CMeshEntity*>	gPieces;


CRenderableMesh*	gQuadGaussX;
CRenderableMesh*	gQuadGaussY;
CRenderableMesh*	gQuadBlur;



struct SShadowLight {
public:
	void initialize( const SVector3& pos, const SVector3& lookAt ) {
		SMatrix4x4 viewMat;
		D3DXMatrixLookAtLH( &viewMat, &pos, &lookAt, &SVector3(1,0,0) );
		D3DXMatrixInverse( &camera.mWorldMat, NULL, &viewMat );
		camera.setProjectionParams( D3DX_PI/5, 1.0f, pos.y*0.1f, pos.y*2.0f );
		viewProj = viewMat * camera.getProjectionMatrix();
	}

public:
	CCameraEntity	camera;
	SMatrix4x4		viewProj;
};


static void gRenderScene( eRenderMode rm )
{
	int i;
	if( gInteractiveMode )
		gBicasUser->render( rm );
	else
		gBicas->render( rm );
	for( i = 0; i < gPieces.size(); ++i )
		gPieces[i]->render( rm );
	for( i = 0; i < CFACE_COUNT; ++i ) {
		gWalls[i]->render( rm );
	}
	
	wall_phys::render( rm );
}


// --------------------------------------------------------------------------
// Shadow mapping


const SVector3 LIGHT_POS = SVector3( ROOM_MID.x, ROOM_MAX.y*1.5f, ROOM_MID.z );

SShadowLight	gSLight;
SMatrix4x4		gSLightVP;
SMatrix4x4		gSShadowProj;
SVector3		gSLightPos = LIGHT_POS;



void gShadowRender()
{
	CD3DDevice& dx = CD3DDevice::getInstance();

	// target light at the guy
	CComplexStuffEntity* targetEntity = gInteractiveMode ? gBicasUser : gBicas;

	SVector3 lookAt = targetEntity->getAnimator().getBoneWorldMatrices()[gBicasSpineBone].getOrigin();
	gSLight.initialize( LIGHT_POS, lookAt );


	// Leave one texel padding...
	D3DVIEWPORT9 vp;
	vp.X = vp.Y = 1; vp.Height = vp.Width = SZ_SHADOWMAP-2;
	vp.MinZ = 0.0f; vp.MaxZ = 1.0f;

	gSLight.camera.setOntoRenderContext();

	gSLightVP = gSLight.viewProj;
	gSLightPos = gSLight.camera.mWorldMat.getOrigin();
	gfx::textureProjectionWorld( gSLightVP, SZ_SHADOWMAP, SZ_SHADOWMAP, gSShadowProj );

	// render shadow map
	dx.setZStencil( RGET_S_SURF(RT_SHADOWZ) );
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWMAP) );
	//dx.clearTargets( true, true, false, 0xFFffffff, 0.0f ); // min based dilation
	dx.clearTargets( true, true, false, 0xFFff00ff, 0.0f ); // gauss

	dx.getDevice().SetViewport( &vp );

	dx.sceneBegin();
	targetEntity->render( RM_CASTERSIMPLE );
	G_RENDERCTX->applyGlobalEffect();

	dx.getStateManager().SetRenderState( D3DRS_ZFUNC, D3DCMP_GREATER );
	G_RENDERCTX->perform();
	dx.getStateManager().SetRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );

	dx.sceneEnd();

	// process shadowmap
	dx.setZStencil( NULL );

	/*
	// dilate shadowmap -> shadowblur
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWBLUR) );
	dx.sceneBegin();
	G_RENDERCTX->attach( *gQuadDilateX );
	G_RENDERCTX->perform();
	dx.sceneEnd();
	// dilate shadowblur -> shadowmap
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWMAP) );
	dx.sceneBegin();
	G_RENDERCTX->attach( *gQuadDilateY );
	G_RENDERCTX->perform();
	dx.sceneEnd();
	*/
	// gaussX shadowmap -> shadowblur
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWBLUR) );
	dx.sceneBegin();
	G_RENDERCTX->attach( *gQuadGaussX );
	G_RENDERCTX->perform();
	dx.sceneEnd();
	// gaussY shadowblur -> shadowmap
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWMAP) );
	dx.sceneBegin();
	G_RENDERCTX->attach( *gQuadGaussY );
	G_RENDERCTX->perform();
	dx.sceneEnd();
	// blur shadowmap -> shadowblur
	dx.setRenderTarget( RGET_S_SURF(RT_SHADOWBLUR) );
	dx.clearTargets( true, false, false, 0xFFffffff );
	dx.getDevice().SetViewport( &vp );
	dx.sceneBegin();
	G_RENDERCTX->attach( *gQuadBlur );
	G_RENDERCTX->perform();
	dx.sceneEnd();
}


// --------------------------------------------------------------------------
// reflective walls


void gRenderWallReflections()
{
	SVector3 planePos[CFACE_COUNT] = {
		SVector3(ROOM_MAX.x,0,0), SVector3(ROOM_MIN.x,0,0),
		SVector3(0,ROOM_MAX.y,0), SVector3(0,ROOM_MIN.y,0),
		SVector3(0,0,ROOM_MAX.z), SVector3(0,0,ROOM_MIN.z),
	};
	SVector3 planeNrm[CFACE_COUNT] = {
		SVector3(-1,0,0), SVector3(1,0,0),
		SVector3(0,-1,0), SVector3(0,1,0),
		SVector3(0,0,-1), SVector3(0,0,1),
	};

	int oldCull = gGlobalCullMode;
	gGlobalCullMode = D3DCULL_CCW;
	
	for( int currWall = 0; currWall < CFACE_COUNT; ++currWall ) {
		//gWallMeshes[currWall]->updateWVPMatrices();
		//if( gWallMeshes[currWall]->frustumCull(gCameraViewProjMatrix) )
		//	continue;

		SPlane reflPlane( planePos[currWall] + planeNrm[currWall]*0.05f, planeNrm[currWall] );
		SMatrix4x4 reflectMat;
		D3DXMatrixReflect( &reflectMat, &reflPlane );
		
		gWallCamera.mWorldMat = gCamera.mWorldMat * reflectMat;
		gWallCamera.setProjFrom( gCamera );
		gWallCamera.setOntoRenderContext();

		CD3DDevice& dx = CD3DDevice::getInstance();
		dx.setRenderTarget( RGET_S_SURF(RT_REFLRT) );
		dx.setZStencil( RGET_S_SURF(RT_REFLZ) );
		dx.clearTargets( true, true, false, 0xFF000020, 1.0f );
		dx.sceneBegin();
		G_RENDERCTX->applyGlobalEffect();
		gRenderScene( RM_REFLECTED );
		G_RENDERCTX->perform();

		dx.sceneEnd();

		// Now reflected stuff is in RT_REFLRT surface. Blur it!
		gPPReflBlur->downsampleRT( *RGET_S_SURF(RT_REFLRT)->getObject() );
		dx.getStateManager().SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
		gPPReflBlur->pingPongBlur( 2 );
		dx.getStateManager().SetRenderState( D3DRS_CULLMODE, gGlobalCullMode );

		// Now blurred stuff is in RT_REFL_TMP1. Copy it to our needed texture.
		IDirect3DSurface9* surf;
		RGET_S_TEX(WALL_TEXS[currWall])->getObject()->GetSurfaceLevel( 0, &surf );
		dx.getDevice().StretchRect( RGET_S_SURF(RT_REFL_TMP1)->getObject(), NULL, surf, NULL, dx.getCaps().getStretchFilter() );
		surf->Release();
	}

	gGlobalCullMode = oldCull;
}


// --------------------------------------------------------------------------
// Initialization


void CDemo::initialize( IDingusAppContext& appContext )
{
	int i;
	
	CSharedTextureBundle& stb = CSharedTextureBundle::getInstance();
	CSharedSurfaceBundle& ssb = CSharedSurfaceBundle::getInstance();

	CD3DDevice& dx = CD3DDevice::getInstance();

	G_INPUTCTX->addListener( *this );

	gNoPixelShaders = (dx.getCaps().getPShaderVersion() < CD3DDeviceCaps::PS_1_1);

	// --------------------------------
	// render targets

	// shadow maps
	if( !gNoPixelShaders ) {
		ITextureCreator* shadowT = new CFixedTextureCreator(
			SZ_SHADOWMAP, SZ_SHADOWMAP, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT );
		ITextureCreator* shadowTMip = new CFixedTextureCreator(
			SZ_SHADOWMAP, SZ_SHADOWMAP, 0, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT );
		stb.registerTexture( RT_SHADOWMAP, *shadowT );
		stb.registerTexture( RT_SHADOWBLUR, *shadowTMip );
		ssb.registerSurface( RT_SHADOWMAP, *(new CTextureLevelSurfaceCreator(*RGET_S_TEX(RT_SHADOWMAP),0)) );
		ssb.registerSurface( RT_SHADOWBLUR, *(new CTextureLevelSurfaceCreator(*RGET_S_TEX(RT_SHADOWBLUR),0)) );
		ssb.registerSurface( RT_SHADOWZ, *(new CFixedSurfaceCreator(SZ_SHADOWMAP,SZ_SHADOWMAP,true,D3DFMT_D16)) );

		G_RENDERCTX->getGlobalParams().addTexture( "tShadow", *RGET_S_TEX(RT_SHADOWBLUR) );
	}
	
	// reflections
	if( !gNoPixelShaders ) {
		ISurfaceCreator* rtcreatReflRT = new CScreenBasedSurfaceCreator(
			SZ_REFLRT_REL, SZ_REFLRT_REL, false, D3DFMT_A8R8G8B8, false );
		ISurfaceCreator* rtcreatReflZ = new CScreenBasedSurfaceCreator(
			SZ_REFLRT_REL, SZ_REFLRT_REL, true, D3DFMT_D16, false );
		ITextureCreator* rtcreatReflBlur = new CScreenBasedTextureCreator(
			SZ_REFLBLUR_REL, SZ_REFLBLUR_REL, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT );
		stb.registerTexture( RT_REFL_PX, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_NX, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_PY, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_NY, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_PZ, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_NZ, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_TMP1, *rtcreatReflBlur );
		stb.registerTexture( RT_REFL_TMP2, *rtcreatReflBlur );
		ssb.registerSurface( RT_REFLRT, *rtcreatReflRT );
		ssb.registerSurface( RT_REFLZ, *rtcreatReflZ );
		ssb.registerSurface( RT_REFL_TMP1, *(new CTextureLevelSurfaceCreator(*RGET_S_TEX(RT_REFL_TMP1),0)) );
		ssb.registerSurface( RT_REFL_TMP2, *(new CTextureLevelSurfaceCreator(*RGET_S_TEX(RT_REFL_TMP2),0)) );
	}

	// --------------------------------
	// common params

	gGlobalCullMode = D3DCULL_CW;
	gGlobalFillMode = D3DFILL_SOLID;
	G_RENDERCTX->getGlobalParams().addIntRef( "iCull", &gGlobalCullMode );
	G_RENDERCTX->getGlobalParams().addIntRef( "iFill", &gGlobalFillMode );
	G_RENDERCTX->getGlobalParams().addFloatRef( "fTime", &gTimeParam );

	G_RENDERCTX->getGlobalParams().addMatrix4x4Ref( "mViewTexProj", gViewTexProjMatrix );
	G_RENDERCTX->getGlobalParams().addMatrix4x4Ref( "mShadowProj", gSShadowProj );
	G_RENDERCTX->getGlobalParams().addMatrix4x4Ref( "mLightViewProj", gSLightVP );
	G_RENDERCTX->getGlobalParams().addVector3Ref( "vLightPos", gSLightPos );

	gDebugRenderer = new CDebugRenderer( *G_RENDERCTX, *RGET_FX("debug") );

	// --------------------------------
	// GUI

	gUIDlg = new CUIDialog();
	//gUIDlg->enableKeyboardInput( true );
	gUIDlg->setCallback( gUICallback );
	gUIDlg->setFont( 1, "Arial", 22, 50 );

	gSetupGUI();

	// --------------------------------
	// scene

	// guy
	gBicas = new CComplexStuffEntity( "BicasAnim" );

	const float WALK_BOUNDS = 0.9f;
	gBicasUser = new CControllableCharacter( ROOM_MIN.x+WALK_BOUNDS, ROOM_MIN.z+WALK_BOUNDS, ROOM_MAX.x-WALK_BOUNDS, ROOM_MAX.z-WALK_BOUNDS );
	gBicasSpineBone = gBicasUser->getAnimator().getCurrAnim()->getCurveIndexByName( "Spine" );

	const float CAMERA_BOUND = 0.15f;
	SVector3 CAMERA_BOUND_MIN = ROOM_MIN + SVector3(CAMERA_BOUND,CAMERA_BOUND,CAMERA_BOUND);
	SVector3 CAMERA_BOUND_MAX = ROOM_MAX - SVector3(CAMERA_BOUND,CAMERA_BOUND,CAMERA_BOUND);
	gCameraController = new CThirdPersonCameraController( gBicasUser->getWorldMatrix(), gCamera.mWorldMat, CAMERA_BOUND_MIN, CAMERA_BOUND_MAX );

	// test objects
	//gPieces.push_back( new CMeshEntity("Box10") );
	//gPieces.push_back( new CMeshEntity("SmallRozete") );
	//gPieces.push_back( new CMeshEntity("BigRosette") );
	//gPieces.push_back( new CMeshEntity("FrontRosette") );

	// walls
	{
		const float ELEM_SIZE = 0.1f; // 0.1f

		gWalls[CFACE_PX] = new CWall3D( SVector2(ROOM_SIZE.z,ROOM_SIZE.y), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_PX] );
		gWalls[CFACE_NX] = new CWall3D( SVector2(ROOM_SIZE.z,ROOM_SIZE.y), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_NX] );
		gWalls[CFACE_PY] = new CWall3D( SVector2(ROOM_SIZE.x,ROOM_SIZE.z), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_PY] );
		gWalls[CFACE_NY] = new CWall3D( SVector2(ROOM_SIZE.x,ROOM_SIZE.z), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_NY] );
		gWalls[CFACE_PZ] = new CWall3D( SVector2(ROOM_SIZE.x,ROOM_SIZE.y), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_PZ] );
		gWalls[CFACE_NZ] = new CWall3D( SVector2(ROOM_SIZE.x,ROOM_SIZE.y), ELEM_SIZE, gNoPixelShaders ? NULL : WALL_TEXS[CFACE_NZ] );

		SMatrix4x4 wm;
		wm.identify();
		
		wm.getAxisX().set( 0, 0, 1 );
		wm.getAxisY().set( 0, 1, 0 );
		wm.getAxisZ().set( -1, 0, 0 );
		wm.getOrigin().set( ROOM_MAX.x, ROOM_MIN.y, ROOM_MIN.z );
		gWalls[CFACE_PX]->setMatrix( wm );
		wm.getAxisX().set( 0, 0, -1 );
		wm.getAxisY().set( 0, 1, 0 );
		wm.getAxisZ().set( 1, 0, 0 );
		wm.getOrigin().set( ROOM_MIN.x, ROOM_MIN.y, ROOM_MAX.z );
		gWalls[CFACE_NX]->setMatrix( wm );
		wm.getAxisX().set( 1, 0, 0 );
		wm.getAxisY().set( 0, 0, 1 );
		wm.getAxisZ().set( 0, -1, 0 );
		wm.getOrigin().set( ROOM_MIN.x, ROOM_MAX.y, ROOM_MIN.z );
		gWalls[CFACE_PY]->setMatrix( wm );
		wm.getAxisX().set( 1, 0, 0 );
		wm.getAxisY().set( 0, 0, -1 );
		wm.getAxisZ().set( 0, 1, 0 );
		wm.getOrigin().set( ROOM_MIN.x, ROOM_MIN.y, ROOM_MAX.z );
		gWalls[CFACE_NY]->setMatrix( wm );
		wm.getAxisX().set( -1, 0, 0 );
		wm.getAxisY().set( 0, 1, 0 );
		wm.getAxisZ().set( 0, 0, -1 );
		wm.getOrigin().set( ROOM_MAX.x, ROOM_MIN.y, ROOM_MAX.z );
		gWalls[CFACE_PZ]->setMatrix( wm );
		wm.getAxisX().set( 1, 0, 0 );
		wm.getAxisY().set( 0, 1, 0 );
		wm.getAxisZ().set( 0, 0, 1 );
		wm.getOrigin().set( ROOM_MIN.x, ROOM_MIN.y, ROOM_MIN.z );
		gWalls[CFACE_NZ]->setMatrix( wm );

		gReadFractureScenario( "data/fractures.txt" );

		for( i = 0; i < CFACE_COUNT; ++i )
			wallFractureCompute( gWalls[i]->getWall2D() );

		wall_phys::initialize( PHYS_UPDATE_DT, ROOM_MIN-SVector3(1.0f,1.0f,1.0f), ROOM_MAX+SVector3(1.0f,1.0f,1.0f) );

		for( i = 0; i < CFACE_COUNT; ++i ) {
			wall_phys::addWall( *gWalls[i] );
		}

		for( i = 0; i < CFACE_COUNT; ++i )
			gWalls[i]->update( 0.0f );
	}

	if( !gNoPixelShaders ) {
		// post processes
		gPPReflBlur = new CPostProcess( RT_REFL_TMP1, RT_REFL_TMP2 );

		// gauss X shadowmap -> shadowblur
		gQuadGaussX = new CRenderableMesh( *RGET_MESH("billboard"), 0, NULL, 0 );
		gQuadGaussX->getParams().setEffect( *RGET_FX("filterGaussX") );
		gQuadGaussX->getParams().addTexture( "tBase", *RGET_S_TEX(RT_SHADOWMAP) );
		// gauss Y shadowblur -> shadowmap
		gQuadGaussY = new CRenderableMesh( *RGET_MESH("billboard"), 0, NULL, 0 );
		gQuadGaussY->getParams().setEffect( *RGET_FX("filterGaussY") );
		gQuadGaussY->getParams().addTexture( "tBase", *RGET_S_TEX(RT_SHADOWBLUR) );
		// blur shadowmap -> shadowblur
		gQuadBlur = new CRenderableMesh( *RGET_MESH("billboard"), 0, NULL, 0 );
		gQuadBlur->getParams().setEffect( *RGET_FX("filterPoisson") );
		gQuadBlur->getParams().addTexture( "tBase", *RGET_S_TEX(RT_SHADOWMAP) );
	}

	double curTime = anim_time();
	//const double HACK_OFFSET_ANIM = -40.0;
	const double HACK_OFFSET_ANIM = 0.0;

	gBicas->getAnimator().playDefaultAnim( curTime + HACK_OFFSET_ANIM );

	gCamera.mWorldMat.identify();
	gCameraAnimStartTime = curTime + HACK_OFFSET_ANIM;
	gCameraAnim = RGET_ANIM("Camera");
	gCameraAnimDuration = gGetAnimDuration( *gCameraAnim, false );

	gCameraAnimPos = gCameraAnim->findVector3Anim("pos");
	gCameraAnimRot = gCameraAnim->findQuatAnim("rot");
	gCameraAnimParams = gCameraAnim->findVector3Anim("cam");
	
	gAnimFrameCount = gCameraAnimPos->getLength();
}



// --------------------------------------------------------------------------
// Perform code (main loop)


class CPhysicsProcess : public CFixedRateProcess {
public:
	CPhysicsProcess() : CFixedRateProcess( PHYS_UPDATE_FREQ, 50 ) { }
protected:
	virtual void performProcess() {
		wall_phys::update();
	};
};

CPhysicsProcess	gPhysProcess;


static void gFetchMousePieces( bool fractureOut )
{
	int i;

	gMouseRay = gCamera.getWorldRay( gMouseX, gMouseY );
	const SVector3& eyePos = gCamera.mWorldMat.getOrigin();
	SLine3 mouseRay;
	mouseRay.pos = eyePos;
	mouseRay.vec = gMouseRay;

	// intersect mouse with walls
	float minWallT = 1.0e6f;
	for( i = 0; i < CFACE_COUNT; ++i ) {
		float t;
		bool ok = gWalls[i]->intersectRay( mouseRay, t );
		if( ok && t < minWallT )
			minWallT = t;
	}
	SVector3 mousePos = eyePos + gMouseRay * minWallT;
	const float MOUSE_RADIUS = 0.6f;

	double t = CSystemTimer::getInstance().getTimeS();
	if( fractureOut ) {
		CConsole::CON_WARNING << mousePos << endl;
	}
	for( i = 0; i < CFACE_COUNT; ++i ) {
		gWalls[i]->fracturePiecesInSphere( t, fractureOut, mousePos, MOUSE_RADIUS, gMousePieces[i] );
	}
}


bool CDemo::msgProc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
	if( !gUIDlg )
		return false;

	bool done = false;

	done = gUIDlg->msgProc( hwnd, msg, wparam, lparam );
	if( done )
		return true;
	
	// track mouse...
	if( msg == WM_LBUTTONDOWN ) {
		gFetchMousePieces( true );
		for( int i = 0; i < CFACE_COUNT; ++i ) {
			int n = gMousePieces[i].size();
			for( int j = 0; j < n; ++j ) {
				wall_phys::spawnPiece( i, gMousePieces[i][j] );
			}
		}
	}
	if( msg == WM_MOUSEMOVE ) {
		CD3DDevice& dx = CD3DDevice::getInstance();
		gMouseX = (float(LOWORD(lparam)) / dx.getBackBufferWidth()) * 2 - 1;
		gMouseY = (float(HIWORD(lparam)) / dx.getBackBufferHeight()) * 2 - 1;
	}
	
	return false;
}

static float	gInputTargetMoveSpeed = 0.0f;
static float	gInputTargetRotpeed = 0.0f;


void CDemo::onInputEvent( const CInputEvent& event )
{
	static bool shiftPressed = false;
	float dt = CSystemTimer::getInstance().getDeltaTimeS();

	if( event.getType() == CKeyEvent::EVENT_TYPE ) {
		const CKeyEvent& ke = (const CKeyEvent&)event;
		switch( ke.getKeyCode() ) {
		case DIK_LSHIFT:
		case DIK_RSHIFT:
			shiftPressed = (ke.getMode() != CKeyEvent::KEY_RELEASED);
			break;

		case DIK_9:
			if( ke.getMode() == CKeyEvent::KEY_PRESSED )
				gShowStats = !gShowStats;
			break;
		case DIK_RETURN:
			if( ke.getMode() == CKeyEvent::KEY_PRESSED ) {
				gInteractiveMode = !gInteractiveMode;
			}
			break;
		case DIK_SPACE:
			if( ke.getMode() == CKeyEvent::KEY_PRESSED ) {
				if( gInteractiveMode )
					gBicasUser->attack();
			}
			break;
		case DIK_LEFT:
			if( gInteractiveMode )
				gInputTargetRotpeed -= 3.0f;
			break;
		case DIK_RIGHT:
			if( gInteractiveMode )
				gInputTargetRotpeed += 3.0f;
			break;
		case DIK_UP:
			if( gInteractiveMode ) {
				gInputTargetMoveSpeed += shiftPressed ? 0.8f : 0.2f;
			}
			break;
		case DIK_DOWN:
			if( gInteractiveMode ) {
				gInputTargetMoveSpeed -= 0.2f;
			}
			break;
		/*
		case DIK_A:
			if( gCameraFollow ) {
				gCameraDist += dt*4;
				gCameraDist = clamp( gCameraDist, 0.5f, 7.0f );
			} else {
				gCamera.mWorldMat.getOrigin() += gCamera.mWorldMat.getAxisZ() * dt * 3;
			}
			break;
		case DIK_Z:
			if( gCameraFollow ) {
				gCameraDist -= dt*4;
				gCameraDist = clamp( gCameraDist, 0.5f, 7.0f );
			} else {
				gCamera.mWorldMat.getOrigin() -= gCamera.mWorldMat.getAxisZ() * dt * 3;
			}
			break;
		*/
		}
	}
}

static char gMoveDebugBuf[1000];

void CDemo::onInputStage()
{
	gBicasUser->move( gInputTargetMoveSpeed, gMoveDebugBuf );
	gBicasUser->rotate( gInputTargetRotpeed );
	gInputTargetRotpeed = 0.0f;
	gInputTargetMoveSpeed = 0.0f;
}



static const float CAM_C0_FRAMES[] = {
	-619-150, -476-150, -79-150, 
	372, 502, 630, 1056, 1144, 1287,
	1390, 1680, 2070,
	2162, 2433, 2497, 2562, 2669, 2722,
	2836, 3018, 3152, 3216, 3247, 3339,
	3519, 3609, 3801, 3883, 4027, 4160,
	4331, 4471, 4700, 4885, 4955, 5063,
	5154, 5252, 5347, 5554, 
};
static const int CAM_C0_FRAMES_SIZE = sizeof(CAM_C0_FRAMES) / sizeof(CAM_C0_FRAMES[0]);
static const int CAM_C0_ADD = 950;


static void gAnimateCamera()
{
	gCamera.mWorldMat.identify();
	gCamera.mWorldMat.getOrigin().set( 0, 1.0f, -3.0f );


	SVector3 camPos;
	SQuaternion camRot;
	SVector3 camParams;

	int c0idx = -1;
	for( int i = 0; i < CAM_C0_FRAMES_SIZE; ++i ) {
		float fr = CAM_C0_FRAMES[i]+CAM_C0_ADD;
		if( gCurrAnimFrame >= fr-2 && gCurrAnimFrame <= fr ) {
			c0idx = i;
			break;
		}
	}
	if( c0idx < 0 ) {
		gCameraAnimPos->sample( gCurrAnimAlpha, 0, 1, &camPos );
		gCameraAnimRot->sample( gCurrAnimAlpha, 0, 1, &camRot );
		gCameraAnimParams->sample( gCurrAnimAlpha, 0, 1, &camParams );
	} else {
		SVector3 pos1, pos2;
		SQuaternion rot1, rot2;
		SVector3 params1, params2;
		double a1 = gCurrAnimAlpha - (3.0/gAnimFrameCount);
		double a2 = gCurrAnimAlpha - (2.5/gAnimFrameCount);
		double lerper = (gCurrAnimAlpha-a1) / (a2-a1);
		gCameraAnimPos->sample( a1, 0, 1, &pos1 );
		gCameraAnimPos->sample( a2, 0, 1, &pos2 );
		gCameraAnimRot->sample( a1, 0, 1, &rot1 );
		gCameraAnimRot->sample( a2, 0, 1, &rot2 );
		gCameraAnimParams->sample( a1, 0, 1, &params1 );
		gCameraAnimParams->sample( a2, 0, 1, &params2 );
		camPos = math_lerp<SVector3>( pos1, pos2, lerper );
		camRot = math_lerp<SQuaternion>( rot1, rot2, lerper );
		camParams = math_lerp<SVector3>( params1, params2, lerper );
	}

	const float fov = camParams.z;

	SMatrix4x4 mr;
	D3DXMatrixRotationX( &mr, D3DX_PI/2 );
	gCamera.mWorldMat = mr * SMatrix4x4( camPos, camRot );

	const float camnear = 0.1f; // not from animation, just hardcoded
	const float camfar = 50.0f;

	float aspect = CD3DDevice::getInstance().getBackBufferAspect();
	gCamera.setProjectionParams( fov / aspect, aspect, camnear, camfar );
}


/**
 *  Main loop code.
 */
void CDemo::perform()
{
	int i;
	char buf[1000];

	CDynamicVBManager::getInstance().discard();
	CDynamicIBManager::getInstance().discard();

	gPhysProcess.perform();
	
	G_INPUTCTX->perform();
	
	double t = CSystemTimer::getInstance().getTimeS();
	float dt = CSystemTimer::getInstance().getDeltaTimeS();
	gTimeParam = float(t);


	double animPlayTime = anim_time() - gCameraAnimStartTime;
	gCurrAnimAlpha = animPlayTime / gCameraAnimDuration;
	gCurrAnimFrame = gCurrAnimAlpha * gAnimFrameCount;



	gWallVertCount = gWallTriCount = 0;
	
	gBicas->update();
	gBicasUser->update();
	gUpdateFractureScenario( gCurrAnimFrame, animPlayTime, gWalls );

	for( i = 0; i < CFACE_COUNT; ++i ) {
		gWalls[i]->update( animPlayTime );
	}
	
	

	CD3DDevice& dx = CD3DDevice::getInstance();
	
	gScreenFixUVs.set( 0.5f/dx.getBackBufferWidth(), 0.5f/dx.getBackBufferHeight(), 0.0f, 0.0f );
	
	if( gInteractiveMode ) {

		gCameraController->update( dt );

		const float camnear = 0.1f;
		const float camfar = 50.0f;
		const float camfov = D3DX_PI/4;
		gCamera.setProjectionParams( camfov, dx.getBackBufferAspect(), camnear, camfar );

	} else {

		gAnimateCamera();

	}

	
	// FPS
	static float maxMsColl = 0;
	static float maxMsPhys = 0;
	static const int MAGIC_COUNT = 1550;
	const wall_phys::SStats& stats = wall_phys::getStats();

	if( stats.msColl > maxMsColl && stats.pieceCount < MAGIC_COUNT )
		maxMsColl = stats.msColl;
	if( stats.msPhys > maxMsPhys && stats.pieceCount < MAGIC_COUNT )
		maxMsPhys = stats.msPhys;
	sprintf( buf, "fps=%.1f  frame=%.1f  phys: c=%.1f (%.1f) p=%.1f (%.1f) u=%.1f ms  pieces: %i",
		dx.getStats().getFPS(),
		gCurrAnimFrame,
		stats.msColl,
		maxMsColl,
		stats.msPhys,
		maxMsPhys,
		stats.msUpdate,
		stats.pieceCount
	);
	gUILabFPS->setText( buf );

	gFetchMousePieces( false );

	
	if( !gNoPixelShaders ) {
		// render shadow map
		gShadowRender();
		// render wall reflections
		gRenderWallReflections();
	}

	gCamera.setOntoRenderContext();
	gCameraViewProjMatrix = G_RENDERCTX->getCamera().getViewProjMatrix();
	gfx::textureProjectionWorld( gCameraViewProjMatrix, 1000.0f, 1000.0f, gViewTexProjMatrix );

	dx.setDefaultRenderTarget();
	dx.setDefaultZStencil();
	dx.clearTargets( true, true, false, 0xFF000080, 1.0f, 0L );

	dx.sceneBegin();
	G_RENDERCTX->applyGlobalEffect();
	gRenderScene( RM_NORMAL );

	G_RENDERCTX->perform();
	
	//for( i = 0; i < CFACE_COUNT; ++i ) {
		//gWalls[i]->debugRender( *gDebugRenderer );
		//gWalls[i]->debugRender( *gDebugRenderer, gMousePieces[i] );
	//}

	// render GUI
	gUIDlg->onRender( dt );
	dx.sceneEnd();

	
	static int maxVerts = 0;
	if( gWallVertCount + stats.vertexCount > maxVerts )
		maxVerts = gWallVertCount + stats.vertexCount;
	static int maxTris = 0;
	if( gWallTriCount + stats.triCount > maxTris )
		maxTris = gWallTriCount + stats.triCount;

	if( gShowStats ) {
		CConsole::getChannel("system") << "wall geom: verts=" << gWallVertCount << " tris=" << gWallTriCount << endl;
		CConsole::getChannel("system") << "phys geom: verts=" << stats.vertexCount << " tris=" << stats.triCount << endl;
		CConsole::getChannel("system") << "max: verts=" << maxVerts << " (" << int(maxVerts*sizeof(SVertexXyzDiffuse)) << ")  tris=" << maxTris << " (" << maxTris*2*3 << ")" << endl;
	}
}



// --------------------------------------------------------------------------
// Cleanup


void CDemo::shutdown()
{
	int i;

	wall_phys::shutdown();
	
	for( i = 0; i < CFACE_COUNT; ++i )
		delete gWalls[i];

	delete gDebugRenderer;

	safeDelete( gUIDlg );
	safeDelete( gPPReflBlur );
	delete gBicas;
	delete gBicasUser;
	delete gCameraController;
}
