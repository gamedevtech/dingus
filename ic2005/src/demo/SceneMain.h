#ifndef __SCENE_MAIN_H
#define __SCENE_MAIN_H

#include "Scene.h"


// --------------------------------------------------------------------------

class CSceneMain : public CScene {
public:
	CSceneMain( CSceneSharedStuff* sharedStuff );
	~CSceneMain();

	virtual void	update( time_value demoTime, float dt );
	virtual void	render( eRenderMode renderMode );
	virtual const SMatrix4x4* getLightTargetMatrix() const;
	bool	isEnded() const { return mCurrAnimAlpha >= 1.0; }

private:
	void	animateCamera();

private:
	CSceneSharedStuff*	mSharedStuff;

	// camera anim
	CAnimationBunch*	mCameraAnim;
	CAnimationBunch::TVector3Animation*	mCameraAnimPos;
	CAnimationBunch::TQuatAnimation*	mCameraAnimRot;
	CAnimationBunch::TVector3Animation*	mCameraAnimParams;

	// timing
	double	mAnimFrameCount;
	double	mAnimDuration;
	double	mCurrAnimFrame;
	double	mCurrAnimAlpha;

	// main character
	CComplexStuffEntity*	mCharacter;
	int			mSpineBoneIndex;

	// other characters
	CComplexStuffEntity*	mCharacter2;
	CComplexStuffEntity*	mCharacter3;

	// bed/stone
	CMeshEntity*			mBedStatic;
	CComplexStuffEntity*	mBedAnim;
	CComplexStuffEntity*	mStone;

	// outside room
	std::vector<CRoomObjectEntity*>	mRoom;
};




#endif