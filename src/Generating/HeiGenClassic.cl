
#include "Noise.cl"

float GetNoise(int a_Seed, int float x, float y)
{
	float oct1 = CubicNoise2D(a_Seed, x * m_HeightFreq1, y * m_HeightFreq1) * m_HeightAmp1;
	float oct2 = CubicNoise2D(a_Seed, x * m_HeightFreq2, y * m_HeightFreq2) * m_HeightAmp2;
	float oct3 = CubicNoise2D(a_Seed, x * m_HeightFreq3, y * m_HeightFreq3) * m_HeightAmp3;

	float height = CubicNoise2D(a_Seed, x * 0.1f, y * 0.1f ) * 2;

	float flatness = ((CubicNoise2D(a_Seed, x * 0.5f, y * 0.5f) + 1.f) * 0.5f) * 1.1f; // 0 ... 1.5
	flatness *= flatness * flatness;

	return (oct1 + oct2 + oct3) * flatness + height;
}


__kernel void GenHeightMap(int a_Seed, int a_ChunkX, int a_ChunkZ, __global unsigned char * a_HeightMap)
{
	int z = get_global_id(0);
	int x = get_global_id(1);
	const float zz = (float)(a_ChunkZ * 16 + z);
	const float xx = (float)(a_ChunkX * 16 + x);
	
	int hei = 64 + (int)(GetNoise(a_Seed, xx * 0.05f, zz * 0.05f) * 16);
	if (hei < 10)
	{
		hei = 10;
	}
	if (hei > 250)
	{
		hei = 250;
	}
	a_HeightMap[x+ 16 * z] = hei;
}
