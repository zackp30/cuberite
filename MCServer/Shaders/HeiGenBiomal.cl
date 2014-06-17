float IntNoise2D(int a_Seed, int a_X, int a_Y)
{
	int n = a_X + a_Y * 57 + a_Seed * 57 * 57;
	n = (n << 13) ^ n;
	return (1 - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824);
	// returns a float number in the range of [-1, 1]
}

float CubicInterpolate(float a_A, float a_B, float a_C, float a_D, float a_Pct)
{
	float P = (a_D - a_C) - (a_A - a_B);
	float Q = (a_A - a_B) - P;
	float R = a_C - a_A;
	float S = a_B;

	return ((P * a_Pct + Q) * a_Pct + R) * a_Pct + S;
}

float CubicNoise2D(int a_Seed, float a_X, float a_Y)
{
	const int	BaseX = floor(a_X);
	const int	BaseY = floor(a_Y);
	
	const float points[4][4] =
	{
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY - 1),
			IntNoise2D(a_Seed, BaseX, BaseY - 1),
			IntNoise2D(a_Seed, BaseX + 1, BaseY - 1), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY - 1), 
		},
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY),
			IntNoise2D(a_Seed, BaseX, BaseY),
			IntNoise2D(a_Seed, BaseX + 1, BaseY),
			IntNoise2D(a_Seed, BaseX + 2, BaseY),
		},
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX + 1, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY + 1), 
		},
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX + 1, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY + 2), 
		},
	};
	
	const float FracX = a_X - BaseX;
	const float interp1 = CubicInterpolate(points[0][0], points[0][1], points[0][2], points[0][3], FracX);
	const float interp2 = CubicInterpolate(points[1][0], points[1][1], points[1][2], points[1][3], FracX);
	const float interp3 = CubicInterpolate(points[2][0], points[2][1], points[2][2], points[2][3], FracX);
	const float interp4 = CubicInterpolate(points[3][0], points[3][1], points[3][2], points[3][3], FracX);


	const float FracY = a_Y - BaseY;
	return CubicInterpolate(interp1, interp2, interp3, interp4, FracY);
}

typedef struct
{
	float m_HeightFreq1, m_HeightAmp1;
	float m_HeightFreq2, m_HeightAmp2;
	float m_HeightFreq3, m_HeightAmp3;
	float m_BaseHeight;
} HeiGenState;

float GetHeightAt(int a_Seed, int a_RelX, int a_RelZ, int a_ChunkX, int a_ChunkZ, int * a_BiomeNeighbors, HeiGenState * a_GenParam)
{
	// Sum up how many biomes of each type there are in the neighborhood:
	int BiomeCounts[256];
	for (int i = 0; i < 256; i++) BiomeCounts[i] = 0;
	int Sum = 0;
	//int8 iota = (0,1,2,3,4,5,6,7);
	for (int z = -8; z <= 8; z++)
	{
		int FinalZ = a_RelZ + z + 16;
		int IdxZ = FinalZ / 16;
		int ModZ = FinalZ % 16;
		int WeightZ = 9 - abs(z);
		for (int x = -8; x <= 8; x++)
		{
			int FinalX = a_RelX + x + 16;
			int IdxX = FinalX / 16;
			int ModX = FinalX % 16;
			int Biome = a_BiomeNeighbors[IdxX * 256 * 3 + IdxZ * 256 + ModX + ModZ * 16];
			int WeightX = 9 - abs(x);
			BiomeCounts[Biome] += WeightX + WeightZ;
			Sum += WeightX + WeightZ;
		}  // for x
	}  // for z
	
	// For each biome type that has a nonzero count, calc its height and add it:
	if (Sum > 0)
	{
		float Height = 0;
		int BlockX = a_ChunkX * 16 + a_RelX;
		int BlockZ = a_ChunkZ * 16 + a_RelZ;
		for (size_t i = 0; i < 256; i++)
		{
			if (BiomeCounts[i] == 0)
			{
				continue;
			}
			
			float oct1 = CubicNoise2D(a_Seed, BlockX * a_GenParam[i].m_HeightFreq1, BlockZ * a_GenParam[i].m_HeightFreq1) * a_GenParam[i].m_HeightAmp1;
			float oct2 = CubicNoise2D(a_Seed, BlockX * a_GenParam[i].m_HeightFreq2, BlockZ * a_GenParam[i].m_HeightFreq2) * a_GenParam[i].m_HeightAmp2;
			float oct3 = CubicNoise2D(a_Seed, BlockX * a_GenParam[i].m_HeightFreq3, BlockZ * a_GenParam[i].m_HeightFreq3) * a_GenParam[i].m_HeightAmp3;
			Height += BiomeCounts[i] * (a_GenParam[i].m_BaseHeight + oct1 + oct2 + oct3);
		}
		float res = Height / Sum;
		return clamp(res, 5.0f, 250.0f);
	}
	
	// No known biome around? Weird. Return a bogus value:
	return 5;
}

/** kernel is 16x16 **/
/** @BiomeMap A 3x3 array of BiomeMaps **/
__kernel void GenHeightMap(int a_Seed, int a_ChunkX, int a_ChunkZ, __global unsigned char * a_HeightMap, __constant int * BiomeMap, __constant HeiGenState * a_GenParam)
{
	int z = get_global_id(0);
	int x = get_global_id(1);

	a_HeightMap[x + 16 * z] = GetHeightAt(a_Seed, x, z, a_ChunkX, a_ChunkZ, BiomeMap, a_GenParam);
}
