
#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#ifndef _WIN32
	#include <cstdlib>
#endif


#include "cChunk.h"
#include "cWorld.h"
#include "cWaterSimulator.h"
#include "cLavaSimulator.h"
#include "cClientHandle.h"
#include "cServer.h"
#include "zlib.h"
#include "Defines.h"
#include "cChestEntity.h"
#include "cFurnaceEntity.h"
#include "cSignEntity.h"
#include "cTorch.h"
#include "cLadder.h"
#include "cPickup.h"
#include "cRedstone.h"
#include "cItem.h"
#include "cNoise.h"
#include "cRoot.h"
#include "cWorldGenerator.h"
#include "cBlockToPickup.h"
#include "MersenneTwister.h"
#include "cPlayer.h"

#include "packets/cPacket_DestroyEntity.h"
#include "packets/cPacket_PreChunk.h"
#include "packets/cPacket_BlockChange.h"
#include "packets/cPacket_MapChunk.h"
#include "packets/cPacket_MultiBlock.h"

#include <json/json.h>





extern bool g_bWaterPhysics;





cChunk::cChunk(int a_X, int a_Y, int a_Z, cWorld * a_World)
	: m_bCalculateLighting( false )
	, m_bCalculateHeightmap( false )
	, m_PosX( a_X )
	, m_PosY( a_Y )
	, m_PosZ( a_Z )
	, m_BlockType( m_BlockData ) // Offset the pointers
	, m_BlockMeta( m_BlockType + c_NumBlocks )
	, m_BlockLight( m_BlockMeta + c_NumBlocks / 2 )
	, m_BlockSkyLight( m_BlockLight + c_NumBlocks / 2 )
	, m_BlockTickNum( 0 )
	, m_BlockTickX( 0 )
	, m_BlockTickY( 0 )
	, m_BlockTickZ( 0 )
	, m_World( a_World )
	, m_IsValid(false)
	, m_IsDirty(false)
	, m_IsSaving(false)
{
	// LOGINFO("### new cChunk (%i, %i) at %p, thread 0x%x ###", a_X, a_Z, this, GetCurrentThreadId());
}





cChunk::~cChunk()
{
	// LOGINFO("### delete cChunk() (%i, %i) from %p, thread 0x%x ###", m_PosX, m_PosZ, this, GetCurrentThreadId() );
	
	cCSLock Lock(m_CSEntities);
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		delete *itr;
	}
	m_BlockEntities.clear();

	// Remove and destroy all entities that are not players:
	cEntityList Entities;
	for (cEntityList::const_iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr)
	{
		if ((*itr)->GetEntityType() != cEntity::E_PLAYER)
		{
			Entities.push_back(*itr);
		}
	}
	for (cEntityList::iterator itr = Entities.begin(); itr != Entities.end(); ++itr)
	{
		(*itr)->RemoveFromChunk();
		(*itr)->Destroy();
	}
	m_Entities.clear();
}





void cChunk::SetValid(bool a_SendToClients)
{
	m_IsValid = true;
	
	if (!a_SendToClients)
	{
		return;
	}
	
	cCSLock Lock(m_CSClients);
	if (m_LoadedByClient.empty())
	{
		return;
	}
	
	// Sending the chunk here interferes with the lighting done in the tick thread and results in the "invalid compressed data" on the client
	/*
	cPacket_PreChunk PreChunk;
	PreChunk.m_PosX = m_PosX;
	PreChunk.m_PosZ = m_PosZ;
	PreChunk.m_bLoad = true;
	cPacket_MapChunk MapChunk(this);
	Broadcast(&PreChunk);
	Broadcast(&MapChunk);
	
	// Let all clients of this chunk know that it has been already sent to the client
	for (cClientHandleList::iterator itr = m_LoadedByClient.begin(); itr != m_LoadedByClient.end(); ++itr)
	{
		(*itr)->ChunkJustSent(this);
	}  // for itr - m_LoadedByClient[]
	*/
}





bool cChunk::CanUnload(void)
{
	cCSLock Lock(m_CSClients);
	return m_LoadedByClient.empty() && !m_IsDirty;
}





void cChunk::MarkSaving(void)
{
	m_IsSaving = true;
}





void cChunk::MarkSaved(void)
{
	if (!m_IsSaving)
	{
		return;
	}
	m_IsDirty = false;
}





void cChunk::MarkLoaded(void)
{
	m_IsDirty = false;
	m_IsValid = true;
}





void cChunk::GetAllData(cChunkDataCallback * a_Callback)
{
	a_Callback->BlockData(m_BlockData);
	
	cCSLock Lock(m_CSEntities);
	for (cEntityList::iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr)
	{
		a_Callback->Entity(*itr);
	}
	
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		a_Callback->BlockEntity(*itr);
	}
}





void cChunk::SetAllData(const char * a_BlockData, cEntityList & a_Entities, cBlockEntityList & a_BlockEntities)
{
	memcpy(m_BlockData, a_BlockData, sizeof(m_BlockData));

	// Clear the internal entities:
	cCSLock Lock(m_CSEntities);
	for (cEntityList::iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr)
	{
		if ((*itr)->GetEntityType() == cEntity::E_PLAYER)
		{
			// Move players into the new entity list
			a_Entities.push_back(*itr);
		}
		else
		{
			// Delete other entities (there should not be any, since we're now loading / generating the chunk)
			LOGWARNING("cChunk: There is an unexpected entity #%d of type %s in chunk [%d, %d]; it will be deleted",
				(*itr)->GetUniqueID(), (*itr)->GetClass(),
				m_PosX, m_PosZ
			);
			delete *itr;
		}
	}
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		delete *itr;
	}
	
	// Swap the entity lists:
	std::swap(a_Entities, m_Entities);
	std::swap(a_BlockEntities, m_BlockEntities);
	
	// Create block entities that the loader didn't load; fill them with defaults
	CreateBlockEntities();

	CalculateHeightmap();
}





/// Returns true if there is a block entity at the coords specified
bool cChunk::HasBlockEntityAt(int a_BlockX, int a_BlockY, int a_BlockZ)
{
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		if (
			((*itr)->GetPosX() == a_BlockX) &&
			((*itr)->GetPosY() == a_BlockY) &&
			((*itr)->GetPosZ() == a_BlockZ)
		)
		{
			return true;
		}
	}  // for itr - m_BlockEntities[]
	return false;
}





void cChunk::Tick(float a_Dt, MTRand & a_TickRandom)
{
	if (m_bCalculateLighting)
	{
		CalculateLighting();
	}
	if (m_bCalculateHeightmap)
	{
		CalculateHeightmap();
	}

	cCSLock Lock(m_CSBlockLists);
	unsigned int PendingSendBlocks = m_PendingSendBlocks.size();
	if( PendingSendBlocks > 1 )
	{
		cPacket_MultiBlock MultiBlock;
		MultiBlock.m_ChunkX = m_PosX;
		MultiBlock.m_ChunkZ = m_PosZ;
		MultiBlock.m_NumBlocks = (short)PendingSendBlocks;
		MultiBlock.m_BlockCoordinates = new unsigned short[PendingSendBlocks];
		MultiBlock.m_BlockTypes = new char[PendingSendBlocks];
		MultiBlock.m_BlockMetas = new char[PendingSendBlocks];
		//LOG("Sending multiblock packet for %i blocks", PendingSendBlocks );
		for( unsigned int i = 0; i < PendingSendBlocks; i++)
		{
			unsigned int index = m_PendingSendBlocks[i];
			unsigned int Y = index % 128;
			unsigned int Z = (index / 128) % 16;
			unsigned int X = (index / (128*16));

			MultiBlock.m_BlockCoordinates[i] = (Z&0xf) | (X&0xf)<<4 | (Y&0xff)<<8;
			//LOG("X: %i Y: %i Z: %i Combo: 0x%04x", X, Y, Z, MultiBlock.m_BlockCoordinates[i] );
			MultiBlock.m_BlockTypes[i] = m_BlockType[index];
			MultiBlock.m_BlockMetas[i] = GetLight( m_BlockMeta, index );
		}
		m_PendingSendBlocks.clear();
		PendingSendBlocks = m_PendingSendBlocks.size();
		Broadcast( MultiBlock );
	}
	if( PendingSendBlocks > 0 )
	{
		for( unsigned int i = 0; i < PendingSendBlocks; i++)
		{
			unsigned int index = m_PendingSendBlocks[i];
			int Y = index % 128;
			int Z = (index / 128) % 16;
			int X = (index / (128*16));

			cPacket_BlockChange BlockChange;
			BlockChange.m_PosX = X + m_PosX*16;
			BlockChange.m_PosY = (char)(Y + m_PosY*128);
			BlockChange.m_PosZ = Z + m_PosZ*16;
			BlockChange.m_BlockType = m_BlockType[index];
			BlockChange.m_BlockMeta = GetLight( m_BlockMeta, index );
			Broadcast( BlockChange );
		}
		m_PendingSendBlocks.clear();
	}
	Lock.Unlock();

	while ( !m_UnloadQuery.empty() )
	{
		cPacket_PreChunk UnloadPacket;
		UnloadPacket.m_PosX = GetPosX();
		UnloadPacket.m_PosZ = GetPosZ();
		UnloadPacket.m_bLoad = false; // Unload
		(*m_UnloadQuery.begin())->Send( UnloadPacket );
		m_UnloadQuery.remove( *m_UnloadQuery.begin() );
	}

	cCSLock Lock2(m_CSBlockLists);
	std::map< unsigned int, int > ToTickBlocks = m_ToTickBlocks;
	m_ToTickBlocks.clear();
	Lock2.Unlock();
	
	bool isRedstone = false;
	for( std::map< unsigned int, int>::iterator itr = ToTickBlocks.begin(); itr != ToTickBlocks.end(); ++itr )
	{
		if( (*itr).second < 0 ) continue;
		unsigned int index = (*itr).first;
		int Y = index % 128;
		int Z = (index / 128) % 16;
		int X = (index / (128*16));

		char BlockID = GetBlock( index );
		switch( BlockID )
		{
		case E_BLOCK_REDSTONE_REPEATER_OFF:
		case E_BLOCK_REDSTONE_REPEATER_ON:
		case E_BLOCK_REDSTONE_WIRE:
			{
				isRedstone = true;
			}
		case E_BLOCK_CACTUS:
		case E_BLOCK_REEDS:
		case E_BLOCK_WOODEN_PRESSURE_PLATE:
		case E_BLOCK_STONE_PRESSURE_PLATE:
		case E_BLOCK_MINECART_TRACKS:
		case E_BLOCK_SIGN_POST:
		case E_BLOCK_CROPS:
		case E_BLOCK_SAPLING:
		case E_BLOCK_YELLOW_FLOWER:
		case E_BLOCK_RED_ROSE:
		case E_BLOCK_RED_MUSHROOM:
		case E_BLOCK_BROWN_MUSHROOM:		// Stuff that drops when block below is destroyed
			{
				if( GetBlock( X, Y-1, Z ) == E_BLOCK_AIR )
				{
					SetBlock( X, Y, Z, E_BLOCK_AIR, 0 );

					int wX, wY, wZ;
					PositionToWorldPosition(X, Y, Z, wX, wY, wZ);

					m_World->GetSimulatorManager()->WakeUp(wX, wY, wZ);
					if (isRedstone) {
						cRedstone Redstone(m_World);
						Redstone.ChangeRedstone( (X+m_PosX*16), (Y+m_PosY*16), (Z+m_PosZ*16), false );
					}
					cPickup* Pickup = new cPickup( (X+m_PosX*16) * 32 + 16, (Y+m_PosY*128) * 32 + 16, (Z+m_PosZ*16) * 32 + 16, cItem( cBlockToPickup::ToPickup( (ENUM_ITEM_ID)BlockID, E_ITEM_EMPTY) , 1 ) );
					Pickup->Initialize( m_World );
				}
			}
			break;
		case E_BLOCK_REDSTONE_TORCH_OFF:
		case E_BLOCK_REDSTONE_TORCH_ON:
				isRedstone = true;
		case E_BLOCK_TORCH:
			{
				char Dir = cTorch::MetaDataToDirection( GetLight( m_BlockMeta, X, Y, Z ) );
				LOG("MetaData: %i", Dir );
				int XX = X + m_PosX*16;
				char YY = (char)Y;
				int ZZ = Z + m_PosZ*16;
				AddDirection( XX, YY, ZZ, Dir, true );
				if( m_World->GetBlock( XX, YY, ZZ ) == E_BLOCK_AIR )
				{
					SetBlock( X, Y, Z, 0, 0 );
					if (isRedstone) {
						cRedstone Redstone(m_World);
						Redstone.ChangeRedstone( (X+m_PosX*16), (Y+m_PosY*16), (Z+m_PosZ*16), false );
					}
					cPickup* Pickup = new cPickup( (X+m_PosX*16) * 32 + 16, (Y+m_PosY*128) * 32 + 16, (Z+m_PosZ*16) * 32 + 16, cItem( cBlockToPickup::ToPickup( (ENUM_ITEM_ID)BlockID, E_ITEM_EMPTY) , 1 ) );
					Pickup->Initialize( m_World );
				}
			}
			break;
		case E_BLOCK_LADDER:
			{
				char Dir = cLadder::MetaDataToDirection( GetLight( m_BlockMeta, X, Y, Z ) );
				int XX = X + m_PosX*16;
				char YY = (char)Y;
				int ZZ = Z + m_PosZ*16;
				AddDirection( XX, YY, ZZ, Dir, true );
				if( m_World->GetBlock( XX, YY, ZZ ) == E_BLOCK_AIR )
				{
					SetBlock( X, Y, Z, E_BLOCK_AIR, 0 );
					cPickup* Pickup = new cPickup( (X+m_PosX*16) * 32 + 16, (Y+m_PosY*128) * 32 + 16, (Z+m_PosZ*16) * 32 + 16,  cItem( (ENUM_ITEM_ID)BlockID, 1 ) );
					Pickup->Initialize( m_World );
				}
			}
			break;
		default:
			break;
		};
	}

	// Tick dem blocks
	int RandomX = a_TickRandom.randInt();
	int RandomY = a_TickRandom.randInt();
	int RandomZ = a_TickRandom.randInt();

	for(int i = 0; i < 50; i++)
	{
		m_BlockTickX = (m_BlockTickX + RandomX) % 16;
		m_BlockTickY = (m_BlockTickY + RandomY) % 128;
		m_BlockTickZ = (m_BlockTickZ + RandomZ) % 16;

		//LOG("%03i %03i %03i", m_BlockTickX, m_BlockTickY, m_BlockTickZ);

		if( m_BlockTickY > m_HeightMap[ m_BlockTickX + m_BlockTickZ*16 ] ) continue; // It's all air up here

		//m_BlockTickNum = (m_BlockTickNum + 1 ) % c_NumBlocks;
		unsigned int Index = MakeIndex( m_BlockTickX, m_BlockTickY, m_BlockTickZ );
		char ID = m_BlockType[Index];
		switch( ID )
		{
			/*
			// TODO: re-enable
			case E_BLOCK_DIRT:
			{
				char AboveBlock = GetBlock( Index+1 );
				if ( (AboveBlock == 0) && GetLight( m_BlockSkyLight, Index ) > 0xf/2 ) // Half lit //changed to not allow grass if any one hit object is on top
				{
					FastSetBlock( m_BlockTickX, m_BlockTickY, m_BlockTickZ, E_BLOCK_GRASS, GetLight( m_BlockMeta, Index ) );
				}
				if ( (g_BlockOneHitDig[AboveBlock]) && GetLight( m_BlockSkyLight, Index+1 ) > 0xf/2 ) // Half lit //ch$
				{
					FastSetBlock( m_BlockTickX, m_BlockTickY, m_BlockTickZ, E_BLOCK_GRASS, GetLight( m_BlockMeta, Index ) );
				}

				break;
			}
			*/
			
			case E_BLOCK_GRASS:
			{
				char AboveBlock = GetBlock( Index+1 );
				if (!( (AboveBlock == 0) || (g_BlockOneHitDig[AboveBlock]) || (g_BlockTransparent[AboveBlock]) ) ) //changed to not allow grass if any one hit object is on top
				{
					FastSetBlock( m_BlockTickX, m_BlockTickY, m_BlockTickZ, E_BLOCK_DIRT, GetLight( m_BlockMeta, Index ) );
				}
			}
			break;
		case E_BLOCK_SAPLING: //todo: check meta of sapling. change m_World->GrowTree to look change trunk and leaves based on meta of sapling
			{
				FastSetBlock( m_BlockTickX, m_BlockTickY, m_BlockTickZ, E_BLOCK_AIR, GetLight( m_BlockMeta, Index ) );
				m_World->GrowTree( m_BlockTickX + m_PosX*16, m_BlockTickY, m_BlockTickZ + m_PosZ*16 );
			}
			break;
		case E_BLOCK_LEAVES: //todo, http://www.minecraftwiki.net/wiki/Data_values#Leaves
			{
			}
			break;
		default:
			break;
		}
	}

	// Tick block entities (furnaces)
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		if ((*itr)->GetBlockType() == E_BLOCK_FURNACE)
		{
			((cFurnaceEntity *)(*itr))->Tick( a_Dt );
		}
	}
}





char cChunk::GetHeight( int a_X, int a_Z )
{
	if( a_X >= 0 && a_X < 16 && a_Z >= 0 && a_Z < 16 )
	{
		return m_HeightMap[a_X + a_Z*16];
	}
	return 0;
}





void cChunk::CreateBlockEntities(void)
{
	for (int x = 0; x < 16; x++)
	{
		for (int z = 0; z < 16; z++)
		{
			for (int y = 0; y < 128; y++)
			{
				ENUM_BLOCK_ID BlockType = (ENUM_BLOCK_ID)m_BlockData[ MakeIndex( x, y, z ) ];
				switch ( BlockType )
				{
					case E_BLOCK_CHEST:
					{
						if (!HasBlockEntityAt(x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16))
						{
							m_BlockEntities.push_back( new cChestEntity( x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16, m_World) );
						}
						break;
					}
					
					case E_BLOCK_FURNACE:
					{
						if (!HasBlockEntityAt(x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16))
						{
							m_BlockEntities.push_back( new cFurnaceEntity( x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16, m_World) );
						}
						break;
					}
					
					case E_BLOCK_SIGN_POST:
					case E_BLOCK_WALLSIGN:
					{
						if (!HasBlockEntityAt(x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16))
						{
							m_BlockEntities.push_back( new cSignEntity( BlockType, x + m_PosX * 16, y + m_PosY * 128, z + m_PosZ * 16, m_World) );
						}
						break;
					}
				}  // switch (BlockType)
			}  // for y
		}  // for z
	}  // for x
}





void cChunk::CalculateHeightmap()
{
	m_bCalculateHeightmap = false;
	for (int x = 0; x < 16; x++)
	{
		for (int z = 0; z < 16; z++)
		{
			for (int y = 127; y > -1; y--)
			{
				int index = MakeIndex( x, y, z );
				if (m_BlockData[index] != E_BLOCK_AIR)
				{
					m_HeightMap[x + z * 16] = (char)y;
					break;
				}
			}  // for y
		}  // for z
	}  // for x
}





void cChunk::CalculateLighting()
{
	// Calculate sunlight
	memset(m_BlockSkyLight, 0xff, c_NumBlocks / 2 ); // Set all to fully lit, so everything above HeightMap is lit
	for(int x = 0; x < 16; x++)
	{
		for(int z = 0; z < 16; z++)
		{
			char sunlight = 0xf;
			for(int y = m_HeightMap[x + z*16]; y > -1; y--)
			{
				int index = y + (z * 128) + (x * 128 * 16);

				if( g_BlockTransparent[ (int)m_BlockData[index] ] == false )
				{
					sunlight = 0x0;
				}
				SetLight( m_BlockSkyLight, x, y, z, sunlight );
			}
		}
	}

	// Calculate blocklights
	for(int x = 0; x < 16; x++)
	{
		for(int z = 0; z < 16; z++)
		{
			int MaxHeight = m_HeightMap[x + z*16];
			for(int y = 0; y < MaxHeight; y++)
			{
				char BlockID = GetBlock(x, y, z);
				SetLight( m_BlockLight, x, y, z, g_BlockLightValue[(int)BlockID] );
			}
		}
	}

	SpreadLight(m_BlockSkyLight);
	SpreadLight(m_BlockLight);

	// Stop it from calculating again :P
	m_bCalculateLighting = false;
}





void cChunk::SpreadLight(char* a_LightBuffer)
{
	// Spread the sunlight
	for(int x = 0; x < 16; x++)	for(int z = 0; z < 16; z++)	for(int y = 0; y < 128; y++)
	{
		int index = y + (z * 128) + (x * 128 * 16);
		if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
		{
			SpreadLightOfBlock(a_LightBuffer, x, y, z, g_BlockSpreadLightFalloff[ m_BlockData[index] ]);
		}
	}

	for(int x = 15; x > -1; x--) for(int z = 15; z > -1; z--) for(int y = 127; y > -1; y--)
	{
		int index = y + (z * 128) + (x * 128 * 16);
		if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
		{
			SpreadLightOfBlock(a_LightBuffer, x, y, z, g_BlockSpreadLightFalloff[ m_BlockData[index] ]);
		}
	}

	bool bCalcLeft, bCalcRight, bCalcFront, bCalcBack;
	bCalcLeft = bCalcRight = bCalcFront = bCalcBack = false;
	
	// Spread to neighbour chunks X-axis
	cChunkPtr LeftChunk  = m_World->GetChunkNoGen( m_PosX - 1, m_PosY, m_PosZ );
	cChunkPtr RightChunk = m_World->GetChunkNoGen( m_PosX + 1, m_PosY, m_PosZ );
	char * LeftSky = NULL, *RightSky = NULL;
	if (LeftChunk->IsValid())
	{
		LeftSky = (a_LightBuffer == m_BlockSkyLight) ? LeftChunk->pGetSkyLight() : LeftChunk->pGetLight();
	}
	if (RightChunk->IsValid())
	{
		RightSky = (a_LightBuffer == m_BlockSkyLight) ? RightChunk->pGetSkyLight() : RightChunk->pGetLight();
	}
	
	for (int z = 0; z < 16; z++) for(int y = 0; y < 128; y++)
	{
		if (LeftSky != NULL)
		{
			int index = y + (z * 128) + (0  * 128 * 16);
			if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
			{
				char CurrentLight = GetLight( a_LightBuffer, 0, y, z );
				char LeftLight = GetLight( LeftSky, 15, y, z );
				if( LeftLight < CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ] )
				{
					SetLight( LeftSky, 15, y, z, MAX(0, CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ]) );
					bCalcLeft = true;
				}
			}
		}
		if (RightSky != NULL)
		{
			int index = y + (z * 128) + (15  * 128 * 16);
			if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
			{
				char CurrentLight = GetLight( a_LightBuffer, 15, y, z );
				char RightLight = GetLight( RightSky, 0, y, z );
				if( RightLight < CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ] )
				{
					SetLight( RightSky, 0, y, z,  MAX(0, CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ]) );
					bCalcRight = true;
				}
			}
		}
	}

	// Spread to neighbour chunks Z-axis
	cChunkPtr FrontChunk = m_World->GetChunkNoGen( m_PosX, m_PosY, m_PosZ - 1 );
	cChunkPtr BackChunk  = m_World->GetChunkNoGen( m_PosX, m_PosY, m_PosZ + 1 );
	char * FrontSky = NULL, * BackSky = NULL;
	if (FrontChunk->IsValid())
	{
		FrontSky = (a_LightBuffer == m_BlockSkyLight) ? FrontChunk->pGetSkyLight() : FrontChunk->pGetLight();
	}
	if (BackChunk->IsValid())
	{
		BackSky = (a_LightBuffer == m_BlockSkyLight) ? BackChunk->pGetSkyLight() : BackChunk->pGetLight();
	}
	for(int x = 0; x < 16; x++)	for(int y = 0; y < 128; y++)
	{
		if (FrontSky != NULL)
		{
			int index = y + (0 * 128) + (x  * 128 * 16);
			if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
			{
				char CurrentLight = GetLight( a_LightBuffer, x, y, 0 );
				char FrontLight = GetLight( FrontSky, x, y, 15 );
				if( FrontLight < CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ] )
				{
					SetLight( FrontSky, x, y, 15,  MAX(0, CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ]) );
					bCalcFront = true;
				}
			}
		}
		if (BackSky != NULL)
		{
			int index = y + (15 * 128) + (x  * 128 * 16);
			if( g_BlockSpreadLightFalloff[ m_BlockData[index] ] > 0 )
			{
				char CurrentLight = GetLight( a_LightBuffer, x, y, 15 );
				char BackLight = GetLight( BackSky, x, y, 0 );
				if ( BackLight < CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ] )
				{
					SetLight( BackSky, x, y, 0, MAX(0, CurrentLight-g_BlockSpreadLightFalloff[ m_BlockData[index] ]) );
					bCalcBack = true;
				}
			}
		}
	}

	if( bCalcLeft )		m_World->ReSpreadLighting( LeftChunk );
	if( bCalcRight )	m_World->ReSpreadLighting( RightChunk );
	if( bCalcFront )	m_World->ReSpreadLighting( FrontChunk );
	if( bCalcBack )		m_World->ReSpreadLighting( BackChunk );
}





void cChunk::AsyncUnload( cClientHandle* a_Client )
{
	m_UnloadQuery.remove( a_Client );	// Make sure this client is only in the list once
	m_UnloadQuery.push_back( a_Client );
}





void cChunk::Send( cClientHandle* a_Client )
{
	cPacket_PreChunk PreChunk;
	PreChunk.m_PosX = m_PosX;
	PreChunk.m_PosZ = m_PosZ;
	PreChunk.m_bLoad = true;
	a_Client->Send( PreChunk );
	a_Client->Send( cPacket_MapChunk( this ) );

	cCSLock Lock(m_CSBlockLists);
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr )
	{
		(*itr)->SendTo( a_Client );
	}
}





void cChunk::SetBlock( int a_X, int a_Y, int a_Z, char a_BlockType, char a_BlockMeta )
{
	if (a_X < 0 || a_X >= 16 || a_Y < 0 || a_Y >= 128 || a_Z < 0 || a_Z >= 16)
	{
		return;  // Clip
	}

	assert(IsValid());  // Is this chunk loaded / generated?
	
	MarkDirty();
	
	int index = a_Y + (a_Z * 128) + (a_X * 128 * 16);
	char OldBlockMeta = GetLight( m_BlockMeta, index );
	char OldBlockType = m_BlockType[index];
	m_BlockType[index] = a_BlockType;

	SetLight( m_BlockMeta, index, a_BlockMeta );

	if ((OldBlockType == a_BlockType) && (OldBlockMeta == a_BlockMeta))
	{
		return;
	}

	cCSLock Lock(m_CSBlockLists);
	m_PendingSendBlocks.push_back( index );

	m_ToTickBlocks[ MakeIndex( a_X, a_Y, a_Z ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X+1, a_Y, a_Z ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X-1, a_Y, a_Z ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X, a_Y+1, a_Z ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X, a_Y-1, a_Z ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X, a_Y, a_Z+1 ) ]++;
	m_ToTickBlocks[ MakeIndex( a_X, a_Y, a_Z-1 ) ]++;

	cBlockEntity* BlockEntity = GetBlockEntity( a_X + m_PosX*16, a_Y+m_PosY*128, a_Z+m_PosZ*16 );
	if( BlockEntity )
	{
		BlockEntity->Destroy();
		RemoveBlockEntity( BlockEntity );
		delete BlockEntity;
	}
	switch( a_BlockType )
	{
		case E_BLOCK_CHEST:
		{
			AddBlockEntity( new cChestEntity( a_X + m_PosX * 16, a_Y + m_PosY * 128, a_Z + m_PosZ * 16, m_World) );
			break;
		}
		case E_BLOCK_FURNACE:
		{
			AddBlockEntity( new cFurnaceEntity( a_X + m_PosX * 16, a_Y + m_PosY * 128, a_Z + m_PosZ * 16, m_World) );
			break;
		}
		case E_BLOCK_SIGN_POST:
		case E_BLOCK_WALLSIGN:
		{
			AddBlockEntity( new cSignEntity( (ENUM_BLOCK_ID)a_BlockType, a_X + m_PosX * 16, a_Y + m_PosY * 128, a_Z + m_PosZ * 16, m_World) );
			break;
		}
	}  // switch (a_BlockType)
}





void cChunk::FastSetBlock( int a_X, int a_Y, int a_Z, char a_BlockType, char a_BlockMeta )
{
	if(a_X < 0 || a_X >= 16 || a_Y < 0 || a_Y >= 128 || a_Z < 0 || a_Z >= 16)
	{
		return; // Clip
	}

	assert(IsValid());
	
	MarkDirty();
	
	const int index = a_Y + (a_Z * 128) + (a_X * 128 * 16);
	const char OldBlock = m_BlockType[index];
	if (OldBlock == a_BlockType)
	{
		return;
	}
	m_BlockType[index] = a_BlockType;

	{
		cCSLock Lock(m_CSBlockLists);
		m_PendingSendBlocks.push_back( index );
	}
	
	SetLight( m_BlockMeta, index, a_BlockMeta );

	// ONLY recalculate lighting if it's necessary!
	if(		g_BlockLightValue[ OldBlock ] != g_BlockLightValue[ a_BlockType ]
		||	g_BlockSpreadLightFalloff[ OldBlock ] != g_BlockSpreadLightFalloff[ a_BlockType ]
		||	g_BlockTransparent[ OldBlock ] != g_BlockTransparent[ a_BlockType ] )
	{
		RecalculateLighting();
	}

	// Recalculate next tick
	RecalculateHeightmap();
}





void cChunk::SendBlockTo( int a_X, int a_Y, int a_Z, cClientHandle* a_Client )
{
	if( a_Client == 0 )
	{
		cCSLock Lock(m_CSBlockLists);
		m_PendingSendBlocks.push_back( MakeIndex( a_X, a_Y, a_Z ) );
		return;
	}

	for (cClientHandleList::iterator itr = m_LoadedByClient.begin(); itr != m_LoadedByClient.end(); ++itr )
	{
		if ( *itr == a_Client )
		{
			unsigned int index = MakeIndex( a_X, a_Y, a_Z );
			cPacket_BlockChange BlockChange;
			BlockChange.m_PosX = a_X + m_PosX*16;
			BlockChange.m_PosY = (char)(a_Y + m_PosY*128);
			BlockChange.m_PosZ = a_Z + m_PosZ*16;
			BlockChange.m_BlockType = m_BlockType[ index ];
			BlockChange.m_BlockMeta = GetLight( m_BlockMeta, index );
			a_Client->Send( BlockChange );
			break;
		}
	}
}





void cChunk::AddBlockEntity( cBlockEntity* a_BlockEntity )
{
	cCSLock Lock(m_CSBlockLists);
	m_BlockEntities.push_back( a_BlockEntity );
}





cBlockEntity * cChunk::GetBlockEntity(int a_X, int a_Y, int a_Z)
{
	// Assumes that the m_CSBlockList is already locked, we're being called from SetBlock()
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		if (
			((*itr)->GetPosX() == a_X) &&
			((*itr)->GetPosY() == a_Y) &&
			((*itr)->GetPosZ() == a_Z)
		)
		{
			return *itr;
		}
	}  // for itr - m_BlockEntities[]
	
	return NULL;
}





void cChunk::UseBlockEntity(cPlayer * a_Player, int a_X, int a_Y, int a_Z)
{
	cBlockEntity * be = GetBlockEntity(a_X, a_Y, a_Z);
	if (be != NULL)
	{
		be->UsedBy(a_Player);
	}
}





void cChunk::CollectPickupsByPlayer(cPlayer * a_Player)
{
	cCSLock Lock(m_CSEntities);

	double PosX = a_Player->GetPosX();
	double PosY = a_Player->GetPosY();
	double PosZ = a_Player->GetPosZ();
	
	for (cEntityList::iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr)
	{
		if ( (*itr)->GetEntityType() != cEntity::E_PICKUP )
		{
			continue; // Only pickups
		}
		float DiffX = (float)((*itr)->GetPosX() - PosX );
		float DiffY = (float)((*itr)->GetPosY() - PosY );
		float DiffZ = (float)((*itr)->GetPosZ() - PosZ );
		float SqrDist = DiffX * DiffX + DiffY * DiffY + DiffZ * DiffZ;
		if (SqrDist < 1.5f * 1.5f)  // 1.5 block
		{
			MarkDirty();
			(reinterpret_cast<cPickup *>(*itr))->CollectedBy( a_Player );
		}
	}
}





void cChunk::UpdateSign(int a_PosX, int a_PosY, int a_PosZ, const AString & a_Line1, const AString & a_Line2, const AString & a_Line3, const AString & a_Line4)
{
	// Also sends update packets to all clients in the chunk
	cCSLock Lock(m_CSEntities);
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		if (
			((*itr)->GetPosX() == a_PosX) &&
			((*itr)->GetPosY() == a_PosY) &&
			((*itr)->GetPosZ() == a_PosZ) &&
			(
				((*itr)->GetBlockType() == E_BLOCK_WALLSIGN) ||
				((*itr)->GetBlockType() == E_BLOCK_SIGN_POST)
			)
		)
		{
			MarkDirty();
			(reinterpret_cast<cSignEntity *>(*itr))->SetLines(a_Line1, a_Line2, a_Line3, a_Line4);
			(*itr)->SendTo(NULL);
		}
	}  // for itr - m_BlockEntities[]
}





void cChunk::RemoveBlockEntity( cBlockEntity* a_BlockEntity )
{
	cCSLock Lock(m_CSBlockLists);
	MarkDirty();
	m_BlockEntities.remove( a_BlockEntity );
}





void cChunk::AddClient( cClientHandle* a_Client )
{
	{
		cCSLock Lock(m_CSClients);
		m_LoadedByClient.remove( a_Client );
		m_LoadedByClient.push_back( a_Client );
	}

	cCSLock Lock(m_CSEntities);
	for (cEntityList::iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr )
	{
		LOG("Entity #%d (%s) at [%i %i %i] spawning for player \"%s\"", (*itr)->GetUniqueID(), (*itr)->GetClass(), m_PosX, m_PosY, m_PosZ, a_Client->GetUsername().c_str() );
		(*itr)->SpawnOn( a_Client );
	}
}





void cChunk::RemoveClient( cClientHandle* a_Client )
{
	{
		cCSLock Lock(m_CSClients);
		m_LoadedByClient.remove( a_Client );
	}

	if ( !a_Client->IsDestroyed() )
	{
		cCSLock Lock(m_CSEntities);
		for (cEntityList::iterator itr = m_Entities.begin(); itr != m_Entities.end(); ++itr )
		{
			LOG("chunk [%i, %i] destroying entity #%i for player \"%s\"", m_PosX, m_PosZ, (*itr)->GetUniqueID(), a_Client->GetUsername().c_str() );
			cPacket_DestroyEntity DestroyEntity( *itr );
			a_Client->Send( DestroyEntity );
		}
	}
}





bool cChunk::HasClient( cClientHandle* a_Client )
{
	cCSLock Lock(m_CSClients);
	for (cClientHandleList::const_iterator itr = m_LoadedByClient.begin(); itr != m_LoadedByClient.end(); ++itr)
	{
		if ((*itr) == a_Client)
		{
			return true;
		}
	}
	return false;
}





bool cChunk::HasAnyClient(void)
{
	cCSLock Lock(m_CSClients);
	return !m_LoadedByClient.empty();
}





void cChunk::AddEntity( cEntity * a_Entity )
{
	cCSLock Lock(m_CSEntities);
	if (a_Entity->GetEntityType() != cEntity::E_PLAYER)
	{
		MarkDirty();
	}
	m_Entities.push_back( a_Entity );
}





void cChunk::RemoveEntity(cEntity * a_Entity)
{
	size_t SizeBefore, SizeAfter;
	{
		cCSLock Lock(m_CSEntities);
		SizeBefore = m_Entities.size();
		m_Entities.remove(a_Entity);
		SizeAfter = m_Entities.size();
	}
	if ((a_Entity->GetEntityType() != cEntity::E_PLAYER) && (SizeBefore != SizeAfter))
	{
		MarkDirty();
	}
}





char cChunk::GetBlock( int a_X, int a_Y, int a_Z )
{
	if ((a_X < 0) || (a_X >= 16) || (a_Y < 0) || (a_Y >= 128) || (a_Z < 0) || (a_Z >= 16)) return 0; // Clip

	int index = a_Y + (a_Z * 128) + (a_X * 128 * 16);
	return m_BlockType[index];
}





char cChunk::GetBlock( int a_BlockIdx )
{
	if( a_BlockIdx < 0 || a_BlockIdx >= c_NumBlocks ) return 0;
	return m_BlockType[ a_BlockIdx ];
}





/// Loads the chunk from the old-format disk file, erases the file afterwards. Returns true if successful
bool cChunk::LoadFromDisk()
{
	AString SourceFile;
	Printf(SourceFile, "world/X%i_Y%i_Z%i.bin", m_PosX, m_PosY, m_PosZ );

	cFile f;
	if (!f.Open(SourceFile, cFile::fmRead))
	{
		return false;
	}

	if (f.Read(m_BlockData, sizeof(m_BlockData)) != sizeof(m_BlockData))
	{
		LOGERROR("ERROR READING FROM FILE %s", SourceFile.c_str()); 
		return false;
	}

	// Now load Block Entities
	cCSLock Lock(m_CSEntities);

	ENUM_BLOCK_ID BlockType;
	while (f.Read(&BlockType, sizeof(ENUM_BLOCK_ID)) == sizeof(ENUM_BLOCK_ID))
	{
		switch (BlockType)
		{
			case E_BLOCK_CHEST:
			{
				cChestEntity * ChestEntity = new cChestEntity( 0, 0, 0, m_World );
				if (!ChestEntity->LoadFromFile(f))
				{
					LOGERROR("ERROR READING CHEST FROM FILE %s", SourceFile.c_str());
					delete ChestEntity;
					return false;
				}
				m_BlockEntities.push_back( ChestEntity );
				break;
			}
			
			case E_BLOCK_FURNACE:
			{
				cFurnaceEntity* FurnaceEntity = new cFurnaceEntity( 0, 0, 0, m_World );
				if (!FurnaceEntity->LoadFromFile(f))
				{
					LOGERROR("ERROR READING FURNACE FROM FILE %s", SourceFile.c_str());
					delete FurnaceEntity;
					return false;
				}
				m_BlockEntities.push_back( FurnaceEntity );
				break;
			}
			
			case E_BLOCK_SIGN_POST:
			case E_BLOCK_WALLSIGN:
			{
				cSignEntity * SignEntity = new cSignEntity(BlockType, 0, 0, 0, m_World );
				if (!SignEntity->LoadFromFile( f ) )
				{
					LOGERROR("ERROR READING SIGN FROM FILE %s", SourceFile.c_str());
					delete SignEntity;
					return false;
				}
				m_BlockEntities.push_back( SignEntity );
				break;
			}
			
			default:
			{
				assert(!"Unhandled block entity in file");
				break;
			}
		}
	}
	Lock.Unlock();
	f.Close();

	// Delete old format file
	if (std::remove(SourceFile.c_str()) != 0)
	{
		LOGERROR("Could not delete file %s", SourceFile.c_str());
	}
	else
	{
		LOGINFO("Successfully deleted old format file \"%s\"", SourceFile.c_str());
	}
	m_IsDirty = false;
	return true;
}





void cChunk::Broadcast( const cPacket * a_Packet, cClientHandle* a_Exclude)
{
	cCSLock Lock(m_CSClients);
	for (cClientHandleList::const_iterator itr = m_LoadedByClient.begin(); itr != m_LoadedByClient.end(); ++itr )
	{
		if (*itr == a_Exclude)
		{
			continue;
		}
		(*itr)->Send( a_Packet );
	}  // for itr - LoadedByClient[]
}





void cChunk::CopyBlockDataFrom(const char * a_NewBlockData)
{
	// Copies all blockdata, recalculates heightmap (used by chunk loaders)
	memcpy(m_BlockData, a_NewBlockData, sizeof(m_BlockData));
	CalculateHeightmap();
}





void cChunk::LoadFromJson( const Json::Value & a_Value )
{
	cCSLock Lock(m_CSEntities);

	// Load chests
	Json::Value AllChests = a_Value.get("Chests", Json::nullValue);
	if (!AllChests.empty())
	{
		for ( Json::Value::iterator itr = AllChests.begin(); itr != AllChests.end(); ++itr )
		{
			Json::Value & Chest = *itr;
			cChestEntity* ChestEntity = new cChestEntity(0, 0, 0, m_World);
			if ( !ChestEntity->LoadFromJson( Chest ) )
			{
				LOGERROR("ERROR READING CHEST FROM JSON!" );
				delete ChestEntity;
			}
			else m_BlockEntities.push_back( ChestEntity );
		}
	}

	// Load furnaces
	Json::Value AllFurnaces = a_Value.get("Furnaces", Json::nullValue);
	if ( !AllFurnaces.empty() )
	{
		for ( Json::Value::iterator itr = AllFurnaces.begin(); itr != AllFurnaces.end(); ++itr )
		{
			Json::Value & Furnace = *itr;
			cFurnaceEntity* FurnaceEntity = new cFurnaceEntity(0, 0, 0, m_World);
			if ( !FurnaceEntity->LoadFromJson( Furnace ) )
			{
				LOGERROR("ERROR READING FURNACE FROM JSON!" );
				delete FurnaceEntity;
			}
			else m_BlockEntities.push_back( FurnaceEntity );
		}
	}

	// Load signs
	Json::Value AllSigns = a_Value.get("Signs", Json::nullValue);
	if ( !AllSigns.empty() )
	{
		for ( Json::Value::iterator itr = AllSigns.begin(); itr != AllSigns.end(); ++itr )
		{
			Json::Value & Sign = *itr;
			cSignEntity* SignEntity = new cSignEntity( E_BLOCK_SIGN_POST, 0, 0, 0, m_World);
			if ( !SignEntity->LoadFromJson( Sign ) )
			{
				LOGERROR("ERROR READING SIGN FROM JSON!" );
				delete SignEntity;
			}
			else m_BlockEntities.push_back( SignEntity );
		}
	}
}





void cChunk::SaveToJson( Json::Value & a_Value )
{
	Json::Value AllChests;
	Json::Value AllFurnaces;
	Json::Value AllSigns;
	cCSLock Lock(m_CSEntities);
	for (cBlockEntityList::iterator itr = m_BlockEntities.begin(); itr != m_BlockEntities.end(); ++itr)
	{
		cBlockEntity * BlockEntity = *itr;
		switch ( BlockEntity->GetBlockType() )
		{
			case E_BLOCK_CHEST:
			{
				cChestEntity* ChestEntity = reinterpret_cast< cChestEntity* >( BlockEntity );
				Json::Value NewChest;
				ChestEntity->SaveToJson( NewChest );
				AllChests.append( NewChest );
				break;
			}
			
			case E_BLOCK_FURNACE:
			{
				cFurnaceEntity* FurnaceEntity = reinterpret_cast< cFurnaceEntity* >( BlockEntity );
				Json::Value NewFurnace;
				FurnaceEntity->SaveToJson( NewFurnace );
				AllFurnaces.append( NewFurnace );
				break;
			}
			
			case E_BLOCK_SIGN_POST:
			case E_BLOCK_WALLSIGN:
			{
				cSignEntity* SignEntity = reinterpret_cast< cSignEntity* >( BlockEntity );
				Json::Value NewSign;
				SignEntity->SaveToJson( NewSign );
				AllSigns.append( NewSign );
				break;
			}
			
			default:
			{
				assert(!"Unhandled blocktype in BlockEntities list while saving to JSON");
				break;
			}
		}  // switch (BlockEntity->GetBlockType())
	}  // for itr - BlockEntities[]

	if( !AllChests.empty() )
	{
		a_Value["Chests"] = AllChests;
	}
	if( !AllFurnaces.empty() )
	{
		a_Value["Furnaces"] = AllFurnaces;
	}
	if( !AllSigns.empty() )
	{
		a_Value["Signs"] = AllSigns;
	}
}





void cChunk::PositionToWorldPosition(int a_ChunkX, int a_ChunkY, int a_ChunkZ, int & a_X, int & a_Y, int & a_Z)
{
	a_Y = a_ChunkY;
	a_X = m_PosX * 16 + a_ChunkX;
	a_Z = m_PosZ * 16 + a_ChunkZ;
}





#if !C_CHUNK_USE_INLINE
# include "cChunk.inc"
#endif




