// --------------------------------------------------------------------------
// Dingus project - a collection of subsystems for game/graphics applications
// --------------------------------------------------------------------------

#ifndef __DYNAMIC_VB_MANAGER_H
#define __DYNAMIC_VB_MANAGER_H

#include "ManagedBuffer.h"
#include "BufferChunk.h"
#include "../../resource/DeviceResource.h"
#include "../../utils/Singleton.h"

namespace dingus {


class CDynamicVBManager : public CManagedBuffer<TVBChunk, CD3DVertexBuffer>,
						public IDeviceResource,
						public CSingleton<CDynamicVBManager>
{
public:
	enum { DEFAULT_CAPACITY = 2*1024*1024 }; // 2 megabytes
	static const DWORD		VB_USAGE;
	static const D3DPOOL	VB_POOL;

public:
	static void initialize( unsigned int capacityBytes ) {
		CDynamicVBManager* vbmgr = new CDynamicVBManager( capacityBytes );
		assert( vbmgr );
		assignInstance( *vbmgr );
	}

public:
	virtual ~CDynamicVBManager();
	
	// IManagedResource
	virtual void createResource();
	virtual void activateResource();
	virtual void passivateResource();
	virtual void deleteResource();

protected:
	// interface for CManagedBuffer
	virtual byte* lockBuffer( unsigned int byteStart, unsigned int byteCount );

	CD3DVertexBuffer* allocateBuffer( unsigned int capacityBytes );

private:
	IDirect3DVertexBuffer9* createBuffer( unsigned int capacityBytes );

private:
	// CSingleton
	CDynamicVBManager( unsigned int capacityBytes );
	static CDynamicVBManager* createInstance() {
		ASSERT_FAIL_MSG( "must be initialized first" );
		return NULL;
	}
	static void deleteInstance( CDynamicVBManager& instance ) { delete &instance; }
	friend CSingleton<CDynamicVBManager>;
};


}; // namespace

#endif
