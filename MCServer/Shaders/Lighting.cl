
// LightingThread.cpp

// Implements the cLightingThread class representing the thread that processes requests for lighting

unsigned char GetValue(__global unsigned char * a_Vals, int a_x, int a_y, int a_z)
{
	int ChunkX = a_x % 16;
	int ChunkZ = a_z % 16;
	int x = a_x - ChunkX * 16;
	int z = a_z - ChunkZ * 16;
	
	return a_Vals[x + z * 16 + a_y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3];
}


int diffuseNeigbour(unsigned char * a_IsSeed, unsigned char * a_LightValues, unsigned char a_LightFilter, int x, int y, int z)
{
	if (x == 0 || y == 0 || z == 0 || x == (16 * 3 - 1) || y == 255 || z == (16 * 3 - 1))
		return 0;
	int NeighbourVal = GetValue(a_LightValues, x, y, z);
	NeighbourVal = max(NeighbourVal - a_LightFilter, 0);
	// Cheap optimisation to avoid branching
	NeighbourVal *= GetValue(a_IsSeed, x, y, z);
	return NeighbourVal;
}

unsigned char GetLocalValue(unsigned char * a_Vals, int a_x, int a_y, int a_z)
{
	return a_Vals[a_x + a_z * 6 + a_y * 36];
}

int diffuseLocalNeigbour(unsigned char * a_IsSeed, unsigned char * a_LightValues, unsigned char a_LightFilter, int x, int y, int z)
{
	int NeighbourVal = GetLocalValue(a_LightValues, x, y, z);
	NeighbourVal = max(NeighbourVal - a_LightFilter, 0);
	// Cheap optimisation to avoid branching
	NeighbourVal *= GetLocalValue(a_IsSeed, x, y, z);
	return NeighbourVal;
}

bool IsSeed(unsigned char * a_IsSeed, int a_x, int a_y, int a_z)
{
	int ChunkX = a_x % 16;
	int ChunkZ = a_z % 16;
	int x = a_x - ChunkX * 16;
	int z = a_z - ChunkZ * 16;
	if (a_y > 255)
	{
		return false;
	}
	if (a_y < 0)
	{
		return false;
	}
	return a_IsSeed[x + z * 16 + a_y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3];
}

/* global: 16 * 3 x 258 x 16 * 3  local: 6 x 6 x 6 */
__kernel void CalcLight(__global unsigned char * a_IsSeed, __global unsigned char * a_LightValues, __constant unsigned char * a_LightFilters)
{
	int z = get_global_id(0);
	int x = get_global_id(1);
	int y = get_global_id(2);
	
	if (y > 255) return;
	
	int z_num = z / 6;
	z = z - z_num * 2;
	int x_num = x / 6;
	x = x - x_num * 2;
	int y_num = y / 6;
	y = y - y_num * 2;

	__local unsigned char Buffer[6*6*6];
	__local unsigned char IsSeed[6*6*6];
	
	int Light1, Light2, Light3, Light4, Light5, Light6, Light7;
	Light1 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x + 1, y, z);
	Light2 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x - 1, z, z);
	Light3 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x, y + 1, z);
	Light4 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x, y - 1, z);
	Light5 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x, y, z + 1);
	Light6 = diffuseNeigbour(a_IsSeed, a_LightValues, GetValue(a_LightFilters, x, y, z), x, y, z - 1);
	Light7 = GetValue(a_LightValues, x, y, z);
	
	int local_z = get_local_id(0);
	int local_x = get_local_id(1);
	int local_y = get_local_id(2);
	
	Buffer[local_x + local_z * 6 + local_y * 36] =
		max(
			max(
				max(
					Light1,
					Light2
				),
				max(
					Light3,
					Light4
				)
			),
			max(
				max(
					Light5,
					Light6
				),
				Light7
			)
		);
	IsSeed[local_x + local_z * 6 + local_y * 36] = (Buffer[local_x + local_z * 6 + local_y * 36] != Light7);
		
	mem_fence(CLK_LOCAL_MEM_FENCE);
	
	if (((local_x != 0 && local_x != 5) || x == 0 || x == (16 * 3 - 1)) && 
		((local_z != 0 && local_z != 5) || z == 0 || z == (16 * 3 - 1)) && 
		((local_y != 0 && local_y != 5) || y == 0 || y == 255))
	{
		Light1 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x + 1, local_y, local_z);
		Light2 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x - 1, local_z, local_z);
		Light3 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x, local_y + 1, local_z);
		Light4 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x, local_y - 1, local_z);
		Light5 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x, local_y, local_z + 1);
		Light6 = diffuseLocalNeigbour(IsSeed, Buffer, GetValue(a_LightFilters, x, y, z), local_x, local_y, local_z - 1);
		Light7 = GetLocalValue(Buffer, local_x, local_y, local_z);
		
		int ChunkX = x % 16;
		int ChunkZ = z % 16;
		x = x - ChunkX * 16;
		z = z - ChunkZ * 16;
		
		a_LightValues[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3] =
			max(
				max(
					max(
						Light1,
						Light2
					),
					max(
						Light3,
						Light4
					)
				),
				max(
					max(
						Light5,
						Light6
					),
					Light7
				)
			);
		a_IsSeed[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3] = 
			(a_LightValues[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3] != Light7);
	}
}

/* global: 16 x 128 x 16 */
__kernel void CompressLight(__global unsigned char * LightIn, __global unsigned char * LightOut)
{
	int z = get_global_id(0);
	int x = get_global_id(1);
	int y = get_global_id(2);
	char lowNibble = LightIn[x + z * 16 + y * 2 * 256 + 1 * 256 * 256 + 1 * 256 * 256 * 3];
	char highNibble = LightIn[x + z * 16 + (y * 2 + 1) * 256 + 1 * 256 * 256 + 1 * 256 * 256 * 3];
	LightOut[x + z * 16 + y * 256 + 1 * 256 * 256 + 1 * 256 * 256 * 3] = (highNibble << 4) | lowNibble;
}

/* global: 16 * 3 x 256 x 16 * 3 */
__kernel void PrepareBlockLight(
	__constant unsigned char * a_BlockTypes,
	__constant unsigned char * a_HeightMap,
	__constant unsigned char * a_BlockEmission,
	__global unsigned char * a_BlockLight, 
	__global unsigned * a_IsSeed
)
{
	int z = get_global_id(0);
	int x = get_global_id(1);
	int y = get_global_id(2);
	
	int ChunkX = x % 16;
	int ChunkZ = z % 16;
	x = x - ChunkX * 16;
	z = z = ChunkZ * 16;
	
	//Todo: handle emmision on device
	//int BlockType = a_BlockTypes[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3];
	int LightVal = a_BlockEmission[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3];
	
	int BlockLight = 0;
	unsigned char IsSeed = 0;

	if (LightVal != 0)
	{
		// Add current block as a seed:
		IsSeed = 1;

		// Light it up:
		BlockLight = LightVal;
	}
	a_BlockLight[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3] = BlockLight;
	a_IsSeed[x + z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3] = IsSeed;
}

int max4(int a, int b, int c, int d);

/* global: 16 * 3 x 256 x 16 * 3 */
__kernel void PrepareSkyLight(__constant unsigned char * a_HeightMap, __global unsigned char * a_SkyLight, __global unsigned char * a_IsSeed)
{
	
	int z = get_global_id(0);
	int x = get_global_id(1);
	int y = get_global_id(2);
	
	int Height;
	{
		int ChunkX = x % 16;
		int ChunkZ = z % 16;
		int sub_x = x - ChunkX * 16;
		int sub_z = z - ChunkZ * 16;
		
		Height = a_HeightMap[sub_x + sub_z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3];
	}
	
	// Clear seeds:
	
	int Current = Height + 1;
	int Neighbor1, Neighbor2, Neighbor3, Neighbor4;
	Neighbor1 = 0;
	Neighbor2 = 0;
	Neighbor3 = 0;
	Neighbor4 = 0;

	if (x != (16 * 3 - 1))
	{
		Neighbor1 = GetValue(a_HeightMap, x + 1, y, z) + 1; // X + 1
	}
	if (x != 0)
	{
		Neighbor2 = GetValue(a_HeightMap, x - 1, y, z) + 1; // X - 1
	}

	if (z != (16 * 3 - 1))
	{
		Neighbor3 = GetValue(a_HeightMap, x, y, z + 1) + 1;
	}
	if (z != 0)
	{
		Neighbor4 = GetValue(a_HeightMap, x, y, z - 1);
	}
	int Neighbour = 
		max4(
			Neighbor1,
			Neighbor2,
			Neighbor3,
			Neighbor4
		);
 
	
	int SkyLight = 0;
	
	unsigned char IsSeed;
	if (y >= Current) 
	{
		SkyLight = 15;
	}
	IsSeed = (unsigned char)(y >= Current && Current <= 255 && y < Neighbour);
	
	{
		int ChunkX = x % 16;
		int ChunkZ = z % 16;
		int sub_x = x - ChunkX * 16;
		int sub_z = z - ChunkZ * 16;
		int idx = sub_x + sub_z * 16 + y * 256 + ChunkX * 256 * 256 + ChunkZ * 256 * 256 * 3;
		a_SkyLight[idx] = SkyLight;
		a_IsSeed[idx] = IsSeed;
	}
}

int max4(int a, int b, int c, int d)
{
	return max(max(a,b),max(c,d));
}



